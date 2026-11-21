/**
 * @file CallStackDockWidget.hpp
 * @brief Multi-frame Call Stack (64-frame backtrace) and OS/OpenMP Thread selector dock widget.
 *
 * Presents live OS/OpenMP threads (`threadSelectorCombo`) and unwound stack frames
 * (`callStackTable`) for the currently focused MPI rank so clicking any caller frame
 * or thread switches `SourceCodeView`, `DisassemblyView`, `VariablesDockWidget`, and
 * `RegisterView` to that frame's execution context.
 */

#pragma once

#include "../../core/hpc/IBackendCoordinator.hpp"

#include <QComboBox>
#include <QTableWidget>
#include <QVector>
#include <QWidget>

namespace gridlock::ui {

/**
 * @class CallStackDockWidget
 * @brief Workstation dock pane displaying OS/OpenMP threads and up to 64 call-stack frames.
 *
 * Hosted inside the top-right `CallStackDock` (`VARIABLES | REGISTERS | WATCH | CALL STACK`)
 * and dynamically styled by `ThemeManager` across both dark and light themes.
 */
class CallStackDockWidget : public QWidget {
  Q_OBJECT

 public:
  /**
   * @brief Constructs the CallStackDockWidget and initializes its header bar and frame table.
   * @param parent Optional parent widget.
   */
  explicit CallStackDockWidget(QWidget* parent = nullptr);

  /**
   * @brief Virtual destructor.
   */
  ~CallStackDockWidget() override = default;

  /**
   * @brief Populates the OS/OpenMP thread selector combobox (`#threadSelectorCombo`).
   * @param threads Ordered thread descriptors reported by GDB `-thread-info` or DAP `threads`.
   */
  void setThreads(const QVector<ThreadEntry>& threads);

  /**
   * @brief Populates the 4-column call-stack table (`#callStackTable`) and highlights @p activeLevel.
   * @param frames Unwound stack frames (`0` = innermost stop point, `1+` = caller frames).
   * @param activeLevel Zero-based frame level currently inspected by the user.
   */
  void setStackFrames(const QVector<StackFrameEntry>& frames, int activeLevel = 0);

  /**
   * @brief Clears all displayed stack frames and resets the thread selector to idle state.
   */
  void clearFrames();

  /**
   * @brief Returns the currently selected 1-based OS/OpenMP thread identifier.
   * @return Active thread ID (`>= 1`).
   */
  int activeThreadId() const { return m_activeThreadId; }

  /**
   * @brief Returns the currently highlighted 0-based stack frame level.
   * @return Active stack frame level (`0` by default).
   */
  int activeFrameLevel() const { return m_activeFrameLevel; }

  /** @brief Programmatically selects and activates stack frame @p row (e.g. on right-click context menu). */
  bool selectFrameRow(int row);

  /** @brief Returns the function name of the currently active stack frame, if known. */
  QString activeFrameFunction() const;

  /**
   * @brief Reapplies active `ThemeManager` palette tokens across header chrome and frame rows.
   */
  Q_INVOKABLE void reloadStyle();

 signals:
  /**
   * @brief Emitted when the user selects a different OS/OpenMP thread in `#threadSelectorCombo`.
   * @param threadId Selected 1-based debugger thread ID.
   */
  void threadSelected(int threadId);

  /**
   * @brief Emitted when a stack frame row is clicked, double-clicked, or activated via `Enter`.
   * @param threadId Active 1-based thread ID owning the stack trace.
   * @param frameLevel Selected 0-based caller frame level.
   * @param file Resolved DWARF source file path (may be empty for stripped frames).
   * @param line 1-based DWARF source line number (`0` when unavailable).
   * @param address 64-bit instruction pointer (`$pc` / return site) for the selected frame.
   */
  void frameActivated(int threadId, int frameLevel, const QString& file,
                      int line, quint64 address);

 private slots:
  /**
   * @brief Handles user selection changes in `#threadSelectorCombo`.
   * @param index Newly selected combobox item index.
   */
  void onThreadComboChanged(int index);

  /**
   * @brief Handles row single-click, double-click, or `Enter` activation in `#callStackTable`.
   * @param row Activated zero-based table row index.
   * @param column Activated zero-based table column index.
   */
  void onFrameRowActivated(int row, int column);

 private:
  /**
   * @brief Builds the 26px thread selector strip and 4-column `#callStackTable`.
   */
  void setupUi();

  /**
   * @brief Refreshes row marker glyphs (`▶ #level`) and bold/accent styling for `m_activeFrameLevel`.
   */
  void refreshActiveRowHighlight();

  QWidget* m_headerBar = nullptr;         ///< Unified 26px top strip hosting the Thread selector.
  QComboBox* m_threadCombo = nullptr;     ///< OS / OpenMP thread selector (`#threadSelectorCombo`).
  QTableWidget* m_table = nullptr;        ///< 4-column call-stack table (`#callStackTable`).
  QVector<StackFrameEntry> m_frames;      ///< Cached stack frames for the focused MPI rank.
  QVector<ThreadEntry> m_threads;         ///< Cached OS/OpenMP threads for the focused MPI rank.
  int m_activeThreadId = 1;               ///< Currently selected 1-based thread identifier.
  int m_activeFrameLevel = 0;             ///< Currently inspected 0-based stack frame level.
};

}  // namespace gridlock::ui
