/**
 * @file MainWindowLifecycle.cpp
 * @brief Coordinator binding, session teardown, global event filtering, and telemetry for MainWindow.
 *
 * Implements backend coordinator signal wiring, preview fixture seeding, live status-bar
 * memory telemetry, graceful closeEvent() session/geometry persistence and synchronous
 * debugger/MPI/LSP reaping, and Double-Shift / global shortcut event filtering.
 */

#include "MainWindow.hpp"

#include <memory>

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QDialog>
#include <QDockWidget>
#include <QDynamicPropertyChangeEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QMap>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPointer>
#include <QSettings>
#include <QSplitter>
#include <QThreadPool>
#include <QTimer>

#include "../core/commands/DebugCommands.hpp"
#include "../core/hpc/analysis/DeadlockAnalyzer.hpp"
#include "../core/hpc/dap/DapCoordinator.hpp"
#include "../core/hpc/gdb/GdbRankCoordinator.hpp"
#include "../core/hpc/lsp/LspCoordinator.hpp"
#include "../core/managers/ConfigManager.hpp"
#include "../core/managers/ShortcutManager.hpp"
#include "../core/managers/ThemeManager.hpp"
#include "../core/utils/ClangdLspClient.hpp"
#include "../core/utils/ElfBinaryInspector.hpp"
#include "EditorTabManager.hpp"
#include "GdbConsoleWidget.hpp"
#include "dialogs/graph/PointerGraphDialog.hpp"
#include "dialogs/plotter/ArrayPlotDialog.hpp"
#include "dialogs/preferences/PreferencesDialog.hpp"
#include "dialogs/project/ProjectSettingsDialog.hpp"
#include "dialogs/search/CommandPaletteDialog.hpp"
#include "docks/TerminalDockWidget.hpp"
#include "docks/VariablesDockWidget.hpp"
#include "utils/FileDialogHelper.hpp"
#include "views/DisassemblyView.hpp"
#include "views/MemView.hpp"
#include "views/RegisterView.hpp"
#include "views/ServerRackView.hpp"
#include "views/SourceCodeView.hpp"
#include "widgets/DifferentialGrid.hpp"
#include "widgets/ExpressionEvaluatorWidget.hpp"
#include "widgets/MpiDiagnosticsWidget.hpp"
#include "widgets/MpiNetworkLogWidget.hpp"
#include "widgets/ProjectExplorerWidget.hpp"

