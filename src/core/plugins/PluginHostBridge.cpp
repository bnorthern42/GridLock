/**
 * @file PluginHostBridge.cpp
 * @brief Implementation of the thread-safe synchronous `GridLockHostApi` bridge.
 */

#include "PluginHostBridge.hpp"

#include <cstdlib>
#include <cstring>

#include <QByteArray>
#include <QEventLoop>
#include <QMetaObject>
#include <QThread>
#include <QTimer>

#include "../hpc/gdb/GdbRankCoordinator.hpp"

namespace gridlock::core::plugins {

namespace {

class ScopedHookGuard {
 public:
  explicit ScopedHookGuard(std::function<void(bool)> hook) : m_hook(std::move(hook)) { if (m_hook) m_hook(true); }
  ~ScopedHookGuard() { if (m_hook) m_hook(false); }
  ScopedHookGuard(const ScopedHookGuard&) = delete;
  ScopedHookGuard& operator=(const ScopedHookGuard&) = delete;
 private:
  std::function<void(bool)> m_hook;
};

struct CoordinatorReplyWaiter {
  QEventLoop loop;
  QTimer timer;
  bool completed = false, timedOut = false, aborted = false;
  QMetaObject::Connection stateConn, sessErrConn, destroyConn, timerConn;

  CoordinatorReplyWaiter(gridlock::GdbRankCoordinator* coord, int32_t rank) {
    timer.setSingleShot(true);
    stateConn = QObject::connect(coord, &gridlock::GdbRankCoordinator::rankStateChanged, &loop,
                                 [this, rank](int r, const RankState& st) {
                                   if (!completed && r == rank && st.currentState != QLatin1String("stopped")) {
                                     aborted = true; completed = true; loop.quit();
                                   }
                                 });
    sessErrConn = QObject::connect(coord, &::IBackendCoordinator::sessionErrorOccurred, &loop,
                                   [this]() { if (!completed) { aborted = true; completed = true; loop.quit(); } });
    destroyConn = QObject::connect(coord, &QObject::destroyed, &loop,
                                   [this]() { if (!completed) { aborted = true; completed = true; loop.quit(); } });
    timerConn = QObject::connect(&timer, &QTimer::timeout, &loop,
                                 [this]() { if (!completed) { timedOut = true; completed = true; loop.quit(); } });
  }
  ~CoordinatorReplyWaiter() {
    QObject::disconnect(stateConn); QObject::disconnect(sessErrConn);
    QObject::disconnect(destroyConn); QObject::disconnect(timerConn);
  }
  void waitIfPending(int timeoutMs) {
    if (!completed) { timer.start(timeoutMs); loop.exec(QEventLoop::ExcludeUserInputEvents); }
  }
};

}  // namespace

PluginHostBridge::PluginHostBridge(gridlock::GdbRankCoordinator* coordinator, QObject* parent)
    : QObject(parent), m_coordinator(coordinator) {
  m_defaultContext.bridge = this;
  m_hostApi = makeHostApi(&m_defaultContext);
  m_defaultContext.boundApi = &m_hostApi;
}

void PluginHostBridge::setCoordinator(gridlock::GdbRankCoordinator* coordinator) {
  std::lock_guard<std::mutex> lock(m_mutex); m_coordinator = coordinator;
}
gridlock::GdbRankCoordinator* PluginHostBridge::coordinator() const {
  std::lock_guard<std::mutex> lock(m_mutex); return m_coordinator.data();
}

GridLockStatus PluginHostBridge::runOnObjectThread(
    const PluginBindingContext* ctx,
    const std::function<GridLockStatus(gridlock::GdbRankCoordinator*)>& fn,
    GridLockStatus missingCoordStatus) {
  if (ctx != nullptr && ctx->shuttingDown.load(std::memory_order_acquire)) return GRIDLOCK_ERR_NOT_STOPPED;
  auto invokeWithCoord = [&]() -> GridLockStatus {
    if (ctx != nullptr && ctx->shuttingDown.load(std::memory_order_acquire)) return GRIDLOCK_ERR_NOT_STOPPED;
    QPointer<gridlock::GdbRankCoordinator> coord;
    { std::lock_guard<std::mutex> lock(m_mutex); coord = m_coordinator; }
    return coord.isNull() ? missingCoordStatus : fn(coord.data());
  };
  if (QThread::currentThread() == this->thread()) return invokeWithCoord();
  GridLockStatus result = GRIDLOCK_ERR_UNSUPPORTED;
  const bool ok = QMetaObject::invokeMethod(this, [&]() { result = invokeWithCoord(); }, Qt::BlockingQueuedConnection);
  return ok ? result : GRIDLOCK_ERR_UNSUPPORTED;
}

void PluginHostBridge::discardStagedActions() {
  QVector<PluginBreakpointRecord> stagedBps;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stagingActive = false; m_stagedVariableActions.clear(); m_stagedCommandActions.clear();
    for (auto it = m_breakpoints.begin(); it != m_breakpoints.end();) {
      if (it.value().staged) { stagedBps.append(it.value()); it = m_breakpoints.erase(it); } else ++it;
    }
  }
  if (!stagedBps.isEmpty()) {
    (void)runOnObjectThread(nullptr, [&](gridlock::GdbRankCoordinator* coord) {
      for (const auto& rec : stagedBps) coord->broadcastBreakpoint(rec.file, rec.line, false);
      return GRIDLOCK_OK;
    });
  }
}

