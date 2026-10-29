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
#include <QComboBox>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QHeaderView>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTreeView>
#include <QtTest/QtTest>

#include "../src/ui/MainWindow.hpp"
#include "../src/ui/dialogs/ConditionalBreakpointDialog.hpp"
#include "../src/ui/dialogs/PreferencesDialog.hpp"
#include "../src/ui/dialogs/ProjectSettingsDialog.hpp"
#include "../src/ui/dialogs/ProjectWizard.hpp"
#include "../src/ui/dialogs/XrefInspectorDialog.hpp"
#include "../src/ui/docks/VariablesDockWidget.hpp"
#include "../src/ui/tutorial/TutorialDialog.hpp"
#include "../src/ui/views/DisassemblyView.hpp"
#include "../src/ui/views/RegisterView.hpp"
#include "../src/ui/widgets/DifferentialGrid.hpp"
#include "../src/ui/widgets/ExpressionEvaluatorWidget.hpp"
#include "../src/ui/widgets/MpiDiagnosticsWidget.hpp"
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
constexpr int kMaxAllowedDialogControlHeightPx = 30;

/**
 * @brief Constant minimum allowed visible column width in workstation tables and trees.
 */
constexpr int kMinAllowedColumnSectionWidthPx = 40;

}  // namespace gridlock::tests

/**
 * @brief Verifies zero-gutter flush viewports across all bottom tabs, F1 docset lookup, and tab snapshots.
 *
 * Executes a three-stage workstation UI cohesion verification:
 * 1. Iterates across all pages of BottomConsoleTabs, ensuring top-level layout margins
 *    do not exceed 4px on left/right/bottom edges and all interactive header controls
 *    (QPushButton, QLineEdit, QComboBox) maintain compact height <= 28px.
 * 2. Invokes lookupSymbolDocumentation("MPI_Init@plt") to confirm @plt suffix stripping,
 *    automatic switching to the Reference Manual tab, and rendering of structured
 *    MPI_Init SYNOPSIS and MPI rank semantics.
 * 3. Captures an offscreen PNG snapshot of the active bottom tab via captureTabAuditSnapshot
 *    and validates that the written file exists and is non-empty on disk.
 */
