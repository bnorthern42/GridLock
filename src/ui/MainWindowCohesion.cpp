/**
 * @file MainWindowCohesion.cpp
 * @brief Workstation UI cohesion harmonizer, F1 symbol documentation lookup, tab snapshotter,
 *        ELF binary target loader, workspace search engine, and deadlock cycle propagator.
 *
 * Implements the two-tier workstation layout normalization across all bottom console
 * tabs, context-sensitive F1 documentation lookup into the Reference Manual tab,
 * offscreen tab PNG snapshot rendering, binary inspection orchestration, and edit actions.
 */

#include "MainWindow.hpp"

#include <algorithm>
#include <memory>

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QFileInfo>
#include <QFrame>
#include <QKeySequence>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
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
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

#include "../core/hpc/analysis/DeadlockAnalyzer.hpp"
#include "../core/hpc/IBackendCoordinator.hpp"
#include "../core/managers/DocsetManager.hpp"
#include "../core/managers/SpackManager.hpp"
#include "../core/models/VariableTreeModel.hpp"
#include "../core/utils/ElfBinaryInspector.hpp"
#include "EditorTabManager.hpp"
#include "GdbConsoleWidget.hpp"
#include "dialogs/plotter/ArrayPlotDialog.hpp"
#include "dialogs/debugger/CheckpointDialog.hpp"
#include "dialogs/graph/PointerGraphDialog.hpp"
#include "dialogs/debugger/SignalCatchpointDialog.hpp"
#include "docks/CallStackDockWidget.hpp"
#include "docks/VariablesDockWidget.hpp"
#include "utils/FileDialogHelper.hpp"
#include "views/DisassemblyView.hpp"
#include "views/MemView.hpp"
#include "views/RegisterView.hpp"
#include "views/SourceCodeView.hpp"
#include "widgets/DifferentialGrid.hpp"
#include "widgets/ExpressionEvaluatorWidget.hpp"
#include "widgets/MpiDiagnosticsWidget.hpp"
#include "widgets/MpiNetworkLogWidget.hpp"
#include "widgets/ProjectExplorerWidget.hpp"
#include "widgets/ReferenceManualWidget.hpp"