bool PluginHostBridge::commitStagedActions(const QString& pluginId, const GridLockHostApi* pluginHostApi) {
  bool changed = false;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& act : m_stagedVariableActions) {
      if (std::any_of(m_variableActions.cbegin(), m_variableActions.cend(),
                      [&](const RegisteredVariableAction& a) { return a.actionId == act.actionId; })) return false;
    }
    for (const auto& cmd : m_stagedCommandActions) {
      if (std::any_of(m_commandActions.cbegin(), m_commandActions.cend(),
                      [&](const RegisteredCommandAction& c) { return c.commandId == cmd.commandId; })) return false;
    }
    m_stagingActive = false;
    for (auto& act : m_stagedVariableActions) {
      act.pluginId = pluginId; if (pluginHostApi != nullptr) act.hostApi = pluginHostApi;
      m_variableActions.append(std::move(act)); changed = true;
    }
    for (auto& cmd : m_stagedCommandActions) {
      cmd.pluginId = pluginId; if (pluginHostApi != nullptr) cmd.hostApi = pluginHostApi;
      m_commandActions.append(std::move(cmd)); changed = true;
    }
    for (auto& bp : m_breakpoints) if (bp.staged) { bp.pluginId = pluginId; bp.staged = false; }
    m_stagedVariableActions.clear(); m_stagedCommandActions.clear();
  }
  if (changed) emit registeredActionsChanged();
  return true;
}

void PluginHostBridge::unregisterPluginBreakpoints(const QString& pluginId) {
  if (pluginId.isEmpty()) return;
  QVector<PluginBreakpointRecord> removedBps;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto it = m_breakpoints.begin(); it != m_breakpoints.end();) {
      if (it.value().pluginId == pluginId) { removedBps.append(it.value()); it = m_breakpoints.erase(it); } else ++it;
    }
  }
  if (!removedBps.isEmpty()) {
    (void)runOnObjectThread(nullptr, [&](gridlock::GdbRankCoordinator* coord) {
      for (const auto& rec : removedBps) coord->broadcastBreakpoint(rec.file, rec.line, false);
      return GRIDLOCK_OK;
    });
  }
}

GridLockStatus PluginHostBridge::invokeVariableAction(const QString& actionId,
                                                      const GridLockVariableContext& varCtx) {
  RegisteredVariableAction target;
  std::function<void(bool)> hook;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = std::find_if(m_variableActions.cbegin(), m_variableActions.cend(),
                           [&](const RegisteredVariableAction& a) { return a.actionId == actionId; });
    if (it == m_variableActions.cend() || it->callback == nullptr) return GRIDLOCK_ERR_INVALID_ARG;
    target = *it; hook = m_callbackScopeHook;
  }
  if (target.bindingCtx != nullptr && target.bindingCtx->shuttingDown.load(std::memory_order_acquire)) {
    return GRIDLOCK_ERR_UNSUPPORTED;
  }
  ScopedHookGuard scope(std::move(hook));
  try {
    return target.callback(target.pluginCtx, (target.hostApi != nullptr) ? target.hostApi : &m_hostApi, &varCtx);
  } catch (...) { return GRIDLOCK_ERR_UNSUPPORTED; }
}

