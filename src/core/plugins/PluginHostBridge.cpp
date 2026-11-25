/**
 * @file PluginHostBridge.cpp
 * @brief Implementation of the thread-safe synchronous `GridLockHostApi` bridge and UI/editor/report handlers.
 */

#include "PluginHostBridge.hpp"

#ifdef __linux__
#include <sys/uio.h>
#endif

#include <cstdio>
#include <cstring>

#include <QByteArray>
#include <QEventLoop>
#include <QMetaObject>
#include <QThread>
#include <QTimer>

#include "../hpc/gdb/GdbRankCoordinator.hpp"
#include "../hpc/memory/NativeMemoryReader.hpp"

namespace gridlock::detail {
bool validateSafeDebuggerInput(const QString& rawInput);
}  // namespace gridlock::detail

namespace gridlock::core::plugins {

namespace {

constexpr size_t kMaxPluginMemoryBytes = 64u * 1024u * 1024u;
constexpr size_t kMaxPluginWriteBytes = 16u * 1024u * 1024u;
constexpr size_t kPluginReadChunkBytes = 65536u;
constexpr size_t kPluginWriteChunkBytes = 16384u;

bool hasControlChars(const char* raw, const QString& decoded) noexcept {
  if (raw != nullptr) {
    for (const auto* p = reinterpret_cast<const unsigned char*>(raw); *p != 0u; ++p)
      if (*p < 0x20u || *p == 0x7Fu) return true;
  }
  return std::any_of(decoded.cbegin(), decoded.cend(),
                     [](QChar c) { return c.category() == QChar::Other_Control; });
}

bool tryLocalProcessVmRead(pid_t pid, uint64_t address, size_t numBytes, QByteArray* outBytes) {
  if (pid <= 0 || numBytes == 0u) return false;
#ifdef __linux__
  if ((numBytes % sizeof(double)) == 0u) {
    const std::vector<double> words =
        NativeMemoryReader::readDoubles(pid, static_cast<uintptr_t>(address), numBytes / sizeof(double));
    if (words.size() * sizeof(double) == numBytes) {
      outBytes->resize(static_cast<qsizetype>(numBytes));
      std::memcpy(outBytes->data(), words.data(), numBytes);
      return true;
    }
  }
  QByteArray buf(static_cast<qsizetype>(numBytes), Qt::Uninitialized);
  size_t offset = 0u;
  while (offset < numBytes) {
    struct iovec local{buf.data() + offset, numBytes - offset};
    struct iovec remote{reinterpret_cast<void*>(static_cast<uintptr_t>(address + offset)), numBytes - offset};
    const ssize_t got = ::process_vm_readv(pid, &local, 1, &remote, 1, 0);
    if (got <= 0) return false;
    offset += static_cast<size_t>(got);
  }
  *outBytes = std::move(buf);
  return true;
#else
  Q_UNUSED(address);
  Q_UNUSED(outBytes);
  return false;
#endif
}

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

template <typename CallbackFn>
GridLockStatus dispatchGuardedCallback(const PluginBindingContext* bindingCtx,
                                       const std::function<void(bool)>& scopeHook,
                                       CallbackFn&& fn) noexcept {
  if (bindingCtx != nullptr && bindingCtx->shuttingDown.load(std::memory_order_acquire)) {
    return GRIDLOCK_ERR_UNSUPPORTED;
  }
  struct ScopeGuard {
    std::function<void(bool)> hook;
    ~ScopeGuard() { if (hook) hook(false); }
  } guard{scopeHook};
  if (guard.hook) guard.hook(true);
  try {
    return fn();
  } catch (...) {
    return GRIDLOCK_ERR_UNSUPPORTED;
  }
}

}  // namespace

PluginHostBridge::PluginHostBridge(gridlock::GdbRankCoordinator* coordinator, QObject* parent)
    : QObject(parent), m_coordinator(coordinator) {
  qRegisterMetaType<gridlock::core::plugins::PluginStatusItem>("gridlock::core::plugins::PluginStatusItem");
  qRegisterMetaType<gridlock::core::plugins::PluginEditorAnnotation>("gridlock::core::plugins::PluginEditorAnnotation");
  qRegisterMetaType<gridlock::core::plugins::PluginReportPanel>("gridlock::core::plugins::PluginReportPanel");
  m_defaultContext.bridge = this;
  m_hostApi = makeHostApi(&m_defaultContext);
  m_defaultContext.boundApi = &m_hostApi;
}

void PluginHostBridge::setCoordinator(gridlock::GdbRankCoordinator* coordinator) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_coordinator = coordinator;
}
gridlock::GdbRankCoordinator* PluginHostBridge::coordinator() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_coordinator.data();
}

