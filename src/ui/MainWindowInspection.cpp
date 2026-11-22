/**
 * @file MainWindowInspection.cpp
 * @brief Cross-view inspection pivots, expression completion, live MemView handlers, and tool dialogs.
 *
 * Implements expression evaluation routing, CallStack/Variables/Console/Watchpoint/MemView
 * coordinator signal wiring, Pointer Graph / Array Plotter expression completer collection,
 * and modeless scientific/inspection dialog launchers.
 */

#include "MainWindow.hpp"

#include "../core/hpc/NativeMemoryReader.hpp"
#include "../core/models/VariableTreeModel.hpp"
#include "EditorTabManager.hpp"
#include "GdbConsoleWidget.hpp"
#include "dialogs/plotter/ArrayPlotDialog.hpp"
#include "dialogs/debugger/CheckpointDialog.hpp"
#include "dialogs/graph/PointerGraphDialog.hpp"
#include "dialogs/debugger/SignalCatchpointDialog.hpp"
#include "docks/CallStackDockWidget.hpp"
#include "docks/VariablesDockWidget.hpp"
#include "views/DisassemblyView.hpp"
#include "views/MemView.hpp"
#include "views/RegisterView.hpp"
#include "views/SourceCodeView.hpp"
#include "widgets/DifferentialGrid.hpp"
#include "widgets/ExpressionEvaluatorWidget.hpp"
#include "widgets/MpiDiagnosticsWidget.hpp"
#include "widgets/MpiNetworkLogWidget.hpp"
#include "widgets/ProjectExplorerWidget.hpp"

#include <algorithm>
#include <cstring>
#include <functional>

#include <QAbstractProxyModel>
#include <QAction>
#include <QComboBox>
#include <QDockWidget>
#include <QEventLoop>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QTreeView>