GridLockStatus PluginHostBridge::invokeCommandAction(const QString& commandId,
                                                     const GridLockCommandSpec& cmdSpec) {
  RegisteredCommandAction target;
  std::function<void(bool)> hook;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = std::find_if(m_commandActions.cbegin(), m_commandActions.cend(),
                           [&](const RegisteredCommandAction& c) { return c.commandId == commandId; });
    if (it == m_commandActions.cend() || it->callback == nullptr) return GRIDLOCK_ERR_INVALID_ARG;
    target = *it; hook = m_callbackScopeHook;
  }
  if (target.bindingCtx != nullptr && target.bindingCtx->shuttingDown.load(std::memory_order_acquire)) {
    return GRIDLOCK_ERR_UNSUPPORTED;
  }
  ScopedHookGuard scope(std::move(hook));
  try {
    return target.callback(target.pluginCtx, (target.hostApi != nullptr) ? target.hostApi : &m_hostApi, &cmdSpec);
  } catch (...) { return GRIDLOCK_ERR_UNSUPPORTED; }
}

void* GRIDLOCK_CALL PluginHostBridge::cbAllocBytes(void* hostCtx, size_t numBytes) noexcept {
  try {
    auto* ctx = static_cast<PluginBindingContext*>(hostCtx);
    if (ctx == nullptr || ctx->bridge == nullptr || numBytes == 0u) return nullptr;
    void* mem = std::malloc(numBytes);
    if (mem != nullptr) ctx->bridge->m_liveAllocations.fetch_add(1, std::memory_order_relaxed);
    return mem;
  } catch (...) { return nullptr; }
}

void GRIDLOCK_CALL PluginHostBridge::cbFreeBytes(void* hostCtx, void* ptr) noexcept {
  try {
    auto* ctx = static_cast<PluginBindingContext*>(hostCtx);
    if (ctx != nullptr && ctx->bridge != nullptr && ptr != nullptr) {
      ctx->bridge->m_liveAllocations.fetch_sub(1, std::memory_order_relaxed);
      std::free(ptr);
    }
  } catch (...) {}
}

GridLockStatus GRIDLOCK_CALL PluginHostBridge::cbLogMessage(
    void* hostCtx, GridLockLogLevel level, const char* utf8Msg) noexcept {
  try {
    auto* ctx = static_cast<PluginBindingContext*>(hostCtx);
    if (ctx == nullptr || ctx->bridge == nullptr || utf8Msg == nullptr ||
        level < GRIDLOCK_LOG_TRACE || level > GRIDLOCK_LOG_ERROR) return GRIDLOCK_ERR_INVALID_ARG;
    emit ctx->bridge->pluginLogReceived(ctx->pluginId, static_cast<int>(level), QString::fromUtf8(utf8Msg));
    return GRIDLOCK_OK;
  } catch (...) { return GRIDLOCK_ERR_UNSUPPORTED; }
}

GridLockStatus GRIDLOCK_CALL PluginHostBridge::cbGetRankCount(void* hostCtx, int32_t* outRanks) noexcept {
  try {
    auto* ctx = static_cast<PluginBindingContext*>(hostCtx);
    if (ctx == nullptr || ctx->bridge == nullptr || outRanks == nullptr) return GRIDLOCK_ERR_INVALID_ARG;
    *outRanks = 0;
    return ctx->bridge->runOnObjectThread(
        ctx, [&](gridlock::GdbRankCoordinator* coord) {
          *outRanks = static_cast<int32_t>(coord->getProcessCount());
          return GRIDLOCK_OK;
        }, GRIDLOCK_OK);
  } catch (...) { return GRIDLOCK_ERR_UNSUPPORTED; }
}

