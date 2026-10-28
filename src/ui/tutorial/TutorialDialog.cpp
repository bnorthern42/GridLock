/**
 * @file TutorialDialog.cpp
 * @brief Implementation of interactive tutorial catalog dialog.
 *
 * Implements demo metadata definitions, preview card construction,
 * predefined breakpoint mapping, and tutorial launch dispatching.
 */

#include "TutorialDialog.hpp"
#include "../../core/managers/ConfigManager.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QVBoxLayout>
#include <vector>

namespace gridlock::ui {

/**
 * @struct TutorialInfo
 * @brief Metadata record describing an interactive tutorial module.
 */
struct TutorialInfo {
    QString name;
    QString description;
    QString filePath;
};

// Static catalog defining built-in tutorial sample sources and descriptions.
static const std::vector<TutorialInfo> TUTORIAL_DATA = {
    {"Deadlock Demo", "Demonstrates GridLock's deadlock detection by forcing a deterministic MPI deadlock.", "tutorial/deadlock_demo.c"},
    {"Inspection Demo", "Deeply nested structs and floating-point arrays for testing complex data types.", "tutorial/inspection_demo.cpp"},
    {"Register Demo", "Assembly-injected operations simulating active CPU register states for the Register view.", "tutorial/register_demo.cpp"},
    {"MemView Demo", "Raw memory allocations and explicit pointer states to exercise MemView.", "tutorial/memview_demo.cpp"},
    {"MemView Diff Demo", "Memory allocations that diverge between ranks mid-execution to demonstrate the memory difference tool.", "tutorial/memview_diff_demo.cpp"}
};

/**
 * @brief Constructs tutorial modal dialog with side-by-side catalog and preview pane.
 * @param parent Optional parent widget.
 */
TutorialDialog::TutorialDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle("Interactive Tutorials");
    setWindowFlags(Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    resize(600, 400);

    m_listWidget = new QListWidget(this);
    m_stackedWidget = new QStackedWidget(this);

    // Arrange sidebar list and preview card with 1:2 stretch ratio.
    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->addWidget(m_listWidget, 1);
    mainLayout->addWidget(m_stackedWidget, 2);

    // Switch preview card when user selects different catalog row.
    connect(m_listWidget, &QListWidget::currentRowChanged, this, &TutorialDialog::onSelectionChanged);

    populateList();

    // Select first tutorial entry by default.
    if (m_listWidget->count() > 0) {
        m_listWidget->setCurrentRow(0);
    }
}

/**
 * @brief Populates the tutorial list items and creates associated preview cards.
 */
void TutorialDialog::populateList() {
    for (const auto& item : TUTORIAL_DATA) {
        m_listWidget->addItem(item.name);

        // Construct description card widget for each tutorial entry.
        QWidget* page = new QWidget();
        auto* pageLayout = new QVBoxLayout(page);
        
        QLabel* titleLabel = new QLabel(QString("<h2>%1</h2>").arg(item.name));
        QLabel* descLabel = new QLabel(item.description);
        descLabel->setWordWrap(true);
        descLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        
        // Launch action button bound to tutorial submission.
        QPushButton* launchBtn = new QPushButton("Launch Tutorial");
        connect(launchBtn, &QPushButton::clicked, this, &TutorialDialog::onLaunchClicked);

        pageLayout->addWidget(titleLabel);
        pageLayout->addWidget(descLabel);
        pageLayout->addStretch();
        pageLayout->addWidget(launchBtn, 0, Qt::AlignRight);

        // Attach relative file path to page property for retrieval on launch.
        page->setProperty("filePath", item.filePath);

        m_stackedWidget->addWidget(page);
    }
}

/**
 * @brief Looks up default breakpoint line set for a tutorial source file.
 * @param filePath Path to tutorial source file.
 * @return Set of line numbers to break at automatically.
 */
QSet<int> TutorialDialog::getBreakpointsForFile(const QString& filePath) {
    // Map specific demo files to educational line locations.
    if (filePath.endsWith("deadlock_demo.c")) {
        return {25, 28};
    } else if (filePath.endsWith("inspection_demo.cpp")) {
        return {76};
    } else if (filePath.endsWith("register_demo.cpp")) {
        return {24, 37};
    } else if (filePath.endsWith("memview_demo.cpp")) {
        return {25};
    } else if (filePath.endsWith("memview_diff_demo.cpp")) {
        return {30};
    }
    return {};
}

/**
 * @brief Injects default breakpoint positions into ConfigManager persistent state.
 * @param relativePath Relative source path.
 * @param absPath Absolute resolved source file path.
 */
void TutorialDialog::injectBreakpoints(const QString& relativePath, const QString& absPath) {
    QSet<int> tutorialBps = getBreakpointsForFile(relativePath);
    if (!tutorialBps.isEmpty()) {
        // Merge demo breakpoints into persistent breakpoints map.
        auto currentBps = gridlock::core::ConfigManager::instance().getBreakpoints();
        currentBps[absPath].unite(tutorialBps);
        gridlock::core::ConfigManager::instance().saveBreakpoints(currentBps);
    }
}

/**
 * @brief Switches active stacked widget page to match current list selection.
 * @param currentRow Selected row index.
 */
void TutorialDialog::onSelectionChanged(int currentRow) {
    if (currentRow >= 0 && currentRow < m_stackedWidget->count()) {
        m_stackedWidget->setCurrentIndex(currentRow);
    }
}

/**
 * @brief Validates file existence, injects breakpoints, and emits launch request.
 */
void TutorialDialog::onLaunchClicked() {
    QWidget* currentPage = m_stackedWidget->currentWidget();
    if (!currentPage) return;

    QString relativePath = currentPage->property("filePath").toString();
    
    // Resolve relative path against project directory structure.
    QDir projectRoot(QCoreApplication::applicationDirPath());
    projectRoot.cdUp();
    
    QFileInfo fi(projectRoot.absoluteFilePath(relativePath));
    if (!fi.exists()) {
        fi = QFileInfo(relativePath);
    }

    // Display error dialog if sample source cannot be found.
    if (!fi.exists()) {
        QMessageBox::critical(this, "Error", QString("Tutorial file not found:\n%1").arg(fi.absoluteFilePath()));
        return;
    }

    // Persist default educational breakpoints for this demo.
    QString absPath = fi.absoluteFilePath();
    injectBreakpoints(relativePath, absPath);

    // Emit launch signal to MainWindow and close dialog.
    emit launchTutorialRequested(absPath);
    accept();
}

}  // namespace gridlock::ui
