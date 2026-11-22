/**
 * @file CommandPaletteDialog.hpp
 * @brief JetBrains-style Search Everywhere and fuzzy Command Palette overlay for GridLock.
 *
 * Supports Double-Shift (All), Ctrl+P (Files), and Ctrl+Shift+P (Actions), tabbed scope
 * switching (All | Files | Symbols | Text | Actions | Ranks) via click or Tab/Shift+Tab,
 * CamelCase/acronym fuzzy ranking, empty-query Recent Files & Actions, and a live
 * syntax-highlighted split preview pane with Enter / Ctrl+Enter navigation.
 */

#pragma once

#include <functional>

#include <QDialog>
#include <QEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QString>
#include <QStringList>
#include <QToolButton>
#include <QVector>
#include <QWidget>

#include "../../../core/utils/SearchEverywhereEngine.hpp"

namespace gridlock::ui {

class EditorSyntaxHighlighter;
class MainWindow;

/**
 * @struct PaletteItem
 * @brief Represents a single searchable, previewable, and executable entry in Search Everywhere.
 */
struct PaletteItem {
    enum class Category {
        Action,  ///< Menu bar action, toolbar command, or debugger workflow (>).
        File,    ///< Workspace or tutorial source, header, script, or build file.
        Symbol,  ///< C/C++/Fortran/Python or ELF symbol entry (@).
        Rank,    ///< Active MPI process rank or cluster execution context (#).
        Theme,   ///< Workstation color theme from ThemeManager (theme:).
        Text     ///< Live workspace line grep match (/).
    };

    Category category = Category::Action;
    QString title;
    QString subtitle;
    QString badge;
    std::function<void()> callback;
    QString filePath = {};
    int lineNumber = 1;
    bool isRecent = false;
    std::function<void()> splitCallback = {};
};

int fuzzyScore(const QString& pattern, const QString& candidate,
               QVector<int>* matchedIndices = nullptr);

/**
 * @class CommandPaletteDialog
 * @brief Frameless top-centered Search Everywhere overlay with scope tabs and live code preview.
 */
class CommandPaletteDialog : public QDialog {
    Q_OBJECT

public:
    using Category = PaletteItem::Category;
    using Scope = gridlock::core::utils::SearchScope;

    explicit CommandPaletteDialog(QWidget* parent = nullptr);
    ~CommandPaletteDialog() override = default;

    static int fuzzyScore(const QString& pattern, const QString& candidate,
                          QVector<int>* matchedIndices = nullptr);

    void setItems(const QVector<PaletteItem>& items);
    void addItem(const PaletteItem& item);
    void clearItems();

    const QVector<PaletteItem>& allItems() const { return m_allItems; }
    const QVector<PaletteItem>& filteredItems() const { return m_filteredItems; }

    void populateFromMainWindow(MainWindow* mainWindow);

    void setFilterText(const QString& text);
    QString filterText() const;

    Scope activeScope() const { return m_activeScope; }
    void setActiveScope(Scope scope);
    void cycleScope(int delta = 1);

    void openWithScope(Scope scope, const QString& initialQuery = QString());
    void openWithPrefix(const QString& initialPrefix = QString());

    int resultCount() const;
    int selectedIndex() const;
    void setSelectedIndex(int row);
    PaletteItem resultItemAt(int row) const;

    void moveSelection(int delta);
    bool triggerSelected(bool openInSplit = false);

    QLineEdit* inputEdit() const { return m_inputEdit; }
    QListWidget* resultsList() const { return m_resultsList; }
    QWidget* headerBar() const { return m_headerBar; }
    QToolButton* sizePresetButton() const { return m_sizePresetBtn; }
    QToolButton* closeButton() const { return m_closeBtn; }
    QToolButton* scopeTabButton(Scope scope) const;
    QPlainTextEdit* previewEdit() const { return m_previewEdit; }
    QLabel* previewHeaderLabel() const { return m_previewHeaderLabel; }
    bool isPreviewVisible() const;
    void setPreviewVisible(bool visible);
    void applySizePreset(const QString& preset = QString());
    void cycleSizePreset();

    void reloadStyle();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    struct ScoredEntry {
        int itemIndex = -1;
        int score = 0;
        QVector<int> matchedIndices;
        PaletteItem dynamicItem = {};
        bool isDynamic = false;
    };

    void syncScopeTabButtons(Scope visualScope);
    void refreshFilteredResults();
    void updatePreviewForRow(int row);
    void positionOverlayNearTopCenter();

    Scope m_activeScope = Scope::All;
    QWidget* m_headerBar = nullptr;
    QLineEdit* m_inputEdit = nullptr;
    QLabel* m_modePillLabel = nullptr;
    QLabel* m_countLabel = nullptr;
    QToolButton* m_sizePresetBtn = nullptr;
    QToolButton* m_previewToggleBtn = nullptr;
    QToolButton* m_closeBtn = nullptr;
    QVector<QToolButton*> m_scopeButtons;
    QSplitter* m_contentSplitter = nullptr;
    QListWidget* m_resultsList = nullptr;
    QWidget* m_previewPane = nullptr;
    QLabel* m_previewHeaderLabel = nullptr;
    QPlainTextEdit* m_previewEdit = nullptr;
    EditorSyntaxHighlighter* m_previewHighlighter = nullptr;

    gridlock::core::utils::SearchEverywhereEngine m_engine;
    MainWindow* m_boundMainWindow = nullptr;
    QString m_activeWorkspaceRoot;
    QVector<PaletteItem> m_allItems;
    QVector<PaletteItem> m_filteredItems;
    QVector<QVector<int>> m_filteredMatchIndices;
    QStringList m_recentFiles;
    QStringList m_recentActions;
};

namespace dialogs {
using CommandPaletteDialog = gridlock::ui::CommandPaletteDialog;
using PaletteItem = gridlock::ui::PaletteItem;
}  // namespace dialogs

}  // namespace gridlock::ui