namespace gridlock::ui {

/** @brief Evaluates @p expr on the target chosen in `#evaluatorRankCombo` (broadcasting across ranks when `All Ranks`). */
void MainWindow::evaluateExpressionFromWidget(const QString& expr) {
    const QString trimmed = expr.trimmed();
    if (!m_coordinator || trimmed.isEmpty()) return;
    const int target = m_expressionEvaluatorWidget ? m_expressionEvaluatorWidget->selectedTargetRank() : m_focusedRank;
    if (target >= 0) { m_coordinator->evaluateExpression(target, trimmed); return; }
    const int count = qMax(1, m_coordinator->getProcessCount());
    QVector<int> ranks;
    ranks.reserve(count);
    for (int r = 0; r < count; ++r) {
        const auto it = m_latestStates.find(r);
        if (it == m_latestStates.end() ||
            (it->second.currentState != QLatin1String("running") && it->second.currentState != QLatin1String("exited"))) {
            ranks.append(r);
        }
    }
    if (ranks.isEmpty()) for (int r = 0; r < count; ++r) ranks.append(r);
    if (m_expressionEvaluatorWidget) m_expressionEvaluatorWidget->beginMultiRankEvaluation(trimmed, ranks);
    for (int r : ranks) m_coordinator->evaluateExpression(r, trimmed);
}

/** @brief Connects `CallStackDockWidget`, inline variable previews, and `m_coordinator` stack/thread signals. */
void MainWindow::bindCallStackCoordinatorRoutes() {
    if (m_variablesDockWidget && !m_variablesDockWidget->property("gl_inline_var_bound").toBool()) {
        m_variablesDockWidget->setProperty("gl_inline_var_bound", true);
        auto* tree = m_variablesDockWidget->findChild<QTreeView*>();
        if (auto* proxy = tree ? qobject_cast<QAbstractProxyModel*>(tree->model()) : nullptr) {
            QPointer<EditorTabManager> etmPtr(m_editorTabManager);
            auto* evalTimer = new QTimer(m_variablesDockWidget);
            evalTimer->setSingleShot(true);
            evalTimer->setInterval(120);
            auto syncVars = [this, proxy, etmPtr, evalTimer]() {
                if (!m_variablesDockWidget || m_variablesDockWidget->focusedRank() != m_focusedRank) return;
                QAbstractItemModel* src = proxy ? proxy->sourceModel() : nullptr;
                if (!src || !etmPtr) return;
                QMap<QString, QString> varMap;
                std::function<void(const QModelIndex&, const QString&)> collect = [&](const QModelIndex& p, const QString& pfx) {
                    for (int r = 0; r < src->rowCount(p); ++r) {
                        const QModelIndex idx = src->index(r, 0, p);
                        const QString n = src->data(idx).toString().trimmed();
                        const QString v = src->data(src->index(r, 1, p)).toString().trimmed();
                        const QString full = pfx.isEmpty() ? n : (pfx + QLatin1Char('.') + n);
                        if (!full.isEmpty() && !v.isEmpty()) varMap.insert(full, v);
                        if (src->rowCount(idx) > 0) collect(idx, full);
                    }
                };
                collect(QModelIndex(), QString());
                etmPtr->setInlineVariableValues(varMap);
                evalTimer->start();
            };
            connect(evalTimer, &QTimer::timeout, this, [this, proxy, etmPtr]() {
                if (!m_variablesDockWidget || m_variablesDockWidget->focusedRank() != m_focusedRank) return;
                QAbstractItemModel* src = proxy ? proxy->sourceModel() : nullptr;
                auto* sv = etmPtr ? etmPtr->currentSourceCodeView() : nullptr;
                if (!src || !sv || !m_coordinator || sv->executionLine() <= 0 || src->rowCount() == 0) return;
                QSet<QString> roots, q;
                for (int r = 0; r < src->rowCount(); ++r) roots.insert(src->data(src->index(r, 0)).toString().trimmed());
                static const QRegularExpression kChainRe(QStringLiteral("\\b([A-Za-z_][A-Za-z0-9_]*(?:(?:\\.|->)[A-Za-z_][A-Za-z0-9_]*)+)\\b"));
                int sent = 0;
                for (int ln = std::max(1, sv->executionLine() - 15); ln <= sv->executionLine() + 15 && sent < 12; ++ln) {
                    for (auto it = kChainRe.globalMatch(sv->document()->findBlockByNumber(ln - 1).text()); it.hasNext() && sent < 12;) {
                        const QString ex = it.next().captured(1);
                        const QString root = ex.section(QRegularExpression(QStringLiteral("\\.|->")), 0, 0);
                        if (!q.contains(ex) && roots.contains(root)) {
                            q.insert(ex);
                            ++sent;
                            m_coordinator->evaluateExpression(m_focusedRank, ex);
                        }
                    }
                }
            });
            connect(proxy, &QAbstractItemModel::modelReset, this, syncVars);
            connect(proxy, &QAbstractItemModel::rowsInserted, this, [syncVars](const QModelIndex&, int, int) { syncVars(); });
        }
    }
    if (m_registerView && !m_registerView->property("gl_reg_edit_bound").toBool()) {
        m_registerView->setProperty("gl_reg_edit_bound", true);
        m_registerView->setFocusedRank(m_focusedRank);
        connect(m_registerView, &RegisterView::registerEditRequested, this, [this](int rankId, const QString& regName, const QString& newVal) {
            if (m_coordinator) m_coordinator->writeRegisterValue(rankId >= 0 ? rankId : m_focusedRank, regName, newVal);
        });
    }
    if (m_coordinator && !m_coordinator->property("gl_inline_eval_bound").toBool()) {
        m_coordinator->setProperty("gl_inline_eval_bound", true);
        QPointer<EditorTabManager> etmPtr(m_editorTabManager);
        connect(m_coordinator, &IBackendCoordinator::expressionEvaluated, this, [this, etmPtr](int rank, const QString& expr, const QString& val) {
            if (rank == m_focusedRank && etmPtr && (expr.contains(QLatin1Char('.')) || expr.contains(QStringLiteral("->"))) && !val.startsWith(QStringLiteral("Error:"))) {
                if (auto* sv = etmPtr->currentSourceCodeView()) sv->mergeInlineVariableValue(expr, val);
            }
        });
    }
    if (m_pointerGraphDialog) { m_pointerGraphDialog->setCoordinator(m_coordinator); m_pointerGraphDialog->setFocusedRank(m_focusedRank); }
    if (m_callStackDockWidget && !m_callStackDockWidget->property("gl_callstack_bound").toBool()) {
        m_callStackDockWidget->setProperty("gl_callstack_bound", true);
        connect(m_callStackDockWidget, &CallStackDockWidget::threadSelected, this, [this](int threadId) {
            m_selectedFrameLevel[m_focusedRank] = 0;
            const auto stIt = m_latestStates.find(m_focusedRank);
            if (m_coordinator && stIt != m_latestStates.end() && stIt->second.currentState == QLatin1String("stopped")) {
                m_coordinator->selectStackFrame(m_focusedRank, threadId, 0);
            }
        });
        connect(m_callStackDockWidget, &CallStackDockWidget::frameActivated, this,
                [this](int threadId, int frameLevel, const QString& file, int line, quint64 address) {
                    m_selectedFrameLevel[m_focusedRank] = frameLevel;
                    if (!file.isEmpty() && QFileInfo::exists(file) && line > 0) {
                        SourceCodeView* view = m_editorTabManager ? m_editorTabManager->openFile(file) : getSourceCodeView();
                        if (view) { view->highlightCurrentLine(line); view->highlightExecutionLine(line); }
                    }
                    if (m_disassemblyView && address != 0ULL) { m_disassemblyView->setCurrentAddress(address); m_disassemblyView->setActiveSteppingRank(m_focusedRank, address); }
                    const auto stIt = m_latestStates.find(m_focusedRank);
                    if (m_coordinator && stIt != m_latestStates.end() && stIt->second.currentState == QLatin1String("stopped")) {
                        m_coordinator->selectStackFrame(m_focusedRank, threadId, frameLevel);
                    }
                });
    }
    if (!m_coordinator) return;

    connect(m_coordinator, &IBackendCoordinator::stackTraceUpdated, this, [this](int rank, const QVector<StackFrameEntry>& frames) {
        if (frames.isEmpty() && m_coordinator && m_coordinator->getProcessCount() == 0) {
            m_cachedStackFrames.clear(); m_cachedThreads.clear(); m_selectedFrameLevel.clear();
            if (m_callStackDockWidget) m_callStackDockWidget->clearFrames();
            return;
        }
        m_cachedStackFrames[rank] = frames;
        if (rank == m_focusedRank && m_callStackDockWidget) m_callStackDockWidget->setStackFrames(frames, m_selectedFrameLevel.value(rank, 0));
        if (rank == m_focusedRank && m_memView && m_coordinator && m_memView->isMemoryMapMode() && (m_memView->isVisible() || m_memView->isPoppedOut())) {
            m_memView->notifyRankStoppedOrSelected(rank, m_coordinator->getPidForRank(rank), std::max(1, m_coordinator->getProcessCount()));
        }
        if (!frames.isEmpty() && m_mpiDiagnosticsWidget) {
            const StackFrameEntry* mpiFrame = &frames.first();
            for (int i = 0; i < std::min<int>(frames.size(), 8); ++i) {
                if (frames[i].function.startsWith(QStringLiteral("MPI_")) || frames[i].function.startsWith(QStringLiteral("PMPI_")) ||
                    frames[i].function.contains(QStringLiteral("barrierWait"))) { mpiFrame = &frames[i]; break; }
            }
            const qint64 livePid = m_coordinator ? m_coordinator->getPidForRank(rank) : 0;
            const QString locStr = (!mpiFrame->file.isEmpty() && mpiFrame->line > 0)
                ? QStringLiteral("%1:%2 (0x%3)").arg(QFileInfo(mpiFrame->file).fileName()).arg(mpiFrame->line).arg(mpiFrame->address, 8, 16, QChar('0'))
                : QStringLiteral("0x%1").arg(mpiFrame->address, 8, 16, QChar('0'));
            m_mpiDiagnosticsWidget->updateLiveRankRow(rank, livePid, QStringLiteral("PAUSED"),
                mpiFrame->function.isEmpty() ? QStringLiteral("main") : mpiFrame->function,
                QStringLiteral("LIVE"), locStr, m_coordinator ? std::max(1, m_coordinator->getProcessCount()) : 1);
        }
    });
    connect(m_coordinator, &IBackendCoordinator::threadsUpdated, this, [this](int rank, const QVector<ThreadEntry>& threads) {
        if (threads.isEmpty() && m_coordinator && m_coordinator->getProcessCount() == 0) return;
        m_cachedThreads[rank] = threads;
        if (rank == m_focusedRank && m_callStackDockWidget) m_callStackDockWidget->setThreads(threads);
    });
}

/** @brief Binds `GdbConsoleWidget` REPL dispatch, hardware data watchpoint triggers, and `WATCH` dock controls. */
void MainWindow::bindConsoleAndWatchpointRoutes() {
    if (m_gdbConsoleWidget && !m_gdbConsoleWidget->property("gl_repl_bound").toBool()) {
        m_gdbConsoleWidget->setProperty("gl_repl_bound", true);
        connect(m_gdbConsoleWidget, &GdbConsoleWidget::commandEntered, this,
                [this](int rank, const QString& cmd) { if (m_coordinator) m_coordinator->sendDebuggerCommand(rank, cmd); });
    }
    if (auto* watchDock = findChild<QDockWidget*>(QStringLiteral("WatchExpressionsDock"));
        watchDock && !watchDock->property("gl_wp_controls_bound").toBool()) {
        watchDock->setProperty("gl_wp_controls_bound", true);
        if (auto* watchInput = watchDock->findChild<QLineEdit*>()) {
            if (auto* barLayout = qobject_cast<QHBoxLayout*>(watchInput->parentWidget() ? watchInput->parentWidget()->layout() : nullptr)) {
                auto* modeCombo = new QComboBox(watchInput->parentWidget());
                modeCombo->setObjectName(QStringLiteral("watchpointAccessCombo"));
                modeCombo->setFixedHeight(22);
                modeCombo->addItem(QStringLiteral("Write"), static_cast<int>(WatchpointAccessMode::Write));
                modeCombo->addItem(QStringLiteral("Read"), static_cast<int>(WatchpointAccessMode::Read));
                modeCombo->addItem(QStringLiteral("R/W"), static_cast<int>(WatchpointAccessMode::ReadWrite));
                auto* wpBtn = new QPushButton(QStringLiteral("+ Watchpoint"), watchInput->parentWidget());
                wpBtn->setObjectName(QStringLiteral("addHardwareWatchpointBtn"));
                wpBtn->setFixedHeight(22);
                connect(wpBtn, &QPushButton::clicked, this, [this, watchInput, modeCombo]() {
                    const QString expr = watchInput->text().trimmed();
                    if (expr.isEmpty()) return;
                    if (m_coordinator) m_coordinator->setDataWatchpoint(expr, static_cast<WatchpointAccessMode>(modeCombo->currentData().toInt()), -1);
                    if (m_differentialGrid) m_differentialGrid->addVariableColumn(expr);
                    watchInput->clear();
                });
                barLayout->addWidget(modeCombo); barLayout->addWidget(wpBtn);
                updateDebugToolbarState();
            }
        }
    }
    if (!m_coordinator) return;
    if (m_gdbConsoleWidget) connect(m_coordinator, &IBackendCoordinator::debuggerConsoleOutput, m_gdbConsoleWidget, &GdbConsoleWidget::appendDebuggerConsoleOutput);
    connect(m_coordinator, &IBackendCoordinator::watchpointHit, this,
            [this](int rank, const QString& expr, const QString& oldVal, const QString& newVal) {
                const QString summary = QStringLiteral("Hardware watchpoint '%1' hit: %2 -> %3").arg(expr, oldVal, newVal);
                if (m_gdbConsoleWidget) m_gdbConsoleWidget->appendDebuggerConsoleOutput(rank, summary, false);
                if (networkLog()) networkLog()->appendText(QStringLiteral("WATCHPOINT"), summary + QLatin1Char('\n'));
                onRankSelected(rank);
                updateStatusTelemetry(m_currentSeekOrPc, QStringLiteral("WATCHPOINT (%1: %2 -> %3)").arg(expr, oldVal, newVal));
            });
}

/** @brief Binds `MemView` live rank address evaluation and `process_vm_readv` + debugger fallback callbacks. */
void MainWindow::bindLiveMemViewHandlers() {
    if (!m_memView) return;
    auto resolver = [this](int rank, const QString& rawExpr, bool* okOut) -> uint64_t {
        if (okOut) *okOut = false;
        const QString expr = rawExpr.trimmed();
        if (expr.isEmpty()) return 0ULL;
        if (!expr.startsWith(QLatin1Char('$')) && !expr.startsWith(QLatin1Char('&'))) {
            const QString clean = expr.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive) ? expr.mid(2) : expr;
            bool hexOk = false;
            if (const uint64_t val = clean.toULongLong(&hexOk, 16); hexOk && val != 0ULL) { if (okOut) *okOut = true; return val; }
        }
        if (expr.startsWith(QLatin1Char('$')) && m_latestStates.count(rank)) {
            static const QHash<QString, int> kRegSlots = {
                {QStringLiteral("rax"), 0}, {QStringLiteral("rbx"), 1}, {QStringLiteral("rcx"), 2}, {QStringLiteral("rdx"), 3},
                {QStringLiteral("rsi"), 4}, {QStringLiteral("rdi"), 5}, {QStringLiteral("rbp"), 6}, {QStringLiteral("fp"), 6},
                {QStringLiteral("rsp"), 7}, {QStringLiteral("sp"), 7},  {QStringLiteral("rip"), 16}, {QStringLiteral("pc"), 16}};
            if (const int slot = kRegSlots.value(expr.mid(1).toLower(), -1); slot >= 0) {
                const QString tok = m_latestStates[rank].registers.value(slot).section(QLatin1Char(' '), 0, 0);
                bool regOk = false;
                const uint64_t rv = tok.mid(tok.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive) ? 2 : 0).toULongLong(&regOk, 16);
                if (regOk && rv != 0ULL) { if (okOut) *okOut = true; return rv; }
            }
        }
        const auto stIt = m_latestStates.find(rank);
        if (m_debugSessionActive && m_coordinator && stIt != m_latestStates.end() && stIt->second.currentState == QLatin1String("stopped")) {
            uint64_t evalAddr = 0ULL; bool evalOk = false;
            QEventLoop loop; QTimer timer; timer.setSingleShot(true);
            QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
            const QMetaObject::Connection conn = QObject::connect(
                m_coordinator, &IBackendCoordinator::expressionEvaluated, &loop,
                [&](int rId, const QString&, const QString& result) {
                    if (rId != rank) return;
                    static const QRegularExpression hexRe(QStringLiteral("0x([0-9a-fA-F]+)"));
                    if (const auto m = hexRe.match(result); m.hasMatch()) evalAddr = m.captured(1).toULongLong(&evalOk, 16);
                    loop.quit();
                });
            timer.start(1200); m_coordinator->evaluateExpression(rank, expr); loop.exec(); QObject::disconnect(conn);
            if (evalOk && evalAddr != 0ULL) { if (okOut) *okOut = true; return evalAddr; }
        }
        return 0ULL;
    };

    auto fetcher = [this](int rank, uint64_t addr, int length) -> QByteArray {
        if (!m_debugSessionActive || !m_coordinator || addr == 0ULL || length <= 0) return {};
        if (const qint64 pid = m_coordinator->getPidForRank(rank); pid > 0) {
            try {
                const std::vector<double> words = NativeMemoryReader::readDoubles(
                    static_cast<pid_t>(pid), static_cast<uintptr_t>(addr), static_cast<size_t>((length + 7) / 8));
                if (!words.empty()) {
                    QByteArray out(length, Qt::Uninitialized);
                    std::memcpy(out.data(), words.data(), static_cast<size_t>(length));
                    return out;
                }
            } catch (...) {}
        }
        const auto stIt = m_latestStates.find(rank);
        if (stIt == m_latestStates.end() || stIt->second.currentState != QLatin1String("stopped")) return {};
        QByteArray fallbackBytes; QEventLoop loop; QTimer timer; timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        m_suppressMemViewBroadcast = true;
        const QMetaObject::Connection conn = QObject::connect(
            m_coordinator, &IBackendCoordinator::memoryRead, &loop,
            [&](int rId, const QString&, const QByteArray& data) { if (rId == rank) { fallbackBytes = data; loop.quit(); } });
        timer.start(1500); m_coordinator->readMemory(rank, QStringLiteral("0x%1").arg(addr, 0, 16), length); loop.exec();
        QObject::disconnect(conn); m_suppressMemViewBroadcast = false;
        if (fallbackBytes.size() > length) fallbackBytes.truncate(length);
        return fallbackBytes;
    };

    m_memView->setLiveMemoryCallbacks(resolver, fetcher, [this](int rank) -> qint64 {
        return (m_debugSessionActive && m_coordinator) ? m_coordinator->getPidForRank(rank) : 0LL;
    });
    if (menuBar()) {
        for (QAction* topAct : menuBar()->actions()) {
            if (QMenu* menu = topAct->menu()) {
                for (QAction* act : menu->actions()) {
                    if (act->text().contains(QStringLiteral("Attach to Process"), Qt::CaseInsensitive)) {
                        act->setObjectName(QStringLiteral("actionAttachProcess"));
                        if (act->shortcut().isEmpty()) act->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_P));
                        act->disconnect(this);
                        connect(act, &QAction::triggered, this, [this]() { attachToRunningProcesses(); });
                    }
                }
            }
        }
    }
}

