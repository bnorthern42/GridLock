/**
 * @file PreferencesDialog.hpp
 * @brief Declaration of the Preferences dialog and modular settings page widgets.
 *
 * Defines the user interface dialog for configuring application appearance,
 * editor preferences, runtime behavior, GDB/DAP debugger integration, HPC cluster
 * topology, remote SSH/SLURM connections, Dash/Zeal docsets, and embedded terminal.
 */

#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTextEdit>
#include <QVector>
#include <QWidget>

namespace gridlock::ui {

// ============================================================================
// Settings Page Widgets
// ============================================================================

/**
 * @class AppearanceSettingsPage
 * @brief Configuration panel for theme selection, dark mode, font sizes, and icon styling.
 */
class AppearanceSettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit AppearanceSettingsPage(QWidget* parent = nullptr);

    QString selectedTheme() const;
    bool isDarkMode() const;
    QString fileTreeStyle() const;
    bool colorizeIcons() const;
    QString uiFontFamily() const;
    int uiFontSize() const;
    QString codeFontFamily() const;
    int codeFontSize() const;

    void loadFromSettings();
    void updateSwatchPreview(const QString& themeId);

private:
    QComboBox* m_themeCombo = nullptr;
    QVector<QLabel*> m_swatchBoxes;
    QCheckBox* m_darkModeCheck = nullptr;
    QComboBox* m_fileTreeStyleCombo = nullptr;
    QCheckBox* m_colorizeIconsCheck = nullptr;
    QComboBox* m_uiFontFamilyCombo = nullptr;
    QSpinBox* m_uiFontSizeBox = nullptr;
    QComboBox* m_codeFontFamilyCombo = nullptr;
    QSpinBox* m_codeFontSizeBox = nullptr;
};

/**
 * @class EditingSettingsPage
 * @brief Configuration panel for indentation, tab stops, whitespace, and margin guidelines.
 */
class EditingSettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit EditingSettingsPage(QWidget* parent = nullptr);

    int tabWidth() const;
    bool insertSpaces() const;
    bool showWhitespace() const;
    bool wordWrap() const;
    bool showEdgeColumn() const;
    int edgeColumn() const;

    void loadFromSettings();

private:
    QSpinBox* m_tabWidthBox = nullptr;
    QComboBox* m_indentModeCombo = nullptr;
    QComboBox* m_whitespaceCombo = nullptr;
    QCheckBox* m_wordWrapCheck = nullptr;
    QCheckBox* m_showEdgeColumnCheck = nullptr;
    QSpinBox* m_edgeColumnBox = nullptr;
};

/**
 * @class BehaviorSettingsPage
 * @brief Configuration panel for session lifecycle and window focus behavior.
 */
class BehaviorSettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit BehaviorSettingsPage(QWidget* parent = nullptr);

    QString restoreBreakpoints() const;
    bool confirmQuit() const;
    bool focusOnStop() const;

    void loadFromSettings();

private:
    QComboBox* m_restoreBreakpointsCheck = nullptr;
    QComboBox* m_confirmQuitCheck = nullptr;
    QComboBox* m_focusOnStopCheck = nullptr;
};

/**
 * @class DebuggerSettingsPage
 * @brief Configuration panel for GDB, DAP adapter, Clangd LSP, and MPI launcher flags.
 */
class DebuggerSettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit DebuggerSettingsPage(QWidget* parent = nullptr);

    QString clangdPath() const;
    QString dapAdapterPath() const;
    QString gdbPath() const;
    QString mpiExecutable() const;
    QString mpiArgs() const;
    int defaultRanks() const;
    bool trapFpe() const;

    void loadFromSettings();

private:
    QLineEdit* m_clangdPathEdit = nullptr;
    QLineEdit* m_dapAdapterEdit = nullptr;
    QLineEdit* m_gdbPathEdit = nullptr;
    QLineEdit* m_mpiExecEdit = nullptr;
    QLineEdit* m_mpiArgsEdit = nullptr;
    QSpinBox* m_rankBox = nullptr;
    QCheckBox* m_trapFpeCheck = nullptr;
};

/**
 * @class HpcSettingsPage
 * @brief Configuration panel for cluster hostfiles, environment variables, and node pinning.
 */
class HpcSettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit HpcSettingsPage(QWidget* parent = nullptr);

    QString hostsFile() const;
    QString envVars() const;
    bool strictAffinity() const;

    void loadFromSettings();

private:
    QLineEdit* m_hostsFileEdit = nullptr;
    QTextEdit* m_envVarsEdit = nullptr;
    QCheckBox* m_strictAffinityBox = nullptr;
};

/**
 * @class HpcIntegrationSettingsPage
 * @brief Configuration panel for remote SSH connections, SLURM templates, and Spack roots.
 */
class HpcIntegrationSettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit HpcIntegrationSettingsPage(QWidget* parent = nullptr);

    // SSH settings
    QString sshHost() const;
    QString sshUser() const;
    QString sshKeyPath() const;

    // SLURM batch settings
    QString slurmTemplate() const;
    QString slurmPartition() const;
    int slurmNodes() const;
    int slurmTasks() const;
    bool requestGpus() const;
    int gpusPerNode() const;

    // SLURM batch script auto-detection and directive accessors
    bool parseSlurmBatchScript(const QString& scriptText);
    bool loadSlurmBatchScriptFile(const QString& filePath);
    QString generateSlurmBatchScript() const;
    QString slurmFieldValue(const QString& key) const;

    // Spack environment
    QString spackRoot() const;

    void loadFromSettings();

private:
    QLineEdit* m_sshHostEdit = nullptr;
    QLineEdit* m_sshUserEdit = nullptr;
    QLineEdit* m_sshKeyEdit = nullptr;

    // 16-Directive SLURM Batch Configuration controls
    QLineEdit* m_sbatchFilePathEdit = nullptr;
    QLineEdit* m_jobNameEdit = nullptr;
    QLineEdit* m_accountEdit = nullptr;
    QComboBox* m_partitionCombo = nullptr;
    QComboBox* m_qosCombo = nullptr;
    QComboBox* m_timeCombo = nullptr;
    QSpinBox* m_nodesBox = nullptr;
    QSpinBox* m_tasksBox = nullptr;
    QSpinBox* m_cpusPerTaskBox = nullptr;
    QComboBox* m_memCombo = nullptr;
    QCheckBox* m_gpuCheck = nullptr;
    QLineEdit* m_gpuTypeEdit = nullptr;
    QSpinBox* m_gpusBox = nullptr;
    QLineEdit* m_constraintEdit = nullptr;
    QComboBox* m_mpiLauncherCombo = nullptr;
    QLineEdit* m_modulesEdit = nullptr;
    QLineEdit* m_outputLogEdit = nullptr;
    QComboBox* m_mailTypeCombo = nullptr;
    QLineEdit* m_mailUserEdit = nullptr;
    QPlainTextEdit* m_templateEdit = nullptr;
    QLineEdit* m_spackRootEdit = nullptr;
};

/**
 * @class DocsetSettingsPage
 * @brief Configuration panel for Dash/Zeal offline API docset storage and discovery.
 */
class DocsetSettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit DocsetSettingsPage(QWidget* parent = nullptr);

    QString docsetDirectory() const;
    void loadFromSettings();
    void refreshTable();

private slots:
    void onAutoDetect();

private:
    QLineEdit* m_dirEdit = nullptr;
    QTableWidget* m_table = nullptr;
    QPushButton* m_autoDetectBtn = nullptr;
};

/**
 * @class TerminalSettingsPage
 * @brief Configuration panel for embedded shell path, font family, and font size.
 */
class TerminalSettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit TerminalSettingsPage(QWidget* parent = nullptr);

    QString shellPath() const;
    QString fontFamily() const;
    int fontSize() const;

    void loadFromSettings();

private:
    QLineEdit* m_shellPathEdit = nullptr;
    QComboBox* m_fontFamilyCombo = nullptr;
    QSpinBox* m_fontSizeBox = nullptr;
};

// ============================================================================
// Main Preferences Dialog
// ============================================================================

/**
 * @class PreferencesDialog
 * @brief Modal dialog orchestrating preference categories via sidebar navigation.
 */
class PreferencesDialog : public QDialog {
    Q_OBJECT

public:
    explicit PreferencesDialog(QWidget* parent = nullptr);

    /// Parses `#SBATCH` directives and module load lines into the SLURM settings page.
    bool parseSlurmBatchScript(const QString& scriptText);
    /// Loads a `.sbatch`/`.slurm` file from disk and populates the SLURM settings page.
    bool loadSlurmBatchScriptFile(const QString& filePath);
    /// Generates a formatted SLURM batch script from the current SLURM form fields.
    QString generateSlurmBatchScript() const;
    /// Returns the string value of a SLURM form field by key for testing and inspection.
    QString slurmFieldValue(const QString& key) const;

signals:
    /// Emitted after preferences are committed to disk to trigger live UI updates.
    void preferencesChanged();

private slots:
    void apply();
    void acceptAndApply();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    void setupSidebar();
    void setupPages();
    void setupButtonBox();

    QListWidget* m_sidebar = nullptr;
    QStackedWidget* m_stack = nullptr;
    QDialogButtonBox* m_buttonBox = nullptr;

    AppearanceSettingsPage* m_appearancePage = nullptr;
    EditingSettingsPage* m_editingPage = nullptr;
    BehaviorSettingsPage* m_behaviorPage = nullptr;
    DebuggerSettingsPage* m_debuggerPage = nullptr;
    HpcSettingsPage* m_hpcPage = nullptr;
    HpcIntegrationSettingsPage* m_hpcIntegrationPage = nullptr;
    DocsetSettingsPage* m_docsetPage = nullptr;
    TerminalSettingsPage* m_terminalPage = nullptr;
};

}  // namespace gridlock::ui