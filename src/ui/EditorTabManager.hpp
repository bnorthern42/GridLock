/**
 * @file EditorTabManager.hpp
 * @brief Multi-document source code tab manager widget for GridLock.
 *
 * Defines the EditorTabManager class, extending QTabWidget to manage
 * multiple open SourceCodeView instances with tab reordering, close buttons,
 * breakpoint propagation, and variable inspection routing.
 */

#pragma once

#include <QMap>
#include <QMessageBox>
#include <QPoint>
#include <QSplitter>
#include <QString>
#include <QTabWidget>
#include <functional>

namespace gridlock::ui {

class SourceCodeView;

/**
 * @class EditorTabManager
 * @brief Manages open source files across multiple tabs in the central workspace.
 *
 * Owns instances of SourceCodeView mapped by absolute canonical file path,
 * restores persistent breakpoints on file opening, and forwards interaction
 * signals (breakpoints, tooltips, variable inspection) to MainWindow.
 */
class EditorTabManager : public QTabWidget {
    Q_OBJECT

public:
    /**
     * @brief Constructs the editor tab manager widget.
     * @param parent Optional parent widget context.
     */
    explicit EditorTabManager(QWidget* parent = nullptr);

    /**
     * @brief Virtual destructor for clean resource release.
     */
    ~EditorTabManager() override = default;

    /**
     * @brief Opens a source file in a new tab or raises its existing tab.
     * @param filePath Relative or absolute path to the file on disk.
     * @return Pointer to the active SourceCodeView, or nullptr if read failed.
     */
    SourceCodeView* openFile(const QString& filePath);

    /**
     * @brief Returns the SourceCodeView of the currently active tab.
     * @return Pointer to current SourceCodeView, or nullptr if no tabs open.
     */
    SourceCodeView* currentSourceCodeView() const;

    /**
     * @brief Looks up an existing open SourceCodeView by canonical file path.
     * @param filePath File path to search.
     * @return Pointer to matched view or nullptr if not open.
     */
    SourceCodeView* getSourceCodeViewForFile(const QString& filePath) const;

    /**
     * @brief Closes and deallocates all currently open editor tabs.
     */
    void clearAllTabs();

    /// Callback returning Save, Discard, or Cancel for an unsaved file close prompt.
    using UnsavedPromptHandler = std::function<QMessageBox::StandardButton(const QString& filePath)>;

    /**
     * @brief Returns whether the editor tab at index has unsaved document modifications.
     * @param index Zero-based tab index.
     * @return True if the underlying SourceCodeView document is modified.
     */
    bool isTabModified(int index) const;

    /**
     * @brief Installs a custom prompt handler (primarily for automated UI tests).
     * @param handler Callback invoked when closing a dirty tab or window.
     */
    void setUnsavedPromptHandler(UnsavedPromptHandler handler) { m_unsavedPromptHandler = std::move(handler); }

    /**
     * @brief Saves the editor tab at `index` to disk and clears its dirty indicator.
     * @param index Zero-based tab index.
     * @return True if saved successfully.
     */
    bool saveTab(int index);

    /**
     * @brief Returns whether any open editor tab currently has unsaved modifications.
     * @return True if one or more tabs are modified.
     */
    bool hasModifiedTabs() const;

    /**
     * @brief Prompts Save/Discard/Cancel for every modified tab before closing the window.
     * @return True if closing may proceed (all dirty tabs saved or discarded); false on Cancel or save failure.
     */
    bool confirmCloseAllModifiedTabs();

    /**
     * @brief Saves all currently modified editor tabs to disk and clears their dirty indicators.
     * @return Number of modified tabs written to disk.
     */
    int saveAllModifiedTabs();

    /**
     * @brief Toggles the active editor tab between corresponding C/C++ header (.hpp/.h) and source (.cpp/.c) files.
     * @return True if a matching counterpart file was found and activated.
     */
    bool switchHeaderSource();

    /**
     * @brief Opens or activates `filePath` and places the editor cursor at `(oneBasedLine, zeroBasedColumn)`.
     * @param filePath Target workspace file path resolved by Go-to-Definition.
     * @param oneBasedLine 1-based target line number.
     * @param zeroBasedColumn 0-based target character column offset.
     * @return Activated SourceCodeView pointer, or nullptr if `filePath` could not be opened.
     */
    SourceCodeView* navigateToLocation(const QString& filePath, int oneBasedLine, int zeroBasedColumn = 0);

    /**
     * @brief Opens or reorients the secondary side-by-side split editor pane.
     * @param orientation Horizontal (Split Right) or Vertical (Split Down) orientation.
     */
    void splitEditor(Qt::Orientation orientation);

