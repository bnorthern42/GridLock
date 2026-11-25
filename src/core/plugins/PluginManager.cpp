/**
 * @file PluginManager.cpp
 * @brief Implementation of shared-library discovery, ABI v1.0 enforcement, and plugin lifecycle dispatch.
 */

#include "PluginManager.hpp"

#include <algorithm>
#include <cstddef>

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

#include "../hpc/gdb/GdbRankCoordinator.hpp"
#include "../managers/ConfigManager.hpp"

namespace gridlock::core::plugins {

namespace {

/** @brief Safely reads a C UTF-8 pointer that may be `nullptr`. */
QString safeUtf8(const char* raw) {
  return (raw != nullptr) ? QString::fromUtf8(raw).trimmed() : QString();
}

}  // namespace

PluginManager::PluginManager(gridlock::GdbRankCoordinator* coordinator, QObject* parent)
    : QObject(parent), m_bridge(coordinator, this) {
  setCoordinator(coordinator);
}

PluginManager::~PluginManager() {
  unloadAll();
}

void PluginManager::setCoordinator(gridlock::GdbRankCoordinator* coordinator) {
  if (m_stopConn) {
    QObject::disconnect(m_stopConn);
  }
  m_coordinator = coordinator;
  m_bridge.setCoordinator(coordinator);
  if (coordinator != nullptr) {
    m_stopConn = QObject::connect(
        coordinator, &::IBackendCoordinator::locationChanged, this,
        [this](int rankId, const QString& file, int line) {
          if (m_coordinator.isNull()) return;
          const auto st = m_coordinator->getRankState(rankId);
          bool ok = false;
          const QString pcStr = st.registers.value(16);
          const uint64_t pc = pcStr.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)
                                  ? pcStr.mid(2).toULongLong(&ok, 16)
                                  : 0u;
          notifyRankStopped(rankId, file, line, st.currentFunction, pc,
                            QStringLiteral("breakpoint-hit"));
        });
  }
}

bool PluginManager::hasSharedLibrarySuffix(const QString& fileName) {
  return fileName.endsWith(QStringLiteral(".so"), Qt::CaseInsensitive) ||
         fileName.endsWith(QStringLiteral(".dylib"), Qt::CaseInsensitive) ||
         fileName.endsWith(QStringLiteral(".dll"), Qt::CaseInsensitive);
}

QStringList PluginManager::searchDirectories() const {
  QStringList dirs;
  const QString userPlugins = ConfigManager::configDirectory() + QStringLiteral("/plugins");
  dirs.append(QDir::cleanPath(userPlugins));

  const QString wsRoot = ConfigManager::instance().workspacePath().trimmed();
  if (!wsRoot.isEmpty()) {
    const QString wsPlugins = QDir::cleanPath(wsRoot + QStringLiteral("/.gridlock/plugins"));
    if (!dirs.contains(wsPlugins)) {
      dirs.append(wsPlugins);
    }
  }
  return dirs;
}

int PluginManager::discoverAndLoad() {
  unloadAll();
  QStringList seenCanonicalPaths;
  int activeCount = 0;

  for (const QString& dirPath : searchDirectories()) {
    QDir dir(dirPath);
    if (!dir.exists()) continue;
    const QFileInfoList entries =
        dir.entryInfoList(QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo& fi : entries) {
      if (!hasSharedLibrarySuffix(fi.fileName())) continue;
      const QString canonical =
          fi.canonicalFilePath().isEmpty() ? fi.absoluteFilePath() : fi.canonicalFilePath();
      if (seenCanonicalPaths.contains(canonical)) continue;
      seenCanonicalPaths.append(canonical);
      if (loadPluginFromPath(canonical)) {
        ++activeCount;
      }
    }
  }
  emit pluginsChanged();
  return activeCount;
}

void PluginManager::cachePluginMetadata(const QString& canonicalPath, const LoadedPluginInfo& info) {
  if (canonicalPath.isEmpty() || info.id.isEmpty()) return;
  m_pathMetadataCache.insert(canonicalPath, info);
  QSettings s(ConfigManager::workstationSettingsPath(), QSettings::IniFormat);
  const QString keyBase =
      QStringLiteral("plugin_cache/") +
      QString::fromLatin1(canonicalPath.toUtf8().toHex());
  s.setValue(keyBase + QStringLiteral("/id"), info.id);
  s.setValue(keyBase + QStringLiteral("/name"), info.name);
  s.setValue(keyBase + QStringLiteral("/version"), info.version);
  s.setValue(keyBase + QStringLiteral("/description"), info.description);
  s.setValue(keyBase + QStringLiteral("/abi_major"), info.abiMajor);
  s.setValue(keyBase + QStringLiteral("/abi_minor"), info.abiMinor);
  s.sync();
}