GridLockStatus GRIDLOCK_CALL PluginHostBridge::cbGetActiveRank(void* hostCtx, int32_t* outRank) noexcept {
  try {
    auto* ctx = static_cast<PluginBindingContext*>(hostCtx);
    if (ctx == nullptr || ctx->bridge == nullptr || outRank == nullptr) return GRIDLOCK_ERR_INVALID_ARG;
    *outRank = ctx->bridge->activeRank();
    return GRIDLOCK_OK;
  } catch (...) { return GRIDLOCK_ERR_UNSUPPORTED; }
}

GridLockStatus PluginHostBridge::evalExprOnCoordinatorThread(
    const PluginBindingContext* ctx, gridlock::GdbRankCoordinator* coord, int32_t rank,
    const QString& expr, int timeoutMs, QByteArray* outPayload) {
  if (ctx != nullptr && ctx->shuttingDown.load(std::memory_order_acquire)) return GRIDLOCK_ERR_NOT_STOPPED;
  if (rank < 0 || rank >= coord->getProcessCount()) return GRIDLOCK_ERR_INVALID_RANK;
  if (coord->getRankState(rank).currentState != QLatin1String("stopped")) return GRIDLOCK_ERR_NOT_STOPPED;

  CoordinatorReplyWaiter waiter(coord, rank);
  QString evalValue;
  const auto evalConn = QObject::connect(
      coord, &::IBackendCoordinator::expressionEvaluated, &waiter.loop,
      [&](int r, const QString& resolvedExpr, const QString& result) {
        if (!waiter.completed && r == rank && (resolvedExpr == expr || resolvedExpr.isEmpty())) {
          evalValue = result; waiter.completed = true; waiter.loop.quit();
        }
      });
  coord->evaluateExpression(rank, expr);
  waiter.waitIfPending(timeoutMs);
  QObject::disconnect(evalConn);

  if (waiter.aborted || (ctx != nullptr && ctx->shuttingDown.load(std::memory_order_acquire))) return GRIDLOCK_ERR_NOT_STOPPED;
  if (waiter.timedOut) return GRIDLOCK_ERR_TIMEOUT;
  if (evalValue.startsWith(QStringLiteral("Error: "))) return GRIDLOCK_ERR_EVAL_FAILED;
  *outPayload = evalValue.toUtf8();
  return GRIDLOCK_OK;
}

GridLockStatus GRIDLOCK_CALL PluginHostBridge::cbEvalExpr(
    void* hostCtx, int32_t rank, const char* exprUtf8, char* outUtf8, size_t cap, size_t* outLen) noexcept {
  try {
    auto* ctx = static_cast<PluginBindingContext*>(hostCtx);
    if (ctx == nullptr || ctx->bridge == nullptr || exprUtf8 == nullptr || exprUtf8[0] == '\0' ||
        outLen == nullptr || (cap > 0u && outUtf8 == nullptr)) return GRIDLOCK_ERR_INVALID_ARG;
    auto* self = ctx->bridge;
    const int timeoutMs = self->requestTimeoutMs();
    const QString expr = QString::fromUtf8(exprUtf8);
    QByteArray utf8Result;
    const GridLockStatus st = self->runOnObjectThread(ctx, [&](gridlock::GdbRankCoordinator* coord) {
      return self->evalExprOnCoordinatorThread(ctx, coord, rank, expr, timeoutMs, &utf8Result);
    });
    if (st != GRIDLOCK_OK) return st;
    const size_t payloadLen = static_cast<size_t>(utf8Result.size());
    *outLen = payloadLen;
    if (outUtf8 == nullptr || cap <= payloadLen) return GRIDLOCK_ERR_BUFFER_TOO_SMALL;
    if (payloadLen > 0u) std::memcpy(outUtf8, utf8Result.constData(), payloadLen);
    outUtf8[payloadLen] = '\0';
    return GRIDLOCK_OK;
  } catch (...) { return GRIDLOCK_ERR_UNSUPPORTED; }
}

