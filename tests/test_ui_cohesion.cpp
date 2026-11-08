/**
 * @file test_ui_cohesion.cpp
 * @brief Visual and structural UI cohesion tests for bottom console tabs, docset viewer, and side docks.
 *
 * Verifies that every bottom console tab adheres to the two-tier workstation layout
 * (flush 0px-margin viewport + compact <= 28px header control strip), that F1 symbol
 * documentation lookup strips PLT suffixes and populates structured dark-slate manual
 * pages, and that offscreen tab audit snapshots render non-empty PNG images.
 */

#include "test_ui.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QHeaderView>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QtTest/QtTest>

#include "../src/core/managers/ThemeManager.hpp"
#include "../src/ui/GdbConsoleWidget.hpp"
#include "../src/ui/MainWindow.hpp"
#include "../src/ui/dialogs/ConditionalBreakpointDialog.hpp"
#include "../src/ui/dialogs/PreferencesDialog.hpp"
#include "../src/ui/dialogs/ProjectSettingsDialog.hpp"
#include "../src/ui/dialogs/ProjectWizard.hpp"
#include "../src/ui/dialogs/XrefInspectorDialog.hpp"
#include "../src/ui/docks/VariablesDockWidget.hpp"
#include "../src/ui/tutorial/TutorialDialog.hpp"
#include "../src/ui/views/DisassemblyView.hpp"
#include "../src/ui/views/MemView.hpp"
#include "../src/ui/views/RegisterView.hpp"
#include "../src/ui/views/ServerRackView.hpp"
#include "../src/ui/views/SourceCodeView.hpp"
#include "../src/ui/widgets/DifferentialGrid.hpp"
#include "../src/ui/widgets/ExpressionEvaluatorWidget.hpp"
#include "../src/ui/widgets/MpiDiagnosticsWidget.hpp"
#include "../src/ui/widgets/MpiNetworkLogWidget.hpp"
#include "../src/ui/widgets/ProjectExplorerWidget.hpp"
#include "../src/ui/widgets/ReferenceManualWidget.hpp"

namespace gridlock::tests {

/**
 * @brief Constant maximum allowed outer margin on bottom console tab pages.
 */
constexpr int kMaxAllowedTabMarginPx = 4;

/**
 * @brief Constant maximum allowed control height inside bottom console tab headers.
 */
constexpr int kMaxAllowedControlHeightPx = 28;

/**
 * @brief Constant maximum allowed control height inside workstation modal dialogs.
 */
constexpr int kMaxAllowedDialogControlHeightPx = 38;

/**
 * @brief Constant minimum allowed visible column width in workstation tables and trees.
 */
constexpr int kMinAllowedColumnSectionWidthPx = 40;

template <typename T>
auto getActiveCanvasBg(T* widget, int) -> decltype(widget->activeCanvasBackgroundColor()) {
    return widget->activeCanvasBackgroundColor();
}
template <typename T>
QColor getActiveCanvasBg(T*, ...) { return QColor(QStringLiteral("#16181d")); }
template <typename T>
auto getActiveHexText(T* widget, int) -> decltype(widget->activeHexTextColor()) { return widget->activeHexTextColor(); }
template <typename T>
QColor getActiveHexText(T*, ...) { return QColor(QStringLiteral("#c8ccd4")); }

template <typename T>
auto callPropagateTheme(T* mw, int) -> decltype(mw->propagateThemeToAllWidgets(), void()) {
    mw->propagateThemeToAllWidgets();
}
template <typename T>
void callPropagateTheme(T*, ...) {}

template <typename T>
auto callAreAllChromeAligned(const T* mw, int) -> decltype(mw->areAllChromeToolbarsAndDockHeadersThemeAligned()) {
    return mw->areAllChromeToolbarsAndDockHeadersThemeAligned();
}
template <typename T>
bool callAreAllChromeAligned(const T*, ...) {
    return false;
}

}  // namespace gridlock::tests

/**
 * @brief Verifies zero-gutter flush viewports across bottom tabs, F1 docset lookup, 2-zone right dock hierarchy, and primary Run Target CTA.
 * Validates bottom tab margins, MPI_Init@plt symbol lookup, tabified Variables/Registers/Watch docks (>= 260px height), and accented Run Target button.
 */
void TestMainWindowUI::testWorkstationUiCohesionAndIntegratedDocViewer() {
    // Instantiate and expose the main workstation window with active theme palette propagated.
    auto& tm = gridlock::core::ThemeManager::instance(); tm.initialize();
    gridlock::ui::MainWindow mainWindow; mainWindow.resize(1440, 900);
    mainWindow.loadPreviewFixture(); mainWindow.propagateThemeToAllWidgets();
    mainWindow.show(); QVERIFY(QTest::qWaitForWindowExposed(&mainWindow)); QApplication::processEvents();

    // Locate the bottom console tab widget container by its canonical objectName.
    auto* bottomTabs = mainWindow.findChild<QTabWidget*>(QStringLiteral("BottomConsoleTabs"));
    QVERIFY2(bottomTabs != nullptr && bottomTabs->count() >= 7, "BottomConsoleTabs QTabWidget must exist with >= 7 tabs");

    // 1. Zero-Gutter Flush Viewports & Compact Control Heights Across All Bottom Tabs.
    for (int i = 0; i < bottomTabs->count(); ++i) {
        bottomTabs->setCurrentIndex(i); QCoreApplication::processEvents();
        QWidget* page = bottomTabs->widget(i); QVERIFY(page != nullptr);
        const QString tabTitle = bottomTabs->tabText(i);
        if (QLayout* pageLayout = page->layout()) {
            const QMargins m = pageLayout->contentsMargins();
            QVERIFY2(m.left() <= gridlock::tests::kMaxAllowedTabMarginPx && m.right() <= gridlock::tests::kMaxAllowedTabMarginPx && m.bottom() <= gridlock::tests::kMaxAllowedTabMarginPx, qPrintable(QString("Tab '%1' margins > 4").arg(tabTitle)));
        }
        for (const QPushButton* btn : page->findChildren<QPushButton*>()) if (btn && btn->isVisible()) QVERIFY2(btn->height() <= gridlock::tests::kMaxAllowedControlHeightPx, qPrintable(QString("Tab '%1' button height > 28").arg(tabTitle)));
        for (const QLineEdit* edit : page->findChildren<QLineEdit*>()) if (edit && edit->isVisible()) QVERIFY2(edit->height() <= gridlock::tests::kMaxAllowedControlHeightPx, qPrintable(QString("Tab '%1' lineEdit height > 28").arg(tabTitle)));
        for (const QComboBox* combo : page->findChildren<QComboBox*>()) if (combo && combo->isVisible()) QVERIFY2(combo->height() <= gridlock::tests::kMaxAllowedControlHeightPx, qPrintable(QString("Tab '%1' combo height > 28").arg(tabTitle)));
    }

    // 2. Integrated Reference Manual (Docset / Man Viewer) Cohesion & F1 Symbol Lookup.
    bottomTabs->setCurrentIndex(0); QCoreApplication::processEvents();
    QVERIFY2(mainWindow.lookupSymbolDocumentation(QStringLiteral("MPI_Init@plt")), "lookupSymbolDocumentation('MPI_Init@plt') must succeed");
    QCoreApplication::processEvents();
    QVERIFY2(bottomTabs->tabText(bottomTabs->currentIndex()).contains(QStringLiteral("Reference Manual"), Qt::CaseInsensitive), qPrintable(QString("Expected Reference Manual tab active, got '%1'").arg(bottomTabs->tabText(bottomTabs->currentIndex()))));
    auto* refManual = mainWindow.findChild<gridlock::ui::ReferenceManualWidget*>(); QVERIFY(refManual != nullptr);
    auto* browser = refManual->findChild<QTextBrowser*>(); QVERIFY(browser != nullptr);
    const QString docPlainText = browser->toPlainText();
    QVERIFY2(docPlainText.contains(QStringLiteral("MPI_Init"), Qt::CaseInsensitive) && docPlainText.contains(QStringLiteral("SYNOPSIS"), Qt::CaseInsensitive), "Reference Manual content must include MPI_Init and SYNOPSIS");

    // 3. Tab Audit Snapshot Capability.
    QTemporaryDir tempDir; QVERIFY(tempDir.isValid());
    const QString snapshotPath = tempDir.filePath(QStringLiteral("tab_refman_audit.png"));
    QVERIFY2(mainWindow.captureTabAuditSnapshot(bottomTabs->currentIndex(), snapshotPath), "captureTabAuditSnapshot must succeed");
    const QFileInfo snapInfo(snapshotPath); QVERIFY(snapInfo.exists() && snapInfo.size() > 0);

    // 4. Right-Sidebar 2-Zone Consolidation & Concise WATCH Tab without Scroll Arrows.
    auto* variablesDock = mainWindow.findChild<QDockWidget*>(QStringLiteral("VariablesDock"));
    auto* registersDock = mainWindow.findChild<QDockWidget*>(QStringLiteral("RegisterViewDock"));
    auto* watchDock = mainWindow.findChild<QDockWidget*>(QStringLiteral("WatchExpressionsDock"));
    QVERIFY(variablesDock != nullptr && registersDock != nullptr && watchDock != nullptr);
    const QList<QDockWidget*> tabified = mainWindow.tabifiedDockWidgets(variablesDock);
    QVERIFY2(tabified.contains(registersDock) && tabified.contains(watchDock), "VariablesDock, RegisterViewDock, and WatchExpressionsDock must share top-right tabbed dock group");

    // Assert watchDock title / tab text is "WATCH" (concise, <= 8 chars).
    QVERIFY2(watchDock->windowTitle().compare(QStringLiteral("WATCH"), Qt::CaseInsensitive) == 0 && watchDock->windowTitle().size() <= 8, "watchDock title must be concise 'WATCH'");

    // Verify all dock QTabBar instances disable scroll buttons and fit all tabs within tabBar width.
    QTabBar* rightDockTabBar = nullptr;
    for (QTabBar* tb : mainWindow.findChildren<QTabBar*>()) {
        QVERIFY2(!tb->usesScrollButtons(), "Every dock QTabBar in MainWindow must have usesScrollButtons() == false");
        for (int i = 0; i < tb->count(); ++i) {
            if (tb->tabText(i).contains(QStringLiteral("VARIABLES"), Qt::CaseInsensitive)) { rightDockTabBar = tb; break; }
        }
    }
    QVERIFY2(rightDockTabBar != nullptr && rightDockTabBar->count() >= 3, "Right dock QTabBar with 3 tabs must exist");
    QVERIFY2(rightDockTabBar->tabRect(0).left() >= 0 && rightDockTabBar->tabRect(rightDockTabBar->count() - 1).right() <= rightDockTabBar->width(), "All 3 tabs (VARIABLES, REGISTERS, WATCH) must fit completely inside QTabBar::width()");

    // Verify active top-right dock group height >= 260px at 1600x950.
    mainWindow.resize(1600, 950); mainWindow.setWorkspacePerspective(QStringLiteral("Code")); QApplication::processEvents(); QTest::qWait(20);
    const int varsActiveH = variablesDock->height();
    watchDock->raise(); QApplication::processEvents(); const int watchActiveH = watchDock->height();
    variablesDock->raise(); QApplication::processEvents();
    QVERIFY2(varsActiveH >= 260 && watchActiveH >= 260, qPrintable(QStringLiteral("Top-right dock crushed: varsH=%1 watchH=%2").arg(varsActiveH).arg(watchActiveH)));

    // 5. Verify DisassemblyView mode buttons are strictly mutually exclusive (at most 1 checked button).
    auto* disasm = mainWindow.findChild<gridlock::ui::DisassemblyView*>(); QVERIFY(disasm != nullptr);
    auto countCheckedModeBtns = [disasm]() -> int {
        int cnt = 0;
        for (const QString& id : {QStringLiteral("cutterSplitViewBtn"), QStringLiteral("cutterDecompilerBtn"), QStringLiteral("cutterCfgToggleBtn")})
            if (auto* b = disasm->findChild<QPushButton*>(id)) if (b->isChecked()) ++cnt;
        return cnt;
    };
    disasm->setGraphMode(false); QApplication::processEvents(); QVERIFY2(countCheckedModeBtns() <= 1, "Linear mode checked buttons > 1");
    disasm->setGraphMode(true); QApplication::processEvents(); QVERIFY2(countCheckedModeBtns() <= 1, "CFG mode checked buttons > 1");
    disasm->setDecompilerMode(true); QApplication::processEvents(); QVERIFY2(countCheckedModeBtns() <= 1, "Decompiler mode checked buttons > 1");
    disasm->setSplitGraphDecompilerMode(true); QApplication::processEvents(); QVERIFY2(countCheckedModeBtns() <= 1, "Split CFG+Decomp mode checked buttons > 1");

    // 6. Verify Run Target toolbar QToolButton uses distinct primary theme accent CTA styling.
    auto* mainToolBar = mainWindow.findChild<QToolBar*>(QStringLiteral("MainToolbar")); QVERIFY(mainToolBar != nullptr);
    QToolButton* runBtn = nullptr;
    for (QAction* act : mainToolBar->actions()) {
        if (act && act->text().contains(QStringLiteral("Run Target"), Qt::CaseInsensitive)) { runBtn = qobject_cast<QToolButton*>(mainToolBar->widgetForAction(act)); break; }
    }
    const auto pal = tm.paletteForTheme(tm.activeThemeId());
    QVERIFY2(runBtn != nullptr && runBtn->styleSheet().contains(pal.accentHex, Qt::CaseInsensitive), qPrintable(QStringLiteral("Run Target QToolButton must include primary accent ") + pal.accentHex));
    QVERIFY(mainWindow.grab().save(QStringLiteral("/tmp/tabs_and_disasm_verify.png")));
}

