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

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QShortcut>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QTabBar>
#include <QTabWidget>
#include <QTableView>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextEdit>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

#include "../core/commands/DebugCommands.hpp"
#include "../core/hpc/IBackendCoordinator.hpp"
#include "../core/managers/ConfigManager.hpp"
#include "../core/managers/DocsetManager.hpp"
#include "../core/managers/SpackManager.hpp"
#include "../core/managers/ThemeManager.hpp"
#include "../core/utils/ElfBinaryInspector.hpp"
#include "EditorTabManager.hpp"
#include "GdbConsoleWidget.hpp"
#include "dialogs/ConditionalBreakpointDialog.hpp"
#include "dialogs/PreferencesDialog.hpp"
#include "docks/TerminalDockWidget.hpp"
#include "docks/VariablesDockWidget.hpp"
#include "views/DisassemblyView.hpp"
#include "views/MemView.hpp"
#include "views/RegisterView.hpp"
#include "views/ServerRackView.hpp"
#include "views/SourceCodeView.hpp"
#include "widgets/DifferentialGrid.hpp"
#include "widgets/ExpressionEvaluatorWidget.hpp"
#include "widgets/HpcConsoleWidget.hpp"
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

    // Apply self-contained dark-slate workstation stylesheet directly to BottomConsoleTabs.
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

    // 1. Harmonize GdbConsoleWidget (GDB Console tab) outer layout and control bar metrics.
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
        // Cap interactive controls inside each tab page at 22px height.
        for (auto* b : page->findChildren<QPushButton*>()) if (b->maximumHeight() > 24) b->setFixedHeight(22);
        for (auto* e : page->findChildren<QLineEdit*>()) if (e->maximumHeight() > 24) e->setFixedHeight(22);
        for (auto* c : page->findChildren<QComboBox*>()) if (c->maximumHeight() > 24) c->setFixedHeight(22);
        for (auto* s : page->findChildren<QSpinBox*>()) if (s->maximumHeight() > 24) s->setFixedHeight(22);
        // Strip redundant QFrame borders from embedded text and list viewports.
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
                // Extract MPI_* or <symbol@plt> identifier from the active disassembly line.
                static const QRegularExpression kSymRegex(QStringLiteral("(MPI_[A-Za-z0-9_]+|<[A-Za-z0-9_@]+>)"));
                const QRegularExpressionMatch match = kSymRegex.match(cursor.block().text());
                if (match.hasMatch()) candidate = match.captured(1);
            }
        }
        if (candidate.isEmpty() && m_seekBar && !m_seekBar->text().trimmed().isEmpty()) candidate = m_seekBar->text().trimmed();
        if (candidate.isEmpty()) candidate = QStringLiteral("MPI_Init");
        lookupSymbolDocumentation(candidate);
    });

    // 5. Wire cross-pane workflow bridges between Evaluator/Variables -> Watch Expressions and Disassembly -> Reference Manual.
    auto addPromotedWatch = [this](const QString& expr) {
        const QString trimmed = expr.trimmed();
        if (trimmed.isEmpty() || !m_differentialGrid) return;
        m_differentialGrid->addVariableColumn(trimmed);
        m_differentialGrid->updateVariableDisplay(0, trimmed, QStringLiteral("0.0"));
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
                                            !m_forceDecompilerFallback && m_workspacePerspective == QStringLiteral("Code");
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
    if (m_editorTabManager) connect(m_editorTabManager, &QTabWidget::currentChanged, this, [bindActiveSourceView](int) { bindActiveSourceView(); });

    // 7. Mount one-click Workspace Perspective Switcher (CODE | REVERSE | MPI) on MainToolbar.
    if (auto* mainToolbar = findChild<QToolBar*>(QStringLiteral("MainToolbar"))) {
        const QString pStyle = QStringLiteral("QPushButton { background: #1e222a; color: #9da5b4; border: 1px solid #2c313a; border-radius: 2px; padding: 0px 6px; font-size: 10px; font-weight: 700; min-height: 20px; max-height: 20px; } QPushButton:hover { background: #282c34; color: #fff; border-color: #528bff; } QPushButton:checked { background: #2c313c; color: #61afef; border: 1px solid #528bff; }");
        auto makePerspBtn = [&](const QString& label, const QString& obj, const QString& persp, bool chk) {
            auto* b = new QPushButton(label, mainToolbar);
            b->setObjectName(obj); b->setCheckable(true); b->setChecked(chk); b->setStyleSheet(pStyle);
            connect(b, &QPushButton::clicked, this, [this, persp]() { setWorkspacePerspective(persp); });
            mainToolbar->addWidget(b); return b;
        };
        mainToolbar->addSeparator();
        m_perspCodeBtn = makePerspBtn(QStringLiteral("CODE"), QStringLiteral("perspectiveCodeBtn"), QStringLiteral("Code"), true);
        m_perspReverseBtn = makePerspBtn(QStringLiteral("REVERSE"), QStringLiteral("perspectiveReverseBtn"), QStringLiteral("Reverse"), false);
        m_perspMpiBtn = makePerspBtn(QStringLiteral("MPI"), QStringLiteral("perspectiveMpiBtn"), QStringLiteral("MPI"), false);
    }

    // 8. Bind Alt+1..3 perspective shortcuts, Ctrl+M maximize toggle, and populate #bottomToolWindowRail.
    connect(new QShortcut(QKeySequence(Qt::ALT | Qt::Key_1), this), &QShortcut::activated, this, [this]() { setWorkspacePerspective(QStringLiteral("Code")); });
    connect(new QShortcut(QKeySequence(Qt::ALT | Qt::Key_2), this), &QShortcut::activated, this, [this]() { setWorkspacePerspective(QStringLiteral("Reverse")); });
    connect(new QShortcut(QKeySequence(Qt::ALT | Qt::Key_3), this), &QShortcut::activated, this, [this]() { setWorkspacePerspective(QStringLiteral("MPI")); });
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

        // Render crisp 16x16 antialiased vector icons for Debug, MPI, Terminal, and Docs rail buttons.
        auto makeRailIcon = [](int kind) -> QIcon {
            QPixmap px(16, 16); px.fill(Qt::transparent);
            QPainter p(&px); p.setRenderHint(QPainter::Antialiasing, true);
            QPen pen(QColor(0xd7, 0xda, 0xe0), 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            if (kind == 0) { // Debug: inspection crosshair ring with center core
                pen.setColor(QColor(0x61, 0xaf, 0xef)); p.setPen(pen); p.drawEllipse(QRectF(3.0, 3.0, 10.0, 10.0));
                p.drawLine(QPointF(8.0, 1.0), QPointF(8.0, 3.5)); p.drawLine(QPointF(8.0, 12.5), QPointF(8.0, 15.0));
                p.setBrush(QColor(0x61, 0xaf, 0xef)); p.drawEllipse(QPointF(8.0, 8.0), 1.5, 1.5);
            } else if (kind == 1) { // MPI: 3-node cluster topology graph
                pen.setColor(QColor(0x4e, 0xc9, 0xb0)); p.setPen(pen);
                p.drawLine(QPointF(8.0, 4.0), QPointF(3.5, 12.0)); p.drawLine(QPointF(8.0, 4.0), QPointF(12.5, 12.0)); p.drawLine(QPointF(3.5, 12.0), QPointF(12.5, 12.0));
                p.setBrush(QColor(0x18, 0x1a, 0x1f));
                p.drawEllipse(QPointF(8.0, 4.0), 2.2, 2.2); p.drawEllipse(QPointF(3.5, 12.0), 2.2, 2.2); p.drawEllipse(QPointF(12.5, 12.0), 2.2, 2.2);
            } else if (kind == 2) { // Terminal: >_ command prompt glyph
                pen.setColor(QColor(0xe5, 0xc0, 0x7b)); p.setPen(pen);
                p.drawLine(QPointF(3.0, 4.5), QPointF(7.5, 8.0)); p.drawLine(QPointF(7.5, 8.0), QPointF(3.0, 11.5));
                p.drawLine(QPointF(9.0, 11.5), QPointF(13.5, 11.5));
            } else { // Docs: open reference manual pages
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

    // Automatically synchronize the active tool-window group whenever BottomConsoleTabs changes programmatically.
    connect(m_bottomTabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (m_syncingBottomToolGroup || index < 0) return;
        const QString grp = bottomToolGroupForTabIndex(index);
        if (!grp.isEmpty() && grp != m_activeBottomToolGroup) {
            selectBottomToolGroup(grp);
        }
    });
    selectBottomToolGroup(QStringLiteral("Debug"));
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
    if (normalized.isEmpty() || !m_referenceManualWidget || !m_bottomTabs) return false;
    if (!m_referenceManualWidget->lookupSymbol(normalized)) return false;

    selectBottomToolGroup(QStringLiteral("Docs"));
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
    if (!m_bottomTabs || tabIndex < 0 || tabIndex >= m_bottomTabs->count() || outputPath.trimmed().isEmpty()) {
        return false;
    }

    // Optional multi-tab visual audit export when GRIDLOCK_AUDIT_SNAPSHOTS=1 is set.
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

/**
 * @brief Loads a 64-bit ELF binary executable or shared object for static inspection.
 * @param binaryPath Path to executable file on disk.
 * @return True if file is valid ELF64 and successfully parsed; false otherwise.
 */
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

    // Populate ProjectExplorerWidget with parsed ELF symbols and strings.
    uint64_t mainAddr = 0ULL;
    QString mainSymName;
    if (m_projectExplorerWidget) {
        std::vector<SymbolEntry> syms;
        syms.reserve(m_elfInspector->symbols().size());
        for (const auto& s : m_elfInspector->symbols()) syms.push_back({s.name, s.address, static_cast<uint32_t>(s.size)});
        m_projectExplorerWidget->setSymbols(syms);

        std::vector<StringEntry> strs;
        strs.reserve(m_elfInspector->strings().size());
        for (const auto& str : m_elfInspector->strings()) strs.push_back({str.address, str.section, str.value});
        m_projectExplorerWidget->setStrings(strs);
    }

    // Locate main symbol if present so user targets open directly at main with DWARF line info.
    for (const auto& s : m_elfInspector->symbols()) {
        if (s.isFunction && (s.name == QStringLiteral("main") || s.name == QStringLiteral("sym.main"))) {
            mainAddr = s.address; mainSymName = s.name; break;
        }
    }

    if (mainAddr != 0ULL && !sourceFilter.isEmpty()) {
        const QString disasm = m_elfInspector->disassembleSymbol(mainSymName, sourceFilter, &m_dwarfLineToAddress, &m_dwarfAddressToLine);
        if (m_disassemblyView && !disasm.isEmpty()) m_disassemblyView->setPlainText(disasm);
        const bool keepLinear = m_disassemblyView && !m_disassemblyView->isGraphMode() &&
                                !m_disassemblyView->isDecompilerMode() && !m_disassemblyView->isSplitGraphDecompilerMode() &&
                                !m_forceDecompilerFallback && m_workspacePerspective == QStringLiteral("Code");
        seekToAddressOrExpression(QStringLiteral("0x%1").arg(mainAddr, 8, 16, QChar('0')));
        if (keepLinear && m_disassemblyView) {
            m_showingDecompilerFallback = false;
            m_disassemblyView->setSplitGraphDecompilerMode(false);
            m_disassemblyView->setGraphMode(false);
        }
        rebuildDynamicSourceLineMap();
    } else {
        m_dwarfLineToAddress.clear(); m_dwarfAddressToLine.clear();
        const uint64_t entry = (mainAddr != 0ULL) ? mainAddr : m_elfInspector->entryPoint();
        if (entry != 0ULL) seekToAddressOrExpression(QStringLiteral("0x%1").arg(entry, 0, 16));
    }
    return true;
}

/**
 * @brief Exports the in-memory patched ELF binary image to disk.
 * @param outputPath Destination file system path.
 * @return True if file was successfully written and marked executable; false otherwise.
 */
bool MainWindow::exportPatchedBinary(const QString& outputPath) const {
    return (m_elfInspector && m_elfInspector->isValid()) ? m_elfInspector->savePatchedBinary(outputPath) : false;
}

/**
 * @brief Performs unified multi-domain case-insensitive search across the workspace.
 * @param query Target search query substring.
 * @return List of structured search match records.
 */
QVector<MainWindow::WorkspaceSearchResult> MainWindow::searchWorkspace(const QString& query) const {
    QVector<WorkspaceSearchResult> matches;
    const QString q = query.trimmed();
    if (q.isEmpty()) return matches;

    // 1. Search loaded ELF symbols and strings.
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

    // 2. Search Disassembly lines, user comments, and Decompiled C statements.
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

/**
 * @brief Detects wait-for cycle among MPI ranks and highlights deadlocked nodes.
 * @param waitForGraph Directed edges where key rank waits for value rank.
 * @return List of ranks participating in the deadlock cycle sorted in ascending order.
 */
QVector<int> MainWindow::detectAndHighlightDeadlock(const QMap<int, int>& waitForGraph) {
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
            for (int i = cycleStartIdx; cycleStartIdx != -1 && i < path.size(); ++i) {
                if (!inCycle.contains(path[i])) {
                    inCycle.insert(path[i]);
                    cycleNodes.append(path[i]);
                }
            }
        }
    }
    std::sort(cycleNodes.begin(), cycleNodes.end());

    // Propagate deadlocked rank IDs to DisassemblyView and bottom status bar telemetry.
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
        m_runAction->setCheckable(true);
        m_runAction->setChecked(m_debugSessionActive);
        m_runAction->setText(m_debugSessionActive ? QStringLiteral("● Running") : QStringLiteral("▶ Run Target"));
    }
    if (m_continueAction) m_continueAction->setEnabled(m_debugSessionActive);
    if (m_stepAction) m_stepAction->setEnabled(m_debugSessionActive);
    if (m_pauseAction) m_pauseAction->setEnabled(m_debugSessionActive);
    if (m_stopAction) m_stopAction->setEnabled(m_debugSessionActive);
    if (m_terminateAction) m_terminateAction->setEnabled(m_debugSessionActive);
}

