/**
 * @file PluginManager.cpp
 * @brief Shared-library discovery, SHA-256 workspace trust gating, ABI v1.x enforcement, and lifecycle dispatch.
 */

#include "PluginManager.hpp"

#ifndef _WIN32
#include <sys/stat.h>
#endif

#include <algorithm>
#include <cstddef>
#include <utility>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QSettings>

#include "../hpc/gdb/GdbRankCoordinator.hpp"
#include "../managers/ConfigManager.hpp"

namespace gridlock::core::plugins {

namespace {

QString safeUtf8(const char* raw) { return (raw != nullptr) ? QString::fromUtf8(raw).trimmed() : QString(); }

bool isWorldWritablePosix(const QString& path) {
#ifndef _WIN32
  struct stat st{};
  return (::stat(QFile::encodeName(path).constData(), &st) != 0) || ((st.st_mode & 002) != 0);
#else
  Q_UNUSED(path);
  return false;
#endif
}

LoadedPluginInfo makeStatusInfo(const QString& id, const QString& name, const QString& path,
                                const QString& sha256Hex, PluginScope scope, bool trusted,
                                const QString& error = QString()) {
  LoadedPluginInfo info{};
  info.id = id; info.name = name; info.libraryPath = path; info.sha256Hex = sha256Hex;
  info.scope = scope; info.trusted = trusted; info.enabled = false; info.errorMessage = error;
  return info;
}

LoadedPluginInfo* findInfoById(QVector<LoadedPluginInfo>& list, const QString& id) {
  if (id.isEmpty()) return nullptr;
  for (auto& info : list) if (info.id == id) return &info;
  return nullptr;
}

}  // namespace

PluginManager::PluginManager(gridlock::GdbRankCoordinator* coordinator, QObject* parent)
    : QObject(parent), m_bridge(coordinator, this) {
  m_bridge.setCallbackScopeHook([this](bool entering) { if (entering) ++m_callbackDepth; else leaveCallbackScope(); });
  setCoordinator(coordinator);
}

PluginManager::~PluginManager() {
  m_inDestructor = true; m_bridge.setCallbackScopeHook(nullptr);
  m_pendingDiscoverAndLoad = false; m_pendingUnloadAll = false; m_callbackDepth = 0;
  unloadAll();
}

void PluginManager::leaveCallbackScope() {
  if (m_callbackDepth > 0) --m_callbackDepth;
  if (m_callbackDepth != 0 || m_inDestructor) return;
  if (m_pendingDiscoverAndLoad) {
    m_pendingDiscoverAndLoad = false; m_pendingUnloadAll = false; (void)discoverAndLoad();
  } else if (m_pendingUnloadAll) {
    m_pendingUnloadAll = false; unloadAll();
  }
}

void PluginManager::setCoordinator(gridlock::GdbRankCoordinator* coordinator) {
  if (m_stopConn) QObject::disconnect(m_stopConn);
  m_coordinator = coordinator; m_bridge.setCoordinator(coordinator);
  if (coordinator == nullptr) return;
  m_stopConn = QObject::connect(
      coordinator, &::IBackendCoordinator::rankStopped, this,
      [this](int rankId, int threadId, const QString& reason, const QString& file,
             int line, const QString& func, uint64_t pc) {
        const QByteArray reasonUtf8 = reason.isEmpty() ? QByteArrayLiteral("stopped") : reason.toUtf8();
        const QByteArray fileUtf8 = file.toUtf8(), funcUtf8 = func.toUtf8();
        GridLockStopInfo info{};
        info.struct_size = static_cast<uint32_t>(sizeof(GridLockStopInfo));
        info.abi_major = GRIDLOCK_PLUGIN_ABI_MAJOR; info.abi_minor = GRIDLOCK_PLUGIN_ABI_MINOR;
        info.rank = static_cast<int32_t>(rankId);
        info.thread_id = static_cast<int32_t>(threadId > 0 ? threadId : (rankId + 1));
        info.line = static_cast<int32_t>(line);
        info.frame_level = 0;
        info.pc_address = pc;
        info.reason_utf8 = reasonUtf8.constData();
        info.file_utf8 = fileUtf8.isEmpty() ? nullptr : fileUtf8.constData();
        info.function_utf8 = funcUtf8.isEmpty() ? nullptr : funcUtf8.constData();
        notifyRankStopped(info);
      });
}

bool PluginManager::hasSharedLibrarySuffix(const QString& fileName) {
#if defined(Q_OS_WIN)
  return fileName.endsWith(QStringLiteral(".dll"), Qt::CaseInsensitive);
#elif defined(Q_OS_MACOS) || defined(Q_OS_DARWIN)
  return fileName.endsWith(QStringLiteral(".dylib"), Qt::CaseInsensitive);
#else
  return fileName.endsWith(QStringLiteral(".so"), Qt::CaseInsensitive);
#endif
}

QString PluginManager::computeFileSha256Hex(const QString& filePath) {
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly)) return QString();
  QCryptographicHash hash(QCryptographicHash::Sha256);
  return hash.addData(&file) ? QString::fromLatin1(hash.result().toHex()).toLower() : QString();
}