GridLockStatus PluginHostBridge::readMemoryOnCoordinatorThread(
    const PluginBindingContext* ctx, gridlock::GdbRankCoordinator* coord, int32_t rank,
    uint64_t address, size_t numBytes, int timeoutMs, QByteArray* outBytes) {
  if (ctx != nullptr && ctx->shuttingDown.load(std::memory_order_acquire)) return GRIDLOCK_ERR_NOT_STOPPED;
  if (rank < 0 || rank >= coord->getProcessCount()) return GRIDLOCK_ERR_INVALID_RANK;
  if (coord->getRankState(rank).currentState != QLatin1String("stopped")) return GRIDLOCK_ERR_NOT_STOPPED;
  if (numBytes == 0u) { outBytes->clear(); return GRIDLOCK_OK; }

  CoordinatorReplyWaiter waiter(coord, rank);
  bool failed = false;
  const auto memConn = QObject::connect(
      coord, &::IBackendCoordinator::memoryRead, &waiter.loop,
      [&](int r, const QString&, const QByteArray& data) {
        if (!waiter.completed && r == rank) { *outBytes = data; waiter.completed = true; waiter.loop.quit(); }
      });
  const auto errConn = QObject::connect(
      coord, &gridlock::GdbRankCoordinator::gdbOutputReceived, &waiter.loop,
      [&](int r, const QString& line) {
        if (!waiter.completed && r == rank && line.startsWith(QLatin1String("^error"))) {
          failed = true; waiter.completed = true; waiter.loop.quit();
        }
      });
  coord->readMemory(rank, QStringLiteral("0x%1").arg(address, 0, 16), static_cast<int>(numBytes));
  waiter.waitIfPending(timeoutMs);
  QObject::disconnect(memConn);
  QObject::disconnect(errConn);

  if (waiter.aborted || (ctx != nullptr && ctx->shuttingDown.load(std::memory_order_acquire))) return GRIDLOCK_ERR_NOT_STOPPED;
  if (waiter.timedOut) return GRIDLOCK_ERR_TIMEOUT;
  return failed ? GRIDLOCK_ERR_MEMORY_READ_FAILED : GRIDLOCK_OK;
}

GridLockStatus GRIDLOCK_CALL PluginHostBridge::cbReadMemory(
    void* hostCtx, int32_t rank, uint64_t address, void* outBytes, size_t numBytes, size_t* outRead) noexcept {
  try {
    auto* ctx = static_cast<PluginBindingContext*>(hostCtx);
    if (ctx == nullptr || ctx->bridge == nullptr || outRead == nullptr ||
        (numBytes > 0u && outBytes == nullptr)) return GRIDLOCK_ERR_INVALID_ARG;
    auto* self = ctx->bridge;
    const int timeoutMs = self->requestTimeoutMs();
    QByteArray raw;
    const GridLockStatus st = self->runOnObjectThread(ctx, [&](gridlock::GdbRankCoordinator* coord) {
      return self->readMemoryOnCoordinatorThread(ctx, coord, rank, address, numBytes, timeoutMs, &raw);
    });
    if (st != GRIDLOCK_OK) return st;
    const size_t copied = std::min(numBytes, static_cast<size_t>(raw.size()));
    if (copied > 0u) std::memcpy(outBytes, raw.constData(), copied);
    *outRead = copied;
    return GRIDLOCK_OK;
  } catch (...) { return GRIDLOCK_ERR_UNSUPPORTED; }
}

GridLockStatus GRIDLOCK_CALL PluginHostBridge::cbWriteMemory(
    void* hostCtx, int32_t rank, uint64_t address, const void* inBytes, size_t numBytes, size_t* outWritten) noexcept {
  try {
    auto* ctx = static_cast<PluginBindingContext*>(hostCtx);
    if (ctx == nullptr || ctx->bridge == nullptr || outWritten == nullptr ||
        (numBytes > 0u && inBytes == nullptr)) return GRIDLOCK_ERR_INVALID_ARG;
    const QByteArray payload(static_cast<const char*>(inBytes), static_cast<qsizetype>(numBytes));
    return ctx->bridge->runOnObjectThread(ctx, [&](gridlock::GdbRankCoordinator* coord) {
      if (rank < 0 || rank >= coord->getProcessCount()) return GRIDLOCK_ERR_INVALID_RANK;
      if (coord->getRankState(rank).currentState != QLatin1String("stopped")) return GRIDLOCK_ERR_NOT_STOPPED;
      if (numBytes > 0u) coord->writeMemory(rank, address, payload);
      *outWritten = numBytes;
      return GRIDLOCK_OK;
    });
  } catch (...) { return GRIDLOCK_ERR_UNSUPPORTED; }
}