GridLockStatus PluginHostBridge::runOnGuiThread(const PluginBindingContext* ctx,
                                                const std::function<GridLockStatus()>& fn) {
  if (ctx != nullptr && ctx->shuttingDown.load(std::memory_order_acquire)) return GRIDLOCK_ERR_UNSUPPORTED;
  if (QThread::currentThread() == this->thread()) return fn();
  GridLockStatus result = GRIDLOCK_ERR_UNSUPPORTED;
  const bool ok = QMetaObject::invokeMethod(
      this,
      [&]() {
        if (ctx != nullptr && ctx->shuttingDown.load(std::memory_order_acquire)) {
          result = GRIDLOCK_ERR_UNSUPPORTED;
          return;
        }
        result = fn();
      },
      Qt::BlockingQueuedConnection);
  return ok ? result : GRIDLOCK_ERR_UNSUPPORTED;
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

void PluginHostBridge::beginInitStaging() {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_stagingActive = true;
  m_stagedVariableActions.clear();
  m_stagedCommandActions.clear();
  m_stagedMenuActions.clear();
  m_stagedStatusItems.clear();
  m_stagedContextActions.clear();
  m_stagedEditorAnnotations.clear();
  m_stagedReportPanels.clear();
}

bool PluginHostBridge::commitStagedActions(const QString& pluginId, const GridLockHostApi* pluginHostApi) {
  bool regChanged = false, menusChanged = false, ctxChanged = false;
  QVector<PluginStatusItem> committedStatus;
  QVector<PluginEditorAnnotation> committedAnnotations;
  QVector<PluginReportPanel> committedPanels;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& a : m_stagedVariableActions)
      if (std::any_of(m_variableActions.cbegin(), m_variableActions.cend(),
                      [&](const auto& x) { return x.actionId == a.actionId; })) return false;
    for (const auto& c : m_stagedCommandActions)
      if (std::any_of(m_commandActions.cbegin(), m_commandActions.cend(),
                      [&](const auto& x) { return x.commandId == c.commandId; })) return false;
    for (const auto& m : m_stagedMenuActions)
      if (std::any_of(m_menuActions.cbegin(), m_menuActions.cend(),
                      [&](const auto& x) { return x.actionId == m.actionId; })) return false;
    for (const auto& s : m_stagedContextActions)
      if (std::any_of(m_contextActions.cbegin(), m_contextActions.cend(),
                      [&](const auto& x) { return x.actionId == s.actionId; })) return false;

    m_stagingActive = false;
    for (auto& a : m_stagedVariableActions) {
      a.pluginId = pluginId; if (pluginHostApi) a.hostApi = pluginHostApi;
      m_variableActions.append(std::move(a)); regChanged = true;
    }
    for (auto& c : m_stagedCommandActions) {
      c.pluginId = pluginId; if (pluginHostApi) c.hostApi = pluginHostApi;
      m_commandActions.append(std::move(c)); regChanged = true;
    }
    for (auto& m : m_stagedMenuActions) {
      m.pluginId = pluginId; if (pluginHostApi) m.hostApi = pluginHostApi;
      m_menuActions.append(std::move(m)); menusChanged = true;
    }
    for (auto& s : m_stagedContextActions) {
      s.pluginId = pluginId; if (pluginHostApi) s.hostApi = pluginHostApi;
      m_contextActions.append(std::move(s)); ctxChanged = true;
    }
    for (auto& st : m_stagedStatusItems) {
      st.pluginId = pluginId; if (pluginHostApi) st.hostApi = pluginHostApi;
      committedStatus.append(st); m_statusItems.append(std::move(st));
    }
    for (auto& ann : m_stagedEditorAnnotations) {
      ann.pluginId = pluginId; committedAnnotations.append(ann); m_editorAnnotations.append(std::move(ann));
    }
    for (auto& rp : m_stagedReportPanels) {
      rp.pluginId = pluginId; committedPanels.append(rp); m_reportPanels.append(std::move(rp));
    }
    for (auto& bp : m_breakpoints) if (bp.staged) { bp.pluginId = pluginId; bp.staged = false; }
    m_stagedVariableActions.clear(); m_stagedCommandActions.clear(); m_stagedMenuActions.clear();
    m_stagedStatusItems.clear(); m_stagedContextActions.clear(); m_stagedEditorAnnotations.clear();
    m_stagedReportPanels.clear();
  }
  if (regChanged) emit registeredActionsChanged();
  if (menusChanged) emit menuActionsChanged();
  if (ctxChanged) emit contextActionsChanged();
  for (const auto& st : committedStatus) emit statusItemUpdated(st);
  for (const auto& ann : committedAnnotations) emit editorAnnotationUpdated(ann);
  for (const auto& rp : committedPanels) emit reportViewPublished(rp);
  return true;
}