QStringList PluginManager::searchDirectories() const {
  QStringList dirs{QDir::cleanPath(ConfigManager::configDirectory() + QStringLiteral("/plugins"))};
  const QString wsRoot = ConfigManager::instance().workspacePath().trimmed();
  if (!wsRoot.isEmpty()) {
    const QString wsPlugins = QDir::cleanPath(wsRoot + QStringLiteral("/.gridlock/plugins"));
    if (!dirs.contains(wsPlugins)) dirs.append(wsPlugins);
  }
  return dirs;
}

PluginScope PluginManager::inferScopeForPath(const QString& canonicalPath) const {
  const QString wsRoot = ConfigManager::instance().workspacePath().trimmed();
  if (wsRoot.isEmpty()) return PluginScope::UserGlobal;
  const QFileInfo wsInfo(wsRoot + QStringLiteral("/.gridlock/plugins"));
  const QString wsCanon = !wsInfo.canonicalFilePath().isEmpty() ? wsInfo.canonicalFilePath() : QDir::cleanPath(wsInfo.absoluteFilePath());
  const QString prefix = wsCanon.endsWith(QLatin1Char('/')) ? wsCanon : (wsCanon + QLatin1Char('/'));
  return canonicalPath.startsWith(prefix) ? PluginScope::Workspace : PluginScope::UserGlobal;
}

int PluginManager::discoverAndLoad() {
  if (m_callbackDepth > 0) { m_pendingDiscoverAndLoad = true; return 0; }
  unloadAll();
  ScopedCallbackDepth depthGuard(this);
  QStringList seenCanonicalPaths;
  int activeCount = 0;
  m_deferMetadataSync = true;
  const QString userRootClean = QDir::cleanPath(ConfigManager::configDirectory() + QStringLiteral("/plugins"));
  for (const QString& dirPath : searchDirectories()) {
    if (m_pendingUnloadAll || m_pendingDiscoverAndLoad) break;
    QDir dir(dirPath);
    if (!dir.exists() || isWorldWritablePosix(dirPath)) continue;
    const QString canonicalRoot = QFileInfo(dirPath).canonicalFilePath();
    if (canonicalRoot.isEmpty()) continue;
    const QString rootPrefix = canonicalRoot.endsWith(QLatin1Char('/')) ? canonicalRoot : (canonicalRoot + QLatin1Char('/'));
    const PluginScope scope = (QDir::cleanPath(dirPath) == userRootClean) ? PluginScope::UserGlobal : PluginScope::Workspace;
    for (const QFileInfo& fi : dir.entryInfoList(QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase)) {
      if (m_pendingUnloadAll || m_pendingDiscoverAndLoad) break;
      if (!hasSharedLibrarySuffix(fi.fileName())) continue;
      const QString canonical = fi.canonicalFilePath();
      if (canonical.isEmpty() || !canonical.startsWith(rootPrefix)) {
        m_pluginInfos.append(makeStatusInfo(fi.completeBaseName(), fi.fileName(), fi.absoluteFilePath(),
                                            QString(), scope, false, QStringLiteral("Rejected symlink escaping plugin directory")));
        continue;
      }
      if (seenCanonicalPaths.contains(canonical)) continue;
      seenCanonicalPaths.append(canonical);
      if (loadPluginInternal(canonical, scope)) ++activeCount;
    }
  }
  m_deferMetadataSync = false;
  flushMetadataCache();
  emit pluginsChanged();
  return activeCount;
}

