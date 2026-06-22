#include "ProjectWizardDialog.hpp"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QPushButton>
#include <QFileDialog>
#include <QLabel>
#include <QDialogButtonBox>
#include "../../core/managers/ConfigManager.hpp"

namespace gridlock::ui::dialogs {

ProjectWizardDialog::ProjectWizardDialog(QWidget* parent) : QDialog(parent) {
    setFixedSize(800, 600);
    setWindowTitle("GridLock - New Project Wizard");
    setupUi();
}

void ProjectWizardDialog::setupUi() {
    QHBoxLayout* mainLayout = new QHBoxLayout(this);
    
    m_sidebar = new QListWidget(this);
    m_sidebar->setFixedWidth(200);
    m_sidebar->addItem("Workspace Selection");
    m_sidebar->addItem("MPI Configuration");
    
    m_stackedWidget = new QStackedWidget(this);
    
    createWorkspacePanel();
    createMpiPanel();
    
    QVBoxLayout* rightLayout = new QVBoxLayout();
    rightLayout->addWidget(m_stackedWidget);
    
    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    rightLayout->addWidget(buttonBox);
    
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    
    mainLayout->addWidget(m_sidebar);
    mainLayout->addLayout(rightLayout);
    
    connect(m_sidebar, &QListWidget::currentRowChanged, m_stackedWidget, &QStackedWidget::setCurrentIndex);
    m_sidebar->setCurrentRow(0);
}

void ProjectWizardDialog::createWorkspacePanel() {
    QWidget* panel = new QWidget();
    QFormLayout* layout = new QFormLayout(panel);
    
    // Project Root
    m_projectRootEdit = new QLineEdit();
    QPushButton* rootBtn = new QPushButton("Browse...");
    connect(rootBtn, &QPushButton::clicked, this, [this]() {
        QString dir = QFileDialog::getExistingDirectory(this, "Select Project Root");
        if (!dir.isEmpty()) m_projectRootEdit->setText(dir);
    });
    QHBoxLayout* rootLayout = new QHBoxLayout();
    rootLayout->addWidget(m_projectRootEdit);
    rootLayout->addWidget(rootBtn);
    layout->addRow("Project Root:", rootLayout);
    
    // Build Directory
    m_buildDirEdit = new QLineEdit();
    QPushButton* buildBtn = new QPushButton("Browse...");
    connect(buildBtn, &QPushButton::clicked, this, [this]() {
        QString dir = QFileDialog::getExistingDirectory(this, "Select Build Directory", m_projectRootEdit->text());
        if (!dir.isEmpty()) m_buildDirEdit->setText(dir);
    });
    QHBoxLayout* buildLayout = new QHBoxLayout();
    buildLayout->addWidget(m_buildDirEdit);
    buildLayout->addWidget(buildBtn);
    layout->addRow("Build Directory:", buildLayout);
    
    // Target Binary
    m_targetBinaryEdit = new QLineEdit();
    QPushButton* targetBtn = new QPushButton("Browse...");
    connect(targetBtn, &QPushButton::clicked, this, [this]() {
        QString file = QFileDialog::getOpenFileName(this, "Select Target Executable", m_buildDirEdit->text());
        if (!file.isEmpty()) m_targetBinaryEdit->setText(file);
    });
    QHBoxLayout* targetLayout = new QHBoxLayout();
    targetLayout->addWidget(m_targetBinaryEdit);
    targetLayout->addWidget(targetBtn);
    layout->addRow("Target Executable:", targetLayout);
    
    // Program Arguments
    m_programArgsEdit = new QLineEdit();
    layout->addRow("Program Arguments:", m_programArgsEdit);
    
    // Environment Variables
    m_envVarsEdit = new QLineEdit();
    m_envVarsEdit->setPlaceholderText("FOO=bar BAZ=1");
    layout->addRow("Environment Variables:", m_envVarsEdit);
    
    m_stackedWidget->addWidget(panel);
}

void ProjectWizardDialog::createMpiPanel() {
    QWidget* panel = new QWidget();
    QFormLayout* layout = new QFormLayout(panel);
    
    auto dbgSettings = core::ConfigManager::instance().getDebuggerSettings();
    
    m_mpiPathEdit = new QLineEdit(dbgSettings.mpiExecutable);
    m_ranksBox = new QSpinBox();
    m_ranksBox->setMinimum(1);
    m_ranksBox->setMaximum(1000000);
    m_ranksBox->setValue(dbgSettings.defaultRanks);
    m_mpiArgsEdit = new QLineEdit(dbgSettings.mpiArgs);
    
    layout->addRow("mpirun Path:", m_mpiPathEdit);
    layout->addRow("Number of Ranks:", m_ranksBox);
    layout->addRow("MPI Arguments:", m_mpiArgsEdit);
    
    m_stackedWidget->addWidget(panel);
}

QString ProjectWizardDialog::getProjectRoot() const { return m_projectRootEdit->text(); }
QString ProjectWizardDialog::getBuildDir() const { return m_buildDirEdit->text(); }
QString ProjectWizardDialog::getTargetBinary() const { return m_targetBinaryEdit->text(); }
QString ProjectWizardDialog::getProgramArguments() const { return m_programArgsEdit->text(); }
QString ProjectWizardDialog::getEnvironmentVariables() const { return m_envVarsEdit->text(); }
QString ProjectWizardDialog::getMpiPath() const { return m_mpiPathEdit->text(); }
int ProjectWizardDialog::getRanks() const { return m_ranksBox->value(); }
QString ProjectWizardDialog::getMpiArgs() const { return m_mpiArgsEdit->text(); }

} // namespace gridlock::ui::dialogs
