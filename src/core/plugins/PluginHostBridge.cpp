/**
 * @file PluginHostBridge.cpp
 * @brief Implementation of the thread-safe synchronous `GridLockHostApi` bridge.
 *
 * Marshals synchronous `eval_expr`, `read_memory`, `write_memory`, and breakpoint
 * operations onto `GdbRankCoordinator`'s affinity thread, enforces bounded timeouts
 * via `QEventLoop` + `QTimer`, and manages per-plugin variable/command actions.
 */

#include "PluginHostBridge.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <functional>

#include <QByteArray>
#include <QEventLoop>
#include <QMetaObject>
#include <QThread>
#include <QTimer>

#include "../hpc/gdb/GdbRankCoordinator.hpp"

namespace gridlock::core::plugins {

namespace {

/** @brief Executes `fn` synchronously on `target`'s thread, or inline if already there. */
GridLockStatus runOnObjectThread(QObject* target, const std::function<GridLockStatus()>& fn) {
  if (target == nullptr) return GRIDLOCK_ERR_NOT_STOPPED;
  if (QThread::currentThread() == target->thread()) return fn();
  GridLockStatus result = GRIDLOCK_ERR_UNSUPPORTED;
  const bool ok = QMetaObject::invokeMethod(
      target, [&]() { result = fn(); }, Qt::BlockingQueuedConnection);
  return ok ? result : GRIDLOCK_ERR_UNSUPPORTED;
}

}  // namespace

PluginHostBridge::PluginHostBridge(gridlock::GdbRankCoordinator* coordinator, QObject* parent)
    : QObject(parent), m_coordinator(coordinator) {
  m_hostApi = GridLockHostApi{};
  m_hostApi.struct_size = static_cast<uint32_t>(sizeof(GridLockHostApi));
  m_hostApi.abi_major = GRIDLOCK_PLUGIN_ABI_MAJOR;
  m_hostApi.abi_minor = GRIDLOCK_PLUGIN_ABI_MINOR;
  m_hostApi.reserved_flags = 0u;
  m_hostApi.host_ctx = this;
  m_hostApi.alloc_bytes = &PluginHostBridge::cbAllocBytes;
  m_hostApi.free_bytes = &PluginHostBridge::cbFreeBytes;
  m_hostApi.log_message = &PluginHostBridge::cbLogMessage;
  m_hostApi.get_rank_count = &PluginHostBridge::cbGetRankCount;
  m_hostApi.get_active_rank = &PluginHostBridge::cbGetActiveRank;
  m_hostApi.eval_expr = &PluginHostBridge::cbEvalExpr;
  m_hostApi.read_memory = &PluginHostBridge::cbReadMemory;
  m_hostApi.write_memory = &PluginHostBridge::cbWriteMemory;
  m_hostApi.set_breakpoint = &PluginHostBridge::cbSetBreakpoint;
  m_hostApi.remove_breakpoint = &PluginHostBridge::cbRemoveBreakpoint;
  m_hostApi.register_variable_action = &PluginHostBridge::cbRegisterVariableAction;
  m_hostApi.register_command_action = &PluginHostBridge::cbRegisterCommandAction;
  m_hostApi.emit_structured_event = &PluginHostBridge::cbEmitStructuredEvent;
}

void PluginHostBridge::setCoordinator(gridlock::GdbRankCoordinator* coordinator) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_coordinator = coordinator;
}
gridlock::GdbRankCoordinator* PluginHostBridge::coordinator() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_coordinator.data();
}
void PluginHostBridge::setActiveRank(int32_t rank) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_activeRank = rank;
}
int32_t PluginHostBridge::activeRank() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_activeRank;
}
void PluginHostBridge::setRequestTimeoutMs(int timeoutMs) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_timeoutMs = std::max(10, timeoutMs);
}
int PluginHostBridge::requestTimeoutMs() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_timeoutMs;
}
void PluginHostBridge::beginInitStaging() {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_stagingActive = true;
  m_stagedVariableActions.clear();
  m_stagedCommandActions.clear();
}
void PluginHostBridge::discardStagedActions() {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_stagingActive = false;
  m_stagedVariableActions.clear();
  m_stagedCommandActions.clear();
}
void PluginHostBridge::setCurrentPluginContextId(const QString& pluginId) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_currentPluginId = pluginId;
}