void PluginManager::cachePluginMetadata(const QString& canonicalPath, const LoadedPluginInfo& info) {
  if (canonicalPath.isEmpty() || info.id.isEmpty()) return;
  m_pathMetadataCache.insert(canonicalPath, info);
  m_dirtyMetadataCache.insert(canonicalPath, info);
  if (!m_deferMetadataSync) flushMetadataCache();
}

void PluginManager::flushMetadataCache() {
  if (m_dirtyMetadataCache.isEmpty()) return;
  QSettings s(ConfigManager::workstationSettingsPath(), QSettings::IniFormat);
  for (auto it = m_dirtyMetadataCache.cbegin(); it != m_dirtyMetadataCache.cend(); ++it) {
    const QString k = QStringLiteral("plugin_cache/") + QString::fromLatin1(it.key().toUtf8().toHex());
    s.setValue(k + QStringLiteral("/id"), it.value().id); s.setValue(k + QStringLiteral("/name"), it.value().name);
    s.setValue(k + QStringLiteral("/version"), it.value().version); s.setValue(k + QStringLiteral("/description"), it.value().description);
    s.setValue(k + QStringLiteral("/abi_major"), it.value().abiMajor); s.setValue(k + QStringLiteral("/abi_minor"), it.value().abiMinor);
  }
  s.sync();
  m_dirtyMetadataCache.clear();
}

bool PluginManager::lookupCachedMetadata(const QString& canonicalPath, LoadedPluginInfo* outInfo) const {
  if (m_pathMetadataCache.contains(canonicalPath)) { *outInfo = m_pathMetadataCache.value(canonicalPath); return true; }
  QSettings s(ConfigManager::workstationSettingsPath(), QSettings::IniFormat);
  const QString k = QStringLiteral("plugin_cache/") + QString::fromLatin1(canonicalPath.toUtf8().toHex());
  const QString cachedId = s.value(k + QStringLiteral("/id")).toString().trimmed();
  if (cachedId.isEmpty()) return false;
  outInfo->id = cachedId; outInfo->name = s.value(k + QStringLiteral("/name")).toString();
  outInfo->version = s.value(k + QStringLiteral("/version")).toString();
  outInfo->description = s.value(k + QStringLiteral("/description")).toString();
  outInfo->libraryPath = canonicalPath;
  outInfo->abiMajor = s.value(k + QStringLiteral("/abi_major"), 1u).toUInt();
  outInfo->abiMinor = s.value(k + QStringLiteral("/abi_minor"), 0u).toUInt();
  return true;
}

bool PluginManager::loadPluginFromPath(const QString& libraryPath) {
  const QFileInfo fi(libraryPath);
  const QString canonical = fi.canonicalFilePath().isEmpty() ? fi.absoluteFilePath() : fi.canonicalFilePath();
  return loadPluginInternal(canonical, inferScopeForPath(canonical));
}