namespace gridlock::ui {

/** @brief Harmonizes all bottom console tab pages into the unified two-tier workstation layout. */
void MainWindow::harmonizeBottomConsoleTabs() {
    if (!m_bottomTabs) return;

    m_bottomTabs->setStyleSheet(QStringLiteral(
        "QTabWidget::pane { border: 0px; margin: 0px; padding: 0px; background: #16181d; }"
        "QTabBar { background: #181a1f; border-bottom: 1px solid #2c313a; }"
        "QTabBar::tab { background: #181a1f; color: #9da5b4; padding: 3px 10px; min-height: 18px; max-height: 22px; border-right: 1px solid #282c34; border-bottom: 1px solid #2c313a; font-size: 11px; font-weight: 600; }"
        "QTabBar::tab:selected { background: #21252b; color: #ffffff; border-bottom: 2px solid #528bff; }"
        "QTabBar::tab:hover:!selected { background: #1e222a; color: #d7dae0; }"
        "QLineEdit, QComboBox, QSpinBox { background: #141619; color: #d7dae0; border: 1px solid #323844; border-radius: 2px; padding: 0px 6px; min-height: 20px; max-height: 22px; font-size: 11px; }"
        "QLineEdit:focus, QComboBox:focus, QSpinBox:focus { border: 1px solid #528bff; background: #1b1e24; }"
        "QPushButton { background: #252932; color: #d7dae0; border: 1px solid #323844; border-radius: 2px; padding: 1px 8px; min-height: 20px; max-height: 22px; font-size: 10px; font-weight: 600; }"
        "QPushButton:hover { background: #2c323d; border: 1px solid #528bff; color: #ffffff; }"
        "QCheckBox { color: #abb2bf; font-size: 11px; font-weight: 600; spacing: 5px; }"
        "QPlainTextEdit, QTextEdit, QTextBrowser, QListWidget { background: #16181d; color: #d7dae0; border: none; font-family: 'JetBrains Mono', monospace; font-size: 11px; }"));

    // 1. Harmonize GdbConsoleWidget outer layout and control bar metrics.
    if (m_gdbConsoleWidget) {
        m_gdbConsoleWidget->setAttribute(Qt::WA_StyledBackground, true);
        m_gdbConsoleWidget->setContentsMargins(0, 0, 0, 0);
        if (QLayout* l = m_gdbConsoleWidget->layout()) { l->setContentsMargins(0, 0, 0, 0); l->setSpacing(0); }
        m_gdbConsoleWidget->setStyleSheet(QStringLiteral(
            "#GdbConsoleWidget { background: #16181d; } #gdbConsoleHeaderBar, #gdbConsolePromptBar { background: #21252b; border-bottom: 1px solid #2c313a; }"
            "#GdbConsoleWidget QPlainTextEdit { background: #16181d; color: #d7dae0; border: none; font-family: 'JetBrains Mono', monospace; font-size: 11px; padding: 4px 6px; }"));
    }

    // 2. Harmonize SpackManager (HPC Console tab) outer layout and control bar metrics.
    if (m_spackManager) {
        m_spackManager->setAttribute(Qt::WA_StyledBackground, true);
        m_spackManager->setContentsMargins(0, 0, 0, 0);
        if (QLayout* l = m_spackManager->layout()) {
            l->setContentsMargins(0, 0, 0, 0); l->setSpacing(0);
            if (auto* vbox = qobject_cast<QVBoxLayout*>(l)) {
                for (int i = 0; i < vbox->count(); ++i) {
                    if (QLayout* sub = vbox->itemAt(i)->layout()) { sub->setContentsMargins(6, 2, 6, 2); sub->setSpacing(6); }
                }
            }
        }
        m_spackManager->setStyleSheet(QStringLiteral(
            "#HpcConsoleDock { background: #21252b; } #HpcConsoleDock QLabel { color: #abb2bf; font-size: 10px; font-weight: 700; padding: 0px 4px; }"
            "#HpcConsoleDock QTableWidget, #HpcConsoleDock QTextBrowser, #HpcConsoleDock QPlainTextEdit { background: #16181d; alternate-background-color: #1b1e24; color: #d7dae0; border: none; border-top: 1px solid #2c313a; border-bottom: 1px solid #2c313a; font-family: 'JetBrains Mono', monospace; font-size: 11px; padding: 2px 4px; }"));
    }

    // 3. Enforce zero-gutter outer margins, compact <= 22px controls, and borderless frames on all tabs.
    for (int i = 0; i < m_bottomTabs->count(); ++i) {
        QWidget* page = m_bottomTabs->widget(i);
        if (!page) continue;
        page->setContentsMargins(0, 0, 0, 0);
        if (QLayout* pl = page->layout()) { pl->setContentsMargins(0, 0, 0, 0); pl->setSpacing(0); }
        for (auto* b : page->findChildren<QPushButton*>()) if (b->maximumHeight() > 24) b->setFixedHeight(22);
        for (auto* e : page->findChildren<QLineEdit*>()) if (e->maximumHeight() > 24) e->setFixedHeight(22);
        for (auto* c : page->findChildren<QComboBox*>()) if (c->maximumHeight() > 24) c->setFixedHeight(22);
        for (auto* s : page->findChildren<QSpinBox*>()) if (s->maximumHeight() > 24) s->setFixedHeight(22);
        for (auto* f : page->findChildren<QFrame*>()) {
            if (qobject_cast<QPlainTextEdit*>(f) || qobject_cast<QTextEdit*>(f) ||
                qobject_cast<QTextBrowser*>(f) || qobject_cast<QListWidget*>(f) ||
                qobject_cast<QTreeView*>(f) || qobject_cast<QTableView*>(f)) f->setFrameShape(QFrame::NoFrame);
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
                static const QRegularExpression kSymRegex(QStringLiteral("(MPI_[A-Za-z0-9_]+|<[A-Za-z0-9_@]+>)"));
                const QRegularExpressionMatch match = kSymRegex.match(cursor.block().text());
                if (match.hasMatch()) candidate = match.captured(1);
            }
        }
        if (candidate.isEmpty() && m_seekBar && !m_seekBar->text().trimmed().isEmpty()) candidate = m_seekBar->text().trimmed();
        if (candidate.isEmpty()) candidate = QStringLiteral("MPI_Init");
        lookupSymbolDocumentation(candidate);
    });

    // 5. Wire cross-pane workflow bridges between Evaluator/Variables -> Watch Expressions and Disassembly -> RefMan.
    auto addPromotedWatch = [this](const QString& expr) {
        const QString trimmed = expr.trimmed();
        if (trimmed.isEmpty() || !m_differentialGrid) return;
        m_differentialGrid->addVariableColumn(trimmed);
        if (m_coordinator) m_coordinator->registerWatchVariable(trimmed);
    };
    if (m_expressionEvaluatorWidget) connect(m_expressionEvaluatorWidget, &ExpressionEvaluatorWidget::watchExpressionRequested, this, addPromotedWatch);
    if (m_variablesDockWidget) connect(m_variablesDockWidget, &gridlock::VariablesDockWidget::watchExpressionRequested, this, addPromotedWatch);
    if (m_disassemblyView) {
        connect(m_disassemblyView, &DisassemblyView::documentationRequested, this, &MainWindow::lookupSymbolDocumentation);
        connect(m_disassemblyView, &DisassemblyView::seekInMemoryViewRequested, this, [this](uint64_t addr) {
            selectBottomToolGroup(QStringLiteral("Debug"));
            if (m_bottomTabs && m_memView) m_bottomTabs->setCurrentWidget(m_memView);
            seekToAddressOrExpression(QStringLiteral("0x%1").arg(addr, 8, 16, QLatin1Char('0')));
        });
    }

    // 6. Wire bidirectional SourceCodeView::sourceLineSelected -> DisassemblyView seek choreography.
    auto bindActiveSourceView = [this]() {
        if (SourceCodeView* srcView = sourceCodeView()) {
            if (!srcView->property("gridlockSourceSyncBound").toBool()) {
                srcView->setProperty("gridlockSourceSyncBound", true);
                connect(srcView, &SourceCodeView::sourceLineSelected, this, [this](int lineOneBased) {
                    const uint64_t addr = sourceLineToAddress(lineOneBased);
                    const bool keepLinear = m_disassemblyView && !m_disassemblyView->isGraphMode() &&
                                            !m_disassemblyView->isDecompilerMode() &&
                                            !m_disassemblyView->isSplitGraphDecompilerMode() &&
                                            !m_forceDecompilerFallback && m_workspacePerspective != QStringLiteral("Binary");
                    seekToAddressOrExpression(QStringLiteral("0x%1").arg(addr, 8, 16, QChar('0')));
                    if (keepLinear && m_disassemblyView) {
                        m_showingDecompilerFallback = false;
                        m_disassemblyView->setSplitGraphDecompilerMode(false);
                        m_disassemblyView->setGraphMode(false);
                    }
                });
            }
        }
    };
    bindActiveSourceView();
    if (m_editorTabManager) {
        connect(m_editorTabManager, &EditorTabManager::currentChanged, this, [this, bindActiveSourceView](int) {
            bindActiveSourceView();
            syncDisassemblyDecompilerVisibility(false);
        });
    }

    // 7. Mount one-click Workspace Perspective Switcher (SOURCE | DEBUG | BINARY | MPI) on MainToolbar.
    if (auto* mainToolbar = findChild<QToolBar*>(QStringLiteral("MainToolbar"))) {
        const QString pStyle = QStringLiteral("QPushButton { background: #1e222a; color: #9da5b4; border: 1px solid #2c313a; border-radius: 2px; padding: 0px 3px; margin: 0px; min-width: 0px; font-size: 10px; font-weight: 700; min-height: 20px; max-height: 20px; } QPushButton:hover { background: #282c34; color: #fff; border-color: #528bff; } QPushButton:checked { background: #2c313c; color: #61afef; border: 1px solid #528bff; }");
        auto makePerspBtn = [&](const QString& label, const QString& obj, const QString& persp, const QString& tip, bool chk) {
            auto* b = new QPushButton(label, mainToolbar);
            b->setObjectName(obj); b->setCheckable(true); b->setChecked(chk); b->setToolTip(tip); b->setStyleSheet(pStyle);
            connect(b, &QPushButton::clicked, this, [this, persp]() { setWorkspacePerspective(persp); });
            mainToolbar->addWidget(b); return b;
        };
        mainToolbar->addSeparator();
        m_perspSourceBtn = makePerspBtn(QStringLiteral("SOURCE"), QStringLiteral("perspectiveSourceBtn"), QStringLiteral("Source"), QStringLiteral("Source Perspective (Alt+1) — full-width editor without debug clutter"), true);
        m_perspDebugBtn = makePerspBtn(QStringLiteral("DEBUG"), QStringLiteral("perspectiveDebugBtn"), QStringLiteral("Debug"), QStringLiteral("Debug Perspective (Alt+2) — source editor + Variables & Call Stack"), false);
        m_perspBinaryBtn = makePerspBtn(QStringLiteral("BINARY"), QStringLiteral("perspectiveBinaryBtn"), QStringLiteral("Binary"), QStringLiteral("Binary Perspective (Alt+3) — CFG disassembly & Decompiler"), false);
        m_perspMpiBtn = makePerspBtn(QStringLiteral("MPI"), QStringLiteral("perspectiveMpiBtn"), QStringLiteral("MPI"), QStringLiteral("MPI Perspective (Alt+8) — topology & collective diagnostics"), false);
    }

    // 8. Bind Alt+1/2/3/8 perspective shortcuts and Ctrl+M bottom console maximize toggle.
    connect(new QShortcut(QKeySequence(Qt::ALT | Qt::Key_1), this), &QShortcut::activated, this, [this]() { setWorkspacePerspective(QStringLiteral("Source")); });
    connect(new QShortcut(QKeySequence(Qt::ALT | Qt::Key_2), this), &QShortcut::activated, this, [this]() { setWorkspacePerspective(QStringLiteral("Debug")); });
    connect(new QShortcut(QKeySequence(Qt::ALT | Qt::Key_3), this), &QShortcut::activated, this, [this]() { setWorkspacePerspective(QStringLiteral("Binary")); });
    connect(new QShortcut(QKeySequence(Qt::ALT | Qt::Key_8), this), &QShortcut::activated, this, [this]() { setWorkspacePerspective(QStringLiteral("MPI")); });
    connect(new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_M), this), &QShortcut::activated, this, &MainWindow::toggleMaximizeBottomConsole);

    // 9. Populate the 36px left vertical tool-window icon rail (#bottomToolWindowRail) with 4 linked tool groups.
    if (m_bottomToolRail && !m_bottomToolRail->layout()) {
        m_bottomToolRail->setStyleSheet(QStringLiteral(
            "#bottomToolWindowRail { background: #181a1f; border-right: 1px solid #252932; }"
            "#bottomToolWindowRail QToolButton { background: transparent; color: #9da5b4; border: 1px solid transparent; border-radius: 4px; min-width: 28px; max-width: 28px; min-height: 28px; max-height: 28px; padding: 2px; margin: 0px; }"
            "#bottomToolWindowRail QToolButton:hover { background: #21252b; color: #ffffff; border: 1px solid #323844; }"
            "#bottomToolWindowRail QToolButton:checked { background: #21252b; color: #61afef; border: 1px solid #528bff; border-left: 2px solid #528bff; }"));
        auto* railLayout = new QVBoxLayout(m_bottomToolRail);
        railLayout->setContentsMargins(3, 4, 3, 4); railLayout->setSpacing(4);

        auto makeRailIcon = [](int kind) -> QIcon {
            QPixmap px(16, 16); px.fill(Qt::transparent);
            QPainter p(&px); p.setRenderHint(QPainter::Antialiasing, true);
            QPen pen(QColor(0xd7, 0xda, 0xe0), 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            if (kind == 0) {
                pen.setColor(QColor(0x61, 0xaf, 0xef)); p.setPen(pen); p.drawEllipse(QRectF(3.0, 3.0, 10.0, 10.0));
                p.drawLine(QPointF(8.0, 1.0), QPointF(8.0, 3.5)); p.drawLine(QPointF(8.0, 12.5), QPointF(8.0, 15.0));
                p.setBrush(QColor(0x61, 0xaf, 0xef)); p.drawEllipse(QPointF(8.0, 8.0), 1.5, 1.5);
            } else if (kind == 1) {
                pen.setColor(QColor(0x4e, 0xc9, 0xb0)); p.setPen(pen);
                p.drawLine(QPointF(8.0, 4.0), QPointF(3.5, 12.0)); p.drawLine(QPointF(8.0, 4.0), QPointF(12.5, 12.0)); p.drawLine(QPointF(3.5, 12.0), QPointF(12.5, 12.0));
                p.setBrush(QColor(0x18, 0x1a, 0x1f));
                p.drawEllipse(QPointF(8.0, 4.0), 2.2, 2.2); p.drawEllipse(QPointF(3.5, 12.0), 2.2, 2.2); p.drawEllipse(QPointF(12.5, 12.0), 2.2, 2.2);
            } else if (kind == 2) {
                pen.setColor(QColor(0xe5, 0xc0, 0x7b)); p.setPen(pen);
                p.drawLine(QPointF(3.0, 4.5), QPointF(7.5, 8.0)); p.drawLine(QPointF(7.5, 8.0), QPointF(3.0, 11.5));
                p.drawLine(QPointF(9.0, 11.5), QPointF(13.5, 11.5));
            } else {
                pen.setColor(QColor(0xc6, 0x78, 0xdd)); p.setPen(pen);
                p.drawRoundedRect(QRectF(2.5, 3.0, 11.0, 10.0), 1.5, 1.5); p.drawLine(QPointF(8.0, 3.2), QPointF(8.0, 12.8));
            }
            return QIcon(px);
        };

        struct RailSpec { const char* id; const char* tip; Qt::Key key; int iconKind; };
        static const RailSpec kSpecs[] = {
            {"Debug", "Debug Tools: GDB Console, Memory View & Evaluator (Alt+4)", Qt::Key_4, 0},
            {"MPI", "MPI Telemetry: Deadlock Analyzer, Network Log & Package Manager (Alt+5)", Qt::Key_5, 1},
            {"Terminal", "Interactive Shell Terminal (Alt+6)", Qt::Key_6, 2},
            {"Docs", "Offline Reference Manual (Alt+7)", Qt::Key_7, 3},
        };
        for (const auto& sp : kSpecs) {
            const QString groupId = QString::fromLatin1(sp.id);
            auto* btn = new QToolButton(m_bottomToolRail);
            btn->setObjectName(QStringLiteral("bottomRailBtn_") + groupId);
            btn->setCheckable(true); btn->setIcon(makeRailIcon(sp.iconKind)); btn->setIconSize(QSize(16, 16));
            btn->setToolTip(QString::fromLatin1(sp.tip));
            connect(btn, &QToolButton::clicked, this, [this, groupId]() { selectBottomToolGroup(groupId); });
            connect(new QShortcut(QKeySequence(Qt::ALT | sp.key), this), &QShortcut::activated, this, [this, groupId]() { selectBottomToolGroup(groupId); });
            m_bottomRailButtons.insert(groupId, btn);
            railLayout->addWidget(btn, 0, Qt::AlignHCenter);
        }
        railLayout->addStretch(1);
    }

    connect(m_bottomTabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (m_syncingBottomToolGroup || index < 0) return;
        const QString grp = bottomToolGroupForTabIndex(index);
        if (!grp.isEmpty() && grp != m_activeBottomToolGroup) selectBottomToolGroup(grp);
    });
    selectBottomToolGroup(QStringLiteral("Debug"));
}

/** @brief Resolves symbol documentation (stripping @plt/@got suffixes) and activates Reference Manual. */
bool MainWindow::lookupSymbolDocumentation(const QString& rawSymbol) {
    const QString normalized = gridlock::core::DocsetManager::normalizeSymbolName(rawSymbol);
    if (normalized.isEmpty() || !m_referenceManualWidget || !m_bottomTabs) return false;
    if (!m_referenceManualWidget->lookupSymbol(normalized)) return false;

    selectBottomToolGroup(QStringLiteral("Docs"));
    m_bottomTabs->setCurrentWidget(m_referenceManualWidget);
    QCoreApplication::processEvents();
    return true;
}

/** @brief Switches to the specified bottom console tab index and captures a PNG audit snapshot. */
bool MainWindow::captureTabAuditSnapshot(int tabIndex, const QString& outputPath) {
    if (!m_bottomTabs || tabIndex < 0 || tabIndex >= m_bottomTabs->count() || outputPath.trimmed().isEmpty()) {
        return false;
    }
    if (qEnvironmentVariableIsSet("GRIDLOCK_AUDIT_SNAPSHOTS")) {
        const QList<QPair<int, QString>> targets = {
            {1, QStringLiteral("/tmp/gridlock_tab_gdb.png")}, {3, QStringLiteral("/tmp/gridlock_tab_mpidiag.png")},
            {5, QStringLiteral("/tmp/gridlock_tab_eval.png")}, {6, QStringLiteral("/tmp/gridlock_tab_refman.png")}};
        for (const auto& tgt : targets) {
            if (tgt.first >= 0 && tgt.first < m_bottomTabs->count()) {
                m_bottomTabs->setCurrentIndex(tgt.first); QCoreApplication::processEvents(); grab().save(tgt.second, "PNG");
            }
        }
    }
    m_bottomTabs->setCurrentIndex(tabIndex);
    QCoreApplication::processEvents();
    const QPixmap pixmap = grab();
    return !pixmap.isNull() && pixmap.save(outputPath, "PNG");
}