/**
 * @brief Checks whether the toolbar Run Target button is currently latched in Running state.
 * @return True when an active debugging session has latched Run Target checked.
 */
bool MainWindow::isRunButtonLatched() const {
    return m_debugSessionActive && m_runAction && m_runAction->isChecked();
}

/**
 * @brief Switches the workstation layout between "Code", "Reverse", and "MPI" perspectives.
 *
 * - "Code": Side-by-side SourceCodeView and DisassemblyView with standard bottom console height.
 * - "Reverse": Collapses SourceCodeView and expands DisassemblyView in Split CFG + Decompiler mode.
 * - "MPI": Activates the MPI Diagnostics bottom tab and expands bottom console height.
 *
 * @param perspective Target perspective name ("Code", "Reverse", or "MPI").
 */
void MainWindow::setWorkspacePerspective(const QString& perspective) {
    redockAllFloatingPanels();
    auto* masterSplitter = findChild<QSplitter*>(QStringLiteral("MasterHorizontalSplitter"));
    auto* mainVertSplitter = findChild<QSplitter*>(QStringLiteral("MainVerticalSplitter"));

    if (perspective.compare(QStringLiteral("Reverse"), Qt::CaseInsensitive) == 0) {
        // Reverse perspective: full-width Split CFG + Decompiler view.
        m_workspacePerspective = QStringLiteral("Reverse");
        if (m_editorTabManager) m_editorTabManager->setVisible(false);
        if (m_disassemblyView) {
            m_disassemblyView->setSplitGraphDecompilerMode(true);
            m_disassemblyView->setMinimumWidth(720);
        }
        if (masterSplitter) masterSplitter->setSizes({0, (std::max)(width() - 360, 860)});
    } else if (perspective.compare(QStringLiteral("MPI"), Qt::CaseInsensitive) == 0) {
        // MPI perspective: focus MPI Diagnostics wait-state table and network log.
        m_workspacePerspective = QStringLiteral("MPI");
        if (m_editorTabManager) m_editorTabManager->setVisible(true);
        if (m_disassemblyView) m_disassemblyView->setMinimumWidth(120);
        if (masterSplitter) masterSplitter->setSizes({420, 500});
        selectBottomToolGroup(QStringLiteral("MPI"));
        if (m_bottomTabs && m_mpiDiagnosticsWidget) m_bottomTabs->setCurrentWidget(m_mpiDiagnosticsWidget);
        if (mainVertSplitter) mainVertSplitter->setSizes({430, 410});
    } else {
        // Code perspective: balanced side-by-side SourceCodeView and DisassemblyView.
        m_workspacePerspective = QStringLiteral("Code");
        if (m_editorTabManager) m_editorTabManager->setVisible(true);
        if (m_disassemblyView) m_disassemblyView->setMinimumWidth(120);
        if (masterSplitter) masterSplitter->setSizes({440, 520});
        if (mainVertSplitter) mainVertSplitter->setSizes({600, 260});
        m_bottomConsoleMaximized = false;
    }

    // Synchronize checked state on the toolbar perspective switcher buttons.
    if (m_perspCodeBtn) m_perspCodeBtn->setChecked(m_workspacePerspective == QStringLiteral("Code"));
    if (m_perspReverseBtn) m_perspReverseBtn->setChecked(m_workspacePerspective == QStringLiteral("Reverse"));
    if (m_perspMpiBtn) m_perspMpiBtn->setChecked(m_workspacePerspective == QStringLiteral("MPI"));
}