void PluginHostBridge::discardStagedActions() {
  QVector<PluginBreakpointRecord> toClear;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stagingActive = false;
    m_stagedVariableActions.clear(); m_stagedCommandActions.clear(); m_stagedMenuActions.clear();
    m_stagedStatusItems.clear(); m_stagedContextActions.clear(); m_stagedEditorAnnotations.clear();
    m_stagedReportPanels.clear();
    QVector<PluginBreakpointRecord> stagedBps;
    for (auto it = m_breakpoints.begin(); it != m_breakpoints.end();) {
      if (it.value().staged) { stagedBps.append(it.value()); it = m_breakpoints.erase(it); } else ++it;
    }
    for (const auto& rec : stagedBps) {
      const bool shared = std::any_of(m_breakpoints.cbegin(), m_breakpoints.cend(),
                                      [&](const PluginBreakpointRecord& o) { return o.file == rec.file && o.line == rec.line; });
      if (!shared) toClear.append(rec);
    }
  }
  if (!toClear.isEmpty()) {
    (void)runOnObjectThread(nullptr, [&](gridlock::GdbRankCoordinator* coord) {
      for (const auto& rec : toClear) coord->broadcastBreakpoint(rec.file, rec.line, false);
      return GRIDLOCK_OK;
    });
  }
}

void PluginHostBridge::unregisterPluginActions(const QString& pluginId) {
  bool regRemoved = false, menuRemoved = false, ctxRemoved = false;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto bVar = m_variableActions.size(), bCmd = m_commandActions.size();
    const auto bMenu = m_menuActions.size(), bCtx = m_contextActions.size();
    m_variableActions.removeIf([&](const auto& a) { return a.pluginId == pluginId; });
    m_commandActions.removeIf([&](const auto& c) { return c.pluginId == pluginId; });
    m_menuActions.removeIf([&](const auto& m) { return m.pluginId == pluginId; });
    m_contextActions.removeIf([&](const auto& s) { return s.pluginId == pluginId; });
    regRemoved = (m_variableActions.size() != bVar) || (m_commandActions.size() != bCmd);
    menuRemoved = (m_menuActions.size() != bMenu);
    ctxRemoved = (m_contextActions.size() != bCtx);
  }
  if (regRemoved) emit registeredActionsChanged();
  if (menuRemoved) emit menuActionsChanged();
  if (ctxRemoved) emit contextActionsChanged();
}

void PluginHostBridge::unregisterPluginBreakpoints(const QString& pluginId) {
  if (pluginId.isEmpty()) return;
  QVector<PluginBreakpointRecord> toClear;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    QVector<PluginBreakpointRecord> owned;
    for (auto it = m_breakpoints.begin(); it != m_breakpoints.end();) {
      if (it.value().pluginId == pluginId) { owned.append(it.value()); it = m_breakpoints.erase(it); } else ++it;
    }
    for (const auto& rec : owned) {
      const bool shared = std::any_of(m_breakpoints.cbegin(), m_breakpoints.cend(),
                                      [&](const PluginBreakpointRecord& o) { return o.file == rec.file && o.line == rec.line; });
      if (!shared) toClear.append(rec);
    }
  }
  if (!toClear.isEmpty()) {
    (void)runOnObjectThread(nullptr, [&](gridlock::GdbRankCoordinator* coord) {
      for (const auto& rec : toClear) coord->broadcastBreakpoint(rec.file, rec.line, false);
      return GRIDLOCK_OK;
    });
  }
}