QStringList collectExpressionCandidates(MainWindow* mw) {
    QStringList out;
    QSet<QString> seen;
    auto push = [&](const QString& raw) {
        const QString s = raw.trimmed();
        if (!s.isEmpty() && !seen.contains(s)) { seen.insert(s); out.append(s); }
    };
    if (!mw) return out;
    if (auto* model = mw->variablesDockWidget() ? mw->variablesDockWidget()->findChild<VariableTreeModel*>() : nullptr) {
        auto walk = [&](auto& self, const QModelIndex& parent, int depth) -> void {
            if (depth > 3) return;
            for (int r = 0, rows = model->rowCount(parent); r < rows; ++r) {
                const QModelIndex idx = model->index(r, 0, parent);
                const QString expr = model->expressionPath(idx);
                const QString name = model->data(idx).toString();
                const QString type = model->data(model->index(r, 2, parent)).toString();
                push(expr); push(name);
                if (!expr.isEmpty() && !expr.endsWith(QLatin1Char(']')) &&
                    (type.contains(QStringLiteral("vector")) || type.contains(QStringLiteral("array")) ||
                     type.contains(QLatin1Char('[')) || type.contains(QLatin1Char('*')))) {
                    push(expr + QStringLiteral("[0]"));
                }
                self(self, idx, depth + 1);
            }
        };
        walk(walk, QModelIndex(), 0);
    }
    const QList<SourceCodeView*> views = mw->editorTabManager()
                                             ? mw->editorTabManager()->openViews()
                                             : (mw->sourceCodeView() ? QList<SourceCodeView*>{mw->sourceCodeView()} : QList<SourceCodeView*>{});
    static const QRegularExpression kTypeRe(QStringLiteral(R"(\b(?:struct|class)\s+([A-Za-z_][A-Za-z0-9_]*)\b)"));
    static const QRegularExpression kVecDeclRe(QStringLiteral(R"(\b(?:(?:vector|array)\s*<[^>;]+>\s+([A-Za-z_][A-Za-z0-9_]*)|[A-Za-z_][A-Za-z0-9_]*\s+([A-Za-z_][A-Za-z0-9_]*)\s*\[[0-9]+\]))"));
    static const QRegularExpression kIdxRe(QStringLiteral(R"(\b([A-Za-z_][A-Za-z0-9_]*(?:\[[0-9]+\])+(?:\.[A-Za-z_][A-Za-z0-9_]*)*))"));
    static const QRegularExpression kDeclRe(QStringLiteral(R"(^\s*(?:const\s+)?(?:std::)?[A-Za-z_][A-Za-z0-9_:<>*&\s]*?\b([A-Za-z_][A-Za-z0-9_]*)\s*(?:\[[^\]]*\]|\(|=|\{|;))"), QRegularExpression::MultilineOption);
    for (SourceCodeView* sv : views) {
        if (!sv) continue;
        const QString src = sv->toPlainText();
        for (auto it = kTypeRe.globalMatch(src); it.hasNext();) push(it.next().captured(1));
        for (auto it = kVecDeclRe.globalMatch(src); it.hasNext();) {
            const auto m = it.next();
            const QString v = !m.captured(1).isEmpty() ? m.captured(1) : m.captured(2);
            push(v); push(v + QStringLiteral("[0]"));
        }
        for (auto it = kIdxRe.globalMatch(src); it.hasNext();) push(it.next().captured(1));
        for (auto it = kDeclRe.globalMatch(src); it.hasNext();) {
            const QString id = it.next().captured(1);
            if (id != QLatin1String("return") && id != QLatin1String("if") && id != QLatin1String("for") &&
                id != QLatin1String("while") && id != QLatin1String("main")) push(id);
        }
    }
    if (auto* symTbl = mw->projectExplorerWidget() ? mw->projectExplorerWidget()->findChild<QTableWidget*>(QStringLiteral("explorerSymbolsTable")) : nullptr) {
        for (int r = 0, cap = qMin(250, symTbl->rowCount()); r < cap; ++r) {
            if (auto* it = symTbl->item(r, 2)) push(it->text());
        }
    }
    return out;
}

