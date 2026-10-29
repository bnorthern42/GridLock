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
#include <QLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QtTest/QtTest>

#include "../src/ui/MainWindow.hpp"
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