bool PluginHostBridge::commitStagedActions(const QString& pluginId) {
  bool changed = false;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& act : m_stagedVariableActions) {
      if (std::any_of(m_variableActions.cbegin(), m_variableActions.cend(),
                      [&](const RegisteredVariableAction& a) { return a.actionId == act.actionId; })) {
        m_stagingActive = false; m_stagedVariableActions.clear(); m_stagedCommandActions.clear(); return false;
      }
    }
    for (const auto& cmd : m_stagedCommandActions) {
      if (std::any_of(m_commandActions.cbegin(), m_commandActions.cend(),
                      [&](const RegisteredCommandAction& c) { return c.commandId == cmd.commandId; })) {
        m_stagingActive = false; m_stagedVariableActions.clear(); m_stagedCommandActions.clear(); return false;
      }
    }
    m_stagingActive = false;
    for (auto& act : m_stagedVariableActions) { act.pluginId = pluginId; m_variableActions.append(std::move(act)); changed = true; }
    for (auto& cmd : m_stagedCommandActions) { cmd.pluginId = pluginId; m_commandActions.append(std::move(cmd)); changed = true; }
    m_stagedVariableActions.clear();
    m_stagedCommandActions.clear();
  }
  if (changed) emit registeredActionsChanged();
  return true;
}

void PluginHostBridge::unregisterPluginActions(const QString& pluginId) {
  bool removed = false;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto beforeVars = m_variableActions.size();
    const auto beforeCmds = m_commandActions.size();
    m_variableActions.removeIf([&](const RegisteredVariableAction& a) { return a.pluginId == pluginId; });
    m_commandActions.removeIf([&](const RegisteredCommandAction& c) { return c.pluginId == pluginId; });
    removed = (m_variableActions.size() != beforeVars) || (m_commandActions.size() != beforeCmds);
  }
  if (removed) emit registeredActionsChanged();
}

QVector<RegisteredVariableAction> PluginHostBridge::variableActions() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_variableActions;
}
QVector<RegisteredCommandAction> PluginHostBridge::commandActions() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_commandActions;
}
QVector<RegisteredVariableAction> PluginHostBridge::matchingVariableActions(const QString& typeName) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  QVector<RegisteredVariableAction> matches;
  for (const auto& act : m_variableActions) {
    if (act.compiledRegex.isValid() && act.compiledRegex.match(typeName).hasMatch()) matches.append(act);
  }
  return matches;
}

GridLockStatus PluginHostBridge::invokeVariableAction(const QString& actionId,
                                                      const GridLockVariableContext& varCtx) {
  RegisteredVariableAction target;
  QString prevPluginId;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = std::find_if(m_variableActions.cbegin(), m_variableActions.cend(),
                           [&](const RegisteredVariableAction& a) { return a.actionId == actionId; });
    if (it == m_variableActions.cend() || it->callback == nullptr) return GRIDLOCK_ERR_INVALID_ARG;
    target = *it;
    prevPluginId = m_currentPluginId;
    m_currentPluginId = target.pluginId;
  }
  const GridLockStatus status = target.callback(target.pluginCtx, &m_hostApi, &varCtx);
  setCurrentPluginContextId(prevPluginId);
  return status;
}

GridLockStatus PluginHostBridge::invokeCommandAction(const QString& commandId,
                                                     const GridLockCommandSpec& cmdSpec) {
  RegisteredCommandAction target;
  QString prevPluginId;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = std::find_if(m_commandActions.cbegin(), m_commandActions.cend(),
                           [&](const RegisteredCommandAction& c) { return c.commandId == commandId; });
    if (it == m_commandActions.cend() || it->callback == nullptr) return GRIDLOCK_ERR_INVALID_ARG;
    target = *it;
    prevPluginId = m_currentPluginId;
    m_currentPluginId = target.pluginId;
  }
  const GridLockStatus status = target.callback(target.pluginCtx, &m_hostApi, &cmdSpec);
  setCurrentPluginContextId(prevPluginId);
  return status;
}