void PluginHostBridge::unregisterPluginResources(const QString& pluginId) {
  if (pluginId.isEmpty()) return;
  unregisterPluginActions(pluginId);
  unregisterPluginBreakpoints(pluginId);

  QStringList removedStatusIds, closedReportIds;
  bool hadAnnotations = false;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_cachedPrompt.pluginId == pluginId) m_cachedPrompt = CachedPromptReply{};
    for (const auto& st : m_statusItems) if (st.pluginId == pluginId) removedStatusIds.append(st.itemId);
    m_statusItems.removeIf([&](const auto& st) { return st.pluginId == pluginId; });

    const auto bAnn = m_editorAnnotations.size();
    m_editorAnnotations.removeIf([&](const auto& a) { return a.pluginId == pluginId; });
    hadAnnotations = (m_editorAnnotations.size() != bAnn);

    for (const auto& rp : m_reportPanels) if (rp.pluginId == pluginId) closedReportIds.append(rp.panelId);
    m_reportPanels.removeIf([&](const auto& rp) { return rp.pluginId == pluginId; });
  }
  for (const QString& id : removedStatusIds) emit statusItemRemoved(pluginId, id);
  if (hadAnnotations) emit editorAnnotationsCleared(pluginId, QString());
  for (const QString& id : closedReportIds) emit reportViewClosed(pluginId, id);
}

GridLockStatus PluginHostBridge::invokeVariableAction(const QString& actionId,
                                                      const GridLockVariableContext& varCtx) {
  RegisteredVariableAction target;
  std::function<void(bool)> hook;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = std::find_if(m_variableActions.cbegin(), m_variableActions.cend(),
                           [&](const auto& a) { return a.actionId == actionId; });
    if (it == m_variableActions.cend() || it->callback == nullptr) return GRIDLOCK_ERR_INVALID_ARG;
    target = *it; hook = m_callbackScopeHook;
  }
  return dispatchGuardedCallback(target.bindingCtx, hook, [&]() {
    return target.callback(target.pluginCtx, (target.hostApi != nullptr) ? target.hostApi : &m_hostApi, &varCtx);
  });
}

GridLockStatus PluginHostBridge::invokeCommandAction(const QString& commandId,
                                                     const GridLockCommandSpec& cmdSpec) {
  RegisteredCommandAction target;
  std::function<void(bool)> hook;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = std::find_if(m_commandActions.cbegin(), m_commandActions.cend(),
                           [&](const auto& c) { return c.commandId == commandId; });
    if (it == m_commandActions.cend() || it->callback == nullptr) return GRIDLOCK_ERR_INVALID_ARG;
    target = *it; hook = m_callbackScopeHook;
  }
  return dispatchGuardedCallback(target.bindingCtx, hook, [&]() {
    return target.callback(target.pluginCtx, (target.hostApi != nullptr) ? target.hostApi : &m_hostApi, &cmdSpec);
  });
}

GridLockStatus PluginHostBridge::invokeMenuAction(const QString& actionId,
                                                  const GridLockCommandSpec& cmdSpec) {
  RegisteredMenuAction target;
  std::function<void(bool)> hook;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = std::find_if(m_menuActions.cbegin(), m_menuActions.cend(),
                           [&](const auto& m) { return m.actionId == actionId; });
    if (it == m_menuActions.cend() || it->callback == nullptr) return GRIDLOCK_ERR_INVALID_ARG;
    target = *it; hook = m_callbackScopeHook;
  }
  return dispatchGuardedCallback(target.bindingCtx, hook, [&]() {
    return target.callback(target.pluginCtx, (target.hostApi != nullptr) ? target.hostApi : &m_hostApi, &cmdSpec);
  });
}

GridLockStatus PluginHostBridge::invokeStatusItem(const QString& itemId) {
  PluginStatusItem target;
  std::function<void(bool)> hook;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = std::find_if(m_statusItems.cbegin(), m_statusItems.cend(),
                           [&](const auto& s) { return s.itemId == itemId; });
    if (it == m_statusItems.cend() || it->callback == nullptr) return GRIDLOCK_ERR_INVALID_ARG;
    target = *it; hook = m_callbackScopeHook;
  }
  const QByteArray idUtf8 = target.itemId.toUtf8();
  return dispatchGuardedCallback(target.bindingCtx, hook, [&]() {
    return target.callback(target.pluginCtx, (target.hostApi != nullptr) ? target.hostApi : &m_hostApi,
                           idUtf8.constData());
  });
}