bool PluginManager::lookupCachedMetadata(const QString& canonicalPath,
                                         LoadedPluginInfo* outInfo) const {
  if (m_pathMetadataCache.contains(canonicalPath)) {
    *outInfo = m_pathMetadataCache.value(canonicalPath);
    return true;
  }
  QSettings s(ConfigManager::workstationSettingsPath(), QSettings::IniFormat);
  const QString keyBase =
      QStringLiteral("plugin_cache/") +
      QString::fromLatin1(canonicalPath.toUtf8().toHex());
  const QString cachedId = s.value(keyBase + QStringLiteral("/id")).toString().trimmed();
  if (cachedId.isEmpty()) return false;
  outInfo->id = cachedId;
  outInfo->name = s.value(keyBase + QStringLiteral("/name")).toString();
  outInfo->version = s.value(keyBase + QStringLiteral("/version")).toString();
  outInfo->description = s.value(keyBase + QStringLiteral("/description")).toString();
  outInfo->libraryPath = canonicalPath;
  outInfo->abiMajor = s.value(keyBase + QStringLiteral("/abi_major"), 1u).toUInt();
  outInfo->abiMinor = s.value(keyBase + QStringLiteral("/abi_minor"), 0u).toUInt();
  return true;
}

bool PluginManager::loadPluginFromPath(const QString& libraryPath) {
  const QFileInfo fi(libraryPath);
  const QString canonical =
      fi.canonicalFilePath().isEmpty() ? fi.absoluteFilePath() : fi.canonicalFilePath();

  auto upsertInfo = [this](const LoadedPluginInfo& info) {
    for (auto& existing : m_pluginInfos) {
      if (existing.libraryPath == info.libraryPath ||
          (!info.id.isEmpty() && existing.id == info.id && existing.enabled == info.enabled)) {
        existing = info;
        return;
      }
    }
    m_pluginInfos.append(info);
  };

  // Skip loading entirely when a cached plugin ID or file stem is marked disabled in ConfigManager.
  LoadedPluginInfo cached{};
  if (lookupCachedMetadata(canonical, &cached) &&
      ConfigManager::instance().isPluginDisabled(cached.id)) {
    cached.libraryPath = canonical;
    cached.enabled = false;
    cached.errorMessage.clear();
    upsertInfo(cached);
    return false;
  }
  if (ConfigManager::instance().isPluginDisabled(fi.completeBaseName()) ||
      ConfigManager::instance().isPluginDisabled(fi.fileName())) {
    LoadedPluginInfo skipped{};
    skipped.id = fi.completeBaseName();
    skipped.name = fi.fileName();
    skipped.libraryPath = canonical;
    skipped.enabled = false;
    upsertInfo(skipped);
    return false;
  }

  auto lib = std::make_unique<QLibrary>(canonical);
  lib->setLoadHints(QLibrary::ResolveAllSymbolsHint);
  if (!lib->load()) {
    LoadedPluginInfo failed{};
    failed.id = fi.completeBaseName();
    failed.name = fi.fileName();
    failed.libraryPath = canonical;
    failed.enabled = false;
    failed.errorMessage = QStringLiteral("Failed to load shared library: %1").arg(lib->errorString());
    upsertInfo(failed);
    return false;
  }

  const QFunctionPointer rawSym = lib->resolve(GRIDLOCK_PLUGIN_INIT_SYMBOL_V1);
  if (rawSym == nullptr) {
    const QString err = lib->errorString();
    lib->unload();
    LoadedPluginInfo failed{};
    failed.id = fi.completeBaseName();
    failed.name = fi.fileName();
    failed.libraryPath = canonical;
    failed.enabled = false;
    failed.errorMessage =
        QStringLiteral("Missing entry point '%1': %2")
            .arg(QLatin1String(GRIDLOCK_PLUGIN_INIT_SYMBOL_V1), err);
    upsertInfo(failed);
    return false;
  }

  // Reinterpret POSIX/Qt QFunctionPointer to the frozen C99 entry-point signature.
  const auto initFn = reinterpret_cast<GridLockPluginInitV1Fn>(rawSym);
  GridLockPluginDescriptor desc{};
  desc.struct_size = static_cast<uint32_t>(sizeof(GridLockPluginDescriptor));
  desc.abi_major = GRIDLOCK_PLUGIN_ABI_MAJOR;
  desc.abi_minor = GRIDLOCK_PLUGIN_ABI_MINOR;
  desc.reserved_flags = 0u;

  m_bridge.beginInitStaging();
  m_bridge.setCurrentPluginContextId(fi.completeBaseName());
  const GridLockStatus status = initFn(m_bridge.hostApi(), &desc);

  const bool abiMajorOk = (desc.abi_major == GRIDLOCK_PLUGIN_ABI_MAJOR);
  const bool structSizeOk =
      (desc.struct_size >= offsetof(GridLockPluginDescriptor, reserved)) &&
      (desc.struct_size <= sizeof(GridLockPluginDescriptor));

  auto rejectPlugin = [&](const QString& reason, bool canInvokeShutdown) {
    const QString parsedId = (abiMajorOk && structSizeOk) ? safeUtf8(desc.plugin_id_utf8) : QString();
    const QString parsedName = (abiMajorOk && structSizeOk) ? safeUtf8(desc.plugin_name_utf8) : QString();
    m_bridge.discardStagedActions();
    if (canInvokeShutdown && desc.shutdown != nullptr) {
      desc.shutdown(desc.plugin_ctx, m_bridge.hostApi());
    }
    m_bridge.setCurrentPluginContextId(QString());
    lib->unload();

    LoadedPluginInfo rejected{};
    rejected.id = !parsedId.isEmpty() ? parsedId : fi.completeBaseName();
    rejected.name = !parsedName.isEmpty() ? parsedName : fi.fileName();
    rejected.libraryPath = canonical;
    rejected.abiMajor = desc.abi_major;
    rejected.abiMinor = desc.abi_minor;
    rejected.enabled = false;
    rejected.errorMessage = reason;
    m_pluginInfos.append(rejected);
    return false;
  };

  if (status != GRIDLOCK_OK) {
    return rejectPlugin(
        QStringLiteral("gridlock_plugin_init_v1 failed with status %1").arg(status), false);
  }
  if (!abiMajorOk) {
    return rejectPlugin(QStringLiteral("ABI major mismatch: plugin reported v%1.%2, host requires v%3.x")
                            .arg(desc.abi_major)
                            .arg(desc.abi_minor)
                            .arg(GRIDLOCK_PLUGIN_ABI_MAJOR),
                        false);
  }
  if (!structSizeOk) {
    return rejectPlugin(
        QStringLiteral("Invalid GridLockPluginDescriptor struct_size (%1 bytes)").arg(desc.struct_size),
        false);
  }

  const QString pluginId = safeUtf8(desc.plugin_id_utf8);
  const QString pluginName = safeUtf8(desc.plugin_name_utf8);
  if (pluginId.isEmpty() || pluginName.isEmpty()) {
    return rejectPlugin(
        QStringLiteral("Plugin descriptor must provide non-empty UTF-8 id and name"), true);
  }

  const bool duplicateId = std::any_of(
      m_activePlugins.cbegin(), m_activePlugins.cend(),
      [&](const ActivePluginHandle& h) { return h.id == pluginId; });
  if (duplicateId) {
    return rejectPlugin(
        QStringLiteral("Duplicate plugin ID '%1' rejected").arg(pluginId), true);
  }

  LoadedPluginInfo info{};
  info.id = pluginId;
  info.name = pluginName;
  info.version = safeUtf8(desc.plugin_version_utf8);
  info.description = safeUtf8(desc.description_utf8);
  info.libraryPath = canonical;
  info.abiMajor = desc.abi_major;
  info.abiMinor = desc.abi_minor;
  cachePluginMetadata(canonical, info);

  // If ConfigManager marks this resolved plugin ID disabled on first discovery, tear down cleanly.
  if (ConfigManager::instance().isPluginDisabled(pluginId)) {
    m_bridge.discardStagedActions();
    if (desc.shutdown != nullptr) {
      m_bridge.setCurrentPluginContextId(pluginId);
      desc.shutdown(desc.plugin_ctx, m_bridge.hostApi());
    }
    m_bridge.setCurrentPluginContextId(QString());
    lib->unload();
    info.enabled = false;
    upsertInfo(info);
    return false;
  }

  if (!m_bridge.commitStagedActions(pluginId)) {
    return rejectPlugin(
        QStringLiteral("Plugin '%1' registered a duplicate action or command ID").arg(pluginId), true);
  }
  m_bridge.setCurrentPluginContextId(QString());

  info.enabled = true;
  upsertInfo(info);
  m_activePlugins.push_back(ActivePluginHandle{pluginId, canonical, desc, std::move(lib)});
  return true;
}

