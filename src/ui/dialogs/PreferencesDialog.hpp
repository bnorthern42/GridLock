#pragma once
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTextEdit>
#include <QWidget>

namespace gridlock::ui {

// ─── Individual settings pages ─────────────────────────────────────────────

class AppearanceSettingsPage : public QWidget {
  Q_OBJECT
public:
  explicit AppearanceSettingsPage(QWidget *parent = nullptr);

  QString selectedTheme() const;
  bool isDarkMode() const;
  QString fileTreeStyle() const;
  bool colorizeIcons() const;
  void loadFromSettings();

  int uiFontSize() const;
  int codeFontSize() const;

private:
  QComboBox *m_themeCombo = nullptr;
  QCheckBox *m_darkModeCheck = nullptr;
  QComboBox *m_fileTreeStyleCombo = nullptr;
  QCheckBox *m_colorizeIconsCheck = nullptr;
  QSpinBox *m_uiFontSizeBox = nullptr;
  QSpinBox *m_codeFontSizeBox = nullptr;
};

class EditingSettingsPage : public QWidget {
  Q_OBJECT
public:
  explicit EditingSettingsPage(QWidget *parent = nullptr);

  int tabWidth() const;
  bool insertSpaces() const;
  bool showWhitespace() const;
  void loadFromSettings();

private:
  QSpinBox *m_tabWidthBox = nullptr;
  QComboBox *m_indentModeCombo = nullptr;
  QComboBox *m_whitespaceCombo = nullptr;
};

/// Owns session-level UX preferences only.
/// MPI / GDB fields live exclusively in DebuggerSettingsPage.
class BehaviorSettingsPage : public QWidget {
  Q_OBJECT
public:
  explicit BehaviorSettingsPage(QWidget *parent = nullptr);

  QString restoreBreakpoints() const;
  bool confirmQuit() const;
  bool focusOnStop() const;
  void loadFromSettings();

private:
  QComboBox *m_restoreBreakpointsCheck = nullptr;
  QComboBox *m_confirmQuitCheck = nullptr;
  QComboBox *m_focusOnStopCheck = nullptr;
};

/// Sole owner of DAP/GDB paths, MPI executable, MPI args, and default rank
/// count. Persists through ConfigManager.
class DebuggerSettingsPage : public QWidget {
  Q_OBJECT
public:
  explicit DebuggerSettingsPage(QWidget *parent = nullptr);

  QString clangdPath() const;
  QString dapAdapterPath() const;
  QString gdbPath() const;
  QString mpiExecutable() const;
  QString mpiArgs() const;
  int defaultRanks() const;
  bool trapFpe() const;
  void loadFromSettings();

private:
  QLineEdit *m_clangdPathEdit = nullptr;
  QLineEdit *m_dapAdapterEdit = nullptr;
  QLineEdit *m_gdbPathEdit = nullptr;
  QLineEdit *m_mpiExecEdit = nullptr;
  QLineEdit *m_mpiArgsEdit = nullptr;
  QSpinBox *m_rankBox = nullptr;
  QCheckBox *m_trapFpeCheck = nullptr;
};

/// HPC cluster / node configuration — hostsfile, env vars, affinity.
/// Persists through ConfigManager::saveHpcSettings().
class HpcSettingsPage : public QWidget {
  Q_OBJECT
public:
  explicit HpcSettingsPage(QWidget *parent = nullptr);

  QString hostsFile() const;
  QString envVars() const;
  bool strictAffinity() const;
  void loadFromSettings();

private:
  QLineEdit *m_hostsFileEdit = nullptr;
  QTextEdit *m_envVarsEdit = nullptr;
  QCheckBox *m_strictAffinityBox = nullptr;
};

/// Remote HPC integration: SSH credentials, SLURM template, Spack root.
/// Persists through ConfigManager::saveSshSettings() + saveSlurmSettings().
class HpcIntegrationSettingsPage : public QWidget {
  Q_OBJECT
public:
  explicit HpcIntegrationSettingsPage(QWidget *parent = nullptr);

  // SSH
  QString sshHost() const;
  QString sshUser() const;
  QString sshKeyPath() const;
  // SLURM
  QString slurmTemplate() const;
  QString slurmPartition() const;
  int slurmNodes() const;
  int slurmTasks() const;
  bool requestGpus() const;
  int gpusPerNode() const;
  // Spack
  QString spackRoot() const;

  void loadFromSettings();

private:
  // SSH fields
  QLineEdit *m_sshHostEdit = nullptr;
  QLineEdit *m_sshUserEdit = nullptr;
  QLineEdit *m_sshKeyEdit = nullptr;
  // SLURM fields
  QPlainTextEdit *m_templateEdit = nullptr;
  QLineEdit *m_partitionEdit = nullptr;
  QSpinBox *m_nodesBox = nullptr;
  QSpinBox *m_tasksBox = nullptr;
  QCheckBox *m_gpuCheck = nullptr;
  QSpinBox *m_gpusBox = nullptr;
  // Spack
  QLineEdit *m_spackRootEdit = nullptr;
};

class DocsetSettingsPage : public QWidget {
  Q_OBJECT
public:
  explicit DocsetSettingsPage(QWidget *parent = nullptr);

  QString docsetDirectory() const;
  void loadFromSettings();
  void refreshTable();

private slots:
  void onAutoDetect();

private:
  QLineEdit *m_dirEdit = nullptr;
  QTableWidget *m_table = nullptr;
  QPushButton *m_autoDetectBtn = nullptr;
};

// ─── Main dialog ───────────────────────────────────────────────────────────

class PreferencesDialog : public QDialog {
  Q_OBJECT
public:
  explicit PreferencesDialog(QWidget *parent = nullptr);

signals:
  /// Emitted after settings are written so live components can refresh.
  void preferencesChanged();

private slots:
  void apply();
  void acceptAndApply();

private:
  void setupSidebar();
  void setupPages();
  void setupButtonBox();

  QListWidget *m_sidebar = nullptr;
  QStackedWidget *m_stack = nullptr;
  QDialogButtonBox *m_buttonBox = nullptr;

  AppearanceSettingsPage *m_appearancePage = nullptr;
  EditingSettingsPage *m_editingPage = nullptr;
  BehaviorSettingsPage *m_behaviorPage = nullptr;
  DebuggerSettingsPage *m_debuggerPage = nullptr;
  HpcSettingsPage *m_hpcPage = nullptr;
  HpcIntegrationSettingsPage *m_hpcIntegrationPage = nullptr;
  DocsetSettingsPage *m_docsetPage = nullptr;
};

} // namespace gridlock::ui