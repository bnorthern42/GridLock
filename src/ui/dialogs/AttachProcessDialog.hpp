/**
 * @file AttachProcessDialog.hpp
 * @brief Modal dialog for discovering running Linux `/proc` processes and grouped MPI jobs.
 *
 * Groups detected OpenMPI/MPICH/PMIx/SLURM processes under expandable `MPI Job (N ranks)`
 * headers, displays standalone processes, checks `kernel.yama.ptrace_scope` permissions
 * with a one-click `sysctl` clipboard fix, and supports single-PID or full-MPI-job attach.
 */

#pragma once

#include <QCheckBox>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QPushButton>
#include <QString>
#include <QTableWidget>
#include <QTreeWidget>
#include <QWidget>

#include "../../core/hpc/ProcessScanner.hpp"

namespace gridlock::ui {

inline namespace proc_attach {

/**
 * @class AttachProcessDialog
 * @brief Interactive `/proc` process & MPI job picker dialog (`File -> Attach to Process...`).
 */
class AttachProcessDialog : public QDialog {
 public:
  /**
   * @brief Constructs the attach dialog and scans @p procRoot and @p sysctlPath.
   * @param parent Optional parent widget.
   * @param procRoot Root procfs directory (defaults to `"/proc"`).
   * @param sysctlPath Path to Yama `ptrace_scope` file.
   */
  explicit AttachProcessDialog(
      QWidget* parent = nullptr,
      const QString& procRoot = QStringLiteral("/proc"),
      const QString& sysctlPath =
          QStringLiteral("/proc/sys/kernel/yama/ptrace_scope"));

  ~AttachProcessDialog() override = default;

  /**
   * @brief Overrides `/proc` and `ptrace_scope` paths (used for deterministic test fixtures) and refreshes.
   */
  void setProcPaths(
      const QString& procRoot,
      const QString& sysctlPath =
          QStringLiteral("/proc/sys/kernel/yama/ptrace_scope"));

  /** @brief Re-scans `m_procRoot` and rebuilds the MPI job tree, flat table, and ptrace banner. */
  void refreshProcesses();

  /** @brief Filters visible tree/table rows matching @p filterText across PID, rank, name, command, or job tag. */
  void filterProcesses(const QString& filterText);

  /** @brief Returns the primary selected OS process ID, or `-1` if none is selected. */
  qint64 selectedPid() const;

  /** @brief Returns all selected OS process IDs (expanding any selected MPI Job group into all its rank PIDs). */
  QList<qint64> selectedPids() const;

  /** @brief Returns all selected `ProcessEntry` descriptors ordered by MPI rank. */
  QList<ProcessEntry> selectedProcesses() const;

  /** @brief Returns the number of top-level MPI Job group rows currently in the tree. */
  int mpiJobGroupCount() const { return m_mpiJobGroupCount; }

  /** @brief Returns the last detected `kernel.yama.ptrace_scope` value (`0..3`). */
  int ptraceScope() const { return m_ptraceScope; }

 private:
  QString m_procRoot;
  QString m_sysctlPath;
  int m_ptraceScope = 0;
  int m_mpiJobGroupCount = 0;
  bool m_tableExplicitlySelected = false;
  QList<ProcessEntry> m_entries;

  QWidget* m_ptraceBanner = nullptr;
  QLabel* m_ptraceLabel = nullptr;
  QPushButton* m_copySysctlBtn = nullptr;
  QLineEdit* m_filterEdit = nullptr;
  QCheckBox* m_myProcessesCheck = nullptr;
  QPushButton* m_refreshBtn = nullptr;
  QTreeWidget* m_processTree = nullptr;
  QTableWidget* m_processTable = nullptr;
  QPushButton* m_attachBtn = nullptr;
};

}  // namespace proc_attach

}  // namespace gridlock::ui

#define AttachProcessDialog proc_attach::AttachProcessDialog