/** @brief Loads a 64-bit ELF binary executable or shared object for static inspection. */
bool MainWindow::loadBinaryTarget(const QString& binaryPath) {
    auto inspector = std::make_unique<gridlock::core::ElfBinaryInspector>();
    if (!inspector->loadFile(binaryPath)) return false;

    const QFileInfo binInfo(binaryPath);
    QString sourceFilter;
    if (QFileInfo::exists(binaryPath + QStringLiteral(".cpp"))) sourceFilter = binInfo.fileName() + QStringLiteral(".cpp");
    else if (QFileInfo::exists(binaryPath + QStringLiteral(".c"))) sourceFilter = binInfo.fileName() + QStringLiteral(".c");
    else if (!m_currentFile.isEmpty() && QFileInfo(m_currentFile).completeBaseName() == binInfo.fileName()) {
        sourceFilter = QFileInfo(m_currentFile).fileName();
    }
    inspector->setSourceFileFilter(sourceFilter);
    m_elfInspector = std::move(inspector);
    m_loadedBinaryPath = binInfo.absoluteFilePath();

    uint64_t mainAddr = 0ULL;
    QString mainSymName;
    if (m_projectExplorerWidget) {
        std::vector<SymbolEntry> syms; syms.reserve(m_elfInspector->symbols().size());
        for (const auto& s : m_elfInspector->symbols()) syms.push_back({s.name, s.address, static_cast<uint32_t>(s.size)});
        m_projectExplorerWidget->setSymbols(syms);
        std::vector<StringEntry> strs; strs.reserve(m_elfInspector->strings().size());
        for (const auto& str : m_elfInspector->strings()) strs.push_back({str.address, str.section, str.value});
        m_projectExplorerWidget->setStrings(strs);
    }
    for (const auto& s : m_elfInspector->symbols()) {
        if (s.isFunction && (s.name == QStringLiteral("main") || s.name == QStringLiteral("sym.main"))) { mainAddr = s.address; mainSymName = s.name; break; }
    }
    if (m_memView && !m_debugSessionActive) m_memView->clear();
    if (mainAddr != 0ULL && !sourceFilter.isEmpty()) {
        const QString disasm = m_elfInspector->disassembleSymbol(mainSymName, sourceFilter, &m_dwarfLineToAddress, &m_dwarfAddressToLine);
        if (m_disassemblyView && !disasm.isEmpty()) m_disassemblyView->setPlainText(disasm);
        const bool keepLinear = m_disassemblyView && !m_disassemblyView->isGraphMode() &&
                                !m_disassemblyView->isDecompilerMode() && !m_disassemblyView->isSplitGraphDecompilerMode() &&
                                !m_forceDecompilerFallback && m_workspacePerspective != QStringLiteral("Binary");
        seekToAddressOrExpression(QStringLiteral("0x%1").arg(mainAddr, 8, 16, QChar('0')));
        if (keepLinear && m_disassemblyView) { m_showingDecompilerFallback = false; m_disassemblyView->setSplitGraphDecompilerMode(false); m_disassemblyView->setGraphMode(false); }
        rebuildDynamicSourceLineMap();
    } else {
        m_dwarfLineToAddress.clear(); m_dwarfAddressToLine.clear();
        const uint64_t entry = (mainAddr != 0ULL) ? mainAddr : m_elfInspector->entryPoint();
        if (entry != 0ULL) seekToAddressOrExpression(QStringLiteral("0x%1").arg(entry, 0, 16));
    }
    if (m_memView && !m_debugSessionActive && m_elfInspector->isValid()) m_memView->setSourceModeLabel(QStringLiteral("STATIC ELF (READ-ONLY)"));
    const bool showBinNav = (m_workspacePerspective == QStringLiteral("Binary")) ||
                            (m_disassemblyView && !m_disassemblyView->isHidden());
    if (auto* nt = findChild<QToolBar*>(QStringLiteral("AddressNavToolbar"))) nt->setVisible(showBinNav);
    if (auto* rf = DynamicAddressRibbonFilter::fromNavBar(m_addressNavBar)) rf->updateFromBinaryAndSession(m_elfInspector.get(), m_debugSessionActive);
    syncDisassemblyDecompilerVisibility(true);
    return true;
}