void refreshDialogCompleters(MainWindow* mw) {
    if (!mw || !mw->variablesDockWidget()) return;
    if (auto* model = mw->variablesDockWidget()->findChild<VariableTreeModel*>(); model && !model->property("gl_completer_wired").toBool()) {
        model->setProperty("gl_completer_wired", true);
        QPointer<MainWindow> safeMw(mw);
        QObject::connect(model, &QAbstractItemModel::modelReset, mw, [safeMw]() { if (safeMw && safeMw->variablesDockWidget()) refreshDialogCompleters(safeMw); }, Qt::QueuedConnection);
        QObject::connect(model, &QAbstractItemModel::rowsInserted, mw, [safeMw]() { if (safeMw && safeMw->variablesDockWidget()) refreshDialogCompleters(safeMw); }, Qt::QueuedConnection);
    }
    const auto pgs = mw->findChildren<PointerGraphDialog*>();
    const auto aps = mw->findChildren<ArrayPlotDialog*>();
    if (pgs.isEmpty() && aps.isEmpty()) return;
    const QStringList cands = collectExpressionCandidates(mw);
    for (auto* pg : pgs) pg->setCompletionCandidates(cands);
    for (auto* ap : aps) ap->setCompletionCandidates(cands);
}

void graphStructTypeFromSource(MainWindow* mw, const QString& typeName) {
    if (!mw || typeName.trimmed().isEmpty()) return;
    const QString cleanType = typeName.trimmed();
    QStringList matchedExprs;
    if (auto* model = mw->variablesDockWidget() ? mw->variablesDockWidget()->findChild<VariableTreeModel*>() : nullptr) {
        for (int r = 0; r < model->rowCount(); ++r) {
            const QModelIndex idx = model->index(r, 0);
            const QString expr = model->expressionPath(idx), tp = model->data(model->index(r, 2)).toString();
            if (tp.contains(cleanType)) {
                matchedExprs.append((tp.contains(QStringLiteral("vector")) || tp.contains(QStringLiteral("array")) || tp.contains(QLatin1Char('['))) ? (expr + QStringLiteral("[0]")) : expr);
            }
        }
    }
    if (matchedExprs.isEmpty() && mw->sourceCodeView()) {
        const QString src = mw->sourceCodeView()->toPlainText();
        const QRegularExpression arrInst(QStringLiteral(R"(\b(?:(?:vector|array)\s*<\s*(?:const\s+)?%1\b[^>]*>\s+([A-Za-z_][A-Za-z0-9_]*)|%1\s+([A-Za-z_][A-Za-z0-9_]*)\s*\[[0-9]+\]))").arg(QRegularExpression::escape(cleanType)));
        for (auto it = arrInst.globalMatch(src); it.hasNext();) {
            const auto m = it.next();
            const QString v = !m.captured(1).isEmpty() ? m.captured(1) : m.captured(2);
            matchedExprs.append(v + QStringLiteral("[0]"));
        }
        const QRegularExpression dirInst(QStringLiteral(R"(\b%1\s*[\*&]?\s+([A-Za-z_][A-Za-z0-9_]*)\b(?!\s*\[))").arg(QRegularExpression::escape(cleanType)));
        for (auto it = dirInst.globalMatch(src); it.hasNext();) {
            const QString v = it.next().captured(1);
            if (v != cleanType && !matchedExprs.contains(v)) matchedExprs.append(v);
        }
    }
    mw->openPointerGraphVisualizer();
    if (auto* pg = mw->findChild<PointerGraphDialog*>(QStringLiteral("PointerGraphDialog"))) {
        for (const QString& ex : matchedExprs) pg->addAndEvaluateRootExpression(ex, cleanType);
        pg->setRootInputText(matchedExprs.isEmpty() ? cleanType : matchedExprs.first());
    }
    refreshDialogCompleters(mw);
}