GridLockStatus GRIDLOCK_CALL PluginHostBridge::cbSetBreakpoint(
    void* hostCtx, const char* fileUtf8, int32_t line, const char* conditionUtf8, int32_t* outBpId) noexcept {
  try {
    auto* ctx = static_cast<PluginBindingContext*>(hostCtx);
    if (ctx == nullptr || ctx->bridge == nullptr || fileUtf8 == nullptr || fileUtf8[0] == '\0' ||
        line <= 0 || outBpId == nullptr) return GRIDLOCK_ERR_INVALID_ARG;
    if (ctx->shuttingDown.load(std::memory_order_acquire)) return GRIDLOCK_ERR_UNSUPPORTED;
    auto* self = ctx->bridge;
    const QString file = QString::fromUtf8(fileUtf8);
    const QString cond = (conditionUtf8 != nullptr) ? QString::fromUtf8(conditionUtf8) : QString();
    const GridLockStatus st = self->runOnObjectThread(
        ctx, [&](gridlock::GdbRankCoordinator* coord) {
          coord->setBreakpoint(file, line, cond, -1);
          return GRIDLOCK_OK;
        }, GRIDLOCK_ERR_UNSUPPORTED);
    if (st != GRIDLOCK_OK) return st;
    std::lock_guard<std::mutex> lock(self->m_mutex);
    const int32_t bpId = self->m_nextBreakpointId++;
    self->m_breakpoints.insert(bpId, PluginBreakpointRecord{ctx->pluginId, file, line, cond, self->m_stagingActive});
    *outBpId = bpId;
    return GRIDLOCK_OK;
  } catch (...) { return GRIDLOCK_ERR_UNSUPPORTED; }
}

GridLockStatus GRIDLOCK_CALL PluginHostBridge::cbRemoveBreakpoint(void* hostCtx, int32_t bpId) noexcept {
  try {
    auto* ctx = static_cast<PluginBindingContext*>(hostCtx);
    if (ctx == nullptr || ctx->bridge == nullptr || bpId <= 0) return GRIDLOCK_ERR_INVALID_ARG;
    auto* self = ctx->bridge;
    PluginBreakpointRecord rec;
    {
      std::lock_guard<std::mutex> lock(self->m_mutex);
      if (!self->m_breakpoints.contains(bpId)) return GRIDLOCK_ERR_INVALID_ARG;
      rec = self->m_breakpoints.take(bpId);
    }
    return self->runOnObjectThread(
        nullptr, [&](gridlock::GdbRankCoordinator* coord) {
          coord->broadcastBreakpoint(rec.file, rec.line, false);
          return GRIDLOCK_OK;
        }, GRIDLOCK_ERR_UNSUPPORTED);
  } catch (...) { return GRIDLOCK_ERR_UNSUPPORTED; }
}

GridLockStatus GRIDLOCK_CALL PluginHostBridge::cbRegisterVariableAction(
    void* hostCtx, const char* actionId, const char* menuLabel, const char* typeRegex,
    GridLockVariableActionCallback cb, void* pluginCtx) noexcept {
  try {
    auto* ctx = static_cast<PluginBindingContext*>(hostCtx);
    if (ctx == nullptr || ctx->bridge == nullptr || actionId == nullptr || actionId[0] == '\0' ||
        menuLabel == nullptr || menuLabel[0] == '\0' || typeRegex == nullptr ||
        typeRegex[0] == '\0' || cb == nullptr) return GRIDLOCK_ERR_INVALID_ARG;
    if (ctx->shuttingDown.load(std::memory_order_acquire)) return GRIDLOCK_ERR_UNSUPPORTED;
    const QRegularExpression compiled(QString::fromUtf8(typeRegex));
    if (!compiled.isValid()) return GRIDLOCK_ERR_INVALID_ARG;
    auto* self = ctx->bridge;
    const QString idStr = QString::fromUtf8(actionId);
    bool notify = false;
    {
      std::lock_guard<std::mutex> lock(self->m_mutex);
      const bool dupActive = !self->m_stagingActive && std::any_of(
          self->m_variableActions.cbegin(), self->m_variableActions.cend(),
          [&](const RegisteredVariableAction& a) { return a.actionId == idStr; });
      const bool dupStaged = std::any_of(
          self->m_stagedVariableActions.cbegin(), self->m_stagedVariableActions.cend(),
          [&](const RegisteredVariableAction& a) { return a.actionId == idStr; });
      if (dupActive || dupStaged) return GRIDLOCK_ERR_INVALID_ARG;
      RegisteredVariableAction entry{
          ctx->pluginId, idStr, QString::fromUtf8(menuLabel), QString::fromUtf8(typeRegex),
          compiled, cb, pluginCtx, (ctx->boundApi != nullptr) ? ctx->boundApi : &self->m_hostApi, ctx};
      if (self->m_stagingActive) self->m_stagedVariableActions.append(std::move(entry));
      else { self->m_variableActions.append(std::move(entry)); notify = true; }
    }
    if (notify) emit self->registeredActionsChanged();
    return GRIDLOCK_OK;
  } catch (...) { return GRIDLOCK_ERR_UNSUPPORTED; }
}