/** @brief Exports the in-memory patched ELF binary image to disk. */
bool MainWindow::exportPatchedBinary(const QString& outputPath) const {
    return (m_elfInspector && m_elfInspector->isValid()) ? m_elfInspector->savePatchedBinary(outputPath) : false;
}

/** @brief Performs unified multi-domain case-insensitive search across the workspace. */
QVector<MainWindow::WorkspaceSearchResult> MainWindow::searchWorkspace(const QString& query) const {
    QVector<WorkspaceSearchResult> matches;
    const QString q = query.trimmed();
    if (q.isEmpty()) return matches;

    if (m_elfInspector && m_elfInspector->isValid()) {
        for (const auto& s : m_elfInspector->symbols()) {
            if (s.name.contains(q, Qt::CaseInsensitive)) {
                matches.append({QStringLiteral("Symbol"), s.address, s.name, QStringLiteral("Symbol %1 (size %2 bytes)").arg(s.name).arg(s.size)});
            }
        }
        for (const auto& str : m_elfInspector->strings()) {
            if (str.value.contains(q, Qt::CaseInsensitive)) {
                matches.append({QStringLiteral("String"), str.address, str.value, QStringLiteral("Section %1: \"%2\"").arg(str.section, str.value)});
            }
        }
    }

    if (m_disassemblyView) {
        const QStringList lines = m_disassemblyView->toPlainText().split('\n');
        for (int i = 0; i < lines.size(); ++i) {
            if (lines[i].contains(q, Qt::CaseInsensitive)) {
                const uint64_t a = m_disassemblyView->addressForLine(i);
                matches.append({QStringLiteral("Disassembly"), a, QStringLiteral("0x%1").arg(a, 8, 16, QChar('0')), lines[i].trimmed()});
            }
        }
        const QString decompilerText = m_disassemblyView->decompiledPseudocode();
        if (!decompilerText.isEmpty()) {
            const QStringList dLines = decompilerText.split('\n');
            for (int i = 0; i < dLines.size(); ++i) {
                if (dLines[i].contains(q, Qt::CaseInsensitive)) {
                    matches.append({QStringLiteral("Decompiler"), m_disassemblyView->decompilerLineToAddress(i + 1), QStringLiteral("Line %1").arg(i + 1), dLines[i].trimmed()});
                }
            }
        }
    }
    return matches;
}

