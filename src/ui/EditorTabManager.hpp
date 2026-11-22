/**
 * @file EditorTabManager.hpp
 * @brief Multi-document source code tab manager and QSplitter split-editor container.
 *
 * Hosts a primary QTabWidget and an on-demand secondary split QTabWidget inside a
 * real QSplitter layout so both panes resize with full scrollbar visibility, active
 * editor tracking, gutter breakpoint forwarding, and dirty-buffer safeguards.
 */

#pragma once

#include <functional>

#include <QList>
#include <QMap>
#include <QMessageBox>
#include <QPoint>
#include <QSplitter>
#include <QString>
#include <QTabBar>
#include <QTabWidget>
#include <QWidget>

namespace gridlock::ui {

class SourceCodeView;

/**
 * @class EditorTabManager
 * @brief Manages open source files across primary and split editor panes in the central workspace.
 *
 * Embeds the primary and secondary `QTabWidget` instances inside a `QSplitter` managed by
 * a zero-margin layout, eliminating widget-overlay occlusion while forwarding tab, breakpoint,
 * hover, and Go-to-Definition signals to `MainWindow`.
 */
class EditorTabManager : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief Constructs the editor tab manager widget and internal horizontal QSplitter.
     * @param parent Optional parent widget context.
     */
    explicit EditorTabManager(QWidget* parent = nullptr);

    /** @brief Virtual destructor. */
    ~EditorTabManager() override = default;

    /**
     * @brief Opens a source file in a new primary tab or activates its existing tab.
     * @param filePath Relative or absolute path to the file on disk.
     * @return Pointer to the active SourceCodeView, or nullptr if read failed.
     */
    SourceCodeView* openFile(const QString& filePath);

    /// Byte threshold (256 KB) above which `.csv`/`.tsv` files open in the virtualized CsvTablePreviewWidget.
    static constexpr qint64 kLargeCsvPreviewThresholdBytes = 256 * 1024;

    /**
     * @brief Opens up to @p maxBytes of @p filePath in a fast raw-text SourceCodeView (no highlighting/wrap).
     * @param filePath Path to the file on disk.
     * @param maxBytes Maximum byte prefix to read (default 2 MB).
     * @return Pointer to the opened SourceCodeView, or nullptr if read failed.
     */
    SourceCodeView* openRawTextPreview(const QString& filePath, qint64 maxBytes = 2 * 1024 * 1024);

    /**
     * @brief Returns the focused SourceCodeView across both split panes (falling back to primary active tab).
     * @return Pointer to current SourceCodeView, or nullptr if no tabs are open.
     */
    SourceCodeView* currentSourceCodeView() const;

    /**
     * @brief Looks up an existing open SourceCodeView by canonical file path across primary and split panes.
     * @param filePath File path to search.
     * @return Pointer to matched view or nullptr if not open.
     */
    SourceCodeView* getSourceCodeViewForFile(const QString& filePath) const;

    /** @brief Closes and deallocates all open primary tabs and collapses any active split. */
    void clearAllTabs();

    /// Callback returning Save, Discard, or Cancel for an unsaved file close prompt.
    using UnsavedPromptHandler = std::function<QMessageBox::StandardButton(const QString& filePath)>;

    /** @brief Returns whether the primary editor tab at @p index has unsaved document modifications. */
    bool isTabModified(int index) const;

    /** @brief Installs a custom prompt handler (primarily for automated UI tests). */
    void setUnsavedPromptHandler(UnsavedPromptHandler handler) { m_unsavedPromptHandler = std::move(handler); }

    /** @brief Saves the primary editor tab at @p index to disk and clears its dirty indicator. */
    bool saveTab(int index);

    /** @brief Returns whether any open primary or active split editor tab has unsaved modifications. */
    bool hasModifiedTabs() const;

    /** @brief Prompts Save/Discard/Cancel for every modified primary or split tab before closing. */
    bool confirmCloseAllModifiedTabs();

    /** @brief Saves all modified primary and split editor buffers to disk. */
    int saveAllModifiedTabs();

    /** @brief Toggles the active editor tab between corresponding C/C++ header and source files. */
    bool switchHeaderSource();

    /** @brief Opens or activates @p filePath and places the cursor at `(oneBasedLine, zeroBasedColumn)`. */
    SourceCodeView* navigateToLocation(const QString& filePath, int oneBasedLine, int zeroBasedColumn = 0);

    /** @brief Shows or reorients the secondary split editor pane inside the layout-managed QSplitter. */
    void splitEditor(Qt::Orientation orientation);

    /** @brief Collapses and hides the secondary split editor pane, restoring full width to the primary pane. */
    void closeSplit();

    /** @brief Returns whether the secondary split editor pane is currently visible/active. */
    bool isSplitActive() const { return m_splitActive; }

    /** @brief Returns the active split orientation (`Qt::Horizontal` or `Qt::Vertical`). */
    Qt::Orientation splitOrientation() const { return m_splitOrientation; }