void bindSourceViewGraphSignals(MainWindow* mw, SourceCodeView* sv) {
    if (!mw || !sv || sv->property("gl_graph_ctx_wired").toBool()) return;
    sv->setProperty("gl_graph_ctx_wired", true);
    QObject::connect(sv, &SourceCodeView::graphExpressionRequested, mw, [mw](const QString& expr) { mw->openPointerGraphVisualizer(expr); });
    QObject::connect(sv, &SourceCodeView::graphStructTypeRequested, mw, [mw](const QString& tp) { graphStructTypeFromSource(mw, tp); });
    QObject::connect(sv, &SourceCodeView::sendToCalculatorRequested, mw, &MainWindow::openProgrammerCalculatorWithText, Qt::UniqueConnection);
}

/** @brief Opens the modeless Pointer & Struct Graph Visualizer and optionally graphs @p exprPath. */
void MainWindow::openPointerGraphVisualizer(const QString& exprPath, const QString& varValue, const QString& varType) {
    if (!m_pointerGraphDialog) m_pointerGraphDialog = new PointerGraphDialog(m_coordinator, this);
    m_pointerGraphDialog->setCoordinator(m_coordinator);
    m_pointerGraphDialog->setFocusedRank(m_focusedRank);
    refreshDialogCompleters(this);
    if (!exprPath.trimmed().isEmpty()) m_pointerGraphDialog->addAndEvaluateRootExpression(exprPath.trimmed(), varType, varValue);
    m_pointerGraphDialog->show(); m_pointerGraphDialog->raise(); m_pointerGraphDialog->activateWindow();
}