namespace gridlock::ui {

/**
 * @brief Generates synthetic telemetry fixture data for headless snapshots and testing.
 *
 * Populates 8 sample MPI ranks with stopped/running execution states and simulated
 * program counter registers across the .text segment.
 */
void MainWindow::loadPreviewFixture() {
    m_previewFixtureOnly = true;
    m_useRealBinaryTarget = false;
    if (m_workspacePerspective == QStringLiteral("Source")) setWorkspacePerspective(QStringLiteral("Code"));
    syncRankCountChrome(8);
    if (m_projectExplorerWidget) {
        m_projectExplorerWidget->seedDefaultSymbols();
        m_projectExplorerWidget->seedDefaultStrings();
    }
    if (m_disassemblyView) m_disassemblyView->loadSampleDisassembly();
    if (m_registerView) m_registerView->seedPreviewRegisters();
    if (m_mpiDiagnosticsWidget) m_mpiDiagnosticsWidget->populateDefaultRankTelemetry();
    if (m_memView) {
        if (const QByteArray pm = synthesizeMemoryWindow(0x00401120ULL, 256); !pm.isEmpty()) {
            m_memView->setMemoryData(QStringLiteral("0x00401120"), pm);
        }
    }
    QList<RankState> fixtureRanks;
    fixtureRanks.reserve(8);
    for (int i = 0; i < 8; ++i) {
        RankState st;
        st.rankId = i;
        st.currentState = (i == 0) ? QStringLiteral("stopped") : QStringLiteral("running");
        st.registers[16] = (i == 0) ? QStringLiteral("0x00401120")
                                    : (i == 1 ? QStringLiteral("0x00401155") : QStringLiteral("0x00401157"));
        st.totalRuntimeMs = 12 + i * 3;
        fixtureRanks.append(st);
        m_latestStates[i] = st;
    }
    if (m_serverRackView) {
        m_serverRackView->updateRanks(fixtureRanks);
    } else if (m_disassemblyView) {
        QMap<int, uint64_t> pcs{{0, 0x00401120ULL}, {1, 0x00401155ULL}};
        for (int i = 2; i < 8; ++i) pcs.insert(i, 0x00401157ULL);
        m_disassemblyView->setRankAddresses(pcs);
    }
    // Seed stack-frame local variables into VariableTreeModel when empty so Variables tab displays 10+ rows.
    if (m_variablesDockWidget) {
        if (auto* varModel = m_variablesDockWidget->findChild<gridlock::VariableTreeModel*>()) {
            if (varModel->rowCount() == 0) {
                auto mkVar = [](const char* n, const char* v, const char* t) {
                    return QJsonObject{{QStringLiteral("name"), QLatin1String(n)},
                                       {QStringLiteral("value"), QLatin1String(v)},
                                       {QStringLiteral("type"), QLatin1String(t)}};
                };
                varModel->onLocalsUpdated(
                    0, 0,
                    QJsonArray{mkVar("mpi_rank", "0", "int"), mkVar("world_size", "8", "int"),
                               mkVar("argc", "1", "int"), mkVar("argv", "0x7fffffffe5a8", "char **"),
                               mkVar("data[0].id", "0", "int"), mkVar("data[0].coords.x", "0.0f", "float"),
                               mkVar("data[0].coords.y", "1.25f", "float"),
                               mkVar("send_buf", "0x00404040", "double *"),
                               mkVar("recv_buf", "0x00404140", "double *"), mkVar("tag", "42", "int"),
                               mkVar("comm", "MPI_COMM_WORLD", "MPI_Comm")});
            }
        }
    }
}

/** @brief Dispatches a polymorphic debugger command object. */
void MainWindow::executeCommand(std::unique_ptr<gridlock::core::commands::IDebugCommand> cmd) {
    if (cmd) cmd->execute();
}

/** @brief Attaches a backend coordinator (GDB or DAP) and establishes signal routes. */
void MainWindow::setCoordinator(IBackendCoordinator* coord) {
    if (m_deadlockAnalyzer) { delete m_deadlockAnalyzer; m_deadlockAnalyzer = nullptr; }
    m_coordinator = coord;
    if (!m_coordinator) m_debugSessionActive = false;
    else if (m_coordinator->getProcessCount() > 0) m_debugSessionActive = true;
    if (!m_isShuttingDown && !m_isClosing) { updateDebugToolbarState(); syncExecutionScopeUi(); }
    const QPointer<MainWindow> self(this);

    if (auto* dapCoord = dynamic_cast<DapCoordinator*>(m_coordinator)) {
        const QPointer<DapCoordinator> dapGuard(dapCoord);
        connect(dapCoord, &DapCoordinator::stateChanged, this, [self, dapGuard](SessionState state) {
            if (!self || self->m_isShuttingDown || self->m_isClosing || !dapGuard) return;
            if (state == SessionState::Disconnected) {
                self->m_debugSessionActive = false; self->updateDebugToolbarState();
                self->updateStatusTelemetry(QString(), QStringLiteral("IDLE"));
            } else if (state == SessionState::Launching) { self->updateStatusTelemetry(QString(), QStringLiteral("LAUNCHING"));
            } else if (state == SessionState::Queued) { self->updateStatusTelemetry(QString(), QStringLiteral("QUEUED"));
            } else if (state == SessionState::Running) {
                self->updateStatusTelemetry(QString(), QStringLiteral("RUNNING"));
                for (int i = 0; i < dapGuard->getProcessCount(); ++i) {
                    RankState rs = self->m_latestStates.contains(i) ? self->m_latestStates[i] : RankState{};
                    rs.rankId = i; rs.currentState = "running"; rs.executionTimer.start(); self->onRankStateChanged(i, rs);
                }
            } else if (state == SessionState::Paused) { self->updateStatusTelemetry(QString(), QStringLiteral("PAUSED")); }
        });
        connect(dapCoord, &DapCoordinator::executionStopped, this, [self](int rankId, const QString&) {
            if (!self || self->m_isShuttingDown || self->m_isClosing) return;
            RankState rs = self->m_latestStates.contains(rankId) ? self->m_latestStates[rankId] : RankState{};
            rs.rankId = rankId; rs.currentState = "stopped";
            rs.totalRuntimeMs += rs.executionTimer.isValid() ? rs.executionTimer.elapsed() : 0;
            self->onRankStateChanged(rankId, rs);
        });
    }

    // Connect variable inspection dock and polymorphic live DeadlockAnalyzer across GDB and DAP.
    if (m_variablesDockWidget) m_variablesDockWidget->setCoordinator(m_coordinator);
    if (m_expressionEvaluatorWidget && m_coordinator && m_coordinator->getProcessCount() > 0)
        m_expressionEvaluatorWidget->setAvailableRankCount(m_coordinator->getProcessCount());
    if (auto* gdbCoord = dynamic_cast<gridlock::GdbRankCoordinator*>(m_coordinator)) {
        connect(gdbCoord, &gridlock::GdbRankCoordinator::rankStateChanged, this, [self](int rankId, const RankState& st) {
            if (self && !self->m_isShuttingDown && !self->m_isClosing) self->onRankStateChanged(rankId, st);
        });
        connect(gdbCoord, &gridlock::GdbRankCoordinator::localsUpdated, this, [self](int rankId, const QJsonArray& locals) {
            if (!self || self->m_isShuttingDown || self->m_isClosing) return;
            if (rankId == self->m_focusedRank && self->m_variablesDockWidget) self->m_variablesDockWidget->applyLocalsForRank(rankId, locals);
        });
    }

    // Bind coordinator execution, call-stack, console REPL, watchpoints, and deadlock routes.
    if (m_coordinator) {
        if (m_expressionEvaluatorWidget)
            connect(m_coordinator, &IBackendCoordinator::expressionEvaluated, m_expressionEvaluatorWidget, &ExpressionEvaluatorWidget::appendResult, Qt::UniqueConnection);
        connect(m_coordinator, &IBackendCoordinator::sessionErrorOccurred, this, [self](const QString& errMsg) {
            if (!self || self->m_isShuttingDown || self->m_isClosing) return;
            if (self->m_debugSessionActive) self->stopTargetWorkflow();
            self->appendProgramOutput(-1, QStringLiteral("[Session Error] %1").arg(errMsg), true); self->flushProgramOutputBatch();
            self->updateStatusTelemetry(QStringLiteral("0x00000000"), QStringLiteral("ATTACH / LAUNCH FAILED"));
        });
        bindCallStackCoordinatorRoutes();
        bindConsoleAndWatchpointRoutes();
        m_deadlockAnalyzer = new gridlock::core::DeadlockAnalyzer(m_coordinator, this);
        if (m_mpiDiagnosticsWidget) {
            connect(m_deadlockAnalyzer, &gridlock::core::DeadlockAnalyzer::deadlockDetected, m_mpiDiagnosticsWidget, &MpiDiagnosticsWidget::onDeadlockDetected);
            connect(m_deadlockAnalyzer, &gridlock::core::DeadlockAnalyzer::rankCleared, m_mpiDiagnosticsWidget, &MpiDiagnosticsWidget::onRankCleared);
        }
        connect(m_deadlockAnalyzer, &gridlock::core::DeadlockAnalyzer::deadlockDetected, this, [self](const gridlock::core::DeadlockInfo& info) {
            if (!self || self->m_isShuttingDown || self->m_isClosing) return;
            RankState rs = self->m_latestStates.contains(info.rankId) ? self->m_latestStates[info.rankId] : RankState{};
            rs.rankId = info.rankId; rs.currentState = QStringLiteral("deadlock"); rs.currentFunction = info.blockedFunction;
            self->m_latestStates[info.rankId] = rs;
            if (self->m_serverRackView) self->m_serverRackView->updateRankState(info.rankId, rs);
        });
        // Route execution location updates (file:line) without hijacking user-focused secondary tabs.
        connect(m_coordinator, &IBackendCoordinator::locationChanged, this, [self](int rankId, const QString& file, int line) {
            if (!self || self->m_isShuttingDown || self->m_isClosing) return;
            const QString prevFile = self->m_latestStates.contains(rankId) ? self->m_latestStates[rankId].currentFile : QString();
            const int prevLine = self->m_latestStates.contains(rankId) ? self->m_latestStates[rankId].currentLine : -1;
            const bool sameLoc = (prevFile == file && prevLine == line && self->m_latestStates[rankId].currentState == QLatin1String("stopped"));
            self->m_latestStates[rankId].currentFile = file; self->m_latestStates[rankId].currentLine = line;
            if (rankId != self->m_focusedRank || line <= 0) return;
            if (self->m_suppressExecutionTabSwitch || sameLoc) {
                if (SourceCodeView* sv = (self->m_editorTabManager && !file.isEmpty()) ? self->m_editorTabManager->getSourceCodeViewForFile(file) : nullptr) sv->setExecutionLineOnly(line);
                return;
            }
            SourceCodeView* view = (!file.isEmpty() && QFileInfo::exists(file) && self->m_editorTabManager) ? self->m_editorTabManager->openFile(file) : self->getSourceCodeView();
            if (view) { self->syncTabBreakpointMetadata(view); view->highlightExecutionLine(line); }
        });

        // Wire GDB-specific hover, raw MI stream, and SIGFPE handlers when using GDB.
        if (auto* gdbCoord = dynamic_cast<gridlock::GdbRankCoordinator*>(m_coordinator)) {
            connect(gdbCoord, &gridlock::GdbRankCoordinator::hoverEvaluationComplete, this, [self](const QString& varName, const QString& result, QPoint globalPos) {
                if (self && !self->m_isShuttingDown && !self->m_isClosing && self->getSourceCodeView() && self->m_plainJaneTooltip)
                    showPlainJaneTooltip(self->m_plainJaneTooltip, globalPos, QString("<b>%1</b>: %2").arg(varName).arg(result));
            });
            if (m_gdbConsoleWidget) {
                connect(gdbCoord, &gridlock::GdbRankCoordinator::gdbOutputReceived, m_gdbConsoleWidget, &GdbConsoleWidget::appendGdbOutput, Qt::UniqueConnection);
                connect(gdbCoord, &gridlock::GdbRankCoordinator::commandSentToGdb, m_gdbConsoleWidget, &GdbConsoleWidget::appendGdbInput, Qt::UniqueConnection);
                connect(m_gdbConsoleWidget, &GdbConsoleWidget::completionRequested, gdbCoord, &gridlock::GdbRankCoordinator::requestConsoleCompletions, Qt::UniqueConnection);
                connect(gdbCoord, &IBackendCoordinator::consoleCompletionsReady, m_gdbConsoleWidget, &GdbConsoleWidget::onConsoleCompletionsReady, Qt::UniqueConnection);
            }
            connect(gdbCoord, &gridlock::GdbRankCoordinator::gdbOutputReceived, this, [self](int rankId, const QString& output) {
                if (!self || self->m_isShuttingDown || self->m_isClosing) return;
                if (output.contains("reason=\"signal-received\"") && output.contains("signal-name=\"SIGFPE\"")) {
                    self->setFocusedRank(rankId);
                    if (self->networkLog()) self->networkLog()->appendError(QString("SIGFPE (Floating-Point Exception) detected on Rank %1!\n").arg(rankId));
                }
            });
            connect(gdbCoord, &gridlock::GdbRankCoordinator::memoryDataReady, this, [self](int rankId, qint64 beginAddress, const QString& hexContents) {
                if (!self || self->m_isShuttingDown || self->m_isClosing) return;
                if (!self->m_suppressMemViewBroadcast && self->m_memView && rankId == self->m_memView->focusedRank()) {
                    self->m_memView->setMemoryData(beginAddress, hexContents);
                    self->m_memView->setSourceModeLabel(QStringLiteral("LIVE RANK %1 (PID %2)").arg(rankId).arg(self->m_coordinator ? self->m_coordinator->getPidForRank(rankId) : 0LL));
                }
            });
        }
        connect(m_coordinator, &IBackendCoordinator::programOutputReceived, this, [self](int rank, const QString& text, bool isErr) {
            if (self && !self->m_isShuttingDown && !self->m_isClosing) self->appendProgramOutput(rank, text, isErr);
        });
        if (networkLog()) {
            connect(m_coordinator, &IBackendCoordinator::targetOutputReceived, this, [self](const QString& category, const QString& output) {
                if (self && !self->m_isShuttingDown && !self->m_isClosing && self->networkLog()) self->networkLog()->appendText(category, output);
            });
        }
        connect(m_coordinator, &IBackendCoordinator::memoryRead, this, [self](int rankId, const QString& address, const QByteArray& data) {
            if (!self || self->m_isShuttingDown || self->m_isClosing) return;
            if (!self->m_suppressMemViewBroadcast && self->m_memView && rankId == self->m_memView->focusedRank()) {
                self->m_memView->setMemoryData(address, data);
                self->m_memView->setSourceModeLabel(QStringLiteral("LIVE RANK %1 (PID %2)").arg(rankId).arg(self->m_coordinator ? self->m_coordinator->getPidForRank(rankId) : 0LL));
            }
        });
        connect(m_coordinator, &IBackendCoordinator::registersUpdated, this, [self](int rankId, const QJsonArray& registers) {
            if (!self || self->m_isShuttingDown || self->m_isClosing) return;
            for (const QJsonValue& rv : registers) {
                const QJsonObject ro = rv.toObject();
                const QString rn = ro.value(QStringLiteral("name")).toString().toLower(), rval = ro.value(QStringLiteral("value")).toString();
                if (rn == QStringLiteral("rip") || rn == QStringLiteral("pc")) self->m_latestStates[rankId].registers[16] = rval;
                else if (rn == QStringLiteral("rsp") || rn == QStringLiteral("sp")) self->m_latestStates[rankId].registers[7] = rval;
            }
            if (rankId == self->m_focusedRank) {
                if (self->m_registerView) self->m_registerView->updateRegisters(registers);
                const QString pcToken = self->m_latestStates[rankId].registers.value(16).section(QLatin1Char(' '), 0, 0);
                bool ok = false;
                const uint64_t pcVal = pcToken.mid(pcToken.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive) ? 2 : 0).toULongLong(&ok, 16);
                if (ok && pcVal != 0ULL && self->m_disassemblyView) {
                    self->m_disassemblyView->setCurrentAddress(pcVal); self->m_disassemblyView->setActiveSteppingRank(rankId, pcVal);
                }
            }
        });
    }
}

