#pragma once
#include <QCheckBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QWizard>

namespace gridlock::ui::dialogs {

class ProjectWizard : public QWizard {
  Q_OBJECT
public:
  explicit ProjectWizard(QWidget *parent = nullptr);
  ~ProjectWizard() override = default;

  QString getMpiLaunchCommand() const;
  QString getProjectRoot() const;
  QString getTargetBinary() const;
  QString getExecutableArguments() const;
  int getRanks() const;

private:
  QWizardPage *createWorkspacePage();
  QWizardPage *createMpiPage();

  // Page 1
  QLineEdit *m_projectNameEdit;
  QLineEdit *m_workspaceDirEdit;
  QLineEdit *m_targetExecEdit;
  QLineEdit *m_execArgsEdit;

  // Page 2
  QSpinBox *m_ranksBox;
  QCheckBox *m_bindToCoreCheck;
  QCheckBox *m_mapByNodeCheck;
  QCheckBox *m_useHwThreadsCheck;
  QCheckBox *m_oversubscribeCheck;
  QCheckBox *m_reportBindingsCheck;
  QCheckBox *m_displayMapCheck;
  QCheckBox *m_tagOutputCheck;
  QCheckBox *m_timestampOutputCheck;
  QLineEdit *m_customMpiArgsEdit;
};

} // namespace gridlock::ui::dialogs
