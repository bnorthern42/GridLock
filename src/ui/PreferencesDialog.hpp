#pragma once
#include <QDialog>
#include <QListWidget>
#include <QStackedWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QWidget>
#include <QDialogButtonBox>

namespace gridlock::ui {

// ─── Individual settings pages ─────────────────────────────────────────────

class AppearanceSettingsPage : public QWidget {
    Q_OBJECT
public:
    explicit AppearanceSettingsPage(QWidget *parent = nullptr);

    QString selectedTheme() const;
    void    loadFromSettings();

private:
    QComboBox *m_themeCombo = nullptr;
};

class EditingSettingsPage : public QWidget {
    Q_OBJECT
public:
    explicit EditingSettingsPage(QWidget *parent = nullptr);

    int  tabWidth()       const;
    bool insertSpaces()   const;
    bool showWhitespace() const;
    void loadFromSettings();

private:
    QSpinBox  *m_tabWidthBox     = nullptr;
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
    bool    confirmQuit()        const;
    bool    focusOnStop()        const;
    void    loadFromSettings();

private:
    QComboBox *m_restoreBreakpointsCheck = nullptr;
    QComboBox *m_confirmQuitCheck        = nullptr;
    QComboBox *m_focusOnStopCheck        = nullptr;
};

/// Sole owner of GDB path, MPI executable, MPI args, and default rank count.
/// Persists through ConfigManager::saveDebuggerSettings().
class DebuggerSettingsPage : public QWidget {
    Q_OBJECT
public:
    explicit DebuggerSettingsPage(QWidget *parent = nullptr);

    QString gdbPath()       const;
    QString mpiExecutable() const;
    QString mpiArgs()       const;
    int     defaultRanks()  const;
    void    loadFromSettings();

private:
    QLineEdit *m_gdbPathEdit  = nullptr;
    QLineEdit *m_mpiExecEdit  = nullptr;
    QLineEdit *m_mpiArgsEdit  = nullptr;
    QSpinBox  *m_rankBox      = nullptr;
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

    QListWidget            *m_sidebar      = nullptr;
    QStackedWidget         *m_stack        = nullptr;
    QDialogButtonBox       *m_buttonBox    = nullptr;

    AppearanceSettingsPage *m_appearancePage = nullptr;
    EditingSettingsPage    *m_editingPage    = nullptr;
    BehaviorSettingsPage   *m_behaviorPage   = nullptr;
    DebuggerSettingsPage   *m_debuggerPage   = nullptr;
};

} // namespace gridlock::ui