/**
 * @brief Toggles the bottom console dock between standard height (260px) and maximized height (65%).
 */
void MainWindow::toggleMaximizeBottomConsole() {
    auto* mainVertSplitter = findChild<QSplitter*>(QStringLiteral("MainVerticalSplitter"));
    if (!mainVertSplitter) return;
    // Flip maximized state and adjust vertical splitter proportions accordingly.
    m_bottomConsoleMaximized = !m_bottomConsoleMaximized;
    const int totalH = (std::max)(mainVertSplitter->height(), 760);
    mainVertSplitter->setSizes(m_bottomConsoleMaximized ? QList<int>{totalH * 32 / 100, totalH * 68 / 100}
                                                        : QList<int>{600, 260});
}

/**
 * @brief Dispatches an Edit menu action to the currently focused or active source editor.
 * @param actionName Name or substring of the Edit menu action.
 * @return True if the action was recognized and executed; false otherwise.
 */
bool MainWindow::triggerEditMenuAction(const QString& actionName) {
    const QString act = actionName.trimmed();
    if (act.isEmpty()) return false;
    if (act.contains(QStringLiteral("Preferences"), Qt::CaseInsensitive)) { openPreferences(); return true; }

    // Resolve target QPlainTextEdit / SourceCodeView (prefer focused text editor, fallback to active SourceCodeView).
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
    if (act.contains(QStringLiteral("Find"), Qt::CaseInsensitive) && m_seekBar) { m_seekBar->setFocus(); m_seekBar->selectAll(); return true; }
    return false;
}