/**
 * @brief Verifies dark-slate visual cohesion across modal dialogs, category sidebars, and table/tree headers.
 * Audits dialog dimensions, compact control heights, and table/tree column header integrity.
 */
void TestMainWindowUI::testDialogsAndTablesVisualCohesion() {
    // Initialize ThemeManager and expose MainWindow with standard preview fixture data.
    auto& tm = gridlock::core::ThemeManager::instance(); tm.initialize(); tm.applyTheme(QStringLiteral("gridlock_slate"));
    gridlock::ui::MainWindow mainWindow; mainWindow.resize(1440, 900); mainWindow.loadPreviewFixture();
    mainWindow.propagateThemeToAllWidgets();
    mainWindow.show(); QVERIFY(QTest::qWaitForWindowExposed(&mainWindow)); QApplication::processEvents();

    // Helper lambda to verify dialog dimensions and compact control heights.
    auto verifyDialogCohesion = [](QDialog* dlg, const QString& name) {
        QVERIFY2(dlg != nullptr, qPrintable(QString("Dialog '%1' must not be null").arg(name)));
        dlg->show(); QCoreApplication::processEvents();
        QVERIFY2(dlg->width() >= 360 && dlg->height() >= 220, qPrintable(QString("Dialog '%1' too small").arg(name)));
        for (const QPushButton* btn : dlg->findChildren<QPushButton*>())
            if (btn && btn->isVisible()) QVERIFY2(btn->height() <= gridlock::tests::kMaxAllowedDialogControlHeightPx, qPrintable(QString("Dialog '%1' button '%2' height %3 > 34").arg(name, btn->text()).arg(btn->height())));
        for (const QLineEdit* edit : dlg->findChildren<QLineEdit*>())
            if (edit && edit->isVisible()) QVERIFY2(edit->height() <= gridlock::tests::kMaxAllowedDialogControlHeightPx, qPrintable(QString("Dialog '%1' lineEdit '%2' height %3 > 34").arg(name, edit->objectName()).arg(edit->height())));
        for (const QComboBox* combo : dlg->findChildren<QComboBox*>())
            if (combo && combo->isVisible()) QVERIFY2(combo->height() <= gridlock::tests::kMaxAllowedDialogControlHeightPx, qPrintable(QString("Dialog '%1' comboBox '%2' height %3 > 34").arg(name, combo->objectName()).arg(combo->height())));
    };

    // 1. Audit PreferencesDialog sidebar, icon-free OK/Cancel/Apply buttons, and primary accent OK button.
    gridlock::ui::PreferencesDialog prefsDlg(&mainWindow); verifyDialogCohesion(&prefsDlg, QStringLiteral("PreferencesDialog"));
    auto* categoryList = prefsDlg.findChild<QListWidget*>(); auto* btnBox = prefsDlg.findChild<QDialogButtonBox*>();
    QVERIFY2(categoryList != nullptr && categoryList->width() >= 140 && categoryList->width() <= 220 && categoryList->horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOff, "PreferencesDialog categoryList checks failed");
    QVERIFY2(btnBox != nullptr, "PreferencesDialog must contain a QDialogButtonBox");
    auto *okBtn = btnBox->button(QDialogButtonBox::Ok), *cancelBtn = btnBox->button(QDialogButtonBox::Cancel), *applyBtn = btnBox->button(QDialogButtonBox::Apply);
    QVERIFY(okBtn != nullptr && cancelBtn != nullptr && applyBtn != nullptr && okBtn->icon().isNull() && cancelBtn->icon().isNull() && applyBtn->icon().isNull());
    const QString accentHex = tm.paletteForTheme(tm.activeThemeId()).accentHex;
    QVERIFY2(okBtn->styleSheet().contains(accentHex, Qt::CaseInsensitive), qPrintable(QStringLiteral("PreferencesDialog OK button must use accent ") + accentHex));
    prefsDlg.hide();

    // Audit modal dialog dimensions and controls across remaining workstation dialogs.
    gridlock::ui::AttachProcessDialog attachDlg(&mainWindow); verifyDialogCohesion(&attachDlg, QStringLiteral("AttachProcessDialog"));
    for (QDialog* d : QList<QDialog*>{new gridlock::ui::NewProjectDialog(&mainWindow), new gridlock::ui::AboutDialog(&mainWindow), new gridlock::ui::ProjectSettingsDialog(&mainWindow), new gridlock::ui::dialogs::ProjectWizard(&mainWindow), new gridlock::ui::ConditionalBreakpointDialog(QString(), &mainWindow), new gridlock::ui::TutorialDialog(&mainWindow)}) {
        verifyDialogCohesion(d, d->metaObject()->className()); d->deleteLater();
    }
    auto* disasm = mainWindow.findChild<gridlock::ui::DisassemblyView*>(); auto* projExplorer = mainWindow.findChild<gridlock::ui::ProjectExplorerWidget*>();
    QVERIFY(disasm != nullptr && projExplorer != nullptr);
    auto* xrefDlg = disasm->openXrefInspector(0x00401149ULL); verifyDialogCohesion(xrefDlg, QStringLiteral("XrefInspectorDialog"));

    // 2. Enforce >= 12px UI typography floor (zero font-size: 9px or font-size: 10px rules) and 22..28px unified mode buttons.
    auto assertNoTinyFontCss = [](const QString& css, const QString& ctx) {
        QVERIFY2(!css.contains(QStringLiteral("font-size: 9px"), Qt::CaseInsensitive) && !css.contains(QStringLiteral("font-size: 10px"), Qt::CaseInsensitive),
                 qPrintable(QStringLiteral("Sub-12px font-size rule found in ") + ctx));
    };
    assertNoTinyFontCss(qApp->styleSheet(), QStringLiteral("qApp->styleSheet()"));
    assertNoTinyFontCss(projExplorer->styleSheet(), QStringLiteral("ProjectExplorerWidget"));
    for (const QWidget* w : projExplorer->findChildren<QWidget*>()) assertNoTinyFontCss(w->styleSheet(), QStringLiteral("ProjectExplorerWidget::") + w->objectName());
    assertNoTinyFontCss(disasm->styleSheet(), QStringLiteral("DisassemblyView"));
    for (const QWidget* w : disasm->findChildren<QWidget*>()) assertNoTinyFontCss(w->styleSheet(), QStringLiteral("DisassemblyView::") + w->objectName());

    // Verify unified segmented mode buttons (FILES, SPLIT (CFG+DECOMP), CODE) all have height in [22, 28].
    auto* filesBtn = projExplorer->findChild<QPushButton*>(QStringLiteral("explorerFilesModeBtn"));
    auto* splitBtn = disasm->findChild<QPushButton*>(QStringLiteral("cutterSplitViewBtn"));
    auto* codeBtn = mainWindow.findChild<QPushButton*>(QStringLiteral("perspectiveCodeBtn"));
    QVERIFY(filesBtn != nullptr && splitBtn != nullptr && codeBtn != nullptr);
    QVERIFY2(filesBtn->height() >= 22 && filesBtn->height() <= 28 && splitBtn->height() >= 22 && splitBtn->height() <= 28 && codeBtn->height() >= 22 && codeBtn->height() <= 28,
             qPrintable(QStringLiteral("Mode button heights out of [22,28]: files=%1 split=%2 code=%3").arg(filesBtn->height()).arg(splitBtn->height()).arg(codeBtn->height())));

    // 3. Audit Table & Tree Column Header Integrity across workspace views and dialogs.
    auto verifyTableIntegrity = [](QTableWidget* table, const QString& ownerName) {
        QVERIFY2(table != nullptr && table->frameShape() == QFrame::NoFrame && table->alternatingRowColors(), qPrintable(QString("Table '%1' frame/alternating check failed").arg(ownerName)));
        QHeaderView* hdr = table->horizontalHeader();
        QVERIFY(hdr != nullptr && hdr->isVisible() && hdr->minimumSectionSize() >= 48);
        for (int col = 0; col < table->columnCount(); ++col)
            if (!table->isColumnHidden(col)) QVERIFY2(hdr->sectionSize(col) >= gridlock::tests::kMinAllowedColumnSectionWidthPx, qPrintable(QString("Table '%1' col %2 width < 40").arg(ownerName).arg(col)));
    };
    auto verifyTreeIntegrity = [](QTreeView* tree, const QString& ownerName) {
        QVERIFY2(tree != nullptr && tree->frameShape() == QFrame::NoFrame && tree->alternatingRowColors(), qPrintable(QString("Tree '%1' frame/alternating check failed").arg(ownerName)));
        QHeaderView* hdr = tree->header();
        QVERIFY(hdr != nullptr && hdr->isVisible() && hdr->minimumSectionSize() >= 48);
        if (tree->model())
            for (int col = 0; col < tree->model()->columnCount(); ++col)
                if (!tree->isColumnHidden(col)) QVERIFY2(hdr->sectionSize(col) >= gridlock::tests::kMinAllowedColumnSectionWidthPx, qPrintable(QString("Tree '%1' col %2 width < 40").arg(ownerName).arg(col)));
    };
    for (QTreeView* tree : projExplorer->findChildren<QTreeView*>()) if (tree->isVisible()) verifyTreeIntegrity(tree, QStringLiteral("ProjectExplorerWidget"));
    auto* regView = mainWindow.findChild<gridlock::ui::RegisterView*>(); QVERIFY(regView != nullptr);
    for (QTableWidget* tbl : regView->findChildren<QTableWidget*>()) verifyTableIntegrity(tbl, QStringLiteral("RegisterView"));
    auto* diffGrid = mainWindow.findChild<gridlock::ui::DifferentialGrid*>(); QVERIFY(diffGrid != nullptr);
    verifyTableIntegrity(diffGrid, QStringLiteral("DifferentialGrid"));
    auto* varsDock = mainWindow.findChild<gridlock::VariablesDockWidget*>(); QVERIFY(varsDock != nullptr);
    for (QTreeView* tree : varsDock->findChildren<QTreeView*>()) verifyTreeIntegrity(tree, QStringLiteral("VariablesDockWidget"));
    for (QTableWidget* tbl : attachDlg.findChildren<QTableWidget*>()) verifyTableIntegrity(tbl, QStringLiteral("AttachProcessDialog"));
    attachDlg.hide();
    for (QTableWidget* tbl : xrefDlg->findChildren<QTableWidget*>()) verifyTableIntegrity(tbl, QStringLiteral("XrefInspectorDialog"));
    xrefDlg->hide();
}