void* PluginHostBridge::cbAllocBytes(void* hostCtx, size_t numBytes) {
  auto* self = static_cast<PluginHostBridge*>(hostCtx);
  if (self == nullptr || numBytes == 0u) return nullptr;
  void* mem = std::malloc(numBytes);
  if (mem != nullptr) self->m_liveAllocations.fetch_add(1, std::memory_order_relaxed);
  return mem;
}
void PluginHostBridge::cbFreeBytes(void* hostCtx, void* ptr) {
  auto* self = static_cast<PluginHostBridge*>(hostCtx);
  if (self != nullptr && ptr != nullptr) {
    self->m_liveAllocations.fetch_sub(1, std::memory_order_relaxed);
    std::free(ptr);
  }
}
GridLockStatus PluginHostBridge::cbLogMessage(void* hostCtx, GridLockLogLevel level, const char* utf8Msg) {
  auto* self = static_cast<PluginHostBridge*>(hostCtx);
  if (self == nullptr || utf8Msg == nullptr || level < GRIDLOCK_LOG_TRACE || level > GRIDLOCK_LOG_ERROR)
    return GRIDLOCK_ERR_INVALID_ARG;
  QString ownerId;
  { std::lock_guard<std::mutex> lock(self->m_mutex); ownerId = self->m_currentPluginId; }
  emit self->pluginLogReceived(ownerId, static_cast<int>(level), QString::fromUtf8(utf8Msg));
  return GRIDLOCK_OK;
}
GridLockStatus PluginHostBridge::cbGetRankCount(void* hostCtx, int32_t* outRanks) {
  auto* self = static_cast<PluginHostBridge*>(hostCtx);
  if (self == nullptr || outRanks == nullptr) return GRIDLOCK_ERR_INVALID_ARG;
  auto* coord = self->coordinator();
  if (coord == nullptr) { *outRanks = 0; return GRIDLOCK_OK; }
  return runOnObjectThread(coord, [&]() {
    *outRanks = static_cast<int32_t>(coord->getProcessCount());
    return GRIDLOCK_OK;
  });
}
GridLockStatus PluginHostBridge::cbGetActiveRank(void* hostCtx, int32_t* outRank) {
  auto* self = static_cast<PluginHostBridge*>(hostCtx);
  if (self == nullptr || outRank == nullptr) return GRIDLOCK_ERR_INVALID_ARG;
  *outRank = self->activeRank();
  return GRIDLOCK_OK;
}

GridLockStatus PluginHostBridge::evalExprOnCoordinatorThread(
    gridlock::GdbRankCoordinator* coord, int32_t rank, const QString& expr, int timeoutMs, QByteArray* outPayload) {
  if (rank < 0 || rank >= coord->getProcessCount()) return GRIDLOCK_ERR_INVALID_RANK;
  if (coord->getRankState(rank).currentState != QLatin1String("stopped")) return GRIDLOCK_ERR_NOT_STOPPED;

  QEventLoop loop;
  QTimer timer;
  timer.setSingleShot(true);
  bool completed = false, timedOut = false;
  QString evalValue;

  const auto evalConn = QObject::connect(
      coord, &::IBackendCoordinator::expressionEvaluated, &loop,
      [&](int r, const QString& resolvedExpr, const QString& result) {
        if (!completed && r == rank && (resolvedExpr == expr || resolvedExpr.isEmpty())) {
          evalValue = result;
          completed = true;
          loop.quit();
        }
      });
  const auto timerConn = QObject::connect(&timer, &QTimer::timeout, &loop, [&]() {
    if (!completed) { timedOut = true; completed = true; loop.quit(); }
  });
  coord->evaluateExpression(rank, expr);
  if (!completed) { timer.start(timeoutMs); loop.exec(QEventLoop::ExcludeUserInputEvents); }
  QObject::disconnect(evalConn);
  QObject::disconnect(timerConn);

  if (timedOut) return GRIDLOCK_ERR_TIMEOUT;
  if (evalValue.startsWith(QStringLiteral("Error: "))) return GRIDLOCK_ERR_EVAL_FAILED;
  *outPayload = evalValue.toUtf8();
  return GRIDLOCK_OK;
}