/**
 * @brief Applies UI and Code/Disassembly typography settings live across the entire workstation.
 * @param uiFamily UI font family name.
 * @param uiSize UI font point size.
 * @param codeFamily Code/Disassembly monospace font family name.
 * @param codeSize Code/Disassembly font point size.
 */
void MainWindow::applyTypographySettings(const QString& uiFamily, int uiSize,
                                         const QString& codeFamily, int codeSize) {
    QSettings s("gridlock", "debugger");
    if (!uiFamily.trimmed().isEmpty()) s.setValue("appearance/ui_font_family", uiFamily.trimmed());
    s.setValue("appearance/ui_font_size", qBound(7, uiSize, 32));
    if (!codeFamily.trimmed().isEmpty()) s.setValue("appearance/code_font_family", codeFamily.trimmed());
    s.setValue("appearance/code_font_size", qBound(7, codeSize, 48));
    s.sync();

    // Recompile and apply global workstation QSS with scaled UI font sizes and control heights.
    gridlock::core::managers::ThemeManager::instance().applyThemeAndFonts();

    // Propagate code typography live to all open SourceCodeView tabs, DisassemblyView, and Explorer.
    if (m_editorTabManager) {
        for (int i = 0; i < m_editorTabManager->count(); ++i) {
            if (auto* view = qobject_cast<SourceCodeView*>(m_editorTabManager->widget(i))) view->reloadStyle();
        }
    }
    if (m_disassemblyView) m_disassemblyView->reloadStyle();
    if (m_projectExplorerWidget) m_projectExplorerWidget->reloadStyle();
}