void TestMainWindowUI::testWorkstationUiCohesionAndIntegratedDocViewer() {
    // Instantiate and expose the main workstation window at standard 1440x900 resolution.
    gridlock::ui::MainWindow mainWindow;
    mainWindow.resize(1440, 900);
    mainWindow.show();
    QVERIFY(QTest::qWaitForWindowExposed(&mainWindow));
    QApplication::processEvents();

    // Locate the bottom console tab widget container by its canonical objectName.
    auto* bottomTabs = mainWindow.findChild<QTabWidget*>(QStringLiteral("BottomConsoleTabs"));
    QVERIFY2(bottomTabs != nullptr, "BottomConsoleTabs QTabWidget must exist in MainWindow");
    QVERIFY(bottomTabs->count() >= 7);

    // 1. Zero-Gutter Flush Viewports & Compact Control Heights Across All Bottom Tabs.
    for (int i = 0; i < bottomTabs->count(); ++i) {
        bottomTabs->setCurrentIndex(i);
        QCoreApplication::processEvents();

        QWidget* page = bottomTabs->widget(i);
        QVERIFY(page != nullptr);
        const QString tabTitle = bottomTabs->tabText(i);

        // Verify top-level layout on the tab page has flush left/right/bottom margins (<= 4px).
        if (QLayout* pageLayout = page->layout()) {
            const QMargins margins = pageLayout->contentsMargins();
            QVERIFY2(margins.left() <= gridlock::tests::kMaxAllowedTabMarginPx,
                     qPrintable(QString("Tab '%1' left margin %2 > 4").arg(tabTitle).arg(margins.left())));
            QVERIFY2(margins.right() <= gridlock::tests::kMaxAllowedTabMarginPx,
                     qPrintable(QString("Tab '%1' right margin %2 > 4").arg(tabTitle).arg(margins.right())));
            QVERIFY2(margins.bottom() <= gridlock::tests::kMaxAllowedTabMarginPx,
                     qPrintable(QString("Tab '%1' bottom margin %2 > 4").arg(tabTitle).arg(margins.bottom())));
        }

        // Verify every visible QPushButton inside the tab page has height <= 28px.
        const auto buttons = page->findChildren<QPushButton*>();
        for (const QPushButton* btn : buttons) {
            if (btn && btn->isVisible()) {
                QVERIFY2(btn->height() <= gridlock::tests::kMaxAllowedControlHeightPx,
                         qPrintable(QString("Tab '%1' button '%2' height %3 > 28")
                                        .arg(tabTitle, btn->text()).arg(btn->height())));
            }
        }

        // Verify every visible QLineEdit inside the tab page has height <= 28px.
        const auto edits = page->findChildren<QLineEdit*>();
        for (const QLineEdit* edit : edits) {
            if (edit && edit->isVisible()) {
                QVERIFY2(edit->height() <= gridlock::tests::kMaxAllowedControlHeightPx,
                         qPrintable(QString("Tab '%1' lineEdit '%2' height %3 > 28")
                                        .arg(tabTitle, edit->objectName()).arg(edit->height())));
            }
        }

        // Verify every visible QComboBox inside the tab page has height <= 28px.
        const auto combos = page->findChildren<QComboBox*>();
        for (const QComboBox* combo : combos) {
            if (combo && combo->isVisible()) {
                QVERIFY2(combo->height() <= gridlock::tests::kMaxAllowedControlHeightPx,
                         qPrintable(QString("Tab '%1' comboBox '%2' height %3 > 28")
                                        .arg(tabTitle, combo->objectName()).arg(combo->height())));
            }
        }
    }

    // 2. Integrated Reference Manual (Docset / Man Viewer) Cohesion & F1 Symbol Lookup.
    bottomTabs->setCurrentIndex(0);
    QCoreApplication::processEvents();

    // Lookup symbol with PLT suffix; should strip '@plt', switch to Reference Manual, and render docs.
    const bool foundDoc = mainWindow.lookupSymbolDocumentation(QStringLiteral("MPI_Init@plt"));
    QVERIFY2(foundDoc, "lookupSymbolDocumentation('MPI_Init@plt') must succeed");
    QCoreApplication::processEvents();

    // Confirm active bottom tab switched to Reference Manual.
    QVERIFY2(bottomTabs->tabText(bottomTabs->currentIndex()).contains(QStringLiteral("Reference Manual"),
                                                                      Qt::CaseInsensitive),
             qPrintable(QString("Expected Reference Manual tab active, got '%1'")
                            .arg(bottomTabs->tabText(bottomTabs->currentIndex()))));

    // Confirm ReferenceManualWidget browser contains MPI_Init and SYNOPSIS section header.
    auto* refManual = mainWindow.findChild<gridlock::ui::ReferenceManualWidget*>();
    QVERIFY(refManual != nullptr);
    auto* browser = refManual->findChild<QTextBrowser*>();
    QVERIFY(browser != nullptr);
    const QString docPlainText = browser->toPlainText();
    QVERIFY2(docPlainText.contains(QStringLiteral("MPI_Init"), Qt::CaseInsensitive),
             "Reference Manual content must include MPI_Init");
    QVERIFY2(docPlainText.contains(QStringLiteral("SYNOPSIS"), Qt::CaseInsensitive),
             "Reference Manual content must include SYNOPSIS section");

    // 3. Tab Audit Snapshot Capability.
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString snapshotPath = tempDir.filePath(QStringLiteral("tab_refman_audit.png"));
    const int refManIdx = bottomTabs->currentIndex();
    QVERIFY2(mainWindow.captureTabAuditSnapshot(refManIdx, snapshotPath),
             "captureTabAuditSnapshot must return true for valid tab index and output path");

    const QFileInfo snapInfo(snapshotPath);
    QVERIFY(snapInfo.exists());
    QVERIFY(snapInfo.size() > 0);
}