GridLockStatus PluginHostBridge::cbEvalExpr(
    void* hostCtx, int32_t rank, const char* exprUtf8, char* outUtf8, size_t cap, size_t* outLen) {
  auto* self = static_cast<PluginHostBridge*>(hostCtx);
  if (self == nullptr || exprUtf8 == nullptr || exprUtf8[0] == '\0' || outLen == nullptr ||
      (cap > 0u && outUtf8 == nullptr)) {
    return GRIDLOCK_ERR_INVALID_ARG;
  }
  auto* coord = self->coordinator();
  const int timeoutMs = self->requestTimeoutMs();
  const QString expr = QString::fromUtf8(exprUtf8);
  QByteArray utf8Result;

  const GridLockStatus st = runOnObjectThread(coord, [&]() {
    return self->evalExprOnCoordinatorThread(coord, rank, expr, timeoutMs, &utf8Result);
  });
  if (st != GRIDLOCK_OK) return st;

  const size_t payloadLen = static_cast<size_t>(utf8Result.size());
  *outLen = payloadLen;
  if (outUtf8 == nullptr || cap <= payloadLen) return GRIDLOCK_ERR_BUFFER_TOO_SMALL;
  if (payloadLen > 0u) std::memcpy(outUtf8, utf8Result.constData(), payloadLen);
  outUtf8[payloadLen] = '\0';
  return GRIDLOCK_OK;
}

GridLockStatus PluginHostBridge::readMemoryOnCoordinatorThread(
    gridlock::GdbRankCoordinator* coord, int32_t rank, uint64_t address, size_t numBytes, int timeoutMs, QByteArray* outBytes) {
  if (rank < 0 || rank >= coord->getProcessCount()) return GRIDLOCK_ERR_INVALID_RANK;
  if (coord->getRankState(rank).currentState != QLatin1String("stopped")) return GRIDLOCK_ERR_NOT_STOPPED;
  if (numBytes == 0u) {
    outBytes->clear();
    return GRIDLOCK_OK;
  }

  QEventLoop loop;
  QTimer timer;
  timer.setSingleShot(true);
  bool completed = false, timedOut = false, failed = false;

  const auto memConn = QObject::connect(
      coord, &::IBackendCoordinator::memoryRead, &loop,
      [&](int r, const QString&, const QByteArray& data) {
        if (!completed && r == rank) {
          *outBytes = data;
          completed = true;
          loop.quit();
        }
      });
  const auto errConn = QObject::connect(
      coord, &gridlock::GdbRankCoordinator::gdbOutputReceived, &loop,
      [&](int r, const QString& line) {
        if (!completed && r == rank && line.startsWith(QLatin1String("^error"))) {
          failed = true;
          completed = true;
          loop.quit();
        }
      });
  const auto timerConn = QObject::connect(&timer, &QTimer::timeout, &loop, [&]() {
    if (!completed) { timedOut = true; completed = true; loop.quit(); }
  });
  coord->readMemory(rank, QStringLiteral("0x%1").arg(address, 0, 16), static_cast<int>(numBytes));
  if (!completed) { timer.start(timeoutMs); loop.exec(QEventLoop::ExcludeUserInputEvents); }
  QObject::disconnect(memConn);
  QObject::disconnect(errConn);
  QObject::disconnect(timerConn);

  if (timedOut) return GRIDLOCK_ERR_TIMEOUT;
  if (failed) return GRIDLOCK_ERR_MEMORY_READ_FAILED;
  return GRIDLOCK_OK;
}