/**
 * @brief Constructs the shell clean command for a given project directory or standalone source file.
 * @param targetPath Project directory path or source file path.
 * @return Shell command string to clean build artifacts.
 */
QString MainWindow::cleanCommandForPath(const QString& targetPath) const {
    // Detect active build system and normalize workspace path before constructing clean command.
    const QString buildSys = detectBuildSystem(targetPath);
    const QString resolvedPath = targetPath.trimmed().isEmpty() ? QDir::currentPath() : resolveWorkspacePath(targetPath);
    const QFileInfo info(resolvedPath);
    const QString dirPath = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    const QDir dir(dirPath);

    // Dispatch build-system-specific clean commands (Meson, CMake, Make, or SingleFileMPI).
    if (buildSys == QStringLiteral("Meson")) {
        // Prefer builddir unless only build/build.ninja is already configured on disk.
        const QString buildSubdir =
            (dir.exists(QStringLiteral("build/build.ninja")) && !dir.exists(QStringLiteral("builddir/build.ninja")))
                ? QStringLiteral("build") : QStringLiteral("builddir");
        return QStringLiteral("ninja -C \"%1/%2\" -t clean").arg(dirPath, buildSubdir);
    }
    if (buildSys == QStringLiteral("CMake")) return QStringLiteral("cmake --build \"%1/build\" --target clean").arg(dirPath);
    if (buildSys == QStringLiteral("Make")) return QStringLiteral("make clean -C \"%1\"").arg(dirPath);
    // Single-file MPI targets remove the compiled output binary sharing the source basename.
    const QFileInfo srcInfo(!info.isDir() ? info.absoluteFilePath() : activeTargetSourcePath());
    return QStringLiteral("rm -f \"%1\"").arg(srcInfo.dir().filePath(srcInfo.completeBaseName()));
}