PluginManager::ActivePluginHandle* PluginManager::findActiveHandle(const QString& pluginId) {
  for (auto& handle : m_activePlugins) {
    if (handle.id == pluginId) return &handle;
  }
  return nullptr;
}

void PluginManager::shutdownAndUnloadHandle(ActivePluginHandle& handle) {
  m_bridge.unregisterPluginActions(handle.id);
  if (handle.descriptor.shutdown != nullptr) {
    m_bridge.setCurrentPluginContextId(handle.id);
    handle.descriptor.shutdown(handle.descriptor.plugin_ctx, m_bridge.hostApi());
    m_bridge.setCurrentPluginContextId(QString());
  }
  if (handle.library) {
    handle.library->unload();
    handle.library.reset();
  }
}

bool PluginManager::setPluginEnabled(const QString& pluginId, bool enabled) {
  const QString trimmed = pluginId.trimmed();
  if (trimmed.isEmpty()) return false;

  auto infoIt = std::find_if(m_pluginInfos.begin(), m_pluginInfos.end(),
                             [&](const LoadedPluginInfo& i) { return i.id == trimmed; });
  if (infoIt == m_pluginInfos.end()) return false;

  ConfigManager::instance().setPluginDisabled(trimmed, !enabled);
  if (!enabled) {
    for (auto it = m_activePlugins.begin(); it != m_activePlugins.end(); ++it) {
      if (it->id == trimmed) {
        shutdownAndUnloadHandle(*it);
        m_activePlugins.erase(it);
        break;
      }
    }
    infoIt->enabled = false;
    emit pluginsChanged();
    return true;
  }

  if (findActiveHandle(trimmed) != nullptr) {
    infoIt->enabled = true;
    return true;
  }
  const QString path = infoIt->libraryPath;
  const bool ok = loadPluginFromPath(path);
  emit pluginsChanged();
  return ok;
}

