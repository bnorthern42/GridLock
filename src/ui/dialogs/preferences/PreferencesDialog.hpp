/**
 * @file PreferencesDialog.hpp
 * @brief Declaration of the central PreferencesDialog shell orchestrating modular settings pages.
 */

#pragma once

#include "AppearanceSettingsPage.hpp"
#include "DebuggerSettingsPage.hpp"
#include "EditingBehaviorPages.hpp"
#include "HpcSettingsPages.hpp"
#include "KeybindingsSettingsPage.hpp"
#include "TerminalDocsetsPages.hpp"

#include <QDialog>
#include <QDialogButtonBox>
#include <QListWidget>
#include <QStackedWidget>
#include <QString>

namespace gridlock::ui {

/**
 * @class PreferencesDialog
 * @brief Modal dialog orchestrating preference categories via sidebar navigation.
 */
class PreferencesDialog : public QDialog {
    Q_OBJECT

public:
    explicit PreferencesDialog(QWidget* parent = nullptr);

    /// Returns the editable Keybindings settings page.
    KeybindingsSettingsPage* keybindingsPage() const { return m_keybindingsPage; }

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
    void paintEvent(QPaintEvent* event) override;

private:
    void setupSidebar();
    void setupPages();
    void setupButtonBox();
    void applyDialogThemeChrome();
    void refreshSidebarIcons();

    QListWidget* m_sidebar = nullptr;
    QStackedWidget* m_stack = nullptr;
    QDialogButtonBox* m_buttonBox = nullptr;

    AppearanceSettingsPage* m_appearancePage = nullptr;
    EditingSettingsPage* m_editingPage = nullptr;
    BehaviorSettingsPage* m_behaviorPage = nullptr;
    KeybindingsSettingsPage* m_keybindingsPage = nullptr;
    DebuggerSettingsPage* m_debuggerPage = nullptr;
    HpcSettingsPage* m_hpcPage = nullptr;
    HpcIntegrationSettingsPage* m_hpcIntegrationPage = nullptr;
    DocsetSettingsPage* m_docsetPage = nullptr;
    TerminalSettingsPage* m_terminalPage = nullptr;
};

}  // namespace gridlock::ui