GridLockStatus PluginHostBridge::cbReadMemory(
    void* hostCtx, int32_t rank, uint64_t address, void* outBytes, size_t numBytes, size_t* outRead) {
  auto* self = static_cast<PluginHostBridge*>(hostCtx);
  if (self == nullptr || outRead == nullptr || (numBytes > 0u && outBytes == nullptr)) {
    return GRIDLOCK_ERR_INVALID_ARG;
  }
  auto* coord = self->coordinator();
  const int timeoutMs = self->requestTimeoutMs();
  QByteArray raw;
  const GridLockStatus st = runOnObjectThread(coord, [&]() {
    return self->readMemoryOnCoordinatorThread(coord, rank, address, numBytes, timeoutMs, &raw);
  });
  if (st != GRIDLOCK_OK) return st;
  const size_t copied = std::min(numBytes, static_cast<size_t>(raw.size()));
  if (copied > 0u) std::memcpy(outBytes, raw.constData(), copied);
  *outRead = copied;
  return GRIDLOCK_OK;
}

GridLockStatus PluginHostBridge::cbWriteMemory(
    void* hostCtx, int32_t rank, uint64_t address, const void* inBytes, size_t numBytes, size_t* outWritten) {
  auto* self = static_cast<PluginHostBridge*>(hostCtx);
  if (self == nullptr || outWritten == nullptr || (numBytes > 0u && inBytes == nullptr)) {
    return GRIDLOCK_ERR_INVALID_ARG;
  }
  auto* coord = self->coordinator();
  const QByteArray payload(static_cast<const char*>(inBytes), static_cast<qsizetype>(numBytes));
  return runOnObjectThread(coord, [&]() {
    if (rank < 0 || rank >= coord->getProcessCount()) return GRIDLOCK_ERR_INVALID_RANK;
    if (coord->getRankState(rank).currentState != QLatin1String("stopped")) return GRIDLOCK_ERR_NOT_STOPPED;
    if (numBytes > 0u) coord->writeMemory(rank, address, payload);
    *outWritten = numBytes;
    return GRIDLOCK_OK;
  });
}

GridLockStatus PluginHostBridge::cbSetBreakpoint(
    void* hostCtx, const char* fileUtf8, int32_t line, const char* conditionUtf8, int32_t* outBpId) {
  auto* self = static_cast<PluginHostBridge*>(hostCtx);
  if (self == nullptr || fileUtf8 == nullptr || fileUtf8[0] == '\0' || line <= 0 || outBpId == nullptr) {
    return GRIDLOCK_ERR_INVALID_ARG;
  }
  auto* coord = self->coordinator();
  if (coord == nullptr) return GRIDLOCK_ERR_UNSUPPORTED;
  const QString file = QString::fromUtf8(fileUtf8);
  const QString cond = (conditionUtf8 != nullptr) ? QString::fromUtf8(conditionUtf8) : QString();
  int32_t bpId = 0;
  {
    std::lock_guard<std::mutex> lock(self->m_mutex);
    bpId = self->m_nextBreakpointId++;
    self->m_breakpoints.insert(bpId, PluginBreakpointRecord{file, line, cond});
  }
  const GridLockStatus st = runOnObjectThread(coord, [&]() {
    coord->setBreakpoint(file, line, cond, -1);
    return GRIDLOCK_OK;
  });
  if (st == GRIDLOCK_OK) *outBpId = bpId;
  return st;
}

GridLockStatus PluginHostBridge::cbRemoveBreakpoint(void* hostCtx, int32_t bpId) {
  auto* self = static_cast<PluginHostBridge*>(hostCtx);
  if (self == nullptr || bpId <= 0) return GRIDLOCK_ERR_INVALID_ARG;
  PluginBreakpointRecord rec;
  {
    std::lock_guard<std::mutex> lock(self->m_mutex);
    if (!self->m_breakpoints.contains(bpId)) return GRIDLOCK_ERR_INVALID_ARG;
    rec = self->m_breakpoints.take(bpId);
  }
  auto* coord = self->coordinator();
  if (coord == nullptr) return GRIDLOCK_ERR_UNSUPPORTED;
  return runOnObjectThread(coord, [&]() {
    coord->broadcastBreakpoint(rec.file, rec.line, false);
    return GRIDLOCK_OK;
  });
}