GridLockStatus GRIDLOCK_CALL PluginHostBridge::cbRegisterCommandAction(
    void* hostCtx, const char* commandId, const char* menuLabel, const char* shortcutHint,
    GridLockCommandActionCallback cb, void* pluginCtx) noexcept {
  try {
    auto* ctx = static_cast<PluginBindingContext*>(hostCtx);
    if (ctx == nullptr || ctx->bridge == nullptr || commandId == nullptr || commandId[0] == '\0' ||
        menuLabel == nullptr || menuLabel[0] == '\0' || cb == nullptr) return GRIDLOCK_ERR_INVALID_ARG;
    if (ctx->shuttingDown.load(std::memory_order_acquire)) return GRIDLOCK_ERR_UNSUPPORTED;
    auto* self = ctx->bridge;
    const QString idStr = QString::fromUtf8(commandId);
    bool notify = false;
    {
      std::lock_guard<std::mutex> lock(self->m_mutex);
      const bool dupActive = !self->m_stagingActive && std::any_of(
          self->m_commandActions.cbegin(), self->m_commandActions.cend(),
          [&](const RegisteredCommandAction& c) { return c.commandId == idStr; });
      const bool dupStaged = std::any_of(
          self->m_stagedCommandActions.cbegin(), self->m_stagedCommandActions.cend(),
          [&](const RegisteredCommandAction& c) { return c.commandId == idStr; });
      if (dupActive || dupStaged) return GRIDLOCK_ERR_INVALID_ARG;
      RegisteredCommandAction entry{
          ctx->pluginId, idStr, QString::fromUtf8(menuLabel),
          (shortcutHint != nullptr) ? QString::fromUtf8(shortcutHint) : QString(), cb, pluginCtx,
          (ctx->boundApi != nullptr) ? ctx->boundApi : &self->m_hostApi, ctx};
      if (self->m_stagingActive) self->m_stagedCommandActions.append(std::move(entry));
      else { self->m_commandActions.append(std::move(entry)); notify = true; }
    }
    if (notify) emit self->registeredActionsChanged();
    return GRIDLOCK_OK;
  } catch (...) { return GRIDLOCK_ERR_UNSUPPORTED; }
}

GridLockStatus GRIDLOCK_CALL PluginHostBridge::cbEmitStructuredEvent(
    void* hostCtx, const char* channelUtf8, const char* jsonPayloadUtf8) noexcept {
  try {
    auto* ctx = static_cast<PluginBindingContext*>(hostCtx);
    if (ctx == nullptr || ctx->bridge == nullptr || channelUtf8 == nullptr ||
        channelUtf8[0] == '\0' || jsonPayloadUtf8 == nullptr) return GRIDLOCK_ERR_INVALID_ARG;
    emit ctx->bridge->structuredEventEmitted(QString::fromUtf8(channelUtf8), QString::fromUtf8(jsonPayloadUtf8));
    return GRIDLOCK_OK;
  } catch (...) { return GRIDLOCK_ERR_UNSUPPORTED; }
}

}  // namespace gridlock::core::plugins