bool PluginManager::loadPluginInternal(const QString& libraryPath, PluginScope scope) {
  const QFileInfo fi(libraryPath);
  const QString canonical = fi.canonicalFilePath().isEmpty() ? fi.absoluteFilePath() : fi.canonicalFilePath();
  auto upsertInfo = [this](const LoadedPluginInfo& info) {
    for (auto& existing : m_pluginInfos) if (existing.libraryPath == info.libraryPath) { existing = info; return; }
    m_pluginInfos.append(info);
  };
  if (isWorldWritablePosix(canonical)) {
    upsertInfo(makeStatusInfo(fi.completeBaseName(), fi.fileName(), canonical, QString(), scope, false,
                              QStringLiteral("Rejected world-writable plugin binary")));
    return false;
  }

  const QString sha256Hex = computeFileSha256Hex(canonical);
  auto& cfg = ConfigManager::instance();
  const bool isTrusted = (scope == PluginScope::UserGlobal) || cfg.isWorkspacePluginTrusted(canonical, sha256Hex);
  LoadedPluginInfo cached{};
  const bool hasCached = lookupCachedMetadata(canonical, &cached);

  if (scope == PluginScope::Workspace && !isTrusted) {
    for (const auto& active : m_activePlugins) {
      if (!sha256Hex.isEmpty() && active.scope == PluginScope::UserGlobal && active.sha256Hex == sha256Hex) {
        upsertInfo(makeStatusInfo(active.id, fi.fileName(), canonical, sha256Hex, scope, false,
                                  QStringLiteral("Duplicate plugin ID '%1' rejected").arg(active.id)));
        return false;
      }
    }
    LoadedPluginInfo untrusted = makeStatusInfo(
        hasCached && !cached.id.isEmpty() ? cached.id : fi.completeBaseName(),
        hasCached && !cached.name.isEmpty() ? cached.name : fi.fileName(),
        canonical, sha256Hex, scope, false, QStringLiteral("Untrusted Workspace Plugin (Approval Required)"));
    untrusted.version = cached.version; untrusted.description = cached.description;
    untrusted.abiMajor = cached.abiMajor; untrusted.abiMinor = cached.abiMinor;
    upsertInfo(untrusted);
    return false;
  }
  if (hasCached && !cfg.effectivePluginEnabled(cached.id)) {
    cached.libraryPath = canonical; cached.sha256Hex = sha256Hex;
    cached.scope = scope; cached.trusted = isTrusted; cached.enabled = false; cached.errorMessage.clear();
    upsertInfo(cached); return false;
  }
  if (!cfg.effectivePluginEnabled(fi.completeBaseName()) || !cfg.effectivePluginEnabled(fi.fileName())) {
    upsertInfo(makeStatusInfo(fi.completeBaseName(), fi.fileName(), canonical, sha256Hex, scope, isTrusted));
    return false;
  }

  auto lib = std::make_unique<QLibrary>(canonical);
  lib->setLoadHints(QLibrary::ResolveAllSymbolsHint);
  if (!lib->load()) {
    upsertInfo(makeStatusInfo(fi.completeBaseName(), fi.fileName(), canonical, sha256Hex, scope, isTrusted,
                              QStringLiteral("Failed to load shared library: %1").arg(lib->errorString())));
    return false;
  }
  const QFunctionPointer rawSym = lib->resolve(GRIDLOCK_PLUGIN_INIT_SYMBOL_V1);
  if (rawSym == nullptr) {
    const QString err = lib->errorString(); lib->unload();
    upsertInfo(makeStatusInfo(fi.completeBaseName(), fi.fileName(), canonical, sha256Hex, scope, isTrusted,
                              QStringLiteral("Missing entry point '%1': %2")
                                  .arg(QLatin1String(GRIDLOCK_PLUGIN_INIT_SYMBOL_V1), err)));
    return false;
  }

  GridLockPluginDescriptor desc{};
  desc.struct_size = static_cast<uint32_t>(sizeof(GridLockPluginDescriptor));
  desc.abi_major = GRIDLOCK_PLUGIN_ABI_MAJOR; desc.abi_minor = GRIDLOCK_PLUGIN_ABI_MINOR;
  auto bindingCtx = std::make_unique<PluginBindingContext>();
  bindingCtx->bridge = &m_bridge; bindingCtx->pluginId = fi.completeBaseName();
  auto pluginApi = std::make_unique<GridLockHostApi>(m_bridge.makeHostApi(bindingCtx.get()));
  bindingCtx->boundApi = pluginApi.get();

  m_bridge.beginInitStaging();
  GridLockStatus status = GRIDLOCK_ERR_UNSUPPORTED;
  { ScopedCallbackDepth guard(this); status = reinterpret_cast<GridLockPluginInitV1Fn>(rawSym)(pluginApi.get(), &desc); }
  const bool abiMajorOk = (desc.abi_major == GRIDLOCK_PLUGIN_ABI_MAJOR);
  const bool structSizeOk = (desc.struct_size >= offsetof(GridLockPluginDescriptor, reserved));

  auto teardownUncommitted = [&](bool canInvokeShutdown) {
    bindingCtx->shuttingDown.store(true, std::memory_order_release);
    if (canInvokeShutdown && desc.shutdown != nullptr) { ScopedCallbackDepth guard(this); desc.shutdown(desc.plugin_ctx, pluginApi.get()); }
    m_bridge.discardStagedActions(); lib->unload();
  };
  auto rejectPlugin = [&](const QString& reason, bool canInvokeShutdown) {
    const QString parsedId = (abiMajorOk && structSizeOk) ? safeUtf8(desc.plugin_id_utf8) : QString();
    const QString parsedName = (abiMajorOk && structSizeOk) ? safeUtf8(desc.plugin_name_utf8) : QString();
    teardownUncommitted(canInvokeShutdown);
    LoadedPluginInfo rejected = makeStatusInfo(
        !parsedId.isEmpty() ? parsedId : fi.completeBaseName(),
        !parsedName.isEmpty() ? parsedName : fi.fileName(), canonical, sha256Hex, scope, isTrusted, reason);
    rejected.abiMajor = desc.abi_major; rejected.abiMinor = desc.abi_minor;
    upsertInfo(rejected);
    return false;
  };

  if (status != GRIDLOCK_OK) return rejectPlugin(QStringLiteral("gridlock_plugin_init_v1 failed with status %1").arg(status), false);
  if (!abiMajorOk) return rejectPlugin(QStringLiteral("ABI major mismatch: plugin reported v%1.%2, host requires v%3.x").arg(desc.abi_major).arg(desc.abi_minor).arg(GRIDLOCK_PLUGIN_ABI_MAJOR), false);
  if (!structSizeOk) return rejectPlugin(QStringLiteral("Invalid GridLockPluginDescriptor struct_size (%1 bytes)").arg(desc.struct_size), false);

  const QString pluginId = safeUtf8(desc.plugin_id_utf8), pluginName = safeUtf8(desc.plugin_name_utf8);
  if (pluginId.isEmpty() || pluginName.isEmpty()) return rejectPlugin(QStringLiteral("Plugin descriptor must provide non-empty UTF-8 id and name"), true);
  bindingCtx->pluginId = pluginId;
  if (findActiveHandle(pluginId) != nullptr) return rejectPlugin(QStringLiteral("Duplicate plugin ID '%1' rejected").arg(pluginId), true);

  LoadedPluginInfo info = makeStatusInfo(pluginId, pluginName, canonical, sha256Hex, scope, isTrusted);
  info.version = safeUtf8(desc.plugin_version_utf8); info.description = safeUtf8(desc.description_utf8);
  info.abiMajor = desc.abi_major; info.abiMinor = desc.abi_minor;
  cachePluginMetadata(canonical, info);

  if (!cfg.effectivePluginEnabled(pluginId)) { teardownUncommitted(true); upsertInfo(info); return false; }
  if (!m_bridge.commitStagedActions(pluginId, pluginApi.get())) {
    return rejectPlugin(QStringLiteral("Plugin '%1' registered a duplicate action or command ID").arg(pluginId), true);
  }
  info.enabled = true; upsertInfo(info);
  m_activePlugins.push_back(ActivePluginHandle{
      pluginId, canonical, sha256Hex, scope, desc, std::move(bindingCtx), std::move(pluginApi), std::move(lib)});
  return true;
}

PluginManager::ActivePluginHandle* PluginManager::findActiveHandle(const QString& pluginId) {
  for (auto& handle : m_activePlugins) if (handle.id == pluginId) return &handle;
  return nullptr;
}

void PluginManager::shutdownAndUnloadHandle(ActivePluginHandle& handle) {
  if (handle.bindingCtx) handle.bindingCtx->shuttingDown.store(true, std::memory_order_release);
  const GridLockHostApi* api = handle.hostApi ? handle.hostApi.get() : m_bridge.hostApi();
  if (m_sessionActive && handle.descriptor.on_session_end != nullptr) {
    ScopedCallbackDepth guard(this);
    (void)handle.descriptor.on_session_end(handle.descriptor.plugin_ctx, api);
  }
  if (handle.descriptor.shutdown != nullptr) {
    ScopedCallbackDepth guard(this);
    handle.descriptor.shutdown(handle.descriptor.plugin_ctx, api);
  }
  m_bridge.unregisterPluginActions(handle.id);
  m_bridge.unregisterPluginBreakpoints(handle.id);
  if (handle.library) { handle.library->unload(); handle.library.reset(); }
}

bool PluginManager::setPluginEnabled(const QString& pluginId, bool enabled) {
  const QString trimmed = pluginId.trimmed();
  LoadedPluginInfo* info = findInfoById(m_pluginInfos, trimmed);
  if (info == nullptr) return false;
  ConfigManager::instance().setPluginDisabled(trimmed, !enabled);
  if (!enabled) {
    if (m_callbackDepth > 0) { info->enabled = false; m_pendingDiscoverAndLoad = true; emit pluginsChanged(); return true; }
    for (auto it = m_activePlugins.begin(); it != m_activePlugins.end(); ++it) {
      if (it->id == trimmed) {
        ActivePluginHandle retiring = std::move(*it);
        m_activePlugins.erase(it);
        ScopedCallbackDepth guard(this);
        shutdownAndUnloadHandle(retiring);
        break;
      }
    }
    info->enabled = false; emit pluginsChanged();
    return true;
  }
  if (info->scope == PluginScope::Workspace && !info->trusted) return false;
  if (findActiveHandle(trimmed) != nullptr) { info->enabled = true; return true; }
  const bool ok = loadPluginInternal(info->libraryPath, info->scope);
  emit pluginsChanged();
  return ok;
}

