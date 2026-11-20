/**
 * @file CommandPaletteDialog.hpp
 * @brief Unified fuzzy command palette and quick-open launcher overlay for GridLock.
 *
 * Implements CommandPaletteDialog (Ctrl+Shift+P / Ctrl+P), providing subsequence
 * fuzzy scoring with word-boundary and consecutive-run bonuses, prefix-based category
 * filtering (> for actions, @ for symbols, # for MPI ranks, theme: for themes, or
 * unified search across files, ranks, symbols, actions, and themes), custom-painted
 * match highlighting, and wrap-around keyboard navigation.
 */

#pragma once

#include <functional>

#include <QDialog>
#include <QEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QString>
#include <QVector>
#include <QWidget>

namespace gridlock::ui {

class MainWindow;

/**
 * @struct PaletteItem
 * @brief Represents a single searchable and executable entry in the Command Palette.
 */
struct PaletteItem {
    /**
     * @enum Category
     * @brief Functional domain classification of a command palette entry.
     */
    enum class Category {
        Action,  ///< Menu bar action, toolbar command, or keyboard shortcut workflow (>).
        File,    ///< Workspace or tutorial source, header, or build file (Ctrl+P default).
        Symbol,  ///< C/C++ function, struct, class, or ELF symbol entry (@).
        Rank,    ///< Active MPI process rank or cluster execution context (#).
        Theme    ///< Built-in workstation color theme from ThemeManager (theme:).
    };

    Category category = Category::Action;  ///< Domain category used for prefix filtering and badge styling.
    QString title;                         ///< Primary searchable label displayed in the result row.
    QString subtitle;                      ///< Secondary context (relative path, menu path, symbol location, or rank state).
    QString badge;                         ///< Right-aligned keybinding, rank status, or address badge.
    std::function<void()> callback;        ///< Action invoked when the user activates the item with Enter/Return.
};

/**
 * @brief Computes a deterministic subsequence fuzzy match score between @p pattern and @p candidate.
 * @param pattern User search query string.
 * @param candidate Candidate item string to score against.
 * @param matchedIndices Optional output vector populated with 0-based matched character indices in @p candidate.
 * @return Non-negative match score (>= 0) when @p pattern is an ordered subsequence of @p candidate; -1 otherwise.
 */
int fuzzyScore(const QString& pattern, const QString& candidate,
               QVector<int>* matchedIndices = nullptr);

/**
 * @class CommandPaletteDialog
 * @brief Frameless top-centered fuzzy launcher overlay for actions, files, symbols, MPI ranks, and themes.
 */
class CommandPaletteDialog : public QDialog {
    Q_OBJECT

public:
    using Category = PaletteItem::Category;

    /**
     * @brief Constructs the CommandPaletteDialog anchored to @p parent.
     * @param parent Owning MainWindow or parent widget.
     */
    explicit CommandPaletteDialog(QWidget* parent = nullptr);

    /**
     * @brief Virtual destructor.
     */
    ~CommandPaletteDialog() override = default;

    /**
     * @brief Deterministic subsequence fuzzy scorer with prefix, word-boundary, and consecutive-run bonuses.
     * @param pattern User search query string.
     * @param candidate Candidate string to score.
     * @param matchedIndices Optional output list of matched character positions in @p candidate.
     * @return Match score (>= 0) if @p pattern is a subsequence of @p candidate, or -1 if not matched.
     */
    static int fuzzyScore(const QString& pattern, const QString& candidate,
                          QVector<int>* matchedIndices = nullptr);

    /**
     * @brief Replaces the full set of registered palette items and refreshes the filtered view.
     * @param items Vector of PaletteItem entries across all categories.
     */
    void setItems(const QVector<PaletteItem>& items);

    /**
     * @brief Appends a single PaletteItem to the dialog's item catalog and refreshes results.
     * @param item PaletteItem entry to register.
     */
    void addItem(const PaletteItem& item);

    /**
     * @brief Removes all registered items and clears the results list.
     */
    void clearItems();

    /**
     * @brief Returns all registered unfiltered palette items.
     * @return Const reference to the internal PaletteItem catalog.
     */
    const QVector<PaletteItem>& allItems() const { return m_allItems; }

    /**
     * @brief Returns the currently visible filtered and fuzzy-sorted palette items.
     * @return Const reference to the filtered PaletteItem list.
     */
    const QVector<PaletteItem>& filteredItems() const { return m_filteredItems; }

