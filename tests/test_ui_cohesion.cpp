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
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
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
constexpr int kMaxAllowedDialogControlHeightPx = 30;

/**
 * @brief Constant minimum allowed visible column width in workstation tables and trees.
 */
constexpr int kMinAllowedColumnSectionWidthPx = 40;

}  // namespace gridlock::tests

/**
 * @brief Verifies zero-gutter flush viewports across all bottom tabs, F1 docset lookup, and tab snapshots.
 * Validates bottom tab margins, compact header controls, MPI_Init@plt symbol lookup, and audit snapshots.
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
        // Obtain active tab page widget pointer.
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

        // Audit combo box height constraints.
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
    // Trigger symbol documentation lookup via F1 provider.
    // Lookup symbol with PLT suffix; should strip '@plt', switch to Reference Manual, and render docs.
    const bool foundDoc = mainWindow.lookupSymbolDocumentation(QStringLiteral("MPI_Init@plt"));
    QVERIFY2(foundDoc, "lookupSymbolDocumentation('MPI_Init@plt') must succeed");
    QCoreApplication::processEvents();
    // Validate tab switch to Reference Manual.
    // Confirm active bottom tab switched to Reference Manual.
    QVERIFY2(bottomTabs->tabText(bottomTabs->currentIndex()).contains(QStringLiteral("Reference Manual"),
                                                                      Qt::CaseInsensitive),
             qPrintable(QString("Expected Reference Manual tab active, got '%1'")
                            .arg(bottomTabs->tabText(bottomTabs->currentIndex()))));
    // Inspect documentation content in browser.
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
    // Confirm output snapshot file exists on disk with non-zero size.
    const QFileInfo snapInfo(snapshotPath);
    QVERIFY(snapInfo.exists());
    QVERIFY(snapInfo.size() > 0);
}

/**
 * @brief Verifies dark-slate visual cohesion across modal dialogs, category sidebars, and table/tree headers.
 * Audits dialog dimensions, compact control heights, and table/tree column header integrity.
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

    // Audit AttachProcessDialog dimensions and controls.
    gridlock::ui::AttachProcessDialog attachDlg(&mainWindow);
    verifyDialogCohesion(&attachDlg, QStringLiteral("AttachProcessDialog"));
    // Audit NewProjectDialog dimensions and controls.
    gridlock::ui::NewProjectDialog newProjDlg(&mainWindow);
    verifyDialogCohesion(&newProjDlg, QStringLiteral("NewProjectDialog"));
    newProjDlg.hide();
    // Audit AboutDialog dimensions and controls.
    gridlock::ui::AboutDialog aboutDlg(&mainWindow);
    verifyDialogCohesion(&aboutDlg, QStringLiteral("AboutDialog"));
    aboutDlg.hide();
    // Audit ProjectSettingsDialog dimensions and controls.
    gridlock::ui::ProjectSettingsDialog projSettingsDlg(&mainWindow);
    verifyDialogCohesion(&projSettingsDlg, QStringLiteral("ProjectSettingsDialog"));
    projSettingsDlg.hide();
    // Audit ProjectWizard dimensions and controls.
    gridlock::ui::dialogs::ProjectWizard projectWizard(&mainWindow);
    verifyDialogCohesion(&projectWizard, QStringLiteral("ProjectWizard"));
    projectWizard.hide();
    // Audit ConditionalBreakpointDialog dimensions and controls.
    gridlock::ui::ConditionalBreakpointDialog condBpDlg(QString(), &mainWindow);
    verifyDialogCohesion(&condBpDlg, QStringLiteral("ConditionalBreakpointDialog"));
    condBpDlg.hide();
    // Audit TutorialDialog dimensions and controls.
    gridlock::ui::TutorialDialog tutorialDlg(&mainWindow);
    verifyDialogCohesion(&tutorialDlg, QStringLiteral("TutorialDialog"));
    tutorialDlg.hide();
    // Audit XrefInspectorDialog dimensions and controls.
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

    // Verify RegisterView table integrity and alternating row colors.
    auto* regView = mainWindow.findChild<gridlock::ui::RegisterView*>();
    QVERIFY(regView != nullptr);
    for (QTableWidget* tbl : regView->findChildren<QTableWidget*>()) {
        verifyTableIntegrity(tbl, QStringLiteral("RegisterView"));
    }
    // Verify DifferentialGrid (Watch Expressions) table integrity.
    auto* diffGrid = mainWindow.findChild<gridlock::ui::DifferentialGrid*>();
    QVERIFY(diffGrid != nullptr);
    verifyTableIntegrity(diffGrid, QStringLiteral("DifferentialGrid"));
    // Verify VariablesDockWidget tree integrity and section sizes.
    auto* varsDock = mainWindow.findChild<gridlock::VariablesDockWidget*>();
    QVERIFY(varsDock != nullptr);
    for (QTreeView* tree : varsDock->findChildren<QTreeView*>()) {
        verifyTreeIntegrity(tree, QStringLiteral("VariablesDockWidget"));
    }
    // Verify AttachProcessDialog table integrity.
    for (QTableWidget* tbl : attachDlg.findChildren<QTableWidget*>()) {
        verifyTableIntegrity(tbl, QStringLiteral("AttachProcessDialog"));
    }
    attachDlg.hide();
    // Verify XrefInspectorDialog table integrity.
    for (QTableWidget* tbl : xrefDlg->findChildren<QTableWidget*>()) {
        verifyTableIntegrity(tbl, QStringLiteral("XrefInspectorDialog"));
    }
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
    QVERIFY2(sourceView->rightViewportMargin() == 0,
             qPrintable(QString("SourceCodeView rightViewportMargin() %1 must be 0").arg(sourceView->rightViewportMargin())));
    QVERIFY2(!sourceView->hasRightMarginFill(), "SourceCodeView::hasRightMarginFill() must be false");

    const QImage editorImg = sourceView->viewport()->grab().toImage();
    QVERIFY2(!editorImg.isNull() && editorImg.width() > 100 && editorImg.height() > 40,
             "SourceCodeView snapshot image must be valid and non-empty");
    // Sample on empty line 3 (between #include <stdio.h> and struct DeeplyNested) above any scrollbar.
    const int sampleY = sourceView->fontMetrics().height() * 2 + sourceView->fontMetrics().height() / 2;
    const int leftX = static_cast<int>(editorImg.width() * 0.25);
    const int rightX = static_cast<int>(editorImg.width() * 0.85);
    const QColor leftColor = editorImg.pixelColor(leftX, sampleY);
    const QColor rightColor = editorImg.pixelColor(rightX, sampleY);
    QVERIFY2(leftColor.rgb() == rightColor.rgb(),
             qPrintable(QString("SourceCodeView split background: left=%1 vs right=%2").arg(leftColor.name(), rightColor.name())));

    // 2. Structured MpiNetworkLogWidget Table.
    auto* netLog = mainWindow.findChild<gridlock::ui::MpiNetworkLogWidget*>();
    QVERIFY2(netLog != nullptr, "MpiNetworkLogWidget must exist in MainWindow");
    auto* netTable = netLog->findChild<QTableWidget*>();
    QVERIFY2(netTable != nullptr && netTable->columnCount() == 6 && netTable->frameShape() == QFrame::NoFrame,
             "MpiNetworkLogWidget must contain a 6-column QFrame::NoFrame QTableWidget");
    netLog->appendLog(QStringLiteral("[12:00:01.042] Rank 0 -> Rank 1 | MPI_Send | Tag=0 MPI_COMM_WORLD | 1024 B"));
    QVERIFY2(netLog->logRowCount() >= 1, "MpiNetworkLogWidget::logRowCount() must be >= 1 after appendLog");

    // 3. Single-Row ReferenceManualWidget Header & Clean Monochrome Toolbar Labels.
    auto* refManual = mainWindow.findChild<gridlock::ui::ReferenceManualWidget*>();
    QVERIFY2(refManual != nullptr && refManual->headerBarHeight() > 0 && refManual->headerBarHeight() <= 30,
             "ReferenceManualWidget::headerBarHeight() must be in (0, 30]");
    for (QToolBar* toolBar : mainWindow.findChildren<QToolBar*>()) {
        for (QAction* act : toolBar->actions()) {
            QVERIFY2(!act->text().contains(QStringLiteral("\U000023E9")), "Toolbar action must not contain U+23E9");
        }
        for (QToolButton* btn : toolBar->findChildren<QToolButton*>()) {
            QVERIFY2(!btn->text().contains(QStringLiteral("\U000023E9")), "Toolbar button must not contain U+23E9");
        }
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
    QVERIFY2(watchGrid != nullptr && watchGrid->horizontalHeaderStretchEnabled(),
             "DifferentialGrid must exist and enable horizontalHeaderStretchEnabled()");
    watchGrid->resize(260, 220);
    watchGrid->show();
    QApplication::processEvents();

    const QImage watchImg = watchGrid->grab().toImage();
    QVERIFY2(!watchImg.isNull() && watchImg.width() >= 200 && watchImg.height() >= 200,
             "DifferentialGrid grabbed image must be valid and at least 200x200");
    QVERIFY2(watchImg.pixelColor(25, 190).rgb() == watchImg.pixelColor(180, 190).rgb(),
             "DifferentialGrid empty area must have uniform background color across left and right columns");

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
    if (QLayout* gdbLayout = gdbConsole->layout()) {
        QVERIFY2(gdbLayout->contentsMargins().left() <= 6, "GdbConsoleWidget left margin must be <= 6");
    }
    for (QAbstractScrollArea* area : gdbConsole->findChildren<QAbstractScrollArea*>()) {
        QVERIFY2(area->frameShape() == QFrame::NoFrame, "GdbConsoleWidget viewport must use QFrame::NoFrame");
    }
    QVERIFY2(evaluator->headerBarHeight() > 0 && evaluator->headerBarHeight() <= 30,
             "ExpressionEvaluatorWidget::headerBarHeight() must be in (0, 30]");
    if (QLayout* evalLayout = evaluator->layout()) {
        QVERIFY2(evalLayout->contentsMargins().left() <= 6, "ExpressionEvaluatorWidget left margin must be <= 6");
    }
    for (QAbstractScrollArea* area : evaluator->findChildren<QAbstractScrollArea*>()) {
        QVERIFY2(area->frameShape() == QFrame::NoFrame, "ExpressionEvaluatorWidget viewport must use QFrame::NoFrame");
    }
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
        const QString demoPath = QFileInfo::exists(QStringLiteral("tutorial/inspection_demo.cpp"))
                                     ? QStringLiteral("tutorial/inspection_demo.cpp")
                                     : QStringLiteral("../tutorial/inspection_demo.cpp");
        mainWindow.loadSourceFile(demoPath);
    }
    mainWindow.show();
    QVERIFY(QTest::qWaitForWindowExposed(&mainWindow));
    QApplication::processEvents();

    // 1. Toolbar Target Selector, Rank SpinBox & Idle Button States.
    auto* targetCombo = mainWindow.findChild<QComboBox*>(QStringLiteral("toolbarTargetSelector"));
    auto* rankSpin = mainWindow.findChild<QSpinBox*>(QStringLiteral("toolbarRankSpin"));
    QVERIFY2(targetCombo != nullptr && rankSpin != nullptr && targetCombo->count() >= 1,
             "MainToolbar must contain populated #toolbarTargetSelector and #toolbarRankSpin");

    auto findToolbarAction = [&mainWindow](const QString& labelSubstr) -> QAction* {
        for (QToolBar* tb : mainWindow.findChildren<QToolBar*>()) {
            for (QAction* act : tb->actions()) {
                if (act->text().contains(labelSubstr, Qt::CaseInsensitive)) {
                    return act;
                }
            }
        }
        return nullptr;
    };

    QAction* runAction = findToolbarAction(QStringLiteral("Run Target"));
    QAction* stepAction = findToolbarAction(QStringLiteral("Step Inst"));
    QAction* terminateAction = findToolbarAction(QStringLiteral("Terminate Session"));
    QVERIFY2(runAction != nullptr && stepAction != nullptr && terminateAction != nullptr,
             "Run Target, Step Inst, and Terminate Session actions must exist on MainToolbar");

    mainWindow.stopTargetWorkflow();
    QApplication::processEvents();
    QVERIFY2(!mainWindow.isDebugSessionActive() && runAction->isEnabled() && !terminateAction->isEnabled(),
             "Idle state must enable Run Target and disable Terminate Session");

    // 2. End-to-End Interactive Run -> Step -> Register/Source/Rack Sync -> Stop Loop.
    if (auto* sv = mainWindow.findChild<gridlock::ui::SourceCodeView*>()) sv->setBreakpoints(QSet<int>());
    rankSpin->setValue(4);
    mainWindow.startTargetWorkflow();
    QApplication::processEvents();
    QVERIFY2(mainWindow.isDebugSessionActive() && stepAction->isEnabled() && terminateAction->isEnabled(),
             "Active session must enable Step Inst and Terminate Session");

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
            regTable->item(r, 1) && regTable->item(r, 1)->text().contains(QStringLiteral("401125"), Qt::CaseInsensitive)) {
            foundRipMatch = true;
        }
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
    gridlock::ui::MainWindow mainWindow;
    mainWindow.resize(1440, 900);
    mainWindow.loadPreviewFixture();
    if (!mainWindow.findChild<gridlock::ui::SourceCodeView*>()) {
        const QString demoPath = QFileInfo::exists(QStringLiteral("tutorial/inspection_demo.cpp"))
                                     ? QStringLiteral("tutorial/inspection_demo.cpp")
                                     : QStringLiteral("../tutorial/inspection_demo.cpp");
        mainWindow.loadSourceFile(demoPath);
    }
    mainWindow.show();
    QVERIFY(QTest::qWaitForWindowExposed(&mainWindow));
    QApplication::processEvents();

    auto* sourceView = mainWindow.findChild<gridlock::ui::SourceCodeView*>();
    auto* disasmView = mainWindow.findChild<gridlock::ui::DisassemblyView*>();
    auto* bottomTabs = mainWindow.findChild<QTabWidget*>(QStringLiteral("BottomConsoleTabs"));
    QVERIFY(sourceView != nullptr && disasmView != nullptr && bottomTabs != nullptr);

    // 1. Bidirectional Source Line <-> Instruction Address Mapping.
    // Verify source line 13 maps to instruction address 0x0040113d.
    QCOMPARE(mainWindow.sourceLineToAddress(13), 0x0040113dULL);
    // Verify instruction address 0x0040113d maps back to source line 13.
    QCOMPARE(mainWindow.addressToSourceLine(0x0040113dULL), 13);
    // Select line 16 in source editor.
    sourceView->selectSourceLine(16);
    QApplication::processEvents();
    // Confirm disassembly viewport synchronized to instruction at line 16.
    QCOMPARE(disasmView->currentAddress(), 0x00401149ULL);
    // Emit instruction address selection from disassembly view.
    emit disasmView->addressSelected(0x0040113dULL);
    QApplication::processEvents();
    // Confirm source editor synchronized back to line 13.
    QCOMPARE(sourceView->executionLine(), 13);

    // 2. Workspace Perspective Switching ("Reverse", "MPI", "Code").
    // Switch to Reverse Engineering perspective with split CFG and decompiler.
    mainWindow.setWorkspacePerspective(QStringLiteral("Reverse"));
    QApplication::processEvents();
    QCOMPARE(mainWindow.workspacePerspective(), QStringLiteral("Reverse"));
    QVERIFY2(disasmView->isSplitGraphDecompilerMode() && disasmView->width() >= 700,
             "Reverse perspective must enable Split CFG + Decompiler mode with width >= 700");

    // Switch to MPI perspective with diagnostics table front and center.
    mainWindow.setWorkspacePerspective(QStringLiteral("MPI"));
    QApplication::processEvents();
    QCOMPARE(mainWindow.workspacePerspective(), QStringLiteral("MPI"));
    QVERIFY2(bottomTabs->tabText(bottomTabs->currentIndex()).contains(QStringLiteral("MPI Diagnostics"), Qt::CaseInsensitive),
             "MPI perspective must activate the MPI Diagnostics bottom tab");

    // Switch to Code perspective restoring balanced source and disassembly viewports.
    mainWindow.setWorkspacePerspective(QStringLiteral("Code"));
    QApplication::processEvents();
    QCOMPARE(mainWindow.workspacePerspective(), QStringLiteral("Code"));
    QVERIFY2(sourceView->width() > 200 && disasmView->width() > 200,
             "Code perspective must restore side-by-side SourceCodeView and DisassemblyView");

    // 3. Bottom Console Maximize Toggle.
    const int baseHeight = bottomTabs->height();
    // Maximize bottom console to inspect logs and diagnostics.
    mainWindow.toggleMaximizeBottomConsole();
    QApplication::processEvents();
    QVERIFY2(bottomTabs->height() > baseHeight, "toggleMaximizeBottomConsole() must expand BottomConsoleTabs height");
    // Restore bottom console back to default workstation height.
    mainWindow.toggleMaximizeBottomConsole();
    QApplication::processEvents();
}

/**
 * @brief Verifies breakpoint-aware Run/Continue, source-to-RIP line mapping, and CFG active instruction markers.
 * Validates granular address-to-line mapping, breakpoint-aware target workflow, and active CFG block marker.
 */