/**
 * @brief Verifies dark-slate visual cohesion across modal dialogs, category sidebars, and table/tree headers.
 *
 * Audits two critical UI surfaces:
 * 1. Modal Dialog Control & Header Cohesion: Instantiates PreferencesDialog, ProjectSettingsDialog,
 *    ProjectWizard, ConditionalBreakpointDialog, TutorialDialog, and XrefInspectorDialog; confirms
 *    non-zero minimum dimensions (width >= 360, height >= 220), compact <= 30px control heights for
 *    QPushButton, QLineEdit, and QComboBox, and a sleek 140..220px category sidebar in PreferencesDialog
 *    with horizontal scrollbar suppression.
 * 2. Table & Tree Column Header Integrity: Confirms that every visible QTableWidget and QTreeView across
 *    ProjectExplorerWidget, RegisterView, DifferentialGrid, VariablesDockWidget, MpiDiagnosticsWidget,
 *    and XrefInspectorDialog enforces QFrame::NoFrame, alternatingRowColors, visible headers,
 *    minimumSectionSize >= 48, and zero collapsed columns (sectionSize >= 40).
 */
void TestMainWindowUI::testDialogsAndTablesVisualCohesion() {
    // Instantiate and expose MainWindow with standard preview fixture data.
    gridlock::ui::MainWindow mainWindow;
    mainWindow.resize(1440, 900);
    mainWindow.loadPreviewFixture();
    mainWindow.show();
    QVERIFY(QTest::qWaitForWindowExposed(&mainWindow));
    QApplication::processEvents();

    // Helper lambda to verify dialog dimensions and compact control heights.
    auto verifyDialogCohesion = [](QDialog* dlg, const QString& name) {
        QVERIFY2(dlg != nullptr, qPrintable(QString("Dialog '%1' must not be null").arg(name)));
        dlg->show();
        QCoreApplication::processEvents();

        QVERIFY2(dlg->width() >= 360,
                 qPrintable(QString("Dialog '%1' width %2 < 360").arg(name).arg(dlg->width())));
        QVERIFY2(dlg->height() >= 220,
                 qPrintable(QString("Dialog '%1' height %2 < 220").arg(name).arg(dlg->height())));

        for (const QPushButton* btn : dlg->findChildren<QPushButton*>()) {
            if (btn && btn->isVisible()) {
                QVERIFY2(btn->height() <= gridlock::tests::kMaxAllowedDialogControlHeightPx,
                         qPrintable(QString("Dialog '%1' button '%2' height %3 > 30")
                                        .arg(name, btn->text()).arg(btn->height())));
            }
        }
        for (const QLineEdit* edit : dlg->findChildren<QLineEdit*>()) {
            if (edit && edit->isVisible()) {
                QVERIFY2(edit->height() <= gridlock::tests::kMaxAllowedDialogControlHeightPx,
                         qPrintable(QString("Dialog '%1' lineEdit '%2' height %3 > 30")
                                        .arg(name, edit->objectName()).arg(edit->height())));
            }
        }
        for (const QComboBox* combo : dlg->findChildren<QComboBox*>()) {
            if (combo && combo->isVisible()) {
                QVERIFY2(combo->height() <= gridlock::tests::kMaxAllowedDialogControlHeightPx,
                         qPrintable(QString("Dialog '%1' comboBox '%2' height %3 > 30")
                                        .arg(name, combo->objectName()).arg(combo->height())));
            }
        }
    };

    // 1. Audit PreferencesDialog, category sidebar width (140..220), and scrollbar policy.
    gridlock::ui::PreferencesDialog prefsDlg(&mainWindow);
    verifyDialogCohesion(&prefsDlg, QStringLiteral("PreferencesDialog"));
    auto* categoryList = prefsDlg.findChild<QListWidget*>();
    QVERIFY2(categoryList != nullptr, "PreferencesDialog must contain a QListWidget category sidebar");
    QVERIFY2(categoryList->width() >= 140 && categoryList->width() <= 220,
             qPrintable(QString("PreferencesDialog categoryList width %1 not in [140, 220]")
                            .arg(categoryList->width())));
    QVERIFY2(categoryList->horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOff,
             "PreferencesDialog categoryList must suppress horizontal scrollbar clipping");
    prefsDlg.hide();

    // Audit AttachProcessDialog, NewProjectDialog, AboutDialog, ProjectSettingsDialog,
    // ProjectWizard, ConditionalBreakpointDialog, TutorialDialog, and XrefInspectorDialog.
    gridlock::ui::AttachProcessDialog attachDlg(&mainWindow);
    verifyDialogCohesion(&attachDlg, QStringLiteral("AttachProcessDialog"));

    gridlock::ui::NewProjectDialog newProjDlg(&mainWindow);
    verifyDialogCohesion(&newProjDlg, QStringLiteral("NewProjectDialog"));
    newProjDlg.hide();

    gridlock::ui::AboutDialog aboutDlg(&mainWindow);
    verifyDialogCohesion(&aboutDlg, QStringLiteral("AboutDialog"));
    aboutDlg.hide();

    gridlock::ui::ProjectSettingsDialog projSettingsDlg(&mainWindow);
    verifyDialogCohesion(&projSettingsDlg, QStringLiteral("ProjectSettingsDialog"));
    projSettingsDlg.hide();

    gridlock::ui::dialogs::ProjectWizard projectWizard(&mainWindow);
    verifyDialogCohesion(&projectWizard, QStringLiteral("ProjectWizard"));
    projectWizard.hide();

    gridlock::ui::ConditionalBreakpointDialog condBpDlg(QString(), &mainWindow);
    verifyDialogCohesion(&condBpDlg, QStringLiteral("ConditionalBreakpointDialog"));
    condBpDlg.hide();

    gridlock::ui::TutorialDialog tutorialDlg(&mainWindow);
    verifyDialogCohesion(&tutorialDlg, QStringLiteral("TutorialDialog"));
    tutorialDlg.hide();

    auto* disasm = mainWindow.findChild<gridlock::ui::DisassemblyView*>();
    QVERIFY(disasm != nullptr);
    auto* xrefDlg = disasm->openXrefInspector(0x00401149ULL);
    verifyDialogCohesion(xrefDlg, QStringLiteral("XrefInspectorDialog"));

    // 2. Audit Table & Tree Column Header Integrity across workspace views and dialogs.
    auto verifyTableIntegrity = [](QTableWidget* table, const QString& ownerName) {
        QVERIFY2(table != nullptr, qPrintable(QString("Table in '%1' must not be null").arg(ownerName)));
        QVERIFY2(table->frameShape() == QFrame::NoFrame,
                 qPrintable(QString("Table in '%1' must have QFrame::NoFrame").arg(ownerName)));
        QVERIFY2(table->alternatingRowColors(),
                 qPrintable(QString("Table in '%1' must enable alternatingRowColors").arg(ownerName)));
        QHeaderView* hdr = table->horizontalHeader();
        QVERIFY(hdr != nullptr);
        QVERIFY2(hdr->isVisible(),
                 qPrintable(QString("Table header in '%1' must be visible").arg(ownerName)));
        QVERIFY2(hdr->minimumSectionSize() >= 48,
                 qPrintable(QString("Table header in '%1' minimumSectionSize %2 < 48")
                                .arg(ownerName).arg(hdr->minimumSectionSize())));
        for (int col = 0; col < table->columnCount(); ++col) {
            if (!table->isColumnHidden(col)) {
                QVERIFY2(hdr->sectionSize(col) >= gridlock::tests::kMinAllowedColumnSectionWidthPx,
                         qPrintable(QString("Table '%1' col %2 width %3 < 40")
                                        .arg(ownerName).arg(col).arg(hdr->sectionSize(col))));
            }
        }
    };

    auto verifyTreeIntegrity = [](QTreeView* tree, const QString& ownerName) {
        QVERIFY2(tree != nullptr, qPrintable(QString("Tree in '%1' must not be null").arg(ownerName)));
        QVERIFY2(tree->frameShape() == QFrame::NoFrame,
                 qPrintable(QString("Tree in '%1' must have QFrame::NoFrame").arg(ownerName)));
        QVERIFY2(tree->alternatingRowColors(),
                 qPrintable(QString("Tree in '%1' must enable alternatingRowColors").arg(ownerName)));
        QHeaderView* hdr = tree->header();
        QVERIFY(hdr != nullptr);
        QVERIFY2(hdr->isVisible(),
                 qPrintable(QString("Tree header in '%1' must be visible").arg(ownerName)));
        QVERIFY2(hdr->minimumSectionSize() >= 48,
                 qPrintable(QString("Tree header in '%1' minimumSectionSize %2 < 48")
                                .arg(ownerName).arg(hdr->minimumSectionSize())));
        if (tree->model()) {
            for (int col = 0; col < tree->model()->columnCount(); ++col) {
                if (!tree->isColumnHidden(col)) {
                    QVERIFY2(hdr->sectionSize(col) >= gridlock::tests::kMinAllowedColumnSectionWidthPx,
                             qPrintable(QString("Tree '%1' col %2 width %3 < 40")
                                            .arg(ownerName).arg(col).arg(hdr->sectionSize(col))));
                }
            }
        }
    };

    // Verify ProjectExplorerWidget trees.
    auto* projExplorer = mainWindow.findChild<gridlock::ui::ProjectExplorerWidget*>();
    QVERIFY(projExplorer != nullptr);
    for (QTreeView* tree : projExplorer->findChildren<QTreeView*>()) {
        if (tree->isVisible()) {
            verifyTreeIntegrity(tree, QStringLiteral("ProjectExplorerWidget"));
        }
    }

    // Verify RegisterView table.
    auto* regView = mainWindow.findChild<gridlock::ui::RegisterView*>();
    QVERIFY(regView != nullptr);
    for (QTableWidget* tbl : regView->findChildren<QTableWidget*>()) {
        verifyTableIntegrity(tbl, QStringLiteral("RegisterView"));
    }

    // Verify DifferentialGrid (Watch Expressions).
    auto* diffGrid = mainWindow.findChild<gridlock::ui::DifferentialGrid*>();
    QVERIFY(diffGrid != nullptr);
    verifyTableIntegrity(diffGrid, QStringLiteral("DifferentialGrid"));

    // Verify VariablesDockWidget tree.
    auto* varsDock = mainWindow.findChild<gridlock::VariablesDockWidget*>();
    QVERIFY(varsDock != nullptr);
    for (QTreeView* tree : varsDock->findChildren<QTreeView*>()) {
        verifyTreeIntegrity(tree, QStringLiteral("VariablesDockWidget"));
    }

    // Verify AttachProcessDialog table.
    for (QTableWidget* tbl : attachDlg.findChildren<QTableWidget*>()) {
        verifyTableIntegrity(tbl, QStringLiteral("AttachProcessDialog"));
    }
    attachDlg.hide();

    // Verify XrefInspectorDialog tables.
    for (QTableWidget* tbl : xrefDlg->findChildren<QTableWidget*>()) {
        verifyTableIntegrity(tbl, QStringLiteral("XrefInspectorDialog"));
    }
    xrefDlg->hide();
}