    /**
     * @brief Collapses and hides the secondary split editor pane.
     */
    void closeSplit();

    /**
     * @brief Returns whether the secondary split editor pane is currently active.
     * @return True when a horizontal or vertical split view is active.
     */
    bool isSplitActive() const { return m_splitActive; }

    /** @brief Updates inline variable ghost values on the active SourceCodeView. */
    void setInlineVariableValues(const QMap<QString, QString>& varValues);
    /** @brief Clears inline variable ghost values across all open SourceCodeView tabs. */
    void clearInlineVariableValues();

    /**
     * @brief Reloads styles and syntax highlighting across all open tabs and the secondary split pane.
     */
    void rehighlightAllTabs();

    /**
     * @brief Returns the active split orientation (Qt::Horizontal or Qt::Vertical).
     * @return Current split orientation.
     */
    Qt::Orientation splitOrientation() const { return m_splitOrientation; }

    /** @brief Returns the secondary SourceCodeView instance when a split is active. */
    SourceCodeView* secondaryEditorView() const { return m_splitActive ? m_secondaryEditorView : nullptr; }

    /** @brief Propagates the currently focused MPI rank index to all open editor tabs. */
    void setActiveFocusRank(int rankId);

    /** @brief Returns all open SourceCodeView tabs tracked by this manager. */
    QList<SourceCodeView*> openViews() const { return m_openFiles.values(); }

protected:
    /**
     * @brief Repositions the active split editor container when the tab widget resizes.
     * @param event Qt resize event details.
     */
    void resizeEvent(QResizeEvent* event) override;

signals:
    /// Emitted when a breakpoint toggle request is dispatched via location string.
    void toggleBreakpointRequested(const QString& location);

    /// Emitted when a breakpoint is toggled directly on a source file line.
    void breakpointToggled(const QString& file, int line, bool isSet,
                           const QString& condition);

    /// Emitted with explicit rank filter metadata whenever a tab breakpoint changes.
    void rankBreakpointChanged(const QString& file, int line, bool isSet, int rankFilter,
                               const QString& condition);

    /// Emitted when the user hovers over an identifier token in the editor.
    void hoverVariableRequested(const QString& varName, const QPoint& globalPos);

    /// Emitted to dismiss any active variable hover inspection popover.
    void hideHoverTooltip();

    /// Emitted when the user requests to pin an identifier to watch expressions.
    void pinVariableRequested(const QString& varName);

    /// Emitted when cross-file Go-to-Definition opens or switches to a target file and line.
    void definitionJumpRequested(const QString& filePath, int oneBasedLine, int zeroBasedColumn);

private slots:
    /**
     * @brief Handles tab close button requests to clean up view mappings.
     * @param index Tab index being closed.
     */
    void onTabCloseRequested(int index);

private:
    /**
     * @brief Removes the tab at `index` from tracking and schedules widget deletion without prompting.
     * @param index Zero-based tab index.
     */
    void closeTabUnchecked(int index);

    /**
     * @brief Prompts Save/Discard/Cancel via custom test handler or modal QMessageBox::warning.
     * @param filePath File path associated with the modified buffer.
     * @return User decision (`Save`, `Discard`, or `Cancel`).
     */
    QMessageBox::StandardButton promptUnsavedDecision(const QString& filePath) const;

    /**
     * @brief Updates the bounding geometry of the internal split editor container.
     */
    void updateSplitGeometry();

    /// Map of canonical absolute file system paths to active SourceCodeView pointers.
    QMap<QString, SourceCodeView*> m_openFiles;
    /// Optional custom unsaved-changes prompt callback used by headless tests.
    UnsavedPromptHandler m_unsavedPromptHandler;
    /// Internal QSplitter hosting the on-demand secondary editor tab pane.
    QSplitter* m_splitContainer = nullptr;
    /// Secondary QTabWidget displayed inside the split pane.
    QTabWidget* m_secondaryTabWidget = nullptr;
    /// Secondary SourceCodeView instance for viewing companion header/source files.
    SourceCodeView* m_secondaryEditorView = nullptr;
    /// Whether the secondary split editor pane is currently active.
    bool m_splitActive = false;
    /// Active orientation of the split editor pane (Horizontal = Right, Vertical = Down).
    Qt::Orientation m_splitOrientation = Qt::Horizontal;
    /// Focused MPI rank forwarded to open SourceCodeView rank-breakpoint actions.
    int m_activeFocusRank = 0;
};

}  // namespace gridlock::ui