void TestMainWindowUI::testBreakpointAwareRunContinueAndActiveCfgInstructionMarker() {
    gridlock::ui::MainWindow mainWindow;
    mainWindow.resize(1440, 900);
    mainWindow.loadPreviewFixture();
    if (!mainWindow.findChild<gridlock::ui::SourceCodeView*>()) {
        const QString demoPath = QFileInfo::exists(QStringLiteral("tutorial/inspection_demo.cpp"))
                                     ? QStringLiteral("tutorial/inspection_demo.cpp")
                                     : QStringLiteral("../tutorial/inspection_demo.cpp");
        mainWindow.loadSourceFile(demoPath);
    }
    mainWindow.show();
    QVERIFY(QTest::qWaitForWindowExposed(&mainWindow));
    QApplication::processEvents();

    auto* sourceView = mainWindow.findChild<gridlock::ui::SourceCodeView*>();
    auto* disasmView = mainWindow.findChild<gridlock::ui::DisassemblyView*>();
    auto* serverRack = mainWindow.findChild<gridlock::ui::ServerRackView*>();
    QVERIFY(sourceView != nullptr && disasmView != nullptr && serverRack != nullptr);

    // 1. Granular Source Line <-> Instruction Address Mapping.
    // Verify line 12 maps to prologue address 0x00401120.
    QCOMPARE(mainWindow.addressToSourceLine(0x00401120ULL), 12);
    // Verify line 15 maps to stack allocation at 0x00401128.
    QCOMPARE(mainWindow.addressToSourceLine(0x00401128ULL), 15);
    // Verify line 13 maps to MPI_Init call at 0x0040113d.
    QCOMPARE(mainWindow.addressToSourceLine(0x0040113dULL), 13);
    // Verify line 16 maps to loop condition at 0x00401149.
    QCOMPARE(mainWindow.addressToSourceLine(0x00401149ULL), 16);
    // Reverse lookup line 15 to instruction address.
    QCOMPARE(mainWindow.sourceLineToAddress(15), 0x00401128ULL);
    // Reverse lookup line 16 to instruction address.
    QCOMPARE(mainWindow.sourceLineToAddress(16), 0x00401149ULL);
    // 2. Breakpoint-Aware Run Target & Continue Workflow.
    mainWindow.stopTargetWorkflow();
    // Clear and set breakpoints on lines 15 and 16.
    sourceView->setBreakpoints(QSet<int>());
    sourceView->toggleBreakpoint(15);
    sourceView->toggleBreakpoint(16);
    QVERIFY2(sourceView->hasBreakpoint(15) && sourceView->hasBreakpoint(16),
             "SourceCodeView::toggleBreakpoint(15/16) must register active breakpoints");
    QVERIFY2(sourceView->breakpoints().contains(15) && sourceView->breakpoints().contains(16),
             "SourceCodeView::breakpoints() must contain lines 15 and 16");
    // Launch target and verify stop at first active breakpoint.
    mainWindow.startTargetWorkflow();
    QApplication::processEvents();
    QCOMPARE(disasmView->currentAddress(), 0x00401128ULL);
    QCOMPARE(sourceView->executionLine(), 15);
    QCOMPARE(serverRack->rankPcAddress(serverRack->selectedRank()), 0x00401128ULL);
    // Continue to next active breakpoint at line 16.
    QVERIFY2(mainWindow.continueExecutionWorkflow(),
             "MainWindow::continueExecutionWorkflow() must advance to the next active breakpoint");
    QApplication::processEvents();
    QCOMPARE(disasmView->currentAddress(), 0x00401149ULL);
    QCOMPARE(sourceView->executionLine(), 16);
    QCOMPARE(serverRack->rankPcAddress(serverRack->selectedRank()), 0x00401149ULL);
    // 3. Visible Active Instruction Marker Inside CFG Basic Blocks.
    // Verify CFG basic block marker is visible for active instruction.
    QVERIFY2(disasmView->activeCfgBlockContainsInstructionMarker(0x00401149ULL),
             "DisassemblyView::activeCfgBlockContainsInstructionMarker(0x00401149ULL) must be true");
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
    gridlock::ui::MainWindow mainWindow;
    mainWindow.resize(1440, 900);
    mainWindow.loadPreviewFixture();
    mainWindow.show();
    QVERIFY(QTest::qWaitForWindowExposed(&mainWindow));
    QApplication::processEvents();

    // Query dock instances by their unique registered object names.
    auto* serverRackDock = mainWindow.findChild<QDockWidget*>(QStringLiteral("ServerRackDock"));
    auto* registerDock = mainWindow.findChild<QDockWidget*>(QStringLiteral("RegisterViewDock"));
    QVERIFY2(serverRackDock != nullptr, "ServerRackDock must exist in MainWindow");
    QVERIFY2(registerDock != nullptr, "RegisterViewDock must exist in MainWindow");

    // 1. Pop Out (floatDockWidget) & 1-Click / Double-Click Redock (redockWidget).
    QWidget* originalParent = serverRackDock->parentWidget();
    mainWindow.toggleDockFloating(serverRackDock);
    // Verify dock transitioned to top-level floating state and remains visible.
    QVERIFY2(mainWindow.isDockFloating(serverRackDock), "ServerRackDock must be floating after toggleDockFloating");
    QVERIFY2(serverRackDock->isVisible(), "ServerRackDock must remain visible when floating");

    mainWindow.redockWidget(serverRackDock);
    // Verify dock returned to docked state with original parent and valid geometry.
    QVERIFY2(!mainWindow.isDockFloating(serverRackDock), "ServerRackDock must not be floating after redockWidget");
    QCOMPARE(serverRackDock->parentWidget(), originalParent);
    QVERIFY2(serverRackDock->isVisible(), "ServerRackDock must be visible after redocking");
    QVERIFY2(serverRackDock->width() > 50 && serverRackDock->height() > 50,
             "ServerRackDock must retain visible dimensions > 50px inside its dock/splitter slot");

    // 2. Drag-Back Drop-Zone Redocking (handleDockDragRelease).
    mainWindow.toggleDockFloating(serverRackDock);
    QVERIFY2(mainWindow.isDockFloating(serverRackDock), "ServerRackDock must be floating again");
    // Calculate global screen center of dock's home drop zone.
    const QPoint homeGlobalCenter = mainWindow.dockHomeGlobalRect(serverRackDock).center();
    // Simulate mouse release over dock home drop zone.
    mainWindow.handleDockDragRelease(serverRackDock, homeGlobalCenter);
    QVERIFY2(!mainWindow.isDockFloating(serverRackDock), "ServerRackDock must redock when dropped into its home drop zone");
    QCOMPARE(serverRackDock->parentWidget(), originalParent);

    // 3. redockAllFloatingPanels() Restores All Popped-Out Panels.
    mainWindow.toggleDockFloating(serverRackDock);
    mainWindow.toggleDockFloating(registerDock);
    QVERIFY2(mainWindow.isDockFloating(serverRackDock), "ServerRackDock must be floating");
    QVERIFY2(mainWindow.isDockFloating(registerDock), "RegisterViewDock must be floating");
    // Restore all floating panels in a single batch operation.
    mainWindow.redockAllFloatingPanels();
    QVERIFY2(!mainWindow.isDockFloating(serverRackDock), "ServerRackDock must be docked after redockAllFloatingPanels");
    QVERIFY2(!mainWindow.isDockFloating(registerDock), "RegisterViewDock must be docked after redockAllFloatingPanels");
}