bool PluginManager::isPluginGloballyEnabled(const QString& pluginId) const { return ConfigManager::instance().isPluginGloballyEnabled(pluginId); }
bool PluginManager::isPluginWorkspaceEnabled(const QString& pluginId) const { return ConfigManager::instance().isPluginWorkspaceEnabled(pluginId); }

bool PluginManager::setPluginGloballyEnabled(const QString& pluginId, bool enabled) {
  const QString trimmed = pluginId.trimmed();
  if (trimmed.isEmpty()) return false;
  ConfigManager::instance().setPluginGloballyEnabled(trimmed, enabled);
  if (LoadedPluginInfo* info = findInfoById(m_pluginInfos, trimmed)) info->requiresRestart = true;
  emit pluginsChanged(); return true;
}

bool PluginManager::setPluginWorkspaceEnabled(const QString& pluginId, bool enabled) {
  const QString trimmed = pluginId.trimmed();
  if (trimmed.isEmpty()) return false;
  ConfigManager::instance().setPluginWorkspaceEnabled(trimmed, enabled);
  if (LoadedPluginInfo* info = findInfoById(m_pluginInfos, trimmed)) info->requiresRestart = true;
  emit pluginsChanged(); return true;
}

bool PluginManager::effectivePluginEnabled(const QString& pluginId) const {
  const QString trimmed = pluginId.trimmed();
  if (!ConfigManager::instance().effectivePluginEnabled(trimmed)) return false;
  for (const auto& i : m_pluginInfos) if (i.id == trimmed && i.scope == PluginScope::Workspace && !i.trusted) return false;
  return true;
}

bool PluginManager::trustWorkspacePlugin(const QString& pluginId) {
  const QString trimmed = pluginId.trimmed();
  LoadedPluginInfo* info = findInfoById(m_pluginInfos, trimmed);
  if (info == nullptr || info->libraryPath.isEmpty()) return false;
  if (info->sha256Hex.isEmpty()) info->sha256Hex = computeFileSha256Hex(info->libraryPath);
  if (info->sha256Hex.isEmpty()) return false;
  ConfigManager::instance().trustWorkspacePlugin(info->libraryPath, info->sha256Hex);
  info->trusted = true; info->errorMessage.clear();
  if (ConfigManager::instance().effectivePluginEnabled(trimmed) && findActiveHandle(trimmed) == nullptr) {
    (void)loadPluginInternal(info->libraryPath, PluginScope::Workspace);
  }
  emit pluginsChanged(); return true;
}

bool PluginManager::revokeWorkspacePluginTrust(const QString& pluginId) {
  const QString trimmed = pluginId.trimmed();
  LoadedPluginInfo* info = findInfoById(m_pluginInfos, trimmed);
  if (info == nullptr || info->libraryPath.isEmpty()) return false;
  ConfigManager::instance().revokeWorkspacePluginTrust(info->libraryPath);
  info->trusted = false;
  if (findActiveHandle(trimmed) != nullptr) info->requiresRestart = true;
  emit pluginsChanged(); return true;
}

void PluginManager::unloadAll() {
  if (!m_inDestructor && m_callbackDepth > 0) { m_pendingUnloadAll = true; return; }
  m_pendingStops.clear();
  auto retiring = std::move(m_activePlugins);
  m_activePlugins.clear(); m_pluginInfos.clear();
  ScopedCallbackDepth guard(this);
  for (auto it = retiring.rbegin(); it != retiring.rend(); ++it) shutdownAndUnloadHandle(*it);
  m_sessionActive = false;
}