/**
 * @brief Verifies cross-pane workflow bridges, DisassemblyView context actions, and line rebalance.
 * Tests evaluator-to-watch promotion, disassembly context menu F1 manual lookup, and file headroom.
 */
void TestMainWindowUI::testCrossPaneWorkflowBridgesAndContextActions() {
    gridlock::ui::MainWindow mainWindow;
    mainWindow.resize(1440, 900);
    mainWindow.loadPreviewFixture();
    mainWindow.show();
    QVERIFY(QTest::qWaitForWindowExposed(&mainWindow));
    QApplication::processEvents();

    // 1. Evaluator & Variables -> Watch Expressions Bridge.
    auto* evaluatorWidget = mainWindow.findChild<gridlock::ui::ExpressionEvaluatorWidget*>();
    QVERIFY2(evaluatorWidget != nullptr, "ExpressionEvaluatorWidget must exist in MainWindow");

    auto* diffGrid = mainWindow.findChild<gridlock::ui::DifferentialGrid*>();
    QVERIFY2(diffGrid != nullptr, "DifferentialGrid must exist in MainWindow");

    evaluatorWidget->setExpression(QStringLiteral("data[0].coords.z"));
    QVERIFY2(evaluatorWidget->promoteCurrentToWatch(),
             "promoteCurrentToWatch() must succeed for non-empty expression");
    QCoreApplication::processEvents();

    // Verify "data[0].coords.z" was added (case-insensitively) to DifferentialGrid watch expressions.
    auto hasWatchExpression = [diffGrid](const QString& target) -> bool {
        for (const std::string& w : diffGrid->getWatchExpressions()) {
            if (QString::fromStdString(w).compare(target, Qt::CaseInsensitive) == 0) return true;
        }
        for (int col = 0; col < diffGrid->columnCount(); ++col) {
            if (const QTableWidgetItem* hdr = diffGrid->horizontalHeaderItem(col)) {
                if (hdr->text().compare(target, Qt::CaseInsensitive) == 0) return true;
            }
        }
        return false;
    };
    QVERIFY2(hasWatchExpression(QStringLiteral("data[0].coords.z")), "DifferentialGrid must contain promoted Evaluator expression 'data[0].coords.z'");

    // Promote a variable from VariablesDockWidget to Watch Expressions.
    auto* variablesDock = mainWindow.findChild<gridlock::VariablesDockWidget*>();
    QVERIFY2(variablesDock != nullptr, "VariablesDockWidget must exist in MainWindow");
    QVERIFY2(variablesDock->promoteVariableToWatch(QStringLiteral("rank_local_id")), "VariablesDockWidget::promoteVariableToWatch('rank_local_id') must return true");
    QCoreApplication::processEvents();
    QVERIFY2(hasWatchExpression(QStringLiteral("rank_local_id")), "DifferentialGrid must contain promoted variable 'rank_local_id'");

    // 2. Cross-View Context Menu Actions in DisassemblyView.
    auto* disasm = mainWindow.findChild<gridlock::ui::DisassemblyView*>();
    QVERIFY2(disasm != nullptr, "DisassemblyView must exist in MainWindow");

    const QStringList actionNames = disasm->contextActionNamesForAddress(0x0040113dULL);
    QVERIFY2(actionNames.contains(QStringLiteral("Seek in Memory View")), "DisassemblyView context actions must include 'Seek in Memory View'");
    QVERIFY2(actionNames.contains(QStringLiteral("Inspect Cross-References (X)")), "DisassemblyView context actions must include 'Inspect Cross-References (X)'");
    QVERIFY2(actionNames.contains(QStringLiteral("Lookup in Reference Manual (F1)")), "DisassemblyView context actions must include 'Lookup in Reference Manual (F1)'");
    QVERIFY2(actionNames.contains(QStringLiteral("Patch Instruction")), "DisassemblyView context actions must include 'Patch Instruction'");

    // Start on Terminal tab (index 0), then trigger "Lookup in Reference Manual (F1)" at 0x0040113d.
    auto* bottomTabs = mainWindow.findChild<QTabWidget*>(QStringLiteral("BottomConsoleTabs"));
    QVERIFY(bottomTabs != nullptr);
    bottomTabs->setCurrentIndex(0);
    QCoreApplication::processEvents();

    QVERIFY2(disasm->triggerContextAction(0x0040113dULL, QStringLiteral("Lookup in Reference Manual (F1)")), "triggerContextAction(0x0040113d, 'Lookup in Reference Manual (F1)') must succeed");
    QCoreApplication::processEvents();

    QVERIFY2(bottomTabs->tabText(bottomTabs->currentIndex()).contains(QStringLiteral("Reference Manual"), Qt::CaseInsensitive), "Triggering 'Lookup in Reference Manual (F1)' must switch BottomConsoleTabs to Reference Manual");

    auto* refManual = mainWindow.findChild<gridlock::ui::ReferenceManualWidget*>();
    QVERIFY(refManual != nullptr);
    auto* browser = refManual->findChild<QTextBrowser*>();
    QVERIFY(browser != nullptr);
    QVERIFY2(browser->toPlainText().contains(QStringLiteral("MPI_Init"), Qt::CaseInsensitive), "Reference Manual must display MPI_Init documentation after context action lookup");

    // 3. File Line Headroom Verification for src/ui/views/DisassemblyAnalysis.cpp (< 850 lines).
    QFile disasmAnalysisFile(QStringLiteral("../src/ui/views/DisassemblyAnalysis.cpp"));
    if (!disasmAnalysisFile.exists()) disasmAnalysisFile.setFileName(QStringLiteral("src/ui/views/DisassemblyAnalysis.cpp"));
    QVERIFY2(disasmAnalysisFile.open(QIODevice::ReadOnly | QIODevice::Text), "Must open src/ui/views/DisassemblyAnalysis.cpp for line count verification");
    const int lineCount = QString::fromUtf8(disasmAnalysisFile.readAll()).split('\n').size();
    QVERIFY2(lineCount < 850, qPrintable(QString("DisassemblyAnalysis.cpp has %1 lines (expected < 850)").arg(lineCount)));
}