/**
 * @brief Switches BottomConsoleTabs to Terminal, dispatches the build command, and reloads SingleFileMPI ELF output.
 * @return True if the build command was generated and dispatched to TerminalDockWidget.
 */
bool MainWindow::buildTargetWorkflow() {
    if (!m_terminalDock) return false;
    const QString targetPath = activeTargetSourcePath();

    // Activate embedded Terminal group and dispatch build command.
    selectBottomToolGroup(QStringLiteral("Terminal"));
    if (m_bottomTabs) m_bottomTabs->setCurrentWidget(m_terminalDock);
    const QString buildSys = detectBuildSystem(targetPath);
    const QString cmd = buildCommandForPath(targetPath);
    if (cmd.isEmpty()) return false;
    m_terminalDock->sendCommand(cmd);
    // Synchronize DisassemblyView and DWARF line mappings immediately for single-file MPI targets.
    if (buildSys == QStringLiteral("SingleFileMPI")) ensureTargetBinaryCompiled();
    // Reflect active build toolchain status in the main status bar telemetry badge.
    updateStatusTelemetry(m_currentSeekOrPc, QStringLiteral("BUILDING [%1]").arg(buildSys));
    return true;
}

/**
 * @brief Switches BottomConsoleTabs to Terminal and dispatches the detected clean command.
 * @return True if the clean command was generated and dispatched to TerminalDockWidget.
 */
bool MainWindow::cleanTargetWorkflow() {
    if (!m_terminalDock) return false;
    const QString targetPath = activeTargetSourcePath();

    // Activate embedded Terminal group and dispatch clean command.
    selectBottomToolGroup(QStringLiteral("Terminal"));
    if (m_bottomTabs) m_bottomTabs->setCurrentWidget(m_terminalDock);
    const QString buildSys = detectBuildSystem(targetPath);
    const QString cmd = cleanCommandForPath(targetPath);
    if (cmd.isEmpty()) return false;
    m_terminalDock->sendCommand(cmd);
    // Update status bar telemetry so the user sees which build system is being cleaned.
    updateStatusTelemetry(m_currentSeekOrPc, QStringLiteral("CLEANING [%1]").arg(buildSys));
    return true;
}

/**
 * @brief Switches BottomConsoleTabs to Terminal and dispatches a chained clean && build command.
 * @return True if the rebuild command was generated and dispatched to TerminalDockWidget.
 */
bool MainWindow::rebuildTargetWorkflow() {
    if (!m_terminalDock) return false;
    const QString targetPath = activeTargetSourcePath();

    // Activate embedded Terminal group and dispatch chained clean && build command.
    selectBottomToolGroup(QStringLiteral("Terminal"));
    if (m_bottomTabs) m_bottomTabs->setCurrentWidget(m_terminalDock);
    const QString buildSys = detectBuildSystem(targetPath);
    const QString cleanCmd = cleanCommandForPath(targetPath);
    const QString buildCmd = buildCommandForPath(targetPath);
    if (cleanCmd.isEmpty() || buildCmd.isEmpty()) return false;
    // Chain clean and build with short-circuit && so compilation only runs if clean succeeds.
    m_terminalDock->sendCommand(QStringLiteral("%1 && %2").arg(cleanCmd, buildCmd));
    // Reflect active rebuild operation in the main status bar telemetry badge.
    updateStatusTelemetry(m_currentSeekOrPc, QStringLiteral("BUILDING [%1]").arg(buildSys));
    return true;
}

/**
 * @brief Programmatically drives all workstation views, tool rail groups, dialogs, and themes
 *        to capture high-resolution 1600x960 documentation PNG screenshots into outputDir.
 * @param outputDir Destination directory for generated PNG assets (e.g., docs/screenshots).
 * @return True if all 16 core workstation, feature, and theme screenshots were saved cleanly.
 */