/** @brief Appends live per-rank Stack/Heap and host RAM/CPU telemetry to the bottom status bar label. */
void MainWindow::refreshStatusBarMemoryTelemetry() {
    if (m_isShuttingDown || m_isClosing || !m_statusTelemetryLabel) return;
    const qint64 rankPid = (m_debugSessionActive && m_coordinator) ? m_coordinator->getPidForRank(m_focusedRank) : 0;
    quint64 currentRsp = 0, stackTop = 0;
    if (rankPid > 0 && m_latestStates.count(m_focusedRank)) {
        const QString rspStr = m_latestStates[m_focusedRank].registers.value(7).section(QLatin1Char(' '), 0, 0);
        bool ok = false;
        currentRsp = rspStr.trimmed().mid(rspStr.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive) ? 2 : 0).toULongLong(&ok, 16);
        if (ok && currentRsp > 0) {
            stackTop = gridlock::core::ElfBinaryInspector::queryStackTopAddress(rankPid);
            if (stackTop == 0) stackTop = (currentRsp & ~0xfffULL) + 0x1000ULL;
        }
    }
    const auto mem = gridlock::core::ElfBinaryInspector::queryProcessMemory(rankPid, currentRsp, stackTop);
    const QString summary = gridlock::core::ElfBinaryInspector::formatStatusBarSummary(
        mem, gridlock::core::ConfigManager::instance().telemetryDisplayFormat());
    QString base = m_statusTelemetryLabel->text();
    int cutIdx = base.indexOf(QStringLiteral(" | STACK:"));
    const int idleIdx = base.indexOf(QStringLiteral(" | TARGET: IDLE"));
    if (idleIdx >= 0 && (cutIdx < 0 || idleIdx < cutIdx)) cutIdx = idleIdx;
    if (cutIdx >= 0) base = base.left(cutIdx);
    m_statusTelemetryLabel->setText(base + QStringLiteral(" | ") + summary);
}

