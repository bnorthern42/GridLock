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
#include <QPoint>
#include <QString>
#include <QTabWidget>

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

signals:
    /// Emitted when a breakpoint toggle request is dispatched via location string.
    void toggleBreakpointRequested(const QString& location);

    /// Emitted when a breakpoint is toggled directly on a source file line.
    void breakpointToggled(const QString& file, int line, bool isSet,
                           const QString& condition);

    /// Emitted when the user hovers over an identifier token in the editor.
    void hoverVariableRequested(const QString& varName, const QPoint& globalPos);

    /// Emitted to dismiss any active variable hover inspection popover.
    void hideHoverTooltip();

    /// Emitted when the user requests to pin an identifier to watch expressions.
    void pinVariableRequested(const QString& varName);

private slots:
    /**
     * @brief Handles tab close button requests to clean up view mappings.
     * @param index Tab index being closed.
     */
    void onTabCloseRequested(int index);

private:
    /// Map of canonical absolute file system paths to active SourceCodeView pointers.
    QMap<QString, SourceCodeView*> m_openFiles;
};

}  // namespace gridlock::ui