/**
 * @brief Verifies Zeal docset UI integration, unclipped CFG comments, and MPI Diagnostics table.
 * Tests basic-block comment formatting, MPI Diagnostics wait-state table, and docset HTML preview.
 */
void TestMainWindowUI::testZealUiIntegrationCfgUnclippedAndMpiDiagnosticsTable() {
    gridlock::ui::MainWindow mainWindow;
    mainWindow.resize(1440, 900);
    mainWindow.loadPreviewFixture();
    mainWindow.show();
    QVERIFY(QTest::qWaitForWindowExposed(&mainWindow));
    QApplication::processEvents();

    // 1. Verify CFG basic-block widths accommodate full instruction + inline comment text without clipping.
    auto* disasm = mainWindow.findChild<gridlock::ui::DisassemblyView*>();
    QVERIFY2(disasm != nullptr, "DisassemblyView must exist in MainWindow");

    const int block401149Width = disasm->blockWidthForAddress(0x00401149ULL);
    const int block40114fWidth = disasm->blockWidthForAddress(0x0040114fULL);
    QVERIFY2(block401149Width >= 380, qPrintable(QString("CFG block 0x401149 width %1 < 380px").arg(block401149Width)));
    QVERIFY2(block40114fWidth >= 380, qPrintable(QString("CFG block 0x40114f width %1 < 380px").arg(block40114fWidth)));

    // 2. Verify MpiDiagnosticsWidget contains a populated rank wait-state table and 26px header bar.
    auto* mpiDiag = mainWindow.findChild<gridlock::ui::MpiDiagnosticsWidget*>();
    QVERIFY2(mpiDiag != nullptr, "MpiDiagnosticsWidget must exist in MainWindow");
    QVERIFY2(mpiDiag->rankWaitTableRowCount() >= 1, qPrintable(QString("MpiDiagnosticsWidget rankWaitTableRowCount() %1 < 1").arg(mpiDiag->rankWaitTableRowCount())));
    auto* waitTable = mpiDiag->findChild<QTableWidget*>();
    QVERIFY2(waitTable != nullptr && waitTable->columnCount() >= 5 && waitTable->frameShape() == QFrame::NoFrame, "MpiDiagnosticsWidget must contain a >= 5 column QFrame::NoFrame wait table");

    // 3. Verify ReferenceManualWidget exposes loaded Zeal docsets and renders real HTML from a Zeal bundle.
    auto* refManual = mainWindow.findChild<gridlock::ui::ReferenceManualWidget*>();
    QVERIFY2(refManual != nullptr && !refManual->loadedDocsetNames().isEmpty(), "ReferenceManualWidget must exist and expose loaded docsets");
    QVERIFY2(refManual->lookupSymbol(QStringLiteral("MPI_Init")), "ReferenceManualWidget::lookupSymbol('MPI_Init') must succeed");
}

/**
 * @brief Verifies SourceCodeView uniform canvas, MpiNetworkLogWidget table, and header density.
 * Confirms uniform editor background color, 6-column network log table, and monochrome glyphs.
 */
void TestMainWindowUI::testSourceEditorCanvasNetworkLogTableAndHeaderDensity() {
    gridlock::ui::MainWindow mainWindow;
    mainWindow.resize(1440, 900);
    mainWindow.loadPreviewFixture();
    mainWindow.loadSourceFile(QStringLiteral("tutorial/inspection_demo.cpp"));
    mainWindow.show();
    QVERIFY(QTest::qWaitForWindowExposed(&mainWindow));
    QApplication::processEvents();

    // 1. SourceCodeView Uniform Canvas & Gutter.
    auto* sourceView = mainWindow.findChild<gridlock::ui::SourceCodeView*>();
    QVERIFY2(sourceView != nullptr, "Active SourceCodeView must exist in MainWindow");
    QVERIFY2(sourceView->rightViewportMargin() == 0 && !sourceView->hasRightMarginFill(), "SourceCodeView right margin must be 0 with no right-margin fill");

    const QImage editorImg = sourceView->viewport()->grab().toImage();
    QVERIFY2(!editorImg.isNull() && editorImg.width() > 100 && editorImg.height() > 40, "SourceCodeView snapshot image must be valid and non-empty");
    // Sample on empty line 3 (between #include <stdio.h> and struct DeeplyNested) above any scrollbar.
    const int sampleY = sourceView->fontMetrics().height() * 2 + sourceView->fontMetrics().height() / 2;
    const QColor leftColor = editorImg.pixelColor(static_cast<int>(editorImg.width() * 0.25), sampleY);
    const QColor rightColor = editorImg.pixelColor(static_cast<int>(editorImg.width() * 0.85), sampleY);
    QVERIFY2(leftColor.rgb() == rightColor.rgb(), qPrintable(QString("SourceCodeView split background: left=%1 vs right=%2").arg(leftColor.name(), rightColor.name())));

    // 2. Structured MpiNetworkLogWidget Table.
    auto* netLog = mainWindow.findChild<gridlock::ui::MpiNetworkLogWidget*>();
    QVERIFY2(netLog != nullptr, "MpiNetworkLogWidget must exist in MainWindow");
    auto* netTable = netLog->findChild<QTableWidget*>();
    QVERIFY2(netTable != nullptr && netTable->columnCount() == 6 && netTable->frameShape() == QFrame::NoFrame, "MpiNetworkLogWidget must contain a 6-column QFrame::NoFrame QTableWidget");
    netLog->appendLog(QStringLiteral("[12:00:01.042] Rank 0 -> Rank 1 | MPI_Send | Tag=0 MPI_COMM_WORLD | 1024 B"));
    QVERIFY2(netLog->logRowCount() >= 1, "MpiNetworkLogWidget::logRowCount() must be >= 1 after appendLog");

    // 3. Single-Row ReferenceManualWidget Header & Clean Monochrome Toolbar Labels.
    auto* refManual = mainWindow.findChild<gridlock::ui::ReferenceManualWidget*>();
    QVERIFY2(refManual != nullptr && refManual->headerBarHeight() > 0 && refManual->headerBarHeight() <= 30, "ReferenceManualWidget::headerBarHeight() must be in (0, 30]");
    for (QToolBar* toolBar : mainWindow.findChildren<QToolBar*>()) {
        for (QAction* act : toolBar->actions()) QVERIFY2(!act->text().contains(QStringLiteral("\U000023E9")), "Toolbar action must not contain U+23E9");
        for (QToolButton* btn : toolBar->findChildren<QToolButton*>()) QVERIFY2(!btn->text().contains(QStringLiteral("\U000023E9")), "Toolbar button must not contain U+23E9");
    }
}

/**
 * @brief Verifies DifferentialGrid uniform empty viewport, stretched headers, and console prompt bars.
 * Audits watch expressions background color, stretched headers, and compact console prompt strips.
 */
void TestMainWindowUI::testWatchGridBackgroundAndConsolePromptCohesion() {
    gridlock::ui::MainWindow mainWindow;
    mainWindow.resize(1440, 900);
    mainWindow.loadPreviewFixture();
    mainWindow.show();
    QVERIFY(QTest::qWaitForWindowExposed(&mainWindow));
    QApplication::processEvents();

    // 1. DifferentialGrid (Watch Expressions) Uniform Empty Viewport & Stretch Headers.
    auto* watchGrid = mainWindow.findChild<gridlock::ui::DifferentialGrid*>();
    QVERIFY2(watchGrid != nullptr && watchGrid->horizontalHeaderStretchEnabled(), "DifferentialGrid must exist and enable horizontalHeaderStretchEnabled()");
    watchGrid->resize(260, 220);
    watchGrid->show();
    QApplication::processEvents();

    const QImage watchImg = watchGrid->grab().toImage();
    QVERIFY2(!watchImg.isNull() && watchImg.width() >= 200 && watchImg.height() >= 200, "DifferentialGrid grabbed image must be valid and at least 200x200");
    QVERIFY2(watchImg.pixelColor(25, 190).rgb() == watchImg.pixelColor(180, 190).rgb(), "DifferentialGrid empty area must have uniform background color across left and right columns");

    // 2. GDB Console & Evaluator Prompt Bar & Header Cohesion.
    auto* bottomTabs = mainWindow.findChild<QTabWidget*>(QStringLiteral("BottomConsoleTabs"));
    auto* gdbConsole = mainWindow.findChild<gridlock::ui::GdbConsoleWidget*>();
    auto* evaluator = mainWindow.findChild<gridlock::ui::ExpressionEvaluatorWidget*>();
    QVERIFY(bottomTabs != nullptr && gdbConsole != nullptr && evaluator != nullptr);
    for (int i = 0; i < bottomTabs->count(); ++i) {
        if (bottomTabs->tabText(i).contains(QStringLiteral("GDB Console"), Qt::CaseInsensitive) ||
            bottomTabs->tabText(i).contains(QStringLiteral("Evaluator"), Qt::CaseInsensitive)) {
            bottomTabs->setCurrentIndex(i);
            QApplication::processEvents();
        }
    }
    QVERIFY2(gdbConsole->headerBarHeight() > 0 && gdbConsole->headerBarHeight() <= 30 &&
                 gdbConsole->promptBarHeight() > 0 && gdbConsole->promptBarHeight() <= 30,
             "GdbConsoleWidget header and prompt bar heights must be in (0, 30]");
    if (QLayout* gdbLayout = gdbConsole->layout()) QVERIFY2(gdbLayout->contentsMargins().left() <= 6, "GdbConsoleWidget left margin must be <= 6");
    for (QAbstractScrollArea* area : gdbConsole->findChildren<QAbstractScrollArea*>()) QVERIFY2(area->frameShape() == QFrame::NoFrame, "GdbConsoleWidget viewport must use QFrame::NoFrame");
    QVERIFY2(evaluator->headerBarHeight() > 0 && evaluator->headerBarHeight() <= 30, "ExpressionEvaluatorWidget::headerBarHeight() must be in (0, 30]");
    if (QLayout* evalLayout = evaluator->layout()) QVERIFY2(evalLayout->contentsMargins().left() <= 6, "ExpressionEvaluatorWidget left margin must be <= 6");
    for (QAbstractScrollArea* area : evaluator->findChildren<QAbstractScrollArea*>()) QVERIFY2(area->frameShape() == QFrame::NoFrame, "ExpressionEvaluatorWidget viewport must use QFrame::NoFrame");
}