/** @brief Handles application termination, cleans up sessions, and persists layout. */
void MainWindow::closeEvent(QCloseEvent* event) {
    const bool hadModified = m_editorTabManager && m_editorTabManager->hasModifiedTabs();
    if (m_editorTabManager && !m_editorTabManager->confirmCloseAllModifiedTabs()) {
        if (event) event->ignore();
        return;
    }
    const bool interactive = QGuiApplication::platformName() != QStringLiteral("offscreen");
    if (!hadModified && interactive &&
        QSettings(gridlock::core::ConfigManager::debuggerSettingsPath(), QSettings::IniFormat).value("behavior/confirm_quit", true).toBool()) {
        if (QMessageBox::question(this, QStringLiteral("Quit GridLock"), QStringLiteral("Exit GridLock?"),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) != QMessageBox::Yes) {
            if (event) event->ignore();
            return;
        }
    }
    // Begin guarded teardown: set shutdown flags immediately, remove event filter, and stop all timers.
    m_isShuttingDown = true;
    m_isClosing = true;
    if (qApp) qApp->removeEventFilter(this);
    if (m_telemetryTimer) m_telemetryTimer->stop();
    for (auto* t : findChildren<QTimer*>()) if (t) t->stop();
    if (m_memView && m_memView->isPoppedOut()) m_memView->setPoppedOut(false);
    if (!m_activeWorkspaceDir.isEmpty()) {
        saveSession(false);
        if (m_differentialGrid) {
            auto ps = gridlock::core::ConfigManager::instance().loadProjectSettings();
            ps.watchExpressions = m_differentialGrid->getWatchExpressions();
            gridlock::core::ConfigManager::instance().saveProjectSettings(ps);
        }
    } else if (!m_loadedBinaryPath.isEmpty()) {
        gridlock::core::ConfigManager::instance().recordRecentSession(m_loadedBinaryPath);
    }
    gridlock::core::ThemeManager::instance().savePersistedTheme();
    if (interactive) {
        QSettings settings(gridlock::core::ConfigManager::debuggerSettingsPath(), QSettings::IniFormat);
        settings.setValue("MainWindow/Geometry_v9", saveGeometry()); settings.setValue("MainWindow/State_v9", saveState());
        for (auto* splitter : findChildren<QSplitter*>())
            if (!splitter->objectName().isEmpty()) settings.setValue("MainWindow/Splitter_v9_" + splitter->objectName(), splitter->saveState());
    }
    // Synchronously disconnect and reap active debug/MPI sessions and embedded terminal PTY before dock teardown.
    shutdownActiveSessionSync();
    if (m_terminalDock) m_terminalDock->shutdownTerminalSession();
    if (m_lspCoordinator) { QObject::disconnect(m_lspCoordinator, nullptr, this, nullptr); m_lspCoordinator->stop(); }
    if (m_editorTabManager) QObject::disconnect(m_editorTabManager, nullptr, this, nullptr);
    QObject::disconnect(&gridlock::core::ClangdLspClient::instance(), nullptr, this, nullptr);
    gridlock::core::ClangdLspClient::instance().shutdown();
    QThreadPool::globalInstance()->waitForDone(500);
    if (event) QMainWindow::closeEvent(event);
}