    /**
     * @brief Populates actions, workspace/tutorial files, symbols, MPI ranks, and themes from @p mainWindow.
     * @param mainWindow Pointer to the owning MainWindow workstation instance.
     */
    void populateFromMainWindow(MainWindow* mainWindow);

    /**
     * @brief Sets the query input text (including any mode prefix such as ">", "@", or "#") and re-filters.
     * @param text Raw filter text string.
     */
    void setFilterText(const QString& text);

    /**
     * @brief Returns the current text in the command palette search input box.
     * @return Raw query text string.
     */
    QString filterText() const;

    /**
     * @brief Opens and centers the palette near the top of the parent window with @p initialPrefix pre-filled.
     * @param initialPrefix Mode prefix (">" for commands, "" for quick-open files/unified, "@" for symbols, "#" for ranks).
     */
    void openWithPrefix(const QString& initialPrefix = QString());

    /**
     * @brief Returns the number of items currently matching the active filter.
     * @return Count of visible rows in CommandPaletteResults.
     */
    int resultCount() const;

    /**
     * @brief Returns the 0-based index of the currently highlighted result row, or -1 if empty.
     * @return Selected row index.
     */
    int selectedIndex() const;

    /**
     * @brief Programmatically selects a specific result row index.
     * @param row 0-based result row index.
     */
    void setSelectedIndex(int row);

    /**
     * @brief Returns the filtered PaletteItem at 0-based index @p row.
     * @param row 0-based row in the filtered results list.
     * @return Matching PaletteItem, or default-constructed PaletteItem if out of bounds.
     */
    PaletteItem resultItemAt(int row) const;

    /**
     * @brief Moves the current selection by @p delta rows with wrap-around navigation.
     * @param delta Row offset (+1 for Down, -1 for Up).
     */
    void moveSelection(int delta);

    /**
     * @brief Executes the callback of the currently selected result item and accepts the dialog.
     * @return True if a valid item was selected and executed; false otherwise.
     */
    bool triggerSelected();

    /**
     * @brief Accessor for the top search input QLineEdit (#CommandPaletteInput).
     * @return Pointer to the search QLineEdit.
     */
    QLineEdit* inputEdit() const { return m_inputEdit; }

    /**
     * @brief Accessor for the results QListWidget (#CommandPaletteResults).
     * @return Pointer to the results QListWidget.
     */
    QListWidget* resultsList() const { return m_resultsList; }

    /**
     * @brief Reloads palette colors and borders from the active ThemeManager theme.
     */
    void reloadStyle();

protected:
    /**
     * @brief Intercepts Up/Down/Return/Escape and Ctrl+N/Ctrl+P navigation key events from the input and list.
     * @param watched Child widget receiving the event.
     * @param event Event payload.
     * @return True if the key press was handled by palette navigation.
     */
    bool eventFilter(QObject* watched, QEvent* event) override;

    /**
     * @brief Handles key presses delivered directly to the dialog frame.
     * @param event Key press event payload.
     */
    void keyPressEvent(QKeyEvent* event) override;

private:
    /**
     * @struct ScoredEntry
     * @brief Internal pairing of a PaletteItem index with its computed fuzzy score and matched character indices.
     */
    struct ScoredEntry {
        int itemIndex = -1;
        int score = 0;
        QVector<int> matchedIndices;
    };

    /**
     * @brief Recomputes the filtered and sorted result list whenever the input text or item catalog changes.
     */
    void refreshFilteredResults();

    /**
     * @brief Positions the overlay horizontally centered near the top of the parent MainWindow.
     */
    void positionOverlayNearTopCenter();

    QLineEdit* m_inputEdit = nullptr;
    QLabel* m_modePillLabel = nullptr;
    QLabel* m_countLabel = nullptr;
    QListWidget* m_resultsList = nullptr;
    QVector<PaletteItem> m_allItems;
    QVector<PaletteItem> m_filteredItems;
    QVector<QVector<int>> m_filteredMatchIndices;
    QStringList m_cachedWorkspaceFiles;
    QString m_cachedWorkspaceRoot;
};

namespace dialogs {
using CommandPaletteDialog = gridlock::ui::CommandPaletteDialog;
using PaletteItem = gridlock::ui::PaletteItem;
}  // namespace dialogs

}  // namespace gridlock::ui