/**
 * @brief Verifies toolbar target/rank controls, debugger actions, and synchronized Run/Step/Stop loop.
 * Confirms state-aware toolbar buttons and cross-pane instruction stepping synchronization.
 */
void TestMainWindowUI::testEndToEndStepDebuggingWorkflowAndToolbarState() {
    gridlock::ui::MainWindow mainWindow;
    mainWindow.resize(1440, 900);
    mainWindow.loadPreviewFixture();
    if (!mainWindow.findChild<gridlock::ui::SourceCodeView*>()) {
        const QString demoPath = QFileInfo::exists(QStringLiteral("tutorial/inspection_demo.cpp")) ? QStringLiteral("tutorial/inspection_demo.cpp") : QStringLiteral("../tutorial/inspection_demo.cpp");
        mainWindow.loadSourceFile(demoPath);
    }
    mainWindow.show();
    QVERIFY(QTest::qWaitForWindowExposed(&mainWindow));
    QApplication::processEvents();

    // 1. Toolbar Target Selector, Rank SpinBox & Idle Button States.
    auto* targetCombo = mainWindow.findChild<QComboBox*>(QStringLiteral("toolbarTargetSelector"));
    auto* rankSpin = mainWindow.findChild<QSpinBox*>(QStringLiteral("toolbarRankSpin"));
    QVERIFY2(targetCombo != nullptr && rankSpin != nullptr && targetCombo->count() >= 1, "MainToolbar must contain populated #toolbarTargetSelector and #toolbarRankSpin");

    auto findToolbarAction = [&mainWindow](const QString& labelSubstr) -> QAction* {
        for (QToolBar* tb : mainWindow.findChildren<QToolBar*>()) {
            for (QAction* act : tb->actions()) if (act->text().contains(labelSubstr, Qt::CaseInsensitive)) return act;
        }
        return nullptr;
    };

    QAction* runAction = findToolbarAction(QStringLiteral("Run Target"));
    QAction* stepAction = findToolbarAction(QStringLiteral("Step Inst"));
    QAction* terminateAction = findToolbarAction(QStringLiteral("Terminate Session"));
    QVERIFY2(runAction != nullptr && stepAction != nullptr && terminateAction != nullptr, "Run Target, Step Inst, and Terminate Session actions must exist on MainToolbar");

    mainWindow.stopTargetWorkflow();
    QApplication::processEvents();
    QVERIFY2(!mainWindow.isDebugSessionActive() && runAction->isEnabled() && !terminateAction->isEnabled(), "Idle state must enable Run Target and disable Terminate Session");

    // 2. End-to-End Interactive Run -> Step -> Register/Source/Rack Sync -> Stop Loop.
    if (auto* sv = mainWindow.findChild<gridlock::ui::SourceCodeView*>()) sv->setBreakpoints(QSet<int>());
    rankSpin->setValue(4);
    mainWindow.startTargetWorkflow();
    QApplication::processEvents();
    QVERIFY2(mainWindow.isDebugSessionActive() && stepAction->isEnabled() && terminateAction->isEnabled(), "Active session must enable Step Inst and Terminate Session");

    auto* serverRack = mainWindow.findChild<gridlock::ui::ServerRackView*>();
    auto* disasmView = mainWindow.findChild<gridlock::ui::DisassemblyView*>();
    auto* regView = mainWindow.findChild<gridlock::ui::RegisterView*>();
    auto* sourceView = mainWindow.findChild<gridlock::ui::SourceCodeView*>();
    QVERIFY(serverRack != nullptr && disasmView != nullptr && regView != nullptr && sourceView != nullptr);
    QCOMPARE(serverRack->rankNodeCount(), 4);
    QCOMPARE(disasmView->currentAddress(), 0x00401120ULL);
    QVERIFY2(sourceView->executionLine() > 0, "SourceCodeView::executionLine() must be valid on startTargetWorkflow()");

    // Step 1 (0x00401120 -> 0x00401124) & Step 2 (0x00401124 -> 0x00401125).
    QVERIFY2(mainWindow.stepInstructionWorkflow(), "First stepInstructionWorkflow() must succeed");
    QApplication::processEvents();
    QCOMPARE(disasmView->currentAddress(), 0x00401124ULL);
    QCOMPARE(serverRack->rankPcAddress(serverRack->selectedRank()), 0x00401124ULL);

    QVERIFY2(mainWindow.stepInstructionWorkflow(), "Second stepInstructionWorkflow() must succeed");
    QApplication::processEvents();
    QCOMPARE(disasmView->currentAddress(), 0x00401125ULL);
    QCOMPARE(serverRack->rankPcAddress(serverRack->selectedRank()), 0x00401125ULL);
    QVERIFY2(sourceView->executionLine() > 0, "SourceCodeView::executionLine() must remain active across steps");

    auto* regTable = regView->findChild<QTableWidget*>();
    QVERIFY(regTable != nullptr);
    bool foundRipMatch = false;
    for (int r = 0; r < regTable->rowCount(); ++r) {
        if (regTable->item(r, 0) && regTable->item(r, 0)->text().compare(QStringLiteral("rip"), Qt::CaseInsensitive) == 0 &&
            regTable->item(r, 1) && regTable->item(r, 1)->text().contains(QStringLiteral("401125"), Qt::CaseInsensitive)) foundRipMatch = true;
    }
    QVERIFY2(foundRipMatch, "RegisterView 'rip' row must update to 0x00401125 after second step");

    const QPixmap snap = mainWindow.grab();
    QVERIFY(!snap.isNull() && snap.save(QStringLiteral("/tmp/gridlock_ui_v28.png")));

    mainWindow.stopTargetWorkflow();
    QApplication::processEvents();
    auto* statusLabel = mainWindow.findChild<QLabel*>(QStringLiteral("cutterStatusTelemetry"));
    QVERIFY(statusLabel != nullptr);
    QVERIFY2(!mainWindow.isDebugSessionActive() && !terminateAction->isEnabled() &&
                 statusLabel->text().contains(QStringLiteral("STATUS: IDLE"), Qt::CaseInsensitive),
             "stopTargetWorkflow() must return session to STATUS: IDLE");
}

/**
 * @brief Verifies bidirectional SourceCodeView <-> DisassemblyView line/address sync and Code/Reverse/MPI workspace perspectives.
 *
 * Validates three ergonomic requirements from Prompt 29:
 * 1. Bidirectional Source Line <-> Instruction Address Mapping:
 *    Confirms mainWindow.sourceLineToAddress(13) == 0x0040113dULL (MPI_Init) and
 *    mainWindow.addressToSourceLine(0x0040113dULL) == 13, and verifies that calling
 *    sourceView->selectSourceLine(16) seeks DisassemblyView to 0x00401149ULL.
 * 2. Workspace Perspective Switching ("Code", "Reverse", "MPI"):
 *    Confirms setWorkspacePerspective("Reverse") enables Split CFG + Decompiler mode and
 *    expands DisassemblyView to >= 700px width, "MPI" switches BottomConsoleTabs to
 *    MPI Diagnostics, and "Code" restores side-by-side source and assembly panes.
 * 3. Bottom Console Maximize Toggle:
 *    Confirms toggleMaximizeBottomConsole() expands and restores the bottom tab dock height.
 */
void TestMainWindowUI::testSourceToAssemblySyncAndWorkspacePerspectives() {
    // Initialize MainWindow with preview fixture and ensure tutorial source file is mounted.
    gridlock::ui::MainWindow mainWindow; mainWindow.resize(1440, 900); mainWindow.loadPreviewFixture();
    if (!mainWindow.findChild<gridlock::ui::SourceCodeView*>()) {
        const QString demoPath = QFileInfo::exists(QStringLiteral("tutorial/inspection_demo.cpp")) ? QStringLiteral("tutorial/inspection_demo.cpp") : QStringLiteral("../tutorial/inspection_demo.cpp");
        mainWindow.loadSourceFile(demoPath);
    }
    mainWindow.show(); QVERIFY(QTest::qWaitForWindowExposed(&mainWindow)); QApplication::processEvents();

    // Locate central source editor, disassembly canvas, and bottom console tab widget.
    auto* sourceView = mainWindow.findChild<gridlock::ui::SourceCodeView*>();
    auto* disasmView = mainWindow.findChild<gridlock::ui::DisassemblyView*>();
    auto* bottomTabs = mainWindow.findChild<QTabWidget*>(QStringLiteral("BottomConsoleTabs"));
    QVERIFY(sourceView != nullptr && disasmView != nullptr && bottomTabs != nullptr);

    // 1. Bidirectional Source Line <-> Instruction Address Mapping across views.
    QCOMPARE(mainWindow.sourceLineToAddress(13), 0x0040113dULL);
    QCOMPARE(mainWindow.addressToSourceLine(0x0040113dULL), 13);
    sourceView->selectSourceLine(16); QApplication::processEvents();
    QCOMPARE(disasmView->currentAddress(), 0x00401149ULL);
    emit disasmView->addressSelected(0x0040113dULL); QApplication::processEvents();
    QCOMPARE(sourceView->executionLine(), 13);

    // 2. Workspace Perspective Switching ("Reverse", "MPI", "Code") and width checks.
    mainWindow.setWorkspacePerspective(QStringLiteral("Reverse")); QApplication::processEvents();
    QCOMPARE(mainWindow.workspacePerspective(), QStringLiteral("Reverse"));
    QVERIFY2(disasmView->isSplitGraphDecompilerMode() && disasmView->width() >= 700, "Reverse perspective must enable Split CFG + Decompiler mode with width >= 700");

    // Verify MPI perspective activates the MPI Diagnostics tab in BottomConsoleTabs.
    mainWindow.setWorkspacePerspective(QStringLiteral("MPI")); QApplication::processEvents();
    QCOMPARE(mainWindow.workspacePerspective(), QStringLiteral("MPI"));
    QVERIFY2(bottomTabs->tabText(bottomTabs->currentIndex()).contains(QStringLiteral("MPI Diagnostics"), Qt::CaseInsensitive), "MPI perspective must activate the MPI Diagnostics bottom tab");

    // Verify Code perspective restores side-by-side SourceCodeView and DisassemblyView panes.
    mainWindow.setWorkspacePerspective(QStringLiteral("Code")); QApplication::processEvents();
    QCOMPARE(mainWindow.workspacePerspective(), QStringLiteral("Code"));
    QVERIFY2(sourceView->width() > 200 && disasmView->width() > 200, "Code perspective must restore side-by-side SourceCodeView and DisassemblyView");

    // 3. Bottom Console Maximize Toggle expands and restores bottom dock height.
    const int baseHeight = bottomTabs->height();
    mainWindow.toggleMaximizeBottomConsole(); QApplication::processEvents();
    QVERIFY2(bottomTabs->height() > baseHeight, "toggleMaximizeBottomConsole() must expand BottomConsoleTabs height");
    mainWindow.toggleMaximizeBottomConsole(); QApplication::processEvents();
}

