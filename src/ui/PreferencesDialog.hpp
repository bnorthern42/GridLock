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

    int  tabWidth() const;
    bool insertSpaces() const;
    bool showWhitespace() const;
    void loadFromSettings();

private:
    QSpinBox   *m_tabWidthBox      = nullptr;
    QComboBox  *m_indentModeCombo  = nullptr;
    QComboBox  *m_whitespaceCombo  = nullptr;
};

class BehaviorSettingsPage : public QWidget {
    Q_OBJECT
public:
    explicit BehaviorSettingsPage(QWidget *parent = nullptr);

    QString mpiExecutable()  const;
    QString extraArgs()      const;
    int     defaultRanks()   const;
    void    loadFromSettings();

private:
    QLineEdit *m_mpiExecEdit  = nullptr;
    QLineEdit *m_extraArgEdit = nullptr;
    QSpinBox  *m_rankBox      = nullptr;
};

class DebuggerSettingsPage : public QWidget {
    Q_OBJECT
public:
    explicit DebuggerSettingsPage(QWidget *parent = nullptr);

    QString gdbPath()      const;
    QString mpiExecutable() const;
    int     defaultRanks() const;
    void    loadFromSettings();

private:
    QLineEdit *m_gdbPathEdit  = nullptr;
    QLineEdit *m_mpiExecEdit  = nullptr;
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