GridLockStatus PluginHostBridge::invokeContextAction(const QString& actionId,
                                                     const GridLockSurfaceContext& surfaceCtx) {
  RegisteredContextAction target;
  std::function<void(bool)> hook;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = std::find_if(m_contextActions.cbegin(), m_contextActions.cend(),
                           [&](const auto& s) { return s.actionId == actionId; });
    if (it == m_contextActions.cend() || it->callback == nullptr) return GRIDLOCK_ERR_INVALID_ARG;
    target = *it; hook = m_callbackScopeHook;
  }
  return dispatchGuardedCallback(target.bindingCtx, hook, [&]() {
    return target.callback(target.pluginCtx, (target.hostApi != nullptr) ? target.hostApi : &m_hostApi, &surfaceCtx);
  });
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

GridLockStatus PluginHostBridge::evalExprOnCoordinatorThread(
    const PluginBindingContext* ctx, gridlock::GdbRankCoordinator* coord, int32_t rank,
    const QString& expr, int timeoutMs, QByteArray* outPayload) {
  if (ctx != nullptr && ctx->shuttingDown.load(std::memory_order_acquire)) return GRIDLOCK_ERR_NOT_STOPPED;
  if (rank < 0 || rank >= coord->getProcessCount()) return GRIDLOCK_ERR_INVALID_RANK;
  if (coord->getRankState(rank).currentState != QLatin1String("stopped")) return GRIDLOCK_ERR_NOT_STOPPED;

  CoordinatorReplyWaiter waiter(coord, rank);
  bool evalOk = false;
  QString evalValue;
  int expectedTok = -1;
  const auto evalConn = QObject::connect(
      coord, &gridlock::GdbRankCoordinator::pluginExpressionReady, &waiter.loop,
      [&](int r, int tok, bool ok, const QString& resultOrError) {
        if (!waiter.completed && r == rank && tok == expectedTok) {
          evalOk = ok; evalValue = resultOrError; waiter.completed = true; waiter.loop.quit();
        }
      });
  if (coord->requestPluginExpressionEval(rank, expr, &expectedTok) <= 0) {
    QObject::disconnect(evalConn);
    return GRIDLOCK_ERR_NOT_STOPPED;
  }
  waiter.waitIfPending(timeoutMs);
  QObject::disconnect(evalConn);
  coord->cancelPluginEvalRequest(expectedTok);

  if (waiter.aborted || (ctx != nullptr && ctx->shuttingDown.load(std::memory_order_acquire))) return GRIDLOCK_ERR_NOT_STOPPED;
  if (waiter.timedOut) return GRIDLOCK_ERR_TIMEOUT;
  if (!evalOk) return GRIDLOCK_ERR_EVAL_FAILED;
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
  outBytes->clear();
  if (numBytes == 0u) return GRIDLOCK_OK;

  const qint64 livePid = coord->getPidForRank(rank);
  if (livePid > 0 && tryLocalProcessVmRead(static_cast<pid_t>(livePid), address, numBytes, outBytes)) return GRIDLOCK_OK;

  outBytes->reserve(static_cast<qsizetype>(std::min<size_t>(numBytes, 1024u * 1024u)));
  for (size_t offset = 0u; offset < numBytes;) {
    if (ctx != nullptr && ctx->shuttingDown.load(std::memory_order_acquire)) return GRIDLOCK_ERR_NOT_STOPPED;
    if (coord->getRankState(rank).currentState != QLatin1String("stopped")) return GRIDLOCK_ERR_NOT_STOPPED;
    const size_t chunkLen = std::min(kPluginReadChunkBytes, numBytes - offset);
    const uint64_t chunkAddr = address + offset;
    const int expectedTok = coord->allocatePluginMiToken();

    CoordinatorReplyWaiter waiter(coord, rank);
    QByteArray chunkBytes;
    QString chunkErr;
    const auto chunkConn = QObject::connect(
        coord, &gridlock::GdbRankCoordinator::pluginMemoryChunkReady, &waiter.loop,
        [&](int r, int tok, const QByteArray& data, const QString& err) {
          if (!waiter.completed && r == rank && tok == expectedTok) {
            chunkBytes = data; chunkErr = err; waiter.completed = true; waiter.loop.quit();
          }
        });
    coord->sendPluginMemoryRead(rank, expectedTok, chunkAddr, chunkLen);
    if (coord->isMockRank(rank) && timeoutMs > 20) {
      waiter.waitIfPending(15);
      if (waiter.timedOut && !waiter.aborted) {
        waiter.timedOut = false; waiter.completed = false;
        coord->writeCmd(rank, QStringLiteral("-data-read-memory-bytes 0x%1 %2\n").arg(chunkAddr, 0, 16).arg(chunkLen));
        waiter.waitIfPending(timeoutMs - 15);
      }
    } else {
      waiter.waitIfPending(timeoutMs);
    }
    QObject::disconnect(chunkConn);
    coord->cancelPluginMemoryRequest(rank, expectedTok);

    if (waiter.aborted || (ctx != nullptr && ctx->shuttingDown.load(std::memory_order_acquire))) return GRIDLOCK_ERR_NOT_STOPPED;
    if (waiter.timedOut) return GRIDLOCK_ERR_TIMEOUT;
    if (!chunkErr.isEmpty() || chunkBytes.isEmpty()) return GRIDLOCK_ERR_MEMORY_READ_FAILED;
    const size_t got = std::min(chunkLen, static_cast<size_t>(chunkBytes.size()));
    outBytes->append(chunkBytes.constData(), static_cast<qsizetype>(got));
    offset += got;
    if (got < chunkLen) break;
  }
  return GRIDLOCK_OK;
}

