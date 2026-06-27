#include "TutorialDialog.hpp"
#include "../../core/managers/ConfigManager.hpp"
#include <QVBoxLayout>
#include <QLabel>
#include <QFileInfo>
#include <QDir>
#include <QMessageBox>
#include <QCoreApplication>

namespace gridlock::ui {

struct TutorialInfo {
    QString name;
    QString description;
    QString filePath;
};

static const std::vector<TutorialInfo> TUTORIAL_DATA = {
    {"Deadlock Demo", "Demonstrates GridLock's deadlock detection by forcing a deterministic MPI deadlock.", "tutorial/deadlock_demo.c"},
    {"Inspection Demo", "Deeply nested structs and floating-point arrays for testing complex data types.", "tutorial/inspection_demo.cpp"},
    {"Register Demo", "Assembly-injected operations simulating active CPU register states for the Register view.", "tutorial/register_demo.cpp"},
    {"MemView Demo", "Raw memory allocations and explicit pointer states to exercise MemView.", "tutorial/memview_demo.cpp"},
    {"MemView Diff Demo", "Memory allocations that diverge between ranks mid-execution to demonstrate the memory difference tool.", "tutorial/memview_diff_demo.cpp"}
};

TutorialDialog::TutorialDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Interactive Tutorials");
    setWindowFlags(Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    resize(600, 400);

    m_listWidget = new QListWidget(this);
    m_stackedWidget = new QStackedWidget(this);

    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->addWidget(m_listWidget, 1);
    mainLayout->addWidget(m_stackedWidget, 2);

    connect(m_listWidget, &QListWidget::currentRowChanged, this, &TutorialDialog::onSelectionChanged);

    populateList();

    if (m_listWidget->count() > 0) {
        m_listWidget->setCurrentRow(0);
    }
}

void TutorialDialog::populateList() {
    for (const auto& item : TUTORIAL_DATA) {
        m_listWidget->addItem(item.name);

        QWidget* page = new QWidget();
        auto* pageLayout = new QVBoxLayout(page);
        
        QLabel* titleLabel = new QLabel(QString("<h2>%1</h2>").arg(item.name));
        QLabel* descLabel = new QLabel(item.description);
        descLabel->setWordWrap(true);
        descLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        
        QPushButton* launchBtn = new QPushButton("Launch Tutorial");
        connect(launchBtn, &QPushButton::clicked, this, &TutorialDialog::onLaunchClicked);

        pageLayout->addWidget(titleLabel);
        pageLayout->addWidget(descLabel);
        pageLayout->addStretch();
        pageLayout->addWidget(launchBtn, 0, Qt::AlignRight);

        page->setProperty("filePath", item.filePath);

        m_stackedWidget->addWidget(page);
    }
}

QSet<int> TutorialDialog::getBreakpointsForFile(const QString& filePath) {
    if (filePath.endsWith("deadlock_demo.c")) {
        return {31, 34};
    } else if (filePath.endsWith("inspection_demo.cpp")) {
        return {76};
    } else if (filePath.endsWith("register_demo.cpp")) {
        return {103, 117};
    } else if (filePath.endsWith("memview_demo.cpp")) {
        return {131, 146};
    } else if (filePath.endsWith("memview_diff_demo.cpp")) {
        return {162, 181};
    }
    return {};
}

void TutorialDialog::injectBreakpoints(const QString& relativePath, const QString& absPath) {
    QSet<int> tutorialBps = getBreakpointsForFile(relativePath);
    if (!tutorialBps.isEmpty()) {
        auto currentBps = gridlock::core::ConfigManager::instance().getBreakpoints();
        currentBps[absPath].unite(tutorialBps);
        gridlock::core::ConfigManager::instance().saveBreakpoints(currentBps);
    }
}

void TutorialDialog::onSelectionChanged(int currentRow) {
    if (currentRow >= 0 && currentRow < m_stackedWidget->count()) {
        m_stackedWidget->setCurrentIndex(currentRow);
    }
}

void TutorialDialog::onLaunchClicked() {
    QWidget* currentPage = m_stackedWidget->currentWidget();
    if (!currentPage) return;

    QString relativePath = currentPage->property("filePath").toString();
    
    QDir projectRoot(QCoreApplication::applicationDirPath());
    projectRoot.cdUp();
    
    QFileInfo fi(projectRoot.absoluteFilePath(relativePath));
    if (!fi.exists()) {
        fi = QFileInfo(relativePath);
    }

    if (!fi.exists()) {
        QMessageBox::critical(this, "Error", QString("Tutorial file not found:\n%1").arg(fi.absoluteFilePath()));
        return;
    }

    QString absPath = fi.absoluteFilePath();
    injectBreakpoints(relativePath, absPath);

    emit launchTutorialRequested(absPath);
    accept();
}

} // namespace gridlock::ui