    /** @brief Returns the internal layout-managed `QSplitter` hosting primary and secondary tab panes. */
    QSplitter* editorSplitter() const { return m_splitter; }

    /** @brief Returns the primary `QTabWidget` pane. */
    QTabWidget* primaryTabWidget() const { return m_primaryTabWidget; }

    /** @brief Returns the secondary `QTabWidget` pane. */
    QTabWidget* secondaryTabWidget() const { return m_secondaryTabWidget; }

    /** @brief Returns the secondary `SourceCodeView` when a split is active, or nullptr otherwise. */
    SourceCodeView* secondaryEditorView() const { return m_splitActive ? m_secondaryEditorView : nullptr; }

    /** @brief Updates inline variable ghost values on the active SourceCodeView. */
    void setInlineVariableValues(const QMap<QString, QString>& varValues);

    /** @brief Clears inline variable ghost values across all open SourceCodeView panes. */
    void clearInlineVariableValues();

    /** @brief Reloads styles and syntax highlighting across all open tabs and the secondary split pane. */
    void rehighlightAllTabs();

    /** @brief Propagates the currently focused MPI rank index to all open editor panes. */
    void setActiveFocusRank(int rankId);

    /** @brief Returns all open SourceCodeView instances (including the secondary split view when active). */
    QList<SourceCodeView*> openViews() const;

    // QTabWidget-compatible forwarding API for callers managing primary tabs.
    int count() const { return m_primaryTabWidget ? m_primaryTabWidget->count() : 0; }
    QWidget* widget(int index) const { return m_primaryTabWidget ? m_primaryTabWidget->widget(index) : nullptr; }
    int indexOf(const QWidget* w) const { return m_primaryTabWidget ? m_primaryTabWidget->indexOf(const_cast<QWidget*>(w)) : -1; }
    int currentIndex() const { return m_primaryTabWidget ? m_primaryTabWidget->currentIndex() : -1; }
    void setCurrentIndex(int index);
    QWidget* currentWidget() const { return m_primaryTabWidget ? m_primaryTabWidget->currentWidget() : nullptr; }
    void setCurrentWidget(QWidget* w);
    QString tabText(int index) const { return m_primaryTabWidget ? m_primaryTabWidget->tabText(index) : QString(); }
    void setTabText(int index, const QString& text);
    QTabBar* tabBar() const { return m_primaryTabWidget ? m_primaryTabWidget->tabBar() : nullptr; }
    void setDocumentMode(bool enabled);

signals:
    /// Emitted when the active tab or focused split pane changes.
    void currentChanged(int index);

    /// Emitted when a primary tab close button is clicked (or triggered programmatically).
    void tabCloseRequested(int index);

    /// Emitted when a breakpoint toggle request is dispatched via location string.
    void toggleBreakpointRequested(const QString& location);

    /// Emitted when a breakpoint is toggled directly on a source file line.
    void breakpointToggled(const QString& file, int line, bool isSet, const QString& condition);

    /// Emitted with explicit rank filter metadata whenever a tab breakpoint changes.
    void rankBreakpointChanged(const QString& file, int line, bool isSet, int rankFilter, const QString& condition);

    /// Emitted when the user hovers over an identifier token in either editor pane.
    void hoverVariableRequested(const QString& varName, const QPoint& globalPos);

    /// Emitted to dismiss any active variable hover inspection popover.
    void hideHoverTooltip();

    /// Emitted when the user requests to pin an identifier to watch expressions.
    void pinVariableRequested(const QString& varName);

    /// Emitted when cross-file Go-to-Definition opens or switches to a target file and line.
    void definitionJumpRequested(const QString& filePath, int oneBasedLine, int zeroBasedColumn);

protected:
    /** @brief Tracks focus transitions between primary and secondary split editors. */
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    /** @brief Handles primary tab close requests with dirty-buffer Save/Discard/Cancel prompts. */
    void onTabCloseRequested(int index);

private:
    /** @brief Connects breakpoint, hover, and navigation signals and focus tracking on @p view. */
    void wireEditorView(SourceCodeView* view);

    /** @brief Removes the primary tab at @p index from tracking and schedules deletion without prompting. */
    void closeTabUnchecked(int index);

    /** @brief Prompts Save/Discard/Cancel via custom test handler or modal QMessageBox::warning. */
    QMessageBox::StandardButton promptUnsavedDecision(const QString& filePath) const;

    QMap<QString, SourceCodeView*> m_openFiles;
    QMap<QString, QWidget*> m_openPreviewTabs;
    UnsavedPromptHandler m_unsavedPromptHandler;
    QSplitter* m_splitter = nullptr;
    QTabWidget* m_primaryTabWidget = nullptr;
    QTabWidget* m_secondaryTabWidget = nullptr;
    SourceCodeView* m_secondaryEditorView = nullptr;
    bool m_splitActive = false;
    bool m_lastActiveSecondary = false;
    Qt::Orientation m_splitOrientation = Qt::Horizontal;
    int m_activeFocusRank = 0;
};

}  // namespace gridlock::ui