/** @brief Detects wait-for cycle among MPI ranks and highlights deadlocked nodes. */
QVector<int> MainWindow::detectAndHighlightDeadlock(const QMap<int, int>& waitForGraph) {
    QVector<int> cycleNodes;
    QSet<int> inCycle;
    for (auto it = waitForGraph.cbegin(); it != waitForGraph.cend(); ++it) {
        int curr = it.key();
        QVector<int> path;
        QSet<int> visited;
        while (waitForGraph.contains(curr) && !visited.contains(curr)) {
            visited.insert(curr);
            path.append(curr);
            curr = waitForGraph.value(curr);
        }
        if (waitForGraph.contains(curr)) {
            const int cycleStartIdx = path.indexOf(curr);
            for (int i = cycleStartIdx; cycleStartIdx != -1 && i < path.size(); ++i) {
                if (!inCycle.contains(path[i])) {
                    inCycle.insert(path[i]);
                    cycleNodes.append(path[i]);
                }
            }
        }
    }
    std::sort(cycleNodes.begin(), cycleNodes.end());

    if (m_disassemblyView) m_disassemblyView->setDeadlockCycleRanks(cycleNodes);
    if (auto* telemetryLabel = findChild<QLabel*>(QStringLiteral("cutterStatusTelemetry"))) {
        QString txt = telemetryLabel->text();
        if (!cycleNodes.isEmpty() && !txt.contains(QStringLiteral("DEADLOCK"))) {
            txt += QStringLiteral(" | STATUS: DEADLOCK DETECTED");
        }
        telemetryLabel->setText(txt);
    }
    return cycleNodes;
}