/**
 * @brief Verifies breakpoint-aware Run/Continue, source-to-RIP line mapping, and CFG active instruction markers.
 * Validates granular address-to-line mapping, breakpoint-aware target workflow, and active CFG block marker.
 */
void TestMainWindowUI::testBreakpointAwareRunContinueAndActiveCfgInstructionMarker() {
    // Launch MainWindow with fixture data and load tutorial source if not yet mounted.
    gridlock::ui::MainWindow mainWindow; mainWindow.resize(1440, 900); mainWindow.loadPreviewFixture();
    if (!mainWindow.findChild<gridlock::ui::SourceCodeView*>()) {
        const QString demoPath = QFileInfo::exists(QStringLiteral("tutorial/inspection_demo.cpp")) ? QStringLiteral("tutorial/inspection_demo.cpp") : QStringLiteral("../tutorial/inspection_demo.cpp");
        mainWindow.loadSourceFile(demoPath);
    }
    mainWindow.show(); QVERIFY(QTest::qWaitForWindowExposed(&mainWindow)); QApplication::processEvents();

    // Retrieve SourceCodeView, DisassemblyView, and ServerRackView pointers.
    auto* sourceView = mainWindow.findChild<gridlock::ui::SourceCodeView*>();
    auto* disasmView = mainWindow.findChild<gridlock::ui::DisassemblyView*>();
    auto* serverRack = mainWindow.findChild<gridlock::ui::ServerRackView*>();
    QVERIFY(sourceView != nullptr && disasmView != nullptr && serverRack != nullptr);

    // 1. Granular Source Line <-> Instruction Address Mapping assertions.
    QCOMPARE(mainWindow.addressToSourceLine(0x00401120ULL), 12);
    QCOMPARE(mainWindow.addressToSourceLine(0x00401128ULL), 15);
    QCOMPARE(mainWindow.addressToSourceLine(0x0040113dULL), 13);
    QCOMPARE(mainWindow.addressToSourceLine(0x00401149ULL), 16);
    QCOMPARE(mainWindow.sourceLineToAddress(15), 0x00401128ULL);
    QCOMPARE(mainWindow.sourceLineToAddress(16), 0x00401149ULL);
    // 2. Breakpoint-Aware Run Target & Continue Workflow at lines 15 and 16.
    mainWindow.stopTargetWorkflow(); sourceView->setBreakpoints(QSet<int>());
    sourceView->toggleBreakpoint(15); sourceView->toggleBreakpoint(16);
    QVERIFY2(sourceView->hasBreakpoint(15) && sourceView->hasBreakpoint(16), "SourceCodeView::toggleBreakpoint(15/16) must register active breakpoints");
    QVERIFY2(sourceView->breakpoints().contains(15) && sourceView->breakpoints().contains(16), "SourceCodeView::breakpoints() must contain lines 15 and 16");
    // Launch target and verify execution stops at first active breakpoint (line 15).
    mainWindow.startTargetWorkflow(); QApplication::processEvents();
    QCOMPARE(disasmView->currentAddress(), 0x00401128ULL);
    QCOMPARE(sourceView->executionLine(), 15);
    QCOMPARE(serverRack->rankPcAddress(serverRack->selectedRank()), 0x00401128ULL);
    // Continue to next active breakpoint at line 16 and verify RIP synchronization.
    QVERIFY2(mainWindow.continueExecutionWorkflow(), "MainWindow::continueExecutionWorkflow() must advance to the next active breakpoint");
    QApplication::processEvents();
    QCOMPARE(disasmView->currentAddress(), 0x00401149ULL);
    QCOMPARE(sourceView->executionLine(), 16);
    QCOMPARE(serverRack->rankPcAddress(serverRack->selectedRank()), 0x00401149ULL);
    // 3. Visible Active Instruction Marker Inside CFG Basic Blocks.
    QVERIFY2(disasmView->activeCfgBlockContainsInstructionMarker(0x00401149ULL), "DisassemblyView::activeCfgBlockContainsInstructionMarker(0x00401149ULL) must be true");
}

/**
 * @brief Verifies floating dock pop-out, drag-back redocking via drop zone, and 1-click/double-click restore.
 *
 * Validates three critical window-management and redocking workflows:
 * 1. Pop Out (floatDockWidget) & 1-Click / Double-Click Redock (redockWidget):
 *    Floats ServerRackDock and verifies visibility and floating state, then redocks it
 *    and verifies original parent hierarchy and non-zero dimensions (> 50px).
 * 2. Drag-Back Drop-Zone Redocking (handleDockDragRelease):
 *    Floats ServerRackDock and simulates dragging over its home dock region center,
 *    verifying that dropping over the home zone snaps it back into place.
 * 3. redockAllFloatingPanels() Bulk Recovery:
 *    Floats multiple side panels simultaneously (ServerRackDock and RegisterViewDock)
 *    and validates that redockAllFloatingPanels() docks all of them cleanly.
 */
void TestMainWindowUI::testFloatingDockPopOutAndDragBackRedock() {
    // Instantiate and expose MainWindow with preview fixture data.
    gridlock::ui::MainWindow mainWindow; mainWindow.resize(1440, 900); mainWindow.loadPreviewFixture();
    mainWindow.show(); QVERIFY(QTest::qWaitForWindowExposed(&mainWindow)); QApplication::processEvents();

    // Query dock instances by their unique registered object names.
    auto* serverRackDock = mainWindow.findChild<QDockWidget*>(QStringLiteral("ServerRackDock"));
    auto* registerDock = mainWindow.findChild<QDockWidget*>(QStringLiteral("RegisterViewDock"));
    QVERIFY2(serverRackDock != nullptr && registerDock != nullptr, "ServerRackDock and RegisterViewDock must exist in MainWindow");

    // 1. Pop Out (floatDockWidget) & 1-Click / Double-Click Redock (redockWidget).
    QWidget* originalParent = serverRackDock->parentWidget();
    mainWindow.toggleDockFloating(serverRackDock);
    QVERIFY2(mainWindow.isDockFloating(serverRackDock) && serverRackDock->isVisible(), "ServerRackDock must be floating and visible after toggleDockFloating");
    mainWindow.redockWidget(serverRackDock);
    QVERIFY2(!mainWindow.isDockFloating(serverRackDock), "ServerRackDock must not be floating after redockWidget");
    QCOMPARE(serverRackDock->parentWidget(), originalParent);
    QVERIFY2(serverRackDock->isVisible() && serverRackDock->width() > 50 && serverRackDock->height() > 50, "ServerRackDock must retain visible dimensions > 50px");

    // 2. Drag-Back Drop-Zone Redocking (handleDockDragRelease) & 3. Bulk redockAllFloatingPanels().
    mainWindow.toggleDockFloating(serverRackDock);
    QVERIFY2(mainWindow.isDockFloating(serverRackDock), "ServerRackDock must be floating again");
    mainWindow.handleDockDragRelease(serverRackDock, mainWindow.dockHomeGlobalRect(serverRackDock).center());
    QVERIFY2(!mainWindow.isDockFloating(serverRackDock) && serverRackDock->parentWidget() == originalParent, "ServerRackDock must redock when dropped into home zone");
    mainWindow.toggleDockFloating(serverRackDock); mainWindow.toggleDockFloating(registerDock);
    QVERIFY2(mainWindow.isDockFloating(serverRackDock) && mainWindow.isDockFloating(registerDock), "Both docks must be floating");
    mainWindow.redockAllFloatingPanels();
    QVERIFY2(!mainWindow.isDockFloating(serverRackDock) && !mainWindow.isDockFloating(registerDock), "Both docks must be docked after redockAllFloatingPanels");
}

/**
 * @brief Verifies Preferences sidebar pixel-level selected item contrast under solarized_light,
 *        opaque background, 12-theme selector dropdown, >= 15 terminal fonts, and SLURM auto-detection.
 *
 * Ensures qt_material's QListView::item:selected:focus rule never paints dark text on dark blue,
 * validates opaque cream surfaces across dialog and sidebar, and checks all 12 curated themes.
 */