GridLockStatus PluginHostBridge::cbRegisterVariableAction(
    void* hostCtx, const char* actionId, const char* menuLabel, const char* typeRegex,
    GridLockVariableActionCallback cb, void* pluginCtx) {
  auto* self = static_cast<PluginHostBridge*>(hostCtx);
  if (self == nullptr || actionId == nullptr || actionId[0] == '\0' || menuLabel == nullptr ||
      menuLabel[0] == '\0' || typeRegex == nullptr || typeRegex[0] == '\0' || cb == nullptr) {
    return GRIDLOCK_ERR_INVALID_ARG;
  }
  const QRegularExpression compiled(QString::fromUtf8(typeRegex));
  if (!compiled.isValid()) return GRIDLOCK_ERR_INVALID_ARG;

  const QString idStr = QString::fromUtf8(actionId);
  bool notify = false;
  {
    std::lock_guard<std::mutex> lock(self->m_mutex);
    const auto dupActive = !self->m_stagingActive && std::any_of(
        self->m_variableActions.cbegin(), self->m_variableActions.cend(),
        [&](const RegisteredVariableAction& a) { return a.actionId == idStr; });
    const auto dupStaged = std::any_of(
        self->m_stagedVariableActions.cbegin(), self->m_stagedVariableActions.cend(),
        [&](const RegisteredVariableAction& a) { return a.actionId == idStr; });
    if (dupActive || dupStaged) return GRIDLOCK_ERR_INVALID_ARG;

    RegisteredVariableAction entry{self->m_currentPluginId, idStr, QString::fromUtf8(menuLabel),
                                   QString::fromUtf8(typeRegex), compiled, cb, pluginCtx};
    if (self->m_stagingActive) self->m_stagedVariableActions.append(std::move(entry));
    else { self->m_variableActions.append(std::move(entry)); notify = true; }
  }
  if (notify) emit self->registeredActionsChanged();
  return GRIDLOCK_OK;
}

GridLockStatus PluginHostBridge::cbRegisterCommandAction(
    void* hostCtx, const char* commandId, const char* menuLabel, const char* shortcutHint,
    GridLockCommandActionCallback cb, void* pluginCtx) {
  auto* self = static_cast<PluginHostBridge*>(hostCtx);
  if (self == nullptr || commandId == nullptr || commandId[0] == '\0' || menuLabel == nullptr ||
      menuLabel[0] == '\0' || cb == nullptr) {
    return GRIDLOCK_ERR_INVALID_ARG;
  }
  const QString idStr = QString::fromUtf8(commandId);
  bool notify = false;
  {
    std::lock_guard<std::mutex> lock(self->m_mutex);
    const auto dupActive = !self->m_stagingActive && std::any_of(
        self->m_commandActions.cbegin(), self->m_commandActions.cend(),
        [&](const RegisteredCommandAction& c) { return c.commandId == idStr; });
    const auto dupStaged = std::any_of(
        self->m_stagedCommandActions.cbegin(), self->m_stagedCommandActions.cend(),
        [&](const RegisteredCommandAction& c) { return c.commandId == idStr; });
    if (dupActive || dupStaged) return GRIDLOCK_ERR_INVALID_ARG;

    RegisteredCommandAction entry{
        self->m_currentPluginId, idStr, QString::fromUtf8(menuLabel),
        (shortcutHint != nullptr) ? QString::fromUtf8(shortcutHint) : QString(), cb, pluginCtx};
    if (self->m_stagingActive) self->m_stagedCommandActions.append(std::move(entry));
    else { self->m_commandActions.append(std::move(entry)); notify = true; }
  }
  if (notify) emit self->registeredActionsChanged();
  return GRIDLOCK_OK;
}

GridLockStatus PluginHostBridge::cbEmitStructuredEvent(
    void* hostCtx, const char* channelUtf8, const char* jsonPayloadUtf8) {
  auto* self = static_cast<PluginHostBridge*>(hostCtx);
  if (self == nullptr || channelUtf8 == nullptr || channelUtf8[0] == '\0' || jsonPayloadUtf8 == nullptr) {
    return GRIDLOCK_ERR_INVALID_ARG;
  }
  emit self->structuredEventEmitted(QString::fromUtf8(channelUtf8), QString::fromUtf8(jsonPayloadUtf8));
  return GRIDLOCK_OK;
}

}  // namespace gridlock::core::plugins