void PluginManager::unloadAll() {
  for (auto it = m_activePlugins.rbegin(); it != m_activePlugins.rend(); ++it) {
    shutdownAndUnloadHandle(*it);
  }
  m_activePlugins.clear();
  m_pluginInfos.clear();
}

void PluginManager::notifySessionStarted(int32_t rankCount) {
  for (auto& handle : m_activePlugins) {
    if (handle.descriptor.on_session_start != nullptr) {
      m_bridge.setCurrentPluginContextId(handle.id);
      handle.descriptor.on_session_start(handle.descriptor.plugin_ctx, m_bridge.hostApi(), rankCount);
      m_bridge.setCurrentPluginContextId(QString());
    }
  }
}

void PluginManager::notifyRankStopped(const GridLockStopInfo& stopInfo) {
  for (auto& handle : m_activePlugins) {
    if (handle.descriptor.on_rank_stopped != nullptr) {
      m_bridge.setCurrentPluginContextId(handle.id);
      handle.descriptor.on_rank_stopped(
          handle.descriptor.plugin_ctx, m_bridge.hostApi(), &stopInfo);
      m_bridge.setCurrentPluginContextId(QString());
    }
  }
}

void PluginManager::notifyRankStopped(int32_t rank,
                                      const QString& file,
                                      int32_t line,
                                      const QString& function,
                                      uint64_t pcAddress,
                                      const QString& reason) {
  const QByteArray reasonUtf8 = reason.toUtf8();
  const QByteArray fileUtf8 = file.toUtf8();
  const QByteArray funcUtf8 = function.toUtf8();

  GridLockStopInfo info{};
  info.struct_size = static_cast<uint32_t>(sizeof(GridLockStopInfo));
  info.abi_major = GRIDLOCK_PLUGIN_ABI_MAJOR;
  info.abi_minor = GRIDLOCK_PLUGIN_ABI_MINOR;
  info.reserved_flags = 0u;
  info.rank = rank;
  info.thread_id = rank + 1;
  info.line = line;
  info.frame_level = 0;
  info.pc_address = pcAddress;
  info.reason_utf8 = reasonUtf8.constData();
  info.file_utf8 = fileUtf8.isEmpty() ? nullptr : fileUtf8.constData();
  info.function_utf8 = funcUtf8.isEmpty() ? nullptr : funcUtf8.constData();
  notifyRankStopped(info);
}

void PluginManager::notifySessionEnded() {
  for (auto& handle : m_activePlugins) {
    if (handle.descriptor.on_session_end != nullptr) {
      m_bridge.setCurrentPluginContextId(handle.id);
      handle.descriptor.on_session_end(handle.descriptor.plugin_ctx, m_bridge.hostApi());
      m_bridge.setCurrentPluginContextId(QString());
    }
  }
}

}  // namespace gridlock::core::plugins
