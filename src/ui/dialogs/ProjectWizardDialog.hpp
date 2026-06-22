#pragma once
#include <QDialog>
#include <QStackedWidget>
#include <QListWidget>
#include <QLineEdit>
#include <QSpinBox>
#include <QString>

namespace gridlock::ui::dialogs {

class ProjectWizardDialog : public QDialog {
    Q_OBJECT
public:
    explicit ProjectWizardDialog(QWidget* parent = nullptr);
    ~ProjectWizardDialog() override = default;

    QString getProjectRoot() const;
    QString getBuildDir() const;
    QString getTargetBinary() const;
    QString getMpiPath() const;
    int getRanks() const;
    QString getMpiArgs() const;

private:
    void setupUi();
    void createWorkspacePanel();
    void createMpiPanel();

    QListWidget* m_sidebar;
    QStackedWidget* m_stackedWidget;

    // Panel 1: Workspace
    QLineEdit* m_projectRootEdit;
    QLineEdit* m_buildDirEdit;
    QLineEdit* m_targetBinaryEdit;

    // Panel 2: MPI Configuration
    QLineEdit* m_mpiPathEdit;
    QSpinBox* m_ranksBox;
    QLineEdit* m_mpiArgsEdit;
};

} // namespace gridlock::ui::dialogs