/** @brief Dispatches an Edit menu action to the currently focused or active source editor. */
bool MainWindow::triggerEditMenuAction(const QString& actionName) {
    const QString act = actionName.trimmed();
    if (act.isEmpty()) return false;
    if (act.contains(QStringLiteral("Preferences"), Qt::CaseInsensitive)) { openPreferences(); return true; }

    SourceCodeView* srcView = sourceCodeView();
    QPlainTextEdit* plainEdit = qobject_cast<QPlainTextEdit*>(QApplication::focusWidget());
    if (!plainEdit) plainEdit = srcView;

    if (act.contains(QStringLiteral("Undo"), Qt::CaseInsensitive)) { if (!plainEdit) return false; plainEdit->undo(); return true; }
    if (act.contains(QStringLiteral("Redo"), Qt::CaseInsensitive)) { if (!plainEdit) return false; plainEdit->redo(); return true; }
    if (act.contains(QStringLiteral("Cut"), Qt::CaseInsensitive)) { if (!plainEdit) return false; plainEdit->cut(); return true; }
    if (act.contains(QStringLiteral("Copy"), Qt::CaseInsensitive)) { if (!plainEdit) return false; plainEdit->copy(); return true; }
    if (act.contains(QStringLiteral("Paste"), Qt::CaseInsensitive)) { if (!plainEdit) return false; plainEdit->paste(); return true; }
    if (act.contains(QStringLiteral("Select All"), Qt::CaseInsensitive)) { if (!plainEdit) return false; plainEdit->selectAll(); return true; }
    if (act.contains(QStringLiteral("Comment"), Qt::CaseInsensitive)) { if (!srcView) return false; srcView->toggleLineComment(); return true; }
    if (act.contains(QStringLiteral("Go to Line"), Qt::CaseInsensitive)) { if (!srcView) return false; srcView->selectSourceLine(1); return true; }
    if (act.contains(QStringLiteral("Go to Address"), Qt::CaseInsensitive) && m_seekBar) {
        if (auto* sbAct = findChild<QAction*>(QStringLiteral("actionToolbarSeekBar"))) sbAct->setVisible(true);
        if (!m_seekBar->isVisible()) m_seekBar->show();
        m_seekBar->setFocus(Qt::ShortcutFocusReason); m_seekBar->selectAll(); return true;
    }
    if (act.contains(QStringLiteral("Find"), Qt::CaseInsensitive)) {
        QWidget* fw = QApplication::focusWidget();
        if (m_gdbConsoleWidget && fw && (fw == m_gdbConsoleWidget || m_gdbConsoleWidget->isAncestorOf(fw))) { m_gdbConsoleWidget->openFindBar(); return true; }
        for (QWidget* p = fw; p; p = p->parentWidget()) if (auto* fsv = qobject_cast<SourceCodeView*>(p)) { fsv->openFindBar(); return true; }
        if (srcView) { srcView->openFindBar(); return true; }
    }
    return false;
}

