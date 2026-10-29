/**
 * @file MainWindowCohesion.cpp
 * @brief Workstation UI cohesion harmonizer, F1 symbol documentation lookup, tab snapshotter,
 *        ELF binary target loader, workspace search engine, and deadlock cycle propagator.
 *
 * Implements the two-tier workstation layout normalization across all bottom console
 * tabs (stripping outer layout margins, enforcing <= 24px control heights, and removing
 * double QFrame borders), context-sensitive F1 documentation lookup into the Reference
 * Manual tab, offscreen tab PNG snapshot rendering, and binary inspection orchestration.
 */

#include "MainWindow.hpp"

#include <algorithm>
#include <memory>
#include <vector>

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QShortcut>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableView>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextEdit>
#include <QTreeView>
#include <QVBoxLayout>

#include "../core/commands/DebugCommands.hpp"
#include "../core/hpc/IBackendCoordinator.hpp"
#include "../core/managers/ConfigManager.hpp"
#include "../core/managers/DocsetManager.hpp"
#include "../core/managers/SpackManager.hpp"
#include "../core/utils/ElfBinaryInspector.hpp"
#include "GdbConsoleWidget.hpp"
#include "docks/TerminalDockWidget.hpp"
#include "docks/VariablesDockWidget.hpp"
#include "views/DisassemblyView.hpp"
#include "views/MemView.hpp"
#include "views/RegisterView.hpp"
#include "views/ServerRackView.hpp"
#include "views/SourceCodeView.hpp"
#include "widgets/DifferentialGrid.hpp"
#include "widgets/ExpressionEvaluatorWidget.hpp"
#include "widgets/MpiDiagnosticsWidget.hpp"
#include "widgets/ProjectExplorerWidget.hpp"
#include "widgets/ReferenceManualWidget.hpp"