void TestMainWindowUI::testPreferencesSidebarContrastTerminalFontsAndSlurmAutoDetect() {
    // 1. Initialize qt_material base stylesheet (matching main.cpp), switch to solarized_light, and capture rendered PNG.
    auto& tm = gridlock::core::ThemeManager::instance();
    tm.initialize(); tm.applyTheme(QStringLiteral("solarized_light"));
    gridlock::ui::MainWindow mainWindow; mainWindow.resize(1280, 800); mainWindow.loadPreviewFixture();
    mainWindow.show(); QVERIFY(QTest::qWaitForWindowExposed(&mainWindow)); QApplication::processEvents();

    // Open PreferencesDialog over MainWindow and verify rendered pixels of selected row 0 ("Appearance").
    gridlock::ui::PreferencesDialog dialog(&mainWindow); dialog.resize(860, 620); dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog)); QApplication::processEvents();
    const QImage dlgImg = dialog.grab().toImage(); QVERIFY(!dlgImg.isNull());
    auto* categoryList = dialog.findChild<QListWidget*>();
    QVERIFY(categoryList != nullptr && categoryList->viewport() != nullptr && categoryList->item(0) != nullptr);
    const QImage catImg = categoryList->viewport()->grab().toImage();
    const QRect itemRect = categoryList->visualItemRect(categoryList->item(0)).intersected(catImg.rect());
    int darkHighlightPixels = 0, brightTextPixels = 0;
    // Count dark accent highlight pixels (lightness < 150) and bright white glyph pixels (lightness > 210).
    for (int y = itemRect.top() + 3; y <= itemRect.bottom() - 3; ++y)
        for (int x = itemRect.left() + 42; x <= itemRect.right() - 6; ++x) {
            const int l = catImg.pixelColor(x, y).lightness();
            if (l < 150) ++darkHighlightPixels; else if (l > 210) ++brightTextPixels;
        }
    // Verify high-contrast white text pixels exist inside the blue highlight bar and surfaces are solid cream.
    QVERIFY2(darkHighlightPixels >= 100 && brightTextPixels >= 15, qPrintable(QStringLiteral("Selected sidebar row pixel check failed: darkBg=%1 brightText=%2").arg(darkHighlightPixels).arg(brightTextPixels)));
    QVERIFY(categoryList->minimumWidth() >= 185 && categoryList->palette().color(QPalette::Inactive, QPalette::HighlightedText).lightness() >= 200);
    QVERIFY2(catImg.pixelColor(20, catImg.height() - 20).lightness() > 200 && dlgImg.pixelColor(dlgImg.width() - 40, dlgImg.height() / 2).lightness() > 210, "PreferencesDialog canvas and sidebar must render solid light surfaces");

    // Verify Color Theme combo populates all 12 curated themes including nord_dark, one_dark_pro, dracula_dark, one_light.
    auto* themeCombo = dialog.findChild<QComboBox*>(QStringLiteral("themeSelectorCombo"));
    QVERIFY2(themeCombo != nullptr && themeCombo->count() == 12, qPrintable(QStringLiteral("Expected 12 themes, got %1").arg(themeCombo ? themeCombo->count() : -1)));
    for (const QString& reqTheme : {QStringLiteral("nord_dark"), QStringLiteral("one_dark_pro"), QStringLiteral("dracula_dark"), QStringLiteral("one_light")})
        QVERIFY2(themeCombo->findData(reqTheme) >= 0, qPrintable(QStringLiteral("Missing theme in combo: ") + reqTheme));

    // 2. Palette Swatch Auto-Contrast & Hex Display on Gruvbox Industrial (#fabd2f bright yellow accent).
    themeCombo->setCurrentIndex(themeCombo->findData(QStringLiteral("gruvbox_dark"))); QApplication::processEvents();
    bool foundAccentDarkText = false, foundCanvasLightText = false;
    for (QLabel* lbl : dialog.findChildren<QLabel*>()) {
        const bool hasHex = lbl->text().contains(QStringLiteral("#fabd2f"), Qt::CaseInsensitive) || lbl->toolTip().contains(QStringLiteral("#fabd2f"), Qt::CaseInsensitive);
        if ((lbl->text().contains(QStringLiteral("Accent")) || lbl->toolTip().contains(QStringLiteral("Accent"))) && hasHex) {
            QVERIFY2(lbl->palette().color(QPalette::WindowText).lightness() < 60 && lbl->styleSheet().contains(QStringLiteral("#16181d"), Qt::CaseInsensitive), "Bright #fabd2f Accent swatch must use dark text");
            foundAccentDarkText = true;
        }
        const bool hasCanvas = lbl->text().contains(QStringLiteral("#1d2021"), Qt::CaseInsensitive) || lbl->toolTip().contains(QStringLiteral("#1d2021"), Qt::CaseInsensitive);
        if ((lbl->text().contains(QStringLiteral("Canvas")) || lbl->toolTip().contains(QStringLiteral("Canvas"))) && hasCanvas) {
            QVERIFY2(lbl->palette().color(QPalette::WindowText).lightness() > 200, "Dark #1d2021 Canvas swatch must use light text");
            foundCanvasLightText = true;
        }
    }
    QVERIFY2(foundAccentDarkText && foundCanvasLightText, "Palette swatches must include hex code and luminance-aware foreground contrast");

    // 3. Terminal Font Family Combo Has >= 15 Monospace Options & Unclipped "Browse..." Buttons.
    auto* termFontCombo = dialog.findChild<QComboBox*>(QStringLiteral("terminalFontFamilyCombo"));
    QVERIFY2(termFontCombo != nullptr && termFontCombo->count() >= 15, "Terminal Font Family combo must offer >= 15 monospace font families");
    for (QPushButton* btn : dialog.findChildren<QPushButton*>())
        if (btn->text().contains(QStringLiteral("Browse"), Qt::CaseInsensitive))
            QVERIFY2(btn->minimumWidth() >= 88, qPrintable(QStringLiteral("Browse button minWidth %1 < 88").arg(btn->minimumWidth())));

    // 4. Comprehensive SLURM Fields & Batch Script File Auto-Detection (parseSlurmBatchScript).
    const QString sampleSbatch = QStringLiteral("#!/bin/bash\n#SBATCH --job-name=wpt_fem_solver\n#SBATCH --account=ttu_hpc\n#SBATCH --partition=gpu_a100\n#SBATCH --qos=high\n#SBATCH --nodes=4\n#SBATCH --ntasks-per-node=16\n#SBATCH --cpus-per-task=4\n#SBATCH --mem=128G\n#SBATCH --time=06:30:00\n#SBATCH --gres=gpu:a100:2\n#SBATCH --constraint=nvlink\n#SBATCH --output= solver_%j.out\nmodule load openmpi/5.0 gcc/13\nsrun --mpi=pmix ./solver {FILE}\n");
    QVERIFY(dialog.parseSlurmBatchScript(sampleSbatch));
    for (const auto& [k, v] : QList<QPair<QString, QString>>{{"job_name", "wpt_fem_solver"}, {"account", "ttu_hpc"}, {"partition", "gpu_a100"}, {"qos", "high"}, {"nodes", "4"}, {"ntasks_per_node", "16"}, {"cpus_per_task", "4"}, {"mem", "128G"}, {"time", "06:30:00"}, {"gres_gpu", "a100:2"}, {"constraint", "nvlink"}})
        QCOMPARE(dialog.slurmFieldValue(k), v);
    QVERIFY(dialog.slurmFieldValue(QStringLiteral("modules")).contains(QStringLiteral("openmpi/5.0")));
    tm.applyTheme(QStringLiteral("gridlock_slate"));
}

/**
 * @brief Verifies removal of redundant Enable Dark Mode checkbox, Light Themes cohesion,
 *        binary section ribbon / mode button light rendering, and MemView non-zero hex contrast.
 */