/** @brief Opens or raises the live 1D/2D Array & Field Plotter dialog and optionally plots @p exprPath. */
void MainWindow::openArrayPlotter(const QString& exprPath, const QString& typeHint) {
    if (!m_arrayPlotDialog) {
        m_arrayPlotDialog = new ArrayPlotDialog(m_coordinator, this);
        connect(m_arrayPlotDialog, &ArrayPlotDialog::multiRankPlotRequested, this,
                [this](const QString& baseExpr, int byteCount, ArrayPlotDialog::ElementType, int, int) {
                    if (!m_coordinator || !m_arrayPlotDialog || baseExpr.isEmpty() || byteCount <= 0) return;
                    const QString addr = (baseExpr.startsWith(QLatin1Char('&')) || baseExpr.startsWith(QStringLiteral("0x")))
                                             ? baseExpr : QStringLiteral("&(%1)[0]").arg(baseExpr);
                    for (int r : m_arrayPlotDialog->selectedOverlayRanks(m_coordinator->getProcessCount())) {
                        m_arrayPlotDialog->expectOverlayReadForRank(r);
                        m_coordinator->readMemory(r, addr, byteCount);
                    }
                });
        if (auto* rc = m_arrayPlotDialog->findChild<QComboBox*>(QStringLiteral("arrayPlotRankCombo"))) {
            connect(rc, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                    [this, rc](int idx) { if (idx >= 0) setFocusedRank(rc->itemData(idx).toInt()); });
        }
    }
    m_arrayPlotDialog->setCoordinator(m_coordinator);
    m_arrayPlotDialog->setFocusedRank(m_focusedRank);
    if (!typeHint.trimmed().isEmpty()) m_arrayPlotDialog->setElementTypeFromString(typeHint);
    if (!exprPath.trimmed().isEmpty()) { m_arrayPlotDialog->setExpression(exprPath.trimmed()); m_arrayPlotDialog->refreshPlot(); }
    m_arrayPlotDialog->show(); m_arrayPlotDialog->raise(); m_arrayPlotDialog->activateWindow();
}