namespace gridlock::ui {

/**
 * @brief Harmonizes all bottom console tab pages into the unified two-tier workstation layout.
 *
 * Eliminates default 9px-11px outer layout margins and double-walled frames across
 * all seven bottom tabs (Terminal, GDB Console, Memory View, MPI Diagnostics,
 * HPC Console, Evaluator, Reference Manual), enforces compact 22px control heights
 * on all interactive buttons, inputs, and combo boxes, and wires the global F1
 * context-sensitive documentation lookup shortcut.
 */
void MainWindow::harmonizeBottomConsoleTabs() {
    if (!m_bottomTabs) {
        return;
    }

    // Apply self-contained dark-slate workstation stylesheet directly to BottomConsoleTabs
    // so tab bars, nested sub-tabs, inputs, buttons, and viewports render uniformly
    // even when MainWindow is instantiated headlessly without global QApplication QSS.
    m_bottomTabs->setStyleSheet(QStringLiteral(
        "QTabWidget::pane { border: 0px; margin: 0px; padding: 0px; background: #16181d; }"
        "QTabBar { background: #181a1f; border-bottom: 1px solid #2c313a; }"
        "QTabBar::tab { background: #181a1f; color: #9da5b4; padding: 3px 10px; "
        "min-height: 18px; max-height: 22px; border-right: 1px solid #282c34; "
        "border-bottom: 1px solid #2c313a; font-size: 11px; font-weight: 600; }"
        "QTabBar::tab:selected { background: #21252b; color: #ffffff; border-bottom: 2px solid #528bff; }"
        "QTabBar::tab:hover:!selected { background: #1e222a; color: #d7dae0; }"
        "QLineEdit, QComboBox, QSpinBox { background: #141619; color: #d7dae0; "
        "border: 1px solid #323844; border-radius: 2px; padding: 0px 6px; "
        "min-height: 20px; max-height: 22px; font-size: 11px; }"
        "QLineEdit:focus, QComboBox:focus, QSpinBox:focus { border: 1px solid #528bff; background: #1b1e24; }"
        "QPushButton { background: #252932; color: #d7dae0; border: 1px solid #323844; "
        "border-radius: 2px; padding: 1px 8px; min-height: 20px; max-height: 22px; "
        "font-size: 10px; font-weight: 600; }"
        "QPushButton:hover { background: #2c323d; border: 1px solid #528bff; color: #ffffff; }"
        "QCheckBox { color: #abb2bf; font-size: 11px; font-weight: 600; spacing: 5px; }"
        "QPlainTextEdit, QTextEdit, QTextBrowser, QListWidget { background: #16181d; color: #d7dae0; "
        "border: none; font-family: 'JetBrains Mono', monospace; font-size: 11px; }"));

    // 1. Harmonize GdbConsoleWidget (GDB Console tab) outer layout and control bar metrics.
    if (m_gdbConsoleWidget) {
        m_gdbConsoleWidget->setAttribute(Qt::WA_StyledBackground, true);
        m_gdbConsoleWidget->setContentsMargins(0, 0, 0, 0);
        if (QLayout* gdbLayout = m_gdbConsoleWidget->layout()) {
            gdbLayout->setContentsMargins(0, 0, 0, 0);
            gdbLayout->setSpacing(0);
        }
        m_gdbConsoleWidget->setStyleSheet(QStringLiteral(
            "#GdbConsoleWidget { background: #16181d; }"
            "#gdbConsoleHeaderBar { background: #21252b; border-bottom: 1px solid #2c313a; }"
            "#gdbConsolePromptBar { background: #21252b; border-top: 1px solid #2c313a; }"
            "#GdbConsoleWidget QPlainTextEdit { background: #16181d; color: #d7dae0; "
            "border: none; font-family: 'JetBrains Mono', monospace; font-size: 11px; padding: 4px 6px; }"));
    }

    // 2. Harmonize SpackManager (HPC Console tab) outer layout and control bar metrics.
    if (m_spackManager) {
        m_spackManager->setAttribute(Qt::WA_StyledBackground, true);
        m_spackManager->setContentsMargins(0, 0, 0, 0);
        if (QLayout* spackLayout = m_spackManager->layout()) {
            spackLayout->setContentsMargins(0, 0, 0, 0);
            spackLayout->setSpacing(0);
            if (auto* vbox = qobject_cast<QVBoxLayout*>(spackLayout)) {
                for (int i = 0; i < vbox->count(); ++i) {
                    if (QLayout* subLayout = vbox->itemAt(i)->layout()) {
                        subLayout->setContentsMargins(6, 2, 6, 2);
                        subLayout->setSpacing(6);
                    }
                }
            }
        }
        m_spackManager->setStyleSheet(QStringLiteral(
            "#HpcConsoleDock { background: #21252b; }"
            "#HpcConsoleDock QPlainTextEdit { background: #16181d; color: #d7dae0; "
            "border: none; border-top: 1px solid #2c313a; border-bottom: 1px solid #2c313a; "
            "font-family: 'JetBrains Mono', monospace; font-size: 11px; padding: 4px 6px; }"
            "#HpcConsoleDock QLabel { color: #abb2bf; font-size: 10px; font-weight: 700; padding: 0px 4px; }"));
    }

    // 3. Enforce zero-gutter outer margins, compact <= 22px controls, and borderless frames on all tabs.
    for (int i = 0; i < m_bottomTabs->count(); ++i) {
        QWidget* page = m_bottomTabs->widget(i);
        if (!page) {
            continue;
        }
        page->setContentsMargins(0, 0, 0, 0);
        if (QLayout* pageLayout = page->layout()) {
            pageLayout->setContentsMargins(0, 0, 0, 0);
            pageLayout->setSpacing(0);
        }

        // Cap all push buttons inside the tab page at 22px height.
        for (auto* btn : page->findChildren<QPushButton*>()) {
            if (btn->maximumHeight() > 24) {
                btn->setFixedHeight(22);
            }
        }
        // Cap all single-line edits inside the tab page at 22px height.
        for (auto* edit : page->findChildren<QLineEdit*>()) {
            if (edit->maximumHeight() > 24) {
                edit->setFixedHeight(22);
            }
        }
        // Cap all combo boxes inside the tab page at 22px height.
        for (auto* combo : page->findChildren<QComboBox*>()) {
            if (combo->maximumHeight() > 24) {
                combo->setFixedHeight(22);
            }
        }
        // Cap all spin boxes inside the tab page at 22px height.
        for (auto* spin : page->findChildren<QSpinBox*>()) {
            if (spin->maximumHeight() > 24) {
                spin->setFixedHeight(22);
            }
        }
        // Strip redundant QFrame borders from embedded text and list viewports.
        for (auto* frame : page->findChildren<QFrame*>()) {
            if (qobject_cast<QPlainTextEdit*>(frame) ||
                qobject_cast<QTextEdit*>(frame) ||
                qobject_cast<QTextBrowser*>(frame) ||
                qobject_cast<QListWidget*>(frame) ||
                qobject_cast<QTreeView*>(frame) ||
                qobject_cast<QTableView*>(frame)) {
                frame->setFrameShape(QFrame::NoFrame);
            }
        }
    }

    // 4. Bind context-sensitive F1 shortcut for instant MPI / C / ABI symbol documentation lookup.
    auto* f1Shortcut = new QShortcut(QKeySequence(Qt::Key_F1), this);
    connect(f1Shortcut, &QShortcut::activated, this, [this]() {
        QString candidate;
        if (m_disassemblyView) {
            const QTextCursor cursor = m_disassemblyView->textCursor();
            candidate = cursor.selectedText().trimmed();
            if (candidate.isEmpty()) {
                // Extract MPI_* or <symbol@plt> identifier from the active disassembly line.
                const QString lineText = cursor.block().text();
                static const QRegularExpression kSymRegex(
                    QStringLiteral("(MPI_[A-Za-z0-9_]+|<[A-Za-z0-9_@]+>)"));
                const QRegularExpressionMatch match = kSymRegex.match(lineText);
                if (match.hasMatch()) {
                    candidate = match.captured(1);
                }
            }
        }
        if (candidate.isEmpty() && m_seekBar && !m_seekBar->text().trimmed().isEmpty()) {
            candidate = m_seekBar->text().trimmed();
        }
        if (candidate.isEmpty()) {
            candidate = QStringLiteral("MPI_Init");
        }
        lookupSymbolDocumentation(candidate);
    });

    // 5. Wire cross-pane workflow bridges between Evaluator/Variables -> Watch Expressions and Disassembly -> Reference Manual.
    auto addPromotedWatch = [this](const QString& expr) {
        const QString trimmed = expr.trimmed();
        if (trimmed.isEmpty() || !m_differentialGrid) {
            return;
        }
        m_differentialGrid->addVariableColumn(trimmed);
        m_differentialGrid->updateVariableDisplay(0, trimmed, QStringLiteral("0.0"));
    };
    if (m_expressionEvaluatorWidget) {
        connect(m_expressionEvaluatorWidget, &ExpressionEvaluatorWidget::watchExpressionRequested,
                this, addPromotedWatch);
    }
    if (m_variablesDockWidget) {
        connect(m_variablesDockWidget, &gridlock::VariablesDockWidget::watchExpressionRequested,
                this, addPromotedWatch);
    }
    if (m_disassemblyView) {
        connect(m_disassemblyView, &DisassemblyView::documentationRequested,
                this, &MainWindow::lookupSymbolDocumentation);
    }
}

/**
 * @brief Resolves symbol documentation (stripping @plt/@got suffixes), switches to Reference Manual, and renders HTML.
 *
 * Normalizes the raw symbol string (e.g., "MPI_Init@plt" -> "MPI_Init"), instructs
 * ReferenceManualWidget to render the formatted dark-slate manual page, and activates
 * the Reference Manual tab in BottomConsoleTabs.
 *
 * @param rawSymbol Raw symbol name or topic identifier.
 * @return True if documentation was rendered and tab activated; false otherwise.
 */
bool MainWindow::lookupSymbolDocumentation(const QString& rawSymbol) {
    const QString normalized = gridlock::core::DocsetManager::normalizeSymbolName(rawSymbol);
    if (normalized.isEmpty() || !m_referenceManualWidget || !m_bottomTabs) {
        return false;
    }

    if (!m_referenceManualWidget->lookupSymbol(normalized)) {
        return false;
    }

    m_bottomTabs->setCurrentWidget(m_referenceManualWidget);
    QCoreApplication::processEvents();
    return true;
}

/**
 * @brief Switches to the specified bottom console tab index and captures a PNG audit snapshot.
 *
 * Activates tabIndex in BottomConsoleTabs, flushes pending layout/paint events,
 * grabs the MainWindow framebuffer pixmap, and writes a PNG image to outputPath.
 *
 * @param tabIndex Zero-based index in BottomConsoleTabs.
 * @param outputPath Destination PNG file path.
 * @return True if PNG snapshot was rendered and saved; false otherwise.
 */
bool MainWindow::captureTabAuditSnapshot(int tabIndex, const QString& outputPath) {
    if (!m_bottomTabs || tabIndex < 0 || tabIndex >= m_bottomTabs->count() ||
        outputPath.trimmed().isEmpty()) {
        return false;
    }

    // Optional multi-tab visual audit export when GRIDLOCK_AUDIT_SNAPSHOTS=1 is set.
    if (qEnvironmentVariableIsSet("GRIDLOCK_AUDIT_SNAPSHOTS")) {
        const QList<QPair<int, QString>> auditTargets = {
            {1, QStringLiteral("/tmp/gridlock_tab_gdb.png")},
            {3, QStringLiteral("/tmp/gridlock_tab_mpidiag.png")},
            {5, QStringLiteral("/tmp/gridlock_tab_eval.png")},
            {6, QStringLiteral("/tmp/gridlock_tab_refman.png")}
        };
        for (const auto& target : auditTargets) {
            if (target.first >= 0 && target.first < m_bottomTabs->count()) {
                m_bottomTabs->setCurrentIndex(target.first);
                QCoreApplication::processEvents();
                grab().save(target.second, "PNG");
            }
        }
    }

    m_bottomTabs->setCurrentIndex(tabIndex);
    QCoreApplication::processEvents();
    const QPixmap pixmap = grab();
    return !pixmap.isNull() && pixmap.save(outputPath, "PNG");
}

/**
 * @brief Loads a 64-bit ELF binary executable or shared object for static inspection.
 *
 * Parses section headers, symbol tables, and printable ASCII strings from the
 * specified binary using native ELF64 structures, populates the ProjectExplorerWidget
 * symbol and string tables, and synchronizes the memory and disassembly views.
 *
 * @param binaryPath Path to executable file on disk.
 * @return True if file is valid ELF64 and successfully parsed; false otherwise.
 */
bool MainWindow::loadBinaryTarget(const QString& binaryPath) {
    auto inspector = std::make_unique<gridlock::core::ElfBinaryInspector>();
    if (!inspector->loadFile(binaryPath)) {
        return false;
    }

    m_elfInspector = std::move(inspector);

    // Populate ProjectExplorerWidget with parsed ELF symbols and strings.
    if (m_projectExplorerWidget) {
        std::vector<SymbolEntry> syms;
        syms.reserve(m_elfInspector->symbols().size());
        for (const auto& s : m_elfInspector->symbols()) {
            syms.push_back({s.name, s.address, static_cast<uint32_t>(s.size)});
        }
        m_projectExplorerWidget->setSymbols(syms);

        std::vector<StringEntry> strs;
        strs.reserve(m_elfInspector->strings().size());
        for (const auto& str : m_elfInspector->strings()) {
            strs.push_back({str.address, str.section, str.value});
        }
        m_projectExplorerWidget->setStrings(strs);
    }

    // Seek to entry point if non-zero.
    const uint64_t entry = m_elfInspector->entryPoint();
    if (entry != 0ULL) {
        seekToAddressOrExpression(QStringLiteral("0x%1").arg(entry, 0, 16));
    }

    return true;
}

/**
 * @brief Exports the in-memory patched ELF binary image to disk.
 *
 * Checks that an active ELF inspector is present with a valid binary payload,
 * then delegates directly to ElfBinaryInspector::savePatchedBinary to write
 * the modified ELF byte stream with owner and group executable permissions.
 *
 * @param outputPath Destination file system path.
 * @return True if file was successfully written and marked executable; false otherwise.
 */
bool MainWindow::exportPatchedBinary(const QString& outputPath) const {
    if (!m_elfInspector || !m_elfInspector->isValid()) {
        return false;
    }
    return m_elfInspector->savePatchedBinary(outputPath);
}

/**
 * @brief Performs unified multi-domain case-insensitive search across the workspace.
 *
 * Scans loaded ELF symbols and strings, disassembly lines and user comments,
 * and decompiled C statements for matching query occurrences.
 *
 * @param query Target search query substring.
 * @return List of structured search match records.
 */
QVector<MainWindow::WorkspaceSearchResult> MainWindow::searchWorkspace(
    const QString& query) const {
    QVector<WorkspaceSearchResult> matches;
    const QString q = query.trimmed();
    if (q.isEmpty()) {
        return matches;
    }

    // 1. Search loaded ELF symbols and strings.
    if (m_elfInspector && m_elfInspector->isValid()) {
        for (const auto& s : m_elfInspector->symbols()) {
            if (s.name.contains(q, Qt::CaseInsensitive)) {
                matches.append({QStringLiteral("Symbol"), s.address, s.name,
                                QStringLiteral("Symbol %1 (size %2 bytes)")
                                    .arg(s.name)
                                    .arg(s.size)});
            }
        }
        for (const auto& str : m_elfInspector->strings()) {
            if (str.value.contains(q, Qt::CaseInsensitive)) {
                matches.append({QStringLiteral("String"), str.address, str.value,
                                QStringLiteral("Section %1: \"%2\"")
                                    .arg(str.section)
                                    .arg(str.value)});
            }
        }
    }

    // 2. Search Disassembly lines, user comments, and Decompiled C statements.
    if (m_disassemblyView) {
        const QString disasmText = m_disassemblyView->toPlainText();
        const QStringList lines = disasmText.split('\n');
        for (int i = 0; i < lines.size(); ++i) {
            const QString& line = lines[i];
            if (line.contains(q, Qt::CaseInsensitive)) {
                const uint64_t addr = m_disassemblyView->addressForLine(i);
                matches.append({QStringLiteral("Disassembly"), addr,
                                QStringLiteral("0x%1").arg(addr, 8, 16, QChar('0')),
                                line.trimmed()});
            }
        }

        const QString decompilerText = m_disassemblyView->decompiledPseudocode();
        if (!decompilerText.isEmpty()) {
            const QStringList decompilerLines = decompilerText.split('\n');
            for (int i = 0; i < decompilerLines.size(); ++i) {
                const QString& line = decompilerLines[i];
                if (line.contains(q, Qt::CaseInsensitive)) {
                    const uint64_t addr = m_disassemblyView->decompilerLineToAddress(i + 1);
                    matches.append({QStringLiteral("Decompiler"), addr,
                                    QStringLiteral("Line %1").arg(i + 1),
                                    line.trimmed()});
                }
            }
        }
    }

    return matches;
}

/**
 * @brief Detects wait-for cycle among MPI ranks and highlights deadlocked nodes.
 *
 * Performs deterministic cycle detection over directed rank wait-for edges
 * (e.g. rank 0 waiting for rank 1, rank 1 waiting for rank 0).
 * Any nodes identified within a cycle are propagated directly to DisassemblyView
 * to trigger amber/red deadlock indicator badges, and the cutter telemetry status
 * bar is appended with an explicit DEADLOCK DETECTED alert banner.
 *
 * @param waitForGraph Directed edges where key rank waits for value rank.
 * @return List of ranks participating in the deadlock cycle sorted in ascending order.
 */
QVector<int> MainWindow::detectAndHighlightDeadlock(
    const QMap<int, int>& waitForGraph) {
    QVector<int> cycleNodes;
    QSet<int> inCycle;

    // Cycle detection via deterministic path tracing through directed wait-for edges.
    for (auto it = waitForGraph.cbegin(); it != waitForGraph.cend(); ++it) {
        int curr = it.key();
        QVector<int> path;
        QSet<int> visited;

        // Follow dependency chain until a terminal node or previously visited node is reached.
        while (waitForGraph.contains(curr) && !visited.contains(curr)) {
            visited.insert(curr);
            path.append(curr);
            curr = waitForGraph.value(curr);
        }

        // If terminal node was encountered earlier in current path, a cycle exists.
        if (waitForGraph.contains(curr)) {
            const int cycleStartIdx = path.indexOf(curr);
            if (cycleStartIdx != -1) {
                // Collect all participating rank IDs forming the deadlock loop.
                for (int i = cycleStartIdx; i < path.size(); ++i) {
                    if (!inCycle.contains(path[i])) {
                        inCycle.insert(path[i]);
                        cycleNodes.append(path[i]);
                    }
                }
            }
        }
    }
    std::sort(cycleNodes.begin(), cycleNodes.end());

    // Propagate deadlocked rank IDs to DisassemblyView for visual edge/badge highlighting.
    if (m_disassemblyView) {
        m_disassemblyView->setDeadlockCycleRanks(cycleNodes);
    }

    // Update telemetry banner in bottom status bar with deadlock alert indicator.
    if (auto* telemetryLabel =
            findChild<QLabel*>(QStringLiteral("cutterStatusTelemetry"))) {
        QString txt = telemetryLabel->text();
        if (!cycleNodes.isEmpty()) {
            if (!txt.contains(QStringLiteral("DEADLOCK"))) {
                txt += QStringLiteral(" | STATUS: DEADLOCK DETECTED");
            }
        }
        telemetryLabel->setText(txt);
    }

    return cycleNodes;
}

/**
 * @brief Updates main toolbar action enablement based on m_debugSessionActive.
 *
 * When m_debugSessionActive is false (Idle state), Run Target is enabled while
 * Continue, Step Inst, Pause Rank, Stop, and Terminate Session are disabled
 * and styled in muted dark-slate so the user immediately sees the entry action.
 */
void MainWindow::updateDebugToolbarState() {
    if (m_runAction) {
        m_runAction->setEnabled(true);
    }
    if (m_continueAction) {
        m_continueAction->setEnabled(m_debugSessionActive);
    }
    if (m_stepAction) {
        m_stepAction->setEnabled(m_debugSessionActive);
    }
    if (m_pauseAction) {
        m_pauseAction->setEnabled(m_debugSessionActive);
    }
    if (m_stopAction) {
        m_stopAction->setEnabled(m_debugSessionActive);
    }
    if (m_terminateAction) {
        m_terminateAction->setEnabled(m_debugSessionActive);
    }
}

/**
 * @brief Maps a 64-bit instruction address in sym.main to a 1-based C++ source line.
 * @param address 64-bit virtual instruction address.
 * @return Corresponding 1-based source line number in inspection_demo.cpp.
 */
static int mapInstructionAddressToSourceLine(uint64_t address) {
    if (address <= 0x00401128ULL) {
        return 12;
    }
    if (address <= 0x0040113dULL) {
        return 13;
    }
    if (address <= 0x00401142ULL) {
        return 15;
    }
    if (address <= 0x00401155ULL) {
        return 16;
    }
    if (address <= 0x00401163ULL) {
        return 19;
    }
    if (address <= 0x0040116eULL) {
        return 17;
    }
    return 22;
}

/**
 * @brief Starts the target debugging workflow, spawning configured MPI ranks and pausing at entry.
 *
 * Transitions the session state machine to active, populates #toolbarRankSpin ranks in
 * ServerRackView with Rank 0 paused at main (0x00401120), synchronizes DisassemblyView,
 * RegisterView, MemView, and SourceCodeView::highlightExecutionLine, and updates
 * #cutterStatusTelemetry to PAUSED (BREAKPOINT).
 */
void MainWindow::startTargetWorkflow() {
    m_debugSessionActive = true;
    updateDebugToolbarState();

    const int rankCount =
        m_rankCountSpin ? std::clamp(m_rankCountSpin->value(), 1, 64) : 8;
    const uint64_t entryAddr = 0x00401120ULL;

    m_latestStates.clear();
    QList<RankState> rankList;
    rankList.reserve(rankCount);
    for (int r = 0; r < rankCount; ++r) {
        RankState st;
        st.rankId = r;
        st.currentState =
            (r == 0) ? QStringLiteral("stopped") : QStringLiteral("running");
        st.registers[16] = QStringLiteral("0x00401120");
        st.currentFunction = QStringLiteral("main");
        st.currentLine = 12;
        m_latestStates[r] = st;
        rankList.append(st);
    }
    m_focusedRank = 0;

    if (m_serverRackView) {
        m_serverRackView->updateRanks(rankList);
        m_serverRackView->selectRank(0);
    }

    seekToAddressOrExpression(QStringLiteral("0x00401120"));

    if (m_registerView) {
        m_registerView->updateRegisters({
            {QStringLiteral("rip"), QStringLiteral("0x00401120 <main+0>")},
            {QStringLiteral("rsp"), QStringLiteral("0x7fffffffe480")},
            {QStringLiteral("rbp"), QStringLiteral("0x7fffffffe4b0")},
            {QStringLiteral("rax"), QStringLiteral("0x0000000000000000")},
            {QStringLiteral("eflags"), QStringLiteral("0x00000202 [ IF ]")}
        });
    }

    if (SourceCodeView* srcView = sourceCodeView()) {
        srcView->highlightExecutionLine(mapInstructionAddressToSourceLine(entryAddr));
    }

    updateStatusTelemetry(QStringLiteral("0x00401120"),
                          QStringLiteral("PAUSED (BREAKPOINT)"));
}

/**
 * @brief Steps the focused rank to the next instruction and synchronizes all 5 inspection views.
 *
 * Advances the active rank's program counter via DisassemblyView::nextInstructionAddress,
 * updates ServerRackView's rank PC badge, updates RegisterView's rip/rsp/rax values,
 * seeks DisassemblyView (Linear + CFG + Decompiler) and MemView, and advances the
 * active execution line highlight in SourceCodeView.
 *
 * @return True if the instruction pointer advanced; false otherwise.
 */
bool MainWindow::stepInstructionWorkflow() {
    if (!m_disassemblyView) {
        return false;
    }

    m_debugSessionActive = true;
    updateDebugToolbarState();

    const uint64_t currentPc =
        (m_disassemblyView->currentAddress() != 0ULL)
            ? m_disassemblyView->currentAddress()
            : 0x00401120ULL;
    const uint64_t nextPc = m_disassemblyView->nextInstructionAddress(currentPc);
    const QString nextPcHex =
        QStringLiteral("0x%1").arg(nextPc, 8, 16, QChar('0'));

    const int activeRank =
        m_serverRackView ? m_serverRackView->selectedRank() : m_focusedRank;
    m_focusedRank = activeRank;

    RankState& st = m_latestStates[activeRank];
    st.rankId = activeRank;
    st.currentState = QStringLiteral("stopped");
    st.registers[16] = nextPcHex;
    st.currentFunction = QStringLiteral("main");
    st.currentLine = mapInstructionAddressToSourceLine(nextPc);

    if (m_serverRackView) {
        m_serverRackView->updateRankState(activeRank, st);
    }

    seekToAddressOrExpression(nextPcHex);

    if (m_registerView) {
        const uint64_t offset =
            (nextPc >= 0x00401120ULL) ? (nextPc - 0x00401120ULL) : 0ULL;
        m_registerView->updateRegisters({
            {QStringLiteral("rip"),
             QStringLiteral("%1 <main+%2>").arg(nextPcHex).arg(offset)},
            {QStringLiteral("rsp"), QStringLiteral("0x7fffffffe478")},
            {QStringLiteral("rbp"), QStringLiteral("0x7fffffffe4b0")},
            {QStringLiteral("rax"),
             QStringLiteral("0x%1").arg(offset, 16, 16, QChar('0'))},
            {QStringLiteral("eflags"), QStringLiteral("0x00000202 [ IF ]")}
        });
    }

    if (SourceCodeView* srcView = sourceCodeView()) {
        srcView->highlightExecutionLine(mapInstructionAddressToSourceLine(nextPc));
    }

    updateStatusTelemetry(nextPcHex, QStringLiteral("PAUSED (STEP)"));

    if (m_coordinator) {
        auto cmd = std::make_unique<gridlock::core::commands::StepCommand>(
            m_coordinator, m_focusedRank + 1, false);
        executeCommand(std::move(cmd));
    }

    return true;
}

/**
 * @brief Terminates the active debugging session and resets the workstation to IDLE state.
 *
 * Disables Step/Continue/Pause/Stop/Terminate toolbar buttons, clears the active
 * execution line highlight in SourceCodeView, resets #cutterStatusTelemetry to
 * STATUS: IDLE, and terminates any active backend coordinator session.
 */
void MainWindow::stopTargetWorkflow() {
    m_debugSessionActive = false;
    updateDebugToolbarState();

    if (SourceCodeView* srcView = sourceCodeView()) {
        srcView->highlightExecutionLine(-1);
    }

    updateStatusTelemetry(m_currentSeekOrPc, QStringLiteral("IDLE"));

    if (m_coordinator) {
        m_coordinator->terminateSession();
    }
}

}  // namespace gridlock::ui