/** @brief Intercepts global dynamic properties, dialog polish/show hooks, Double-Shift, and IDE keybindings. */
bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (m_isShuttingDown || m_isClosing) return QMainWindow::eventFilter(watched, event);
    if (watched == qApp && event->type() == QEvent::DynamicPropertyChange) {
        const auto* dp = static_cast<QDynamicPropertyChangeEvent*>(event);
        if (dp->propertyName() == QByteArrayLiteral("gridlock_debugger_engine")) syncCoordinatorWithConfig();
        else if (dp->propertyName() == QByteArrayLiteral("gridlock_disasm_prefs_rev")) syncDisassemblyDecompilerVisibility(true);
        else if (dp->propertyName() == QByteArrayLiteral("gridlock_editor_highlight_rev") && m_editorTabManager) m_editorTabManager->rehighlightAllTabs();
    }
    if (event->type() == QEvent::Show || event->type() == QEvent::Polish) {
        if (auto* sv = qobject_cast<SourceCodeView*>(watched)) bindSourceViewGraphSignals(this, sv);
        if (auto* fd = qobject_cast<QFileDialog*>(watched))
            FileDialogHelper::configureFileDialog(fd, !m_activeWorkspaceDir.isEmpty() ? m_activeWorkspaceDir : (m_projectExplorerWidget ? m_projectExplorerWidget->rootPath() : QString()));
        if (auto* psPre = qobject_cast<ProjectSettingsDialog*>(watched)) {
            if (m_terminalDock) m_terminalDock->ensureShellStarted();
            if (!psPre->property("_glWd").toBool()) {
                psPre->setProperty("_glWd", true);
                connect(psPre, &QDialog::accepted, this, [this]() {
                    if (m_terminalDock) m_terminalDock->setWorkingDirectory(QString::fromStdString(gridlock::core::ConfigManager::instance().projectSettings().workingDirectory));
                    if (!m_activeWorkspaceDir.isEmpty()) saveSession(false);
                });
            }
        }
        if (event->type() == QEvent::Show) {
            if (m_pauseAllAction && menuBar()) {
                for (QAction* ma : menuBar()->actions())
                    if (QMenu* dm = ma ? ma->menu() : nullptr; dm && dm->title().remove(QLatin1Char('&')) == QLatin1String("Debug") && !dm->actions().contains(m_pauseAllAction)) {
                        const QList<QAction*> acts = dm->actions(); const int pIdx = m_pauseAction ? acts.indexOf(m_pauseAction) : -1;
                        dm->insertAction((pIdx >= 0 && pIdx + 1 < acts.size()) ? acts.at(pIdx + 1) : nullptr, m_pauseAllAction);
                    }
            }
            if (qobject_cast<PointerGraphDialog*>(watched) || qobject_cast<ArrayPlotDialog*>(watched)) refreshDialogCompleters(this);
            if (auto* prefs = qobject_cast<PreferencesDialog*>(watched)) {
                gridlock::core::ShortcutManager::instance().syncMenuActionsFromMainWindow();
                if (prefs->keybindingsPage()) prefs->keybindingsPage()->refreshBindings();
                connect(prefs, &PreferencesDialog::preferencesChanged, this, &MainWindow::syncCoordinatorWithConfig, Qt::UniqueConnection);
                connect(prefs, &PreferencesDialog::preferencesChanged, this, qOverload<>(&MainWindow::syncDisassemblyDecompilerVisibility), Qt::UniqueConnection);
                if (m_editorTabManager) connect(prefs, &PreferencesDialog::preferencesChanged, m_editorTabManager, &EditorTabManager::rehighlightAllTabs, Qt::UniqueConnection);
            }
        }
    }
    if (watched == m_statusTelemetryLabel && (event->type() == QEvent::Paint || event->type() == QEvent::Show) &&
        m_statusTelemetryLabel && !m_statusTelemetryLabel->text().contains(QStringLiteral("CPU:"))) refreshStatusBarMemoryTelemetry();
    if (handleDockHeaderEvent(watched, event)) return true;
    if (event->type() == QEvent::Resize)
        if (auto* dock = qobject_cast<QDockWidget*>(watched); dock && dock->parentWidget() == this && !dock->isFloating() && dock->width() > 155 && dock->height() > 155)
            for (QDockWidget* sib : tabifiedDockWidgets(dock)) if (sib && !sib->isFloating() && sib->size() != dock->size()) sib->resize(dock->size());
    if (event->type() == QEvent::MouseButtonPress) { m_bareShiftArmed = false; m_lastBareShiftReleaseMs = 0; }
    // Detect bare Double-Shift (<= 350 ms, no intervening keys/modifiers) to open Search Everywhere in All mode.
    if (watched && watched->isWidgetType() && event->type() == QEvent::KeyRelease) {
        if (auto* ke = static_cast<QKeyEvent*>(event); !ke->isAutoRepeat() && ke->key() == Qt::Key_Shift && m_bareShiftArmed) {
            m_bareShiftArmed = false; const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
            if (m_lastBareShiftReleaseMs > 0 && (nowMs - m_lastBareShiftReleaseMs) <= 350 && isVisible() &&
                !QApplication::activeModalWidget() && (!m_commandPaletteDialog || !m_commandPaletteDialog->isVisible())) {
                m_lastBareShiftReleaseMs = 0; openSearchEverywhere(0); return true;
            }
            m_lastBareShiftReleaseMs = nowMs;
        }
    }
    if (event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        const auto mods = ke->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier);
        if (watched && watched->isWidgetType() && !ke->isAutoRepeat()) {
            if (ke->key() == Qt::Key_Shift && (mods & ~Qt::ShiftModifier) == 0) m_bareShiftArmed = true;
            else { m_bareShiftArmed = false; m_lastBareShiftReleaseMs = 0; }
        }
        const QKeySequence pressed(ke->key() | mods);
        auto matchesAct = [this, &pressed](const QString& objName, const QKeySequence& fallback) {
            if (pressed.isEmpty()) return false;
            if (auto* a = findChild<QAction*>(objName)) return !a->shortcut().isEmpty() && a->shortcut() == pressed;
            return !fallback.isEmpty() && fallback == pressed;
        };
        if (matchesAct(QStringLiteral("actionPauseAllRanks"), QKeySequence(Qt::Key_F6))) { pauseAllRanksWorkflow(); return true; }
        if (matchesAct(QStringLiteral("actionCommandPalette"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P))) { openCommandPalette(QStringLiteral(">")); return true; }
        if (matchesAct(QStringLiteral("actionAttachProcess"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_P))) { attachToProcessWorkflow(); return true; }
        if (matchesAct(QStringLiteral("actionProgrammerCalculator"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_C))) { openProgrammerCalculator(); return true; }
        if (matchesAct(QStringLiteral("actionSignalsAndCatchpoints"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_S))) { openSignalsAndCatchpoints(); return true; }
        if (matchesAct(QStringLiteral("actionQuickOpen"), QKeySequence(Qt::CTRL | Qt::Key_P)) && (!m_commandPaletteDialog || !m_commandPaletteDialog->isVisible())) { openCommandPalette(QString()); return true; }
        if (watched && !watched->inherits("QKeySequenceEdit") && (!watched->parent() || !watched->parent()->inherits("QKeySequenceEdit")) &&
            matchesAct(QStringLiteral("actionFindInView"), QKeySequence(Qt::CTRL | Qt::Key_F)) && triggerEditMenuAction(QStringLiteral("Find in View"))) return true;
        if (gridlock::core::ShortcutManager::instance().handleKeyPress(watched, ke)) return true;
    }
    return QMainWindow::eventFilter(watched, event);
}

}  // namespace gridlock::ui