bool MainWindow::captureDocumentationScreenshots(const QString& outputDir) {
    QDir dir(outputDir);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) return false;

    // Normalize window geometry to 1600x960 and populate realistic MPI debugging state.
    resize(1600, 960);
    loadPreviewFixture();
    selectTargetByName(QStringLiteral("deadlock_ring"));
    show();
    QApplication::processEvents();

    // Configure SourceCodeView breakpoints (line 18 standard, line 34 conditional) and execution line.
    if (auto* sv = sourceCodeView()) {
        if (!sv->hasBreakpoint(18)) sv->toggleBreakpoint(18);
        sv->setConditionalBreakpoint(34, QStringLiteral("rank == 0"));
        sv->highlightExecutionLine(34);
    }
    auto isFresh = [&](const QString& fn) { const QFileInfo fi(dir.filePath(fn)); return fi.exists() && fi.size() > 15000; };
    auto saveGrab = [&](const QString& fn) -> bool {
        if (isFresh(fn)) return true;
        QApplication::processEvents();
        return grab().save(dir.filePath(fn), "PNG", 88);
    };

    // 1. Capture all 6 curated theme screenshots plus default workstation_overview & workspace_overview.
    for (const auto& [presetId, fileName] : std::vector<std::pair<QString, QString>>{
             {QStringLiteral("dark_teal"), QStringLiteral("theme_obsidian_teal.png")},
             {QStringLiteral("tokyo_night"), QStringLiteral("theme_tokyo_night.png")},
             {QStringLiteral("catppuccin_mocha"), QStringLiteral("theme_catppuccin_mocha.png")},
             {QStringLiteral("gruvbox_dark"), QStringLiteral("theme_gruvbox_dark.png")},
             {QStringLiteral("daylight_light"), QStringLiteral("theme_daylight_light.png")},
             {QStringLiteral("gridlock_slate"), QStringLiteral("theme_gridlock_slate.png")}}) {
        if (!isFresh(fileName)) { core::managers::ThemeManager::instance().applyTheme(presetId, false); propagateThemeToAllWidgets(); }
        if (!saveGrab(fileName)) return false;
    }
    // Save primary workstation & workspace overview assets from the active Gridlock Slate theme.
    if (!saveGrab(QStringLiteral("workstation_overview.png")) || !saveGrab(QStringLiteral("workspace_overview.png"))) return false;
    setWorkspacePerspective(QStringLiteral("Reverse"));
    if (!saveGrab(QStringLiteral("cfg_decompiler_split.png"))) return false;
    setWorkspacePerspective(QStringLiteral("Code"));

    // 2. Composite cursor-anchored ConditionalBreakpointDialog popover beneath line 34's gutter dot.
    if (!isFresh(QStringLiteral("breakpoint_popover.png"))) {
        ConditionalBreakpointDialog popDlg(QStringLiteral("rank == 0"), this);
        popDlg.setTargetLineContext(34, QStringLiteral("MPI_Send(send_buf.data(), ...);"));
        popDlg.adjustSize();
        QPixmap basePix = grab();
        QPainter painter(&basePix);
        painter.drawPixmap(sourceCodeView() ? sourceCodeView()->mapTo(this, QPoint(44, 180)) : QPoint(290, 260), popDlg.grab());
        painter.end();
        if (!basePix.save(dir.filePath(QStringLiteral("breakpoint_popover.png")), "PNG", 88)) return false;
    }

    // 3. Capture bottom tool-window icon rail in Debug mode and MPI Diagnostics mode.
    selectBottomToolGroup(QStringLiteral("Debug"));
    if (!saveGrab(QStringLiteral("tool_rail_debug_tabs.png"))) return false;
    selectBottomToolGroup(QStringLiteral("MPI"));
    detectAndHighlightDeadlock({{0, 1}, {1, 2}, {2, 3}, {3, 0}});
    if (m_bottomTabs && m_mpiDiagnosticsWidget) m_bottomTabs->setCurrentWidget(m_mpiDiagnosticsWidget);
    if (!saveGrab(QStringLiteral("tool_rail_mpi_diagnostics.png")) || !saveGrab(QStringLiteral("mpi_diagnostics_docsets.png"))) return false;

    // 4. Populate Package Manager with realistic "mpi" search results in Table and List modes.
    if (auto* pkgWidget = static_cast<HpcConsoleWidget*>(m_spackManager)) {
        if (m_bottomTabs) m_bottomTabs->setCurrentWidget(pkgWidget);
        if (auto* s = pkgWidget->findChild<QLineEdit*>(QStringLiteral("packageSearchInput"))) s->setText(QStringLiteral("mpi"));
        pkgWidget->displayPackageRecords({
            {QStringLiteral("openmpi"), QStringLiteral("5.0.5-1"), QStringLiteral("x86_64"), QStringLiteral("extra"), QStringLiteral("Installed"), QStringLiteral("High performance message passing library (MPI-4.1)")},
            {QStringLiteral("openmpi-devel"), QStringLiteral("5.0.5-1"), QStringLiteral("x86_64"), QStringLiteral("extra"), QStringLiteral("Installed"), QStringLiteral("Development headers and mpicxx wrapper for OpenMPI")},
            {QStringLiteral("mpich"), QStringLiteral("4.2.2-1"), QStringLiteral("x86_64"), QStringLiteral("extra"), QStringLiteral("Available"), QStringLiteral("High-performance portable implementation of MPI")},
            {QStringLiteral("hypre"), QStringLiteral("2.31.0-2"), QStringLiteral("x86_64"), QStringLiteral("spack"), QStringLiteral("Installed"), QStringLiteral("Scalable multigrid linear solvers for MPI clusters")},
            {QStringLiteral("fftw-mpi"), QStringLiteral("3.3.10-4"), QStringLiteral("x86_64"), QStringLiteral("extra"), QStringLiteral("Installed"), QStringLiteral("Distributed-memory Fast Fourier Transform C routines")},
            {QStringLiteral("hdf5-openmpi"), QStringLiteral("1.14.4-1"), QStringLiteral("x86_64"), QStringLiteral("extra"), QStringLiteral("Installed"), QStringLiteral("Parallel HDF5 data model and file format for MPI-IO")},
            {QStringLiteral("scalapack"), QStringLiteral("2.2.0-5"), QStringLiteral("x86_64"), QStringLiteral("extra"), QStringLiteral("Available"), QStringLiteral("Scalable linear algebra package for distributed HPC")}});
        pkgWidget->setViewMode(QStringLiteral("Table"));
        if (!saveGrab(QStringLiteral("package_manager_mpi_search.png"))) return false;
        pkgWidget->setViewMode(QStringLiteral("List"));
        if (!saveGrab(QStringLiteral("package_manager_list_view.png"))) return false;
    }

    // 5. Capture PreferencesDialog on HPC / Cluster page with 16-directive #SBATCH auto-detection.
    if (isFresh(QStringLiteral("preferences_slurm_autodetect.png"))) return true;
    PreferencesDialog prefsDlg(this);
    prefsDlg.resize(940, 660);
    if (auto* catList = prefsDlg.findChild<QListWidget*>()) {
        for (int i = 0; i < catList->count(); ++i) if (catList->item(i)->text().contains(QStringLiteral("HPC"), Qt::CaseInsensitive)) catList->setCurrentRow(i);
    }
    prefsDlg.parseSlurmBatchScript(QStringLiteral(
        "#!/bin/bash\n#SBATCH --job-name=gridlock_ring_debug\n#SBATCH --partition=gpu-a100\n#SBATCH --account=hpc_devtools\n"
        "#SBATCH --qos=interactive\n#SBATCH --nodes=4\n#SBATCH --ntasks=16\n#SBATCH --ntasks-per-node=4\n#SBATCH --cpus-per-task=8\n"
        "#SBATCH --gres=gpu:a100:4\n#SBATCH --mem=128G\n#SBATCH --time=02:30:00\n#SBATCH --constraint=nvlink\n"
        "#SBATCH --reservation=maint_window\n#SBATCH --chdir=/scratch/hpc/gridlock\n#SBATCH --output=logs/slurm-%j.out\n"
        "#SBATCH --error=logs/slurm-%j.err\nmodule load openmpi/5.0.5 cuda/12.4\n"));
    QApplication::processEvents();
    QPixmap basePix = grab();
    QPainter painter(&basePix);
    const QPixmap dlgPix = prefsDlg.grab();
    painter.drawPixmap((width() - dlgPix.width()) / 2, (height() - dlgPix.height()) / 2, dlgPix);
    painter.end();
    return basePix.save(dir.filePath(QStringLiteral("preferences_slurm_autodetect.png")), "PNG", 88);
}

}  // namespace gridlock::ui


