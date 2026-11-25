/**
 * @file PluginVerifier.cpp
 * @brief Implementation of sandboxed C99 plugin symbol, ABI, registration, lifecycle, and leak verification.
 */

#include "PluginVerifier.hpp"

#include "PluginHostBridge.hpp"
#include "PluginManager.hpp"
#include "gridlock/plugin_api.h"

#ifdef __linux__
#include <elf.h>
#endif
#ifndef _WIN32
#include <sys/stat.h>
#endif

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <mutex>

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLibrary>
#include <QProcess>

#include "../hpc/gdb/GdbRankCoordinator.hpp"

namespace gridlock::core::plugins {

namespace {

bool isIgnorableDynamicFuncSymbol(const QByteArray& sym) {
  return sym.isEmpty() || sym == QByteArrayLiteral(GRIDLOCK_PLUGIN_INIT_SYMBOL_V1) ||
         sym == QByteArrayLiteral("_init") || sym == QByteArrayLiteral("_fini") ||
         sym == QByteArrayLiteral("__cxa_finalize") || sym == QByteArrayLiteral("_dl_relocate_static_pie");
}

void inspectSymbolHygiene(const QString& canonicalPath, bool* outSawInitExport, QStringList* outExtraTextSyms,
                          QStringList* outWarnings) {
  *outSawInitExport = false;
  outExtraTextSyms->clear();

  if (PluginManager::hasGnuUniqueSymbols(canonicalPath)) {
    outWarnings->append(QStringLiteral(
        "Shared library exports STB_GNU_UNIQUE ('u') symbol(s); link with -Wl,--version-script "
        "(or -fno-gnu-unique) so Linux dlclose() can hot-reload cleanly."));
  }

#ifdef __linux__
  QFile f(canonicalPath);
  if (!f.open(QIODevice::ReadOnly)) return;
  const QByteArray buf = f.readAll();
  const auto* raw = reinterpret_cast<const unsigned char*>(buf.constData());
  const size_t len = static_cast<size_t>(buf.size());
  if (len < sizeof(Elf64_Ehdr) || std::memcmp(raw, ELFMAG, SELFMAG) != 0 ||
      raw[EI_CLASS] != ELFCLASS64 || raw[EI_DATA] != ELFDATA2LSB) {
    return;
  }
  Elf64_Ehdr eh{};
  std::memcpy(&eh, raw, sizeof(eh));
  if (eh.e_shoff == 0 || eh.e_shentsize < sizeof(Elf64_Shdr) ||
      eh.e_shoff + static_cast<uint64_t>(eh.e_shnum) * eh.e_shentsize > len) {
    return;
  }
  for (uint16_t i = 0; i < eh.e_shnum; ++i) {
    Elf64_Shdr sh{};
    std::memcpy(&sh, raw + eh.e_shoff + static_cast<size_t>(i) * eh.e_shentsize, sizeof(sh));
    if (sh.sh_type != SHT_DYNSYM || sh.sh_entsize < sizeof(Elf64_Sym) ||
        sh.sh_offset + sh.sh_size > len || sh.sh_link >= eh.e_shnum) {
      continue;
    }
    Elf64_Shdr strSh{};
    std::memcpy(&strSh, raw + eh.e_shoff + static_cast<size_t>(sh.sh_link) * eh.e_shentsize, sizeof(strSh));
    if (strSh.sh_type != SHT_STRTAB || strSh.sh_offset + strSh.sh_size > len) continue;
    const char* strTab = reinterpret_cast<const char*>(raw + strSh.sh_offset);
    const size_t strCap = static_cast<size_t>(strSh.sh_size);

    for (uint64_t off = 0; off + sizeof(Elf64_Sym) <= sh.sh_size; off += sh.sh_entsize) {
      Elf64_Sym sym{};
      std::memcpy(&sym, raw + sh.sh_offset + off, sizeof(sym));
      if (sym.st_shndx == SHN_UNDEF || sym.st_name >= strCap) continue;
      const unsigned char bind = ELF64_ST_BIND(sym.st_info);
      const unsigned char type = ELF64_ST_TYPE(sym.st_info);
      const unsigned char vis = ELF64_ST_VISIBILITY(sym.st_other);
      if (vis != STV_DEFAULT || (bind != STB_GLOBAL && bind != STB_WEAK)) continue;
      const size_t maxName = strCap - sym.st_name;
      const QByteArray name(strTab + sym.st_name, static_cast<qsizetype>(::strnlen(strTab + sym.st_name, maxName)));
      if (name == QByteArrayLiteral(GRIDLOCK_PLUGIN_INIT_SYMBOL_V1)) {
        *outSawInitExport = true;
      } else if (type == STT_FUNC && !isIgnorableDynamicFuncSymbol(name)) {
        const QString s = QString::fromUtf8(name);
        if (!outExtraTextSyms->contains(s)) outExtraTextSyms->append(s);
      }
    }
  }
#elif !defined(_WIN32)
  QProcess proc;
  proc.start(QStringLiteral("nm"), {QStringLiteral("-D"), QStringLiteral("--defined-only"), canonicalPath});
  if (proc.waitForFinished(3000) && proc.exitCode() == 0) {
    for (const QByteArray& line : proc.readAllStandardOutput().split('\n')) {
      const QList<QByteArray> cols = line.simplified().split(' ');
      if (cols.size() < 3) continue;
      const QByteArray code = cols.at(1), sym = cols.at(2);
      const QByteArray normSym = sym.startsWith('_') ? sym.mid(1) : sym;
      if (sym == QByteArrayLiteral(GRIDLOCK_PLUGIN_INIT_SYMBOL_V1) ||
          normSym == QByteArrayLiteral(GRIDLOCK_PLUGIN_INIT_SYMBOL_V1)) {
        *outSawInitExport = true;
      } else if ((code == QByteArrayLiteral("T") || code == QByteArrayLiteral("W")) &&
                 !isIgnorableDynamicFuncSymbol(sym)) {
        outExtraTextSyms->append(QString::fromUtf8(sym));
      }
    }
  }
#endif

  if (!outExtraTextSyms->isEmpty()) {
    const QString preview = outExtraTextSyms->mid(0, 5).join(QStringLiteral(", "));
    outWarnings->append(QStringLiteral(
        "Leaked %1 extra global text symbol(s) (%2); compile with -fvisibility=hidden "
        "-fvisibility-inlines-hidden and -Wl,--version-script.")
                            .arg(outExtraTextSyms->size())
                            .arg(preview));
  }
}

bool decodeRequiredDescriptorField(const char* raw, const char* fieldName, QString* outVal, QStringList* outErrors) {
  if (raw == nullptr || raw[0] == '\0') {
    outErrors->append(QStringLiteral("Descriptor %1 must be non-null and non-empty.").arg(QLatin1String(fieldName)));
    return false;
  }
  const size_t n = ::strnlen(raw, 257u);
  if (n > 256u) {
    outErrors->append(QStringLiteral("Descriptor %1 exceeds 256 bytes.").arg(QLatin1String(fieldName)));
    return false;
  }
  for (size_t i = 0; i < n; ++i) {
    const auto ch = static_cast<unsigned char>(raw[i]);
    if (ch < 0x20u || ch == 0x7Fu) {
      outErrors->append(QStringLiteral("Descriptor %1 contains control characters.").arg(QLatin1String(fieldName)));
      return false;
    }
  }
  *outVal = QString::fromUtf8(raw, static_cast<qsizetype>(n)).trimmed();
  if (outVal->isEmpty()) {
    outErrors->append(QStringLiteral("Descriptor %1 must not be whitespace-only.").arg(QLatin1String(fieldName)));
    return false;
  }
  return true;
}

struct VerifierHarnessContext {
  PluginBindingContext binding;
  GridLockHostApi baseApi{};
  std::mutex allocMutex;
  QHash<void*, size_t> liveBlocks;
  size_t outstandingBytes = 0u;
  bool inInitStage = false;
  QStringList initRegistrationErrors;

