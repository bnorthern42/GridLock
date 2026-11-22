/**
 * @file CheckpointDialog.hpp
 * @brief Copy-on-Write Fork Checkpoints (`checkpoint` / `restart`) manager with `/proc/[pid]/fd` & `/proc/[pid]/maps` safety checks.
 *
 * Inspects open file descriptors and memory mappings of the focused MPI rank prior to
 * issuing GDB `checkpoint` so users are warned before forking processes with active
 * sockets, pipes, shared-memory segments, RDMA verbs, or GPU device mappings.
 */

#pragma once

#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QStringList>
#include <QTableWidget>
#include <QVector>

#include "../../../core/hpc/IBackendCoordinator.hpp"

namespace gridlock::ui {

/**
 * @enum SafetyLevel
 * @brief Risk classification for Linux `fork()`-based GDB checkpoints on a target PID.
 */
enum class SafetyLevel {
  Safe,             ///< No external IPC sockets, shared-memory IPC, RDMA verbs, or GPU VMAs detected.
  Warning,          ///< Active sockets, pipes, eventfd, `/dev/shm`, `SYSV`, or writable files (safe locally if barriers are not crossed).
  BlockedOrUnsafe   ///< Active RDMA (`/dev/infiniband`, `uverbs`, `mlx5`) or GPU (`/dev/nvidia`, `/dev/kfd`, `/dev/dri`) device mappings.
};

/**
 * @struct CheckpointSafetyReport
 * @brief Diagnostic findings produced by inspecting `/proc/<pid>/fd` and `/proc/<pid>/maps`.
 */
struct CheckpointSafetyReport {
  qint64 pid = 0;                        ///< Inspected OS process ID.
  SafetyLevel level = SafetyLevel::Safe; ///< Highest-severity classification found.
  QString badgeText;                     ///< Short banner pill badge text.
  QString summary;                       ///< Human-readable safety guidance summary.
  QStringList blockers;                  ///< RDMA / GPU device mappings (`BlockedOrUnsafe`).
  QStringList warnings;                  ///< Sockets, pipes, shared-memory IPC, or writable files (`Warning`).
  QStringList details;                   ///< Ordered diagnostic lines shown in the expandable details pane.
};

/**
 * @brief Scans `<procRoot>/<pid>/fd` and `<procRoot>/<pid>/maps` for IPC, SHM, RDMA, and GPU fork hazards.
 * @param pid Target OS process ID to inspect.
 * @param procRoot Root procfs directory (defaults to `"/proc"`, overridable for headless unit tests).
 * @return Structured safety classification and diagnostic list.
 */
CheckpointSafetyReport inspectProcessCheckpointSafety(
    qint64 pid,
    const QString& procRoot = QStringLiteral("/proc"));

/**
 * @class CheckpointDialog
 * @brief Floating Copy-on-Write Process Checkpoints manager (`Ctrl+Alt+K`) with pre-fork `/proc` safety banner.
 */
class CheckpointDialog : public QDialog {
  Q_OBJECT

 public:
  /**
   * @brief Constructs the floating Process Checkpoints dialog.
   * @param coordinator Optional initial backend coordinator.
   * @param parent Optional parent widget.
   */
  explicit CheckpointDialog(IBackendCoordinator* coordinator = nullptr,
                            QWidget* parent = nullptr);

  /** @brief Virtual destructor. */
  ~CheckpointDialog() override = default;

  /** @brief Binds or swaps the active backend coordinator and connects `checkpointsReady`. */
  void setCoordinator(IBackendCoordinator* coordinator);

  /** @brief Focuses the dialog on 0-based MPI @p rank and refreshes its `/proc` safety scan and checkpoints. */
  void setFocusedRank(int rank);

  /** @brief Overrides the `/proc` root path used by live safety scans (for deterministic headless tests). */
  void setProcRootForTesting(const QString& procRoot);

  /** @brief Re-runs the `/proc/[pid]/fd` & `/proc/[pid]/maps` safety scanner for the selected rank. */
  CheckpointSafetyReport refreshSafetyScan();

  /** @brief Returns the most recent safety scan report displayed in the banner. */
  CheckpointSafetyReport latestSafetyReport() const { return m_lastReport; }

  /** @brief Returns the currently displayed checkpoint table widget. */
  QTableWidget* checkpointTable() const { return m_checkpointTable; }

  /** @brief Returns the currently displayed checkpoints for the selected rank. */
  QVector<ProcessCheckpoint> checkpoints() const { return m_checkpoints; }

  /** @brief Re-applies active ThemeManager colors across the dialog, safety banner, and table. */
  void reloadStyle();

 public slots:
  /** @brief Updates the checkpoint table when `checkpointsReady` arrives for the active rank. */
  void onCheckpointsReady(int rank, const QVector<ProcessCheckpoint>& checkpoints);

  /**
   * @brief Runs pre-checkpoint safety checks (prompting on Warning/Unsafe unless @p bypassConfirm is true) and calls `createCheckpoint`.
   * @param bypassConfirm When true, skips interactive `QMessageBox` confirmation (used by automated tests).
   * @return True if `createCheckpoint` was dispatched to the backend coordinator.
   */
  bool createCheckpointNow(bool bypassConfirm = false);

  /** @brief Confirms naming `Rank <rank>` before restoring Checkpoint `<checkpointId>` (`restart <id>`). */
  bool restoreCheckpointWithConfirm(int rank, int checkpointId, bool bypassConfirm = false);

  /** @brief Confirms naming `Rank <rank>` before permanently deleting Checkpoint `<checkpointId>`. */
  bool deleteCheckpointWithConfirm(int rank, int checkpointId, bool bypassConfirm = false);

 private:
  void setupUi();
  void applySafetyReportToBanner(const CheckpointSafetyReport& report);
  void rebuildCheckpointTable();
  qint64 activeRankPid() const;

  QPointer<IBackendCoordinator> m_coordinator;
  QString m_procRoot = QStringLiteral("/proc");
  int m_focusedRank = 0;
  CheckpointSafetyReport m_lastReport;
  QVector<ProcessCheckpoint> m_checkpoints;

  QSpinBox* m_rankSpin = nullptr;
  QPushButton* m_refreshBtn = nullptr;
  QPushButton* m_closeBtn = nullptr;
  QWidget* m_safetyBanner = nullptr;
  QLabel* m_safetyBadgeLabel = nullptr;
  QLabel* m_safetySummaryLabel = nullptr;
  QPushButton* m_diagToggleBtn = nullptr;
  QPlainTextEdit* m_diagText = nullptr;
  QLineEdit* m_noteEdit = nullptr;
  QPushButton* m_createBtn = nullptr;
  QTableWidget* m_checkpointTable = nullptr;
};

}  // namespace gridlock::ui