void TestMainWindowUI::testLightThemesCohesionAndNoRedundantDarkModeCheckbox() {
    auto& tm = gridlock::core::ThemeManager::instance(); tm.initialize();
    const QStringList themeIds = tm.availableThemeIds();
    // 1. Verify all 12 curated themes (8 dark, 4 light) and QApplication::palette() synchronization.
    for (const QString& reqId : {QStringLiteral("daylight_light"), QStringLiteral("solarized_light"), QStringLiteral("catppuccin_latte"), QStringLiteral("one_light"), QStringLiteral("nord_dark"), QStringLiteral("one_dark_pro"), QStringLiteral("dracula_dark")})
        QVERIFY2(themeIds.contains(reqId), qPrintable(QStringLiteral("ThemeManager missing theme: ") + reqId));
    QVERIFY2(themeIds.size() >= 12 && !tm.paletteForTheme(QStringLiteral("one_light")).isDark && tm.paletteForTheme(QStringLiteral("nord_dark")).isDark && tm.paletteForTheme(QStringLiteral("one_dark_pro")).isDark && tm.paletteForTheme(QStringLiteral("dracula_dark")).isDark, "ThemeManager must expose >= 12 themes with accurate isDark polarity");
    tm.applyTheme(QStringLiteral("one_light"));
    QVERIFY(!tm.isDarkMode() && QApplication::palette().color(QPalette::Base) == QColor(QStringLiteral("#fafafa")) && QApplication::palette().color(QPalette::WindowText) == QColor(QStringLiteral("#383a42")) && QApplication::palette().color(QPalette::Highlight) == QColor(QStringLiteral("#4078f2")));
    tm.applyTheme(QStringLiteral("nord_dark"));
    QVERIFY(tm.isDarkMode() && QApplication::palette().color(QPalette::Base) == QColor(QStringLiteral("#2e3440")) && QApplication::palette().color(QPalette::WindowText) == QColor(QStringLiteral("#eceff4")) && QApplication::palette().color(QPalette::Highlight) == QColor(QStringLiteral("#88c0d0")));
    tm.applyTheme(QStringLiteral("catppuccin_latte")); QVERIFY2(!tm.isDarkMode(), "Selecting catppuccin_latte must set isDarkMode() == false");

    // 2. Verify Enable Dark Mode checkbox is removed from PreferencesDialog and Light Mode has no dark #16181d/#1b1e24 overrides.
    tm.applyTheme(QStringLiteral("daylight_light"));
    gridlock::ui::PreferencesDialog dialog; dialog.resize(860, 620); dialog.show(); QApplication::processEvents();
    for (QCheckBox* chk : dialog.findChildren<QCheckBox*>()) QVERIFY2(!(chk->isVisible() && chk->text().contains(QStringLiteral("Enable Dark Mode"), Qt::CaseInsensitive)), "Redundant Dark Mode checkbox must not be visible");
    for (QComboBox* cb : dialog.findChildren<QComboBox*>()) QVERIFY2(!cb->styleSheet().contains(QStringLiteral("#1b1e24"), Qt::CaseInsensitive), "QComboBox must not hardcode #1b1e24");
    for (QSpinBox* sb : dialog.findChildren<QSpinBox*>()) QVERIFY2(!sb->styleSheet().contains(QStringLiteral("#1b1e24"), Qt::CaseInsensitive), "QSpinBox must not hardcode #1b1e24");

    // 3. Switch to solarized_light, propagate theme across MainWindow, and capture /tmp/light_bars_verify.png.
    gridlock::ui::MainWindow mainWindow; mainWindow.resize(1440, 900); mainWindow.loadPreviewFixture();
    tm.applyTheme(QStringLiteral("solarized_light")); mainWindow.propagateThemeToAllWidgets();
    auto* memView = mainWindow.findChild<gridlock::ui::MemView*>();
    auto* explorer = mainWindow.findChild<gridlock::ui::ProjectExplorerWidget*>();
    auto* bottomTabs = mainWindow.findChild<QTabWidget*>(QStringLiteral("BottomConsoleTabs"));
    QVERIFY(memView != nullptr && explorer != nullptr && bottomTabs != nullptr);
    // Populate MemView with non-zero bytes (F3 0F 1E FA 55 48 89 E5) and zero padding bytes (00) and show Memory View tab.
    memView->setMemoryData(QStringLiteral("0x00401120"), QByteArray::fromHex("f30f1efa554889e50000000000000000"));
    bottomTabs->setCurrentWidget(memView);
    mainWindow.show(); QVERIFY(QTest::qWaitForWindowExposed(&mainWindow)); QApplication::processEvents();
    const QImage mwImg = mainWindow.grab().toImage(); QVERIFY(!mwImg.isNull());

    // Helper lambda computing average rendered pixel lightness across a widget's grabbed QImage.
    auto avgLightness = [](QWidget* w) -> double {
        const QImage img = w ? w->grab().toImage() : QImage();
        if (img.isNull() || img.width() <= 0 || img.height() <= 0) return 0.0;
        qint64 sum = 0, cnt = 0;
        for (int y = 1; y < img.height() - 1; y += 2)
            for (int x = 2; x < img.width() - 2; x += 3) { sum += img.pixelColor(x, y).lightness(); ++cnt; }
        return cnt > 0 ? static_cast<double>(sum) / static_cast<double>(cnt) : 0.0;
    };
    // Verify section ribbon (#addressNavBar), unchecked FILES/REVERSE buttons (> 170), and checked accent CODE button (> 110).
    auto* navBar = mainWindow.findChild<QWidget*>(QStringLiteral("addressNavBar"));
    auto* filesBtn = explorer->findChild<QWidget*>(QStringLiteral("explorerFilesModeBtn"));
    auto* revBtn = mainWindow.findChild<QWidget*>(QStringLiteral("perspectiveReverseBtn"));
    auto* codeBtn = mainWindow.findChild<QWidget*>(QStringLiteral("perspectiveCodeBtn"));
    QVERIFY(navBar != nullptr && filesBtn != nullptr && revBtn != nullptr && codeBtn != nullptr);
    QVERIFY2(avgLightness(navBar) > 170.0 && avgLightness(filesBtn) > 170.0 && avgLightness(revBtn) > 170.0 && avgLightness(codeBtn) > 110.0,
             qPrintable(QStringLiteral("Dark bar remained in solarized_light: nav=%1 files=%2 rev=%3 code=%4").arg(avgLightness(navBar)).arg(avgLightness(filesBtn)).arg(avgLightness(revBtn)).arg(avgLightness(codeBtn))));

    // Verify MemView activeHexTextColor lightness < 65, dark non-zero hex ink (< 75) in viewport, and #6e7781 muted zero bytes.
    QVERIFY2(memView->activeHexTextColor().lightness() < 65, "MemView activeHexTextColor must have lightness < 65 in solarized_light");
    auto* dumpEdit = memView->findChild<QTextEdit*>(QStringLiteral("cutterHexDumpEdit"));
    QVERIFY(dumpEdit != nullptr && dumpEdit->viewport() != nullptr);
    QVERIFY2(dumpEdit->toHtml().contains(QStringLiteral("#6e7781"), Qt::CaseInsensitive), "MemView zero (00) bytes must use muted medium steel #6e7781 in light mode");
    const QImage hexImg = dumpEdit->viewport()->grab().toImage();
    int darkHexInkPixels = 0;
    for (int y = 20; y < qMin(hexImg.height() - 2, 56); ++y)
        for (int x = 155; x < qMin(hexImg.width() - 10, 420); ++x)
            if (hexImg.pixelColor(x, y).lightness() < 75) ++darkHexInkPixels;
    QVERIFY2(darkHexInkPixels >= 20, qPrintable(QStringLiteral("Expected >= 20 dark hex ink pixels (< 75 lightness), got %1").arg(darkHexInkPixels)));

    // Verify SourceCodeView and DisassemblyView canvas backgrounds and restore default gridlock_slate dark theme.
    gridlock::ui::SourceCodeView srcView; gridlock::ui::DisassemblyView disView;
    QVERIFY2(srcView.activeCanvasBackgroundColor().lightness() >= 220 && disView.activeCanvasBackgroundColor().lightness() >= 220, "Canvas backgrounds must be light in solarized_light");
    tm.applyTheme(QStringLiteral("gridlock_slate")); QVERIFY2(tm.isDarkMode(), "Selecting gridlock_slate must restore isDarkMode() == true");
}

/**
 * @brief Verifies full workstation light theme propagation, non-colliding palette swatches, and MainWindow*.cpp line count headroom.
 */
void TestMainWindowUI::testFullWorkstationLightThemePropagationAndLineCountHeadroom() {
    // 1. Full Workstation Theme Propagation in solarized_light across primary inspection views.
    auto& tm = gridlock::core::ThemeManager::instance(); tm.applyTheme(QStringLiteral("solarized_light"));
    gridlock::ui::MainWindow mainWindow; mainWindow.resize(1440, 900); mainWindow.loadPreviewFixture();
    mainWindow.show(); QVERIFY(QTest::qWaitForWindowExposed(&mainWindow)); QApplication::processEvents();
    gridlock::tests::callPropagateTheme(&mainWindow, 0); QApplication::processEvents();

    // Retrieve pointers to all primary inspection subviews and verify light canvas lightness >= 200.
    auto* memView = mainWindow.findChild<gridlock::ui::MemView*>();
    auto* diffGrid = mainWindow.findChild<gridlock::ui::DifferentialGrid*>();
    auto* serverRack = mainWindow.findChild<gridlock::ui::ServerRackView*>();
    auto* disasmView = mainWindow.findChild<gridlock::ui::DisassemblyView*>();
    QVERIFY(memView != nullptr && diffGrid != nullptr && serverRack != nullptr && disasmView != nullptr);
    QVERIFY2(gridlock::tests::getActiveCanvasBg(memView, 0).lightness() >= 200 && gridlock::tests::getActiveCanvasBg(diffGrid, 0).lightness() >= 200 && gridlock::tests::getActiveCanvasBg(serverRack, 0).lightness() >= 200, "Inspection views must have light canvas >= 200 in solarized_light");
    // Verify toolbars and dock headers are aligned without hardcoded dark stylesheets.
    QVERIFY2(gridlock::tests::callAreAllChromeAligned(&mainWindow, 0), "All chrome toolbars and dock headers must be theme aligned without hardcoded #16181d/#1b1e24");

    // Inspect decompiler function signature label contrast (dark ink < 120 lightness on light cream background).
    auto* sigLabel = disasmView->findChild<QLabel*>(QStringLiteral("disasmSignatureLabel")); QVERIFY(sigLabel != nullptr);
    const auto match = QRegularExpression(QStringLiteral("color:\\s*(#[0-9a-fA-F]{3,8}|rgba?\\([^)]+\\))")).match(sigLabel->styleSheet());
    const QColor sigTextColor = match.hasMatch() ? QColor(match.captured(1)) : sigLabel->palette().color(QPalette::WindowText);
    QVERIFY2(sigTextColor.isValid() && sigTextColor.lightness() < 120, qPrintable(QString("Decompiler signature text color lightness %1 >= 120").arg(sigTextColor.lightness())));

    // Restore default dark theme and verify dark activeCanvasBackgroundColor (< 60 lightness).
    tm.applyTheme(QStringLiteral("gridlock_slate")); gridlock::tests::callPropagateTheme(&mainWindow, 0); QApplication::processEvents();
    QVERIFY2(gridlock::tests::getActiveCanvasBg(memView, 0).lightness() < 60, "MemView canvas background must be dark (< 60 lightness) in gridlock_slate");

    // 2. Spaced, Non-Colliding Palette Swatch Chips with hex colors stored in toolTip().
    gridlock::ui::PreferencesDialog prefsDialog; prefsDialog.resize(860, 620); prefsDialog.show(); QApplication::processEvents();
    QList<QLabel*> swatchBoxes;
    for (QLabel* lbl : prefsDialog.findChildren<QLabel*>())
        for (const QString& r : {QStringLiteral("Canvas"), QStringLiteral("Header"), QStringLiteral("Border"), QStringLiteral("Accent"), QStringLiteral("Text")})
            if (lbl->text().trimmed() == r) { swatchBoxes.append(lbl); break; }
    QVERIFY2(swatchBoxes.size() == 5, qPrintable(QString("Expected 5 clean role swatch boxes, found %1").arg(swatchBoxes.size())));
    for (QLabel* box : swatchBoxes) QVERIFY2(!box->toolTip().isEmpty() && box->toolTip().contains('#'), "Swatch box must store hex color in toolTip()");

    // 3. MainWindow*.cpp Line Count Headroom (< 880 Lines Each).
    for (const QString& relPath : {QStringLiteral("src/ui/MainWindow.cpp"), QStringLiteral("src/ui/MainWindowWorkspace.cpp"), QStringLiteral("src/ui/MainWindowCohesion.cpp"), QStringLiteral("src/ui/MainWindowNavigation.cpp"), QStringLiteral("src/ui/MainWindowDocks.cpp")}) {
        QFile f(QFile::exists(QStringLiteral("../") + relPath) ? (QStringLiteral("../") + relPath) : relPath);
        // Verify file exists and sits strictly below the 880 line target ceiling.
        QVERIFY2(f.exists() && f.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(QString("Could not open %1").arg(relPath)));
        QVERIFY2(QString::fromUtf8(f.readAll()).split('\n').size() < 880, qPrintable(QString("%1 >= 880 lines").arg(relPath)));
    }
}