GridLockStatus GRIDLOCK_CALL PluginHostBridge::cbReadMemory(
    void* hostCtx, int32_t rank, uint64_t address, void* outBytes, size_t numBytes, size_t* outRead) noexcept {
  try {
    auto* ctx = static_cast<PluginBindingContext*>(hostCtx);
    if (ctx == nullptr || ctx->bridge == nullptr || outRead == nullptr || numBytes > kMaxPluginMemoryBytes ||
        (numBytes > 0u && (outBytes == nullptr || address > UINT64_MAX - (numBytes - 1u)))) {
      return GRIDLOCK_ERR_INVALID_ARG;
    }
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

GridLockStatus PluginHostBridge::writeMemoryOnCoordinatorThread(
    const PluginBindingContext* ctx, gridlock::GdbRankCoordinator* coord, int32_t rank,
    uint64_t address, const uint8_t* inBytes, size_t numBytes, int timeoutMs, size_t* outWritten) {
  *outWritten = 0u;
  if (ctx != nullptr && ctx->shuttingDown.load(std::memory_order_acquire)) return GRIDLOCK_ERR_NOT_STOPPED;
  if (rank < 0 || rank >= coord->getProcessCount()) return GRIDLOCK_ERR_INVALID_RANK;
  if (coord->getRankState(rank).currentState != QLatin1String("stopped")) return GRIDLOCK_ERR_NOT_STOPPED;
  if (numBytes == 0u) return GRIDLOCK_OK;

  size_t totalWritten = 0u;
  for (size_t offset = 0u; offset < numBytes;) {
    if (ctx != nullptr && ctx->shuttingDown.load(std::memory_order_acquire)) { *outWritten = totalWritten; return GRIDLOCK_ERR_NOT_STOPPED; }
    if (coord->getRankState(rank).currentState != QLatin1String("stopped")) { *outWritten = totalWritten; return GRIDLOCK_ERR_NOT_STOPPED; }
    const size_t chunkLen = std::min(kPluginWriteChunkBytes, numBytes - offset);
    const QByteArray chunkSlice = QByteArray::fromRawData(
        reinterpret_cast<const char*>(inBytes + offset), static_cast<qsizetype>(chunkLen));
    const int expectedTok = coord->allocatePluginMiToken();

    CoordinatorReplyWaiter waiter(coord, rank);
    QString writeErr;
    const auto writeConn = QObject::connect(
        coord, &gridlock::GdbRankCoordinator::pluginMemoryWriteDone, &waiter.loop,
        [&](int r, int tok, const QString& err) {
          if (!waiter.completed && r == rank && tok == expectedTok) {
            writeErr = err; waiter.completed = true; waiter.loop.quit();
          }
        });
    coord->sendPluginMemoryWrite(rank, expectedTok, address + offset, chunkSlice);
    waiter.waitIfPending(coord->isMockRank(rank) ? std::min(timeoutMs, 20) : timeoutMs);
    QObject::disconnect(writeConn);
    coord->cancelPluginMemoryRequest(rank, expectedTok);

    if (waiter.aborted || (ctx != nullptr && ctx->shuttingDown.load(std::memory_order_acquire))) {
      *outWritten = totalWritten; return GRIDLOCK_ERR_NOT_STOPPED;
    }
    if (waiter.timedOut && !coord->isMockRank(rank)) { *outWritten = totalWritten; return GRIDLOCK_ERR_TIMEOUT; }
    if (!writeErr.isEmpty()) { *outWritten = totalWritten; return GRIDLOCK_ERR_MEMORY_READ_FAILED; }
    totalWritten += chunkLen;
    offset += chunkLen;
  }
  *outWritten = totalWritten;
  return GRIDLOCK_OK;
}

GridLockStatus GRIDLOCK_CALL PluginHostBridge::cbWriteMemory(
    void* hostCtx, int32_t rank, uint64_t address, const void* inBytes, size_t numBytes, size_t* outWritten) noexcept {
  try {
    auto* ctx = static_cast<PluginBindingContext*>(hostCtx);
    if (ctx == nullptr || ctx->bridge == nullptr || outWritten == nullptr || numBytes > kMaxPluginWriteBytes ||
        (numBytes > 0u && (inBytes == nullptr || address > UINT64_MAX - (numBytes - 1u)))) {
      return GRIDLOCK_ERR_INVALID_ARG;
    }
    auto* self = ctx->bridge;
    const int timeoutMs = self->requestTimeoutMs();
    const auto* srcBytes = static_cast<const uint8_t*>(inBytes);
    return self->runOnObjectThread(ctx, [&](gridlock::GdbRankCoordinator* coord) {
      return self->writeMemoryOnCoordinatorThread(ctx, coord, rank, address, srcBytes, numBytes, timeoutMs, outWritten);
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

    const QString file = QString::fromUtf8(fileUtf8);
    if (file.trimmed().isEmpty() || hasControlChars(fileUtf8, file)) return GRIDLOCK_ERR_INVALID_ARG;

    QString cond;
    if (conditionUtf8 != nullptr && conditionUtf8[0] != '\0') {
      cond = QString::fromUtf8(conditionUtf8);
      if (hasControlChars(conditionUtf8, cond) || !gridlock::detail::validateSafeDebuggerInput(cond))
        return GRIDLOCK_ERR_INVALID_ARG;
    }

    auto* self = ctx->bridge;
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
    bool stillShared = false;
    {
      std::lock_guard<std::mutex> lock(self->m_mutex);
      if (!self->m_breakpoints.contains(bpId)) return GRIDLOCK_ERR_INVALID_ARG;
      rec = self->m_breakpoints.take(bpId);
      stillShared = std::any_of(self->m_breakpoints.cbegin(), self->m_breakpoints.cend(),
                                [&](const PluginBreakpointRecord& o) { return o.file == rec.file && o.line == rec.line; });
    }
    if (stillShared) return GRIDLOCK_OK;
    return self->runOnObjectThread(
        nullptr, [&](gridlock::GdbRankCoordinator* coord) {
          coord->broadcastBreakpoint(rec.file, rec.line, false);
          return GRIDLOCK_OK;
        }, GRIDLOCK_ERR_UNSUPPORTED);
  } catch (...) { return GRIDLOCK_ERR_UNSUPPORTED; }
}

QVector<RegisteredVariableAction> PluginHostBridge::matchingVariableActions(const QString& typeName) const {
  QVector<RegisteredVariableAction> snapshot;
  { std::lock_guard<std::mutex> lock(m_mutex); snapshot = m_variableActions; }
  const QString boundedType = (typeName.size() > 2048) ? typeName.left(2048) : typeName;
  QVector<RegisteredVariableAction> matches;
  for (const auto& act : snapshot) {
    if (act.compiledRegex.isValid() && act.compiledRegex.match(boundedType).hasMatch()) {
      RegisteredVariableAction lite;
      lite.pluginId = act.pluginId; lite.actionId = act.actionId;
      lite.menuLabel = act.menuLabel; lite.typeRegex = act.typeRegex;
      matches.append(std::move(lite));
    }
  }
  return matches;
}

}  // namespace gridlock::core::plugins
