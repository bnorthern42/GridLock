#include "TutorialWizard.hpp"
#include "../MainWindow.hpp"
#include <QProcess>
#include <QMessageBox>
#include <QDir>
#include <QCoreApplication>
#include <QFileInfo>

namespace gridlock::ui::tutorial {

TutorialWizard::TutorialWizard(gridlock::ui::MainWindow* mainWindow, QWidget* parent)
    : QDialog(parent), m_mainWindow(mainWindow)
{
    setWindowTitle("GridLock Interactive Tutorials");
    resize(400, 300);

    m_model = new TutorialModel(this);
    m_listView = new QListView(this);
    m_listView->setModel(m_model);

    m_launchBtn = new QPushButton("Launch", this);
    connect(m_launchBtn, &QPushButton::clicked, this, &TutorialWizard::onLaunchClicked);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_listView);
    layout->addWidget(m_launchBtn);
}

QString TutorialWizard::getSelectedFilePath() const {
    QModelIndex index = m_listView->currentIndex();
    if (!index.isValid()) return QString();
    return m_model->data(index, TutorialModel::FilePathRole).toString();
}

void TutorialWizard::onLaunchClicked() {
    QString filePath = getSelectedFilePath();
    if (filePath.isEmpty()) {
        QMessageBox::warning(this, "Error", "Please select a tutorial.");
        return;
    }
    compileAndLaunch(filePath);
}

void TutorialWizard::compileAndLaunch(const QString& sourceFile) {
    QFileInfo fi(sourceFile);
    QString compiler = fi.suffix() == "c" ? "mpicc" : "mpic++";
    QString outDir = QDir::tempPath();
    QString outBin = outDir + "/" + fi.baseName() + "_demo";

    QProcess proc;
    QStringList args;
    args << sourceFile << "-g" << "-O0" << "-o" << outBin;
    
    proc.start(compiler, args);
    if (!proc.waitForFinished()) {
        QMessageBox::critical(this, "Compilation Error", "Failed to compile the tutorial.");
        return;
    }

    if (proc.exitCode() != 0) {
        QMessageBox::critical(this, "Compilation Error", "Compiler returned error: " + proc.readAllStandardError());
        return;
    }

    // Launch debugger
    if (m_mainWindow) {
        int ranks = (fi.baseName() == "deadlock_demo") ? 2 : 3;
        m_mainWindow->startDebuggingSession(outBin, ranks);
    }
    accept();
}

} // namespace gridlock::ui::tutorial