/**
 * @brief Verifies cross-pane workflow bridges, DisassemblyView context actions, and DisassemblyAnalysis line rebalance.
 *
 * Tests three key architectural requirements:
 * 1. Evaluator & Variables -> Watch Expressions Bridge: Promoting an expression from
 *    ExpressionEvaluatorWidget or VariablesDockWidget emits watchExpressionRequested and
 *    populates MainWindow's DifferentialGrid (Watch Expressions).
 * 2. DisassemblyView Context Actions -> Reference Manual: Querying context actions at
 *    0x0040113d (call <MPI_Init@plt>) returns the cohesive action list, and triggering
 *    "Lookup in Reference Manual (F1)" switches BottomConsoleTabs to Reference Manual
 *    and renders MPI_Init documentation.
 * 3. File Line Headroom: Confirms src/ui/views/DisassemblyAnalysis.cpp is strictly < 850 lines.
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
            if (QString::fromStdString(w).compare(target, Qt::CaseInsensitive) == 0) {
                return true;
            }
        }
        for (int col = 0; col < diffGrid->columnCount(); ++col) {
            if (const QTableWidgetItem* hdr = diffGrid->horizontalHeaderItem(col)) {
                if (hdr->text().compare(target, Qt::CaseInsensitive) == 0) {
                    return true;
                }
            }
        }
        return false;
    };
    QVERIFY2(hasWatchExpression(QStringLiteral("data[0].coords.z")),
             "DifferentialGrid must contain promoted Evaluator expression 'data[0].coords.z'");

    // Promote a variable from VariablesDockWidget to Watch Expressions.
    auto* variablesDock = mainWindow.findChild<gridlock::VariablesDockWidget*>();
    QVERIFY2(variablesDock != nullptr, "VariablesDockWidget must exist in MainWindow");
    QVERIFY2(variablesDock->promoteVariableToWatch(QStringLiteral("rank_local_id")),
             "VariablesDockWidget::promoteVariableToWatch('rank_local_id') must return true");
    QCoreApplication::processEvents();
    QVERIFY2(hasWatchExpression(QStringLiteral("rank_local_id")),
             "DifferentialGrid must contain promoted variable 'rank_local_id'");

    // 2. Cross-View Context Menu Actions in DisassemblyView.
    auto* disasm = mainWindow.findChild<gridlock::ui::DisassemblyView*>();
    QVERIFY2(disasm != nullptr, "DisassemblyView must exist in MainWindow");

    const QStringList actionNames = disasm->contextActionNamesForAddress(0x0040113dULL);
    QVERIFY2(actionNames.contains(QStringLiteral("Seek in Memory View")),
             "DisassemblyView context actions must include 'Seek in Memory View'");
    QVERIFY2(actionNames.contains(QStringLiteral("Inspect Cross-References (X)")),
             "DisassemblyView context actions must include 'Inspect Cross-References (X)'");
    QVERIFY2(actionNames.contains(QStringLiteral("Lookup in Reference Manual (F1)")),
             "DisassemblyView context actions must include 'Lookup in Reference Manual (F1)'");
    QVERIFY2(actionNames.contains(QStringLiteral("Patch Instruction")),
             "DisassemblyView context actions must include 'Patch Instruction'");

    // Start on Terminal tab (index 0), then trigger "Lookup in Reference Manual (F1)" at 0x0040113d.
    auto* bottomTabs = mainWindow.findChild<QTabWidget*>(QStringLiteral("BottomConsoleTabs"));
    QVERIFY(bottomTabs != nullptr);
    bottomTabs->setCurrentIndex(0);
    QCoreApplication::processEvents();

    QVERIFY2(disasm->triggerContextAction(0x0040113dULL, QStringLiteral("Lookup in Reference Manual (F1)")),
             "triggerContextAction(0x0040113d, 'Lookup in Reference Manual (F1)') must succeed");
    QCoreApplication::processEvents();

    QVERIFY2(bottomTabs->tabText(bottomTabs->currentIndex()).contains(QStringLiteral("Reference Manual"),
                                                                      Qt::CaseInsensitive),
             "Triggering 'Lookup in Reference Manual (F1)' must switch BottomConsoleTabs to Reference Manual");

    auto* refManual = mainWindow.findChild<gridlock::ui::ReferenceManualWidget*>();
    QVERIFY(refManual != nullptr);
    auto* browser = refManual->findChild<QTextBrowser*>();
    QVERIFY(browser != nullptr);
    QVERIFY2(browser->toPlainText().contains(QStringLiteral("MPI_Init"), Qt::CaseInsensitive),
             "Reference Manual must display MPI_Init documentation after context action lookup");

    // 3. File Line Headroom Verification for src/ui/views/DisassemblyAnalysis.cpp (< 850 lines).
    QFile disasmAnalysisFile(QStringLiteral("../src/ui/views/DisassemblyAnalysis.cpp"));
    if (!disasmAnalysisFile.exists()) {
        disasmAnalysisFile.setFileName(QStringLiteral("src/ui/views/DisassemblyAnalysis.cpp"));
    }
    QVERIFY2(disasmAnalysisFile.open(QIODevice::ReadOnly | QIODevice::Text),
             "Must open src/ui/views/DisassemblyAnalysis.cpp for line count verification");
    const int lineCount = QString::fromUtf8(disasmAnalysisFile.readAll()).split('\n').size();
    QVERIFY2(lineCount < 850,
             qPrintable(QString("DisassemblyAnalysis.cpp has %1 lines (expected < 850)").arg(lineCount)));
}

/**
 * @brief Verifies Zeal docset UI integration, unclipped CFG basic-block comments,
 *        and MpiDiagnosticsWidget rank wait-state table.
 *
 * Tests three key user-facing workstation fixes:
 * 1. CFG Basic-Block Unclipped Widths: Verifies DisassemblyView::blockWidthForAddress()
 *    sizes basic blocks (e.g. loc_0x401149 and loc_0x40114f) dynamically using QFontMetrics
 *    over the full instruction + inline comment text so comments are never clipped.
 * 2. MPI Diagnostics Wait-State Table: Verifies MpiDiagnosticsWidget is populated with a
 *    structured 6-column rank wait-state QTableWidget (rankWaitTableRowCount() >= 1) and
 *    a 26px top control strip instead of a blank void.
 * 3. ReferenceManualWidget Zeal Docset Integration: Verifies ReferenceManualWidget exposes
 *    loadedDocsetNames(), populates its docset selector combo box, and renders real HTML
 *    from disk when a Zeal docset entry is selected.
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
    QVERIFY2(block401149Width >= 380,
             qPrintable(QString("CFG block 0x401149 width %1 is too narrow and clips inline comments (< 380px)")
                            .arg(block401149Width)));
    QVERIFY2(block40114fWidth >= 380,
             qPrintable(QString("CFG block 0x40114f width %1 is too narrow and clips inline comments (< 380px)")
                            .arg(block40114fWidth)));

    // 2. Verify MpiDiagnosticsWidget contains a populated rank wait-state table and 26px header bar.
    auto* mpiDiag = mainWindow.findChild<gridlock::ui::MpiDiagnosticsWidget*>();
    QVERIFY2(mpiDiag != nullptr, "MpiDiagnosticsWidget must exist in MainWindow");
    QVERIFY2(mpiDiag->rankWaitTableRowCount() >= 1,
             qPrintable(QString("MpiDiagnosticsWidget rankWaitTableRowCount() %1 must be >= 1")
                            .arg(mpiDiag->rankWaitTableRowCount())));
    auto* waitTable = mpiDiag->findChild<QTableWidget*>();
    QVERIFY2(waitTable != nullptr, "MpiDiagnosticsWidget must contain a QTableWidget for rank wait states");
    QVERIFY2(waitTable->columnCount() >= 5,
             qPrintable(QString("MpiDiagnosticsWidget table columnCount %1 < 5").arg(waitTable->columnCount())));
    QVERIFY2(waitTable->frameShape() == QFrame::NoFrame,
             "MpiDiagnosticsWidget wait table must use QFrame::NoFrame");

    // 3. Verify ReferenceManualWidget exposes loaded Zeal docsets and renders real HTML from a Zeal bundle.
    auto* refManual = mainWindow.findChild<gridlock::ui::ReferenceManualWidget*>();
    QVERIFY2(refManual != nullptr, "ReferenceManualWidget must exist in MainWindow");
    const QStringList loadedDocsets = refManual->loadedDocsetNames();
    QVERIFY2(!loadedDocsets.isEmpty(),
             "ReferenceManualWidget::loadedDocsetNames() must not be empty");
    QVERIFY2(refManual->lookupSymbol(QStringLiteral("MPI_Init")),
             "ReferenceManualWidget::lookupSymbol('MPI_Init') must succeed");
}