/** @brief Opens or raises the Signal Disposition Matrix & Catchpoints manager dialog (Ctrl+Alt+S). */
void MainWindow::openSignalsAndCatchpoints() {
    if (!m_signalCatchpointDialog) m_signalCatchpointDialog = new SignalCatchpointDialog(m_coordinator, this);
    m_signalCatchpointDialog->setCoordinator(m_coordinator);
    m_signalCatchpointDialog->reloadStyle();
    m_signalCatchpointDialog->show(); m_signalCatchpointDialog->raise(); m_signalCatchpointDialog->activateWindow();
}

/** @brief Opens or raises the Copy-on-Write Process Checkpoints manager dialog (Ctrl+Alt+K). */
void MainWindow::openProcessCheckpoints() {
    if (!m_checkpointDialog) {
        m_checkpointDialog = new CheckpointDialog(m_coordinator, this);
        if (auto* rs = m_checkpointDialog->findChild<QSpinBox*>(QStringLiteral("checkpointRankSpin"))) {
            connect(rs, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::setFocusedRank);
        }
    }
    m_checkpointDialog->setCoordinator(m_coordinator);
    m_checkpointDialog->setFocusedRank(m_focusedRank);
    m_checkpointDialog->reloadStyle();
    m_checkpointDialog->show(); m_checkpointDialog->raise(); m_checkpointDialog->activateWindow();
}

}  // namespace gridlock::ui