void PluginManager::notifySessionStarted(int32_t rankCount) {
  m_sessionActive = true;
  ScopedCallbackDepth guard(this);
  for (size_t i = 0; i < m_activePlugins.size() && !m_pendingUnloadAll && !m_pendingDiscoverAndLoad; ++i) {
    auto& h = m_activePlugins[i];
    if (h.descriptor.on_session_start != nullptr) (void)h.descriptor.on_session_start(h.descriptor.plugin_ctx, h.hostApi.get(), rankCount);
  }
}

void PluginManager::notifyRankStopped(const GridLockStopInfo& stopInfo) {
  PendingStopEvent ev{stopInfo.reserved_flags, stopInfo.rank, stopInfo.thread_id, stopInfo.line,
                      stopInfo.frame_level, stopInfo.pc_address, {}, {}, {}, false, false, false};
  if (stopInfo.reason_utf8 != nullptr) { ev.reasonUtf8 = QByteArray(stopInfo.reason_utf8); ev.hasReason = true; }
  if (stopInfo.file_utf8 != nullptr) { ev.fileUtf8 = QByteArray(stopInfo.file_utf8); ev.hasFile = true; }
  if (stopInfo.function_utf8 != nullptr) { ev.functionUtf8 = QByteArray(stopInfo.function_utf8); ev.hasFunction = true; }
  m_pendingStops.enqueue(std::move(ev));
  if (m_dispatchingStop) return;

  m_dispatchingStop = true;
  ScopedCallbackDepth guard(this);
  while (!m_pendingStops.isEmpty() && !m_pendingUnloadAll && !m_pendingDiscoverAndLoad) {
    const PendingStopEvent cur = m_pendingStops.dequeue();
    GridLockStopInfo info{static_cast<uint32_t>(sizeof(GridLockStopInfo)), GRIDLOCK_PLUGIN_ABI_MAJOR,
                          GRIDLOCK_PLUGIN_ABI_MINOR, cur.reservedFlags, cur.rank, cur.threadId,
                          cur.line, cur.frameLevel, cur.pcAddress,
                          cur.hasReason ? cur.reasonUtf8.constData() : nullptr,
                          cur.hasFile ? cur.fileUtf8.constData() : nullptr,
                          cur.hasFunction ? cur.functionUtf8.constData() : nullptr};
    for (size_t i = 0; i < m_activePlugins.size() && !m_pendingUnloadAll && !m_pendingDiscoverAndLoad; ++i) {
      auto& h = m_activePlugins[i];
      if (h.descriptor.on_rank_stopped != nullptr) (void)h.descriptor.on_rank_stopped(h.descriptor.plugin_ctx, h.hostApi.get(), &info);
    }
  }
  m_dispatchingStop = false;
}

void PluginManager::notifyRankStopped(int32_t rank, const QString& file, int32_t line,
                                      const QString& function, uint64_t pcAddress, const QString& reason) {
  const QByteArray reasonUtf8 = reason.toUtf8(), fileUtf8 = file.toUtf8(), funcUtf8 = function.toUtf8();
  GridLockStopInfo info{};
  info.struct_size = static_cast<uint32_t>(sizeof(GridLockStopInfo));
  info.abi_major = GRIDLOCK_PLUGIN_ABI_MAJOR; info.abi_minor = GRIDLOCK_PLUGIN_ABI_MINOR;
  info.rank = rank; info.thread_id = rank + 1; info.line = line; info.pc_address = pcAddress;
  info.reason_utf8 = reasonUtf8.constData();
  info.file_utf8 = fileUtf8.isEmpty() ? nullptr : fileUtf8.constData();
  info.function_utf8 = funcUtf8.isEmpty() ? nullptr : funcUtf8.constData();
  notifyRankStopped(info);
}

void PluginManager::notifySessionEnded() {
  m_sessionActive = false;
  m_pendingStops.clear();
  ScopedCallbackDepth guard(this);
  for (size_t i = 0; i < m_activePlugins.size() && !m_pendingUnloadAll && !m_pendingDiscoverAndLoad; ++i) {
    auto& h = m_activePlugins[i];
    if (h.descriptor.on_session_end != nullptr) (void)h.descriptor.on_session_end(h.descriptor.plugin_ctx, h.hostApi.get());
  }
}

}  // namespace gridlock::core::plugins