  static void* GRIDLOCK_CALL cbAlloc(void* rawCtx, size_t numBytes) noexcept {
    auto* self = static_cast<VerifierHarnessContext*>(rawCtx);
    if (!self || !self->baseApi.alloc_bytes) return nullptr;
    void* ptr = self->baseApi.alloc_bytes(&self->binding, numBytes);
    if (ptr != nullptr) {
      std::lock_guard<std::mutex> lock(self->allocMutex);
      self->liveBlocks.insert(ptr, numBytes);
      self->outstandingBytes += numBytes;
    }
    return ptr;
  }

  static void GRIDLOCK_CALL cbFree(void* rawCtx, void* ptr) noexcept {
    auto* self = static_cast<VerifierHarnessContext*>(rawCtx);
    if (!self || ptr == nullptr) return;
    {
      std::lock_guard<std::mutex> lock(self->allocMutex);
      auto it = self->liveBlocks.find(ptr);
      if (it == self->liveBlocks.end()) {
        self->initRegistrationErrors.append(
            QStringLiteral("Invalid or double free_bytes() call detected during verification."));
        return;
      }
      self->outstandingBytes -= it.value();
      self->liveBlocks.erase(it);
    }
    if (self->baseApi.free_bytes) self->baseApi.free_bytes(&self->binding, ptr);
  }