/** @brief Clears variable, register, call-stack, MPI diagnostics, network log, and memory docks to idle. */
void clearAllInspectionDocks(VariablesDockWidget* varsDock, RegisterView* regView, CallStackDockWidget* csDock,
                             MpiDiagnosticsWidget* mpiDiag, MpiNetworkLogWidget* netLog, MemView* memView) {
    if (varsDock) if (auto* vm = varsDock->findChild<gridlock::VariableTreeModel*>()) vm->clear();
    if (regView) regView->clearRegisters();
    if (csDock) csDock->clearFrames();
    if (mpiDiag) mpiDiag->clear();
    if (netLog) netLog->clear();
    if (memView) memView->clear();
}

/** @brief Disconnects all child docks/dialogs from the active coordinator and stops live ranks synchronously. */
void MainWindow::shutdownActiveSessionSync() {
    m_isShuttingDown = true; m_isClosing = true;
    if (m_telemetryTimer) m_telemetryTimer->stop();
    if (m_programOutputFlushTimer) m_programOutputFlushTimer->stop();
    if (m_deadlockAnalyzer) {
        QObject::disconnect(m_deadlockAnalyzer, nullptr, this, nullptr);
        if (m_mpiDiagnosticsWidget) QObject::disconnect(m_deadlockAnalyzer, nullptr, m_mpiDiagnosticsWidget, nullptr);
    }
    if (m_coordinator) {
        for (QObject* child : {static_cast<QObject*>(this), static_cast<QObject*>(m_variablesDockWidget), static_cast<QObject*>(m_callStackDockWidget),
                               static_cast<QObject*>(m_disassemblyView), static_cast<QObject*>(m_memView), static_cast<QObject*>(m_registerView),
                               static_cast<QObject*>(m_differentialGrid), static_cast<QObject*>(m_arrayPlotDialog), static_cast<QObject*>(m_pointerGraphDialog),
                               static_cast<QObject*>(m_gdbConsoleWidget), static_cast<QObject*>(m_expressionEvaluatorWidget),
                               static_cast<QObject*>(m_signalCatchpointDialog), static_cast<QObject*>(m_checkpointDialog), static_cast<QObject*>(m_deadlockAnalyzer)}) {
            if (child) { QObject::disconnect(m_coordinator, nullptr, child, nullptr); QObject::disconnect(child, nullptr, m_coordinator, nullptr); }
        }
        if (m_variablesDockWidget) m_variablesDockWidget->setCoordinator(nullptr);
        if (m_arrayPlotDialog) m_arrayPlotDialog->setCoordinator(nullptr);
        if (m_pointerGraphDialog) m_pointerGraphDialog->setCoordinator(nullptr);
        if (m_signalCatchpointDialog) m_signalCatchpointDialog->setCoordinator(nullptr);
        if (m_checkpointDialog) m_checkpointDialog->setCoordinator(nullptr);
    }
    if (m_debugSessionActive || (m_coordinator && m_coordinator->getProcessCount() > 0)) stopTargetWorkflow();
    else if (m_coordinator) m_coordinator->terminateSession();
}

}  // namespace gridlock::ui