  static GridLockStatus GRIDLOCK_CALL cbRegVar(
      void* rawCtx, const char* id, const char* lbl, const char* rx,
      GridLockVariableActionCallback cb, void* pctx) noexcept {
    auto* s = static_cast<VerifierHarnessContext*>(rawCtx);
    const GridLockStatus st = s->baseApi.register_variable_action(&s->binding, id, lbl, rx, cb, pctx);
    if (st != GRIDLOCK_OK && s->inInitStage) {
      s->initRegistrationErrors.append(
          QStringLiteral("register_variable_action('%1') failed validation (status %2; check ID and PCRE2 regex).")
              .arg(QString::fromUtf8(id ? id : ""), QString::number(st)));
    }
    return st;
  }

  static GridLockStatus GRIDLOCK_CALL cbRegCmd(
      void* rawCtx, const char* id, const char* lbl, const char* hint,
      GridLockCommandActionCallback cb, void* pctx) noexcept {
    auto* s = static_cast<VerifierHarnessContext*>(rawCtx);
    const GridLockStatus st = s->baseApi.register_command_action(&s->binding, id, lbl, hint, cb, pctx);
    if (st != GRIDLOCK_OK && s->inInitStage) {
      s->initRegistrationErrors.append(
          QStringLiteral("register_command_action('%1') failed validation (status %2).")
              .arg(QString::fromUtf8(id ? id : ""), QString::number(st)));
    }
    return st;
  }

  static GridLockStatus GRIDLOCK_CALL cbRegMenu(
      void* rawCtx, const char* id, const char* path, const char* hint,
      GridLockCommandActionCallback cb, void* pctx) noexcept {
    auto* s = static_cast<VerifierHarnessContext*>(rawCtx);
    const GridLockStatus st = s->baseApi.register_menu_action(&s->binding, id, path, hint, cb, pctx);
    if (st != GRIDLOCK_OK && s->inInitStage) {
      s->initRegistrationErrors.append(
          QStringLiteral("register_menu_action('%1', '%2') failed validation (status %3; check '/' path depth <= 4).")
              .arg(QString::fromUtf8(id ? id : ""), QString::fromUtf8(path ? path : ""), QString::number(st)));
    }
    return st;
  }

  static GridLockStatus GRIDLOCK_CALL cbRegCtx(
      void* rawCtx, GridLockContextSurface surf, const char* id, const char* lbl,
      const char* rx, GridLockContextActionFn cb, void* pctx) noexcept {
    auto* s = static_cast<VerifierHarnessContext*>(rawCtx);
    const GridLockStatus st = s->baseApi.register_context_action(&s->binding, surf, id, lbl, rx, cb, pctx);
    if (st != GRIDLOCK_OK && s->inInitStage) {
      s->initRegistrationErrors.append(
          QStringLiteral("register_context_action('%1') failed validation (status %2; check surface and PCRE2 regex).")
              .arg(QString::fromUtf8(id ? id : ""), QString::number(st)));
    }
    return st;
  }
};

}  // namespace

QString PluginVerifyReport::formatSummary() const {
  QStringList lines;
  const QString title = !pluginName.isEmpty() ? pluginName : QFileInfo(libraryPath).fileName();
  const QString ver = !pluginVersion.isEmpty() ? QStringLiteral(" %1").arg(pluginVersion) : QString();
  lines.append(QStringLiteral("[%1] %2%3 (%4)")
                   .arg(passed ? QStringLiteral("PASS") : QStringLiteral("FAIL"), title, ver, libraryPath));
  if (!pluginId.isEmpty()) {
    lines.append(QStringLiteral("  id=%1 abi=%2.%3 actions=%4 outstanding_alloc_bytes=%5")
                     .arg(pluginId)
                     .arg(abiMajor)
                     .arg(abiMinor)
                     .arg(registeredActionCount)
                     .arg(outstandingBytes));
  }
  for (const QString& w : warnings) lines.append(QStringLiteral("  warning: %1").arg(w));
  for (const QString& e : errors) lines.append(QStringLiteral("  error: %1").arg(e));
  return lines.join(QLatin1Char('\n'));
}

bool PluginVerifier::verifyBinary(const QString& soPath, PluginVerifyReport* outReport) {
  PluginVerifyReport rep;
  const QFileInfo fi(soPath.trimmed());
  rep.libraryPath = !fi.canonicalFilePath().isEmpty() ? fi.canonicalFilePath() : fi.absoluteFilePath();
  auto finish = [&]() {
    rep.passed = rep.errors.isEmpty();
    if (outReport) *outReport = rep;
    return rep.passed;
  };

  if (soPath.trimmed().isEmpty() || !fi.exists() || !fi.isFile() || !fi.isReadable()) {
    rep.errors.append(QStringLiteral("Plugin shared library does not exist or is not readable: %1").arg(soPath));
    return finish();
  }
#ifndef _WIN32
  struct stat st{};
  if (::stat(QFile::encodeName(rep.libraryPath).constData(), &st) != 0 || (st.st_mode & 002) != 0) {
    rep.errors.append(QStringLiteral("Plugin binary has unsafe world-writable permissions: %1").arg(rep.libraryPath));
    return finish();
  }
#endif

  bool sawInitInSymtab = false;
  QStringList extraTextSyms;
  inspectSymbolHygiene(rep.libraryPath, &sawInitInSymtab, &extraTextSyms, &rep.warnings);

  // Isolated coordinator + PluginManager + PluginHostBridge sandbox (zero user config or live session contact).
  gridlock::GdbRankCoordinator sandboxCoord;
  sandboxCoord.initializeMockSession(2);
  PluginManager sandboxMgr(&sandboxCoord);
  PluginHostBridge* bridge = sandboxMgr.hostBridge();

  QLibrary lib(rep.libraryPath);
  lib.setLoadHints(QLibrary::ResolveAllSymbolsHint);
  if (!lib.load()) {
    rep.errors.append(QStringLiteral("Failed to load shared library: %1").arg(lib.errorString()));
    return finish();
  }

  const QFunctionPointer rawInit = lib.resolve(GRIDLOCK_PLUGIN_INIT_SYMBOL_V1);
  if (rawInit == nullptr) {
    rep.errors.append(QStringLiteral("Missing required entry point '%1': %2")
                          .arg(QLatin1String(GRIDLOCK_PLUGIN_INIT_SYMBOL_V1), lib.errorString()));
    lib.unload();
    return finish();
  }

  VerifierHarnessContext harness;
  harness.binding.bridge = bridge;
  harness.binding.pluginId = fi.completeBaseName();
  harness.baseApi = bridge->makeHostApi(&harness.binding);
  GridLockHostApi wrappedApi = harness.baseApi;
  wrappedApi.alloc_bytes = &VerifierHarnessContext::cbAlloc;
  wrappedApi.free_bytes = &VerifierHarnessContext::cbFree;
  wrappedApi.register_variable_action = &VerifierHarnessContext::cbRegVar;
  wrappedApi.register_command_action = &VerifierHarnessContext::cbRegCmd;
  wrappedApi.register_menu_action = &VerifierHarnessContext::cbRegMenu;
  wrappedApi.register_context_action = &VerifierHarnessContext::cbRegCtx;
  // Route wrapped callbacks via `harness`, while non-wrapped callbacks continue using `&harness.binding` (first member!).
  static_assert(offsetof(VerifierHarnessContext, binding) == 0u);
  wrappedApi.host_ctx = &harness;
  harness.binding.boundApi = &wrappedApi;

  GridLockPluginDescriptor desc{};
  desc.struct_size = static_cast<uint32_t>(sizeof(GridLockPluginDescriptor));
  desc.abi_major = GRIDLOCK_PLUGIN_ABI_MAJOR;
  desc.abi_minor = GRIDLOCK_PLUGIN_ABI_MINOR;

  bridge->beginInitStaging();
  harness.inInitStage = true;
  const GridLockStatus initSt = reinterpret_cast<GridLockPluginInitV1Fn>(rawInit)(&wrappedApi, &desc);
  harness.inInitStage = false;

  rep.abiMajor = desc.abi_major;
  rep.abiMinor = desc.abi_minor;
  rep.errors.append(harness.initRegistrationErrors);
  harness.initRegistrationErrors.clear();

  const bool abiMajorOk = (desc.abi_major == GRIDLOCK_PLUGIN_ABI_MAJOR);
  const bool structSizeOk = (desc.struct_size >= offsetof(GridLockPluginDescriptor, reserved));
  if (initSt != GRIDLOCK_OK) {
    rep.errors.append(QStringLiteral("gridlock_plugin_init_v1 failed with status %1.").arg(initSt));
  }
  if (!abiMajorOk) {
    rep.errors.append(QStringLiteral("ABI major mismatch: plugin reported v%1.%2, host requires v%3.x.")
                          .arg(desc.abi_major)
                          .arg(desc.abi_minor)
                          .arg(GRIDLOCK_PLUGIN_ABI_MAJOR));
  }
  if (!structSizeOk) {
    rep.errors.append(QStringLiteral("Invalid GridLockPluginDescriptor struct_size (%1 bytes).").arg(desc.struct_size));
  }
  if (abiMajorOk && structSizeOk) {
    if (desc.reserved_flags != 0u) {
      rep.errors.append(QStringLiteral("GridLockPluginDescriptor reserved_flags must be 0 in ABI v1.0."));
    }
    decodeRequiredDescriptorField(desc.plugin_id_utf8, "plugin_id_utf8", &rep.pluginId, &rep.errors);
    decodeRequiredDescriptorField(desc.plugin_name_utf8, "plugin_name_utf8", &rep.pluginName, &rep.errors);
    decodeRequiredDescriptorField(desc.plugin_version_utf8, "plugin_version_utf8", &rep.pluginVersion, &rep.errors);
  }

  auto releaseLeakedBlocks = [&]() {
    std::lock_guard<std::mutex> lock(harness.allocMutex);
    for (auto it = harness.liveBlocks.begin(); it != harness.liveBlocks.end(); ++it) {
      if (harness.baseApi.free_bytes) harness.baseApi.free_bytes(&harness.binding, it.key());
    }
    harness.liveBlocks.clear();
  };

  if (!rep.errors.isEmpty() || initSt != GRIDLOCK_OK || !abiMajorOk || !structSizeOk) {
    harness.binding.shuttingDown.store(true, std::memory_order_release);
    if (initSt == GRIDLOCK_OK && abiMajorOk && structSizeOk && desc.shutdown != nullptr) {
      desc.shutdown(desc.plugin_ctx, &wrappedApi);
    }
    bridge->discardStagedActions();
    rep.outstandingAllocCount = static_cast<int32_t>(harness.liveBlocks.size());
    rep.outstandingBytes = harness.outstandingBytes;
    releaseLeakedBlocks();
    lib.unload();
    return finish();
  }

  harness.binding.pluginId = rep.pluginId;
  if (!bridge->commitStagedActions(rep.pluginId, &wrappedApi)) {
    rep.errors.append(QStringLiteral("Duplicate action or command ID registered during gridlock_plugin_init_v1."));
  }
  rep.registeredActionCount = static_cast<int>(bridge->variableActions().size() + bridge->commandActions().size() +
                                               bridge->menuActions().size() + bridge->contextActions().size());

  // Dry-run lifecycle: on_session_start(2) -> on_session_end() -> shutdown().
  if (desc.on_session_start != nullptr) {
    const GridLockStatus startSt = desc.on_session_start(desc.plugin_ctx, &wrappedApi, 2);
    if (startSt != GRIDLOCK_OK) {
      rep.errors.append(QStringLiteral("on_session_start(2) returned non-OK status %1.").arg(startSt));
    }
  }
  if (desc.on_session_end != nullptr) {
    const GridLockStatus endSt = desc.on_session_end(desc.plugin_ctx, &wrappedApi);
    if (endSt != GRIDLOCK_OK) {
      rep.errors.append(QStringLiteral("on_session_end() returned non-OK status %1.").arg(endSt));
    }
  }

  harness.binding.shuttingDown.store(true, std::memory_order_release);
  if (desc.shutdown != nullptr) desc.shutdown(desc.plugin_ctx, &wrappedApi);
  rep.errors.append(harness.initRegistrationErrors);

  bridge->unregisterPluginResources(rep.pluginId);
  rep.outstandingAllocCount = static_cast<int32_t>(harness.liveBlocks.size());
  rep.outstandingBytes = harness.outstandingBytes;
  if (rep.outstandingBytes > 0u || rep.outstandingAllocCount > 0) {
    rep.errors.append(
        QStringLiteral("Leaked %1 host allocation(s) (%2 byte(s)): every alloc_bytes() block must be freed in shutdown().")
            .arg(rep.outstandingAllocCount)
            .arg(rep.outstandingBytes));
    releaseLeakedBlocks();
  }
  lib.unload();
  return finish();
}

}  // namespace gridlock::core::plugins
