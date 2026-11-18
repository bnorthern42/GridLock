/**
 * @file AttachProcessDialog.hpp
 * @brief Two-tab modal dialog for attaching to local `/proc` processes/MPI jobs or remote `gdbserver`/QEMU stubs.
 *
 * Tab 1 ("Local Processes (/proc)") groups detected OpenMPI/MPICH/PMIx/SLURM processes under
 * expandable `MPI Job (N ranks)` headers and checks `kernel.yama.ptrace_scope`.
 * Tab 2 ("Remote Target (gdbserver / QEMU)") connects `gdb` or `gdb-multiarch` via
 * `-target-select remote` / `extended-remote` to single endpoints, port ranges (`localhost:1234..1237`),
 * or comma-separated `host:port` lists with local ELF DWARF symbol loading.
 */

#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QPushButton>
#include <QString>
#include <QTabWidget>
#include <QTableWidget>
#include <QTreeWidget>
#include <QWidget>

#include "../../core/hpc/ProcessScanner.hpp"

namespace gridlock::ui {

inline namespace proc_attach {

/**
 * @class AttachProcessDialog
 * @brief Interactive local `/proc` process & remote `gdbserver`/QEMU picker dialog (`File -> Attach to Process...`).
 */
class AttachProcessDialog : public QDialog {
 public:
  /**
   * @brief Constructs the two-tab attach dialog and scans @p procRoot and @p sysctlPath.
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

  /** @brief Convenience override for setting the synthetic `/proc` root directory and refreshing. */
  void setProcRoot(const QString& procRoot);

  /**
   * @brief Captures sanitized `attach_process.png` and `attach_remote.png` screenshots into @p outputDir.
   */
  static bool captureDocumentationPreviews(QWidget* parentWindow,
                                           const QString& outputDir);

  /** @brief Re-scans `m_procRoot` and rebuilds the MPI job tree, flat table, and ptrace banner. */
  void refreshProcesses();

  /** @brief Filters visible tree/table rows matching @p filterText across PID, rank, name, command, or job tag. */
  void filterProcesses(const QString& filterText);

  /** @brief Returns the primary selected OS process ID, or `-1` if none is selected. */
  qint64 selectedPid() const;

  /** @brief Returns all selected OS process IDs (expanding any selected MPI Job group into all its rank PIDs). */
  QList<qint64> selectedPids() const;

  /** @brief Returns all selected local `ProcessEntry` descriptors ordered by MPI rank. */
  QList<ProcessEntry> selectedProcesses() const;

  /** @brief Returns true when Tab 2 ("Remote Target (gdbserver / QEMU)") is the active tab. */
  bool isRemoteTargetTabActive() const;

  /** @brief Switches between Tab 0 (Local Processes) and Tab 1 (Remote Target). */
  void setRemoteTargetTabActive(bool active);

  /** @brief Expands and returns the configured `RemoteTargetSpec` list from Tab 2 controls (empty if invalid). */
  QList<RemoteTargetSpec> selectedRemoteTargets() const;

  /** @brief Returns the number of top-level MPI Job group rows currently in the tree. */
  int mpiJobGroupCount() const { return m_mpiJobGroupCount; }

  /** @brief Returns the last detected `kernel.yama.ptrace_scope` value (`0..3`). */
  int ptraceScope() const { return m_ptraceScope; }

 private:
  void updateRemoteEndpointValidation();

  QString m_procRoot;
  QString m_sysctlPath;
  int m_ptraceScope = 0;
  int m_mpiJobGroupCount = 0;
  bool m_tableExplicitlySelected = false;
  QList<ProcessEntry> m_entries;

  QTabWidget* m_tabWidget = nullptr;
  QWidget* m_ptraceBanner = nullptr;
  QLabel* m_ptraceLabel = nullptr;
  QPushButton* m_copySysctlBtn = nullptr;
  QLineEdit* m_filterEdit = nullptr;
  QCheckBox* m_myProcessesCheck = nullptr;
  QPushButton* m_refreshBtn = nullptr;
  QTreeWidget* m_processTree = nullptr;
  QTableWidget* m_processTable = nullptr;

  QLineEdit* m_remoteEndpointEdit = nullptr;
  QLineEdit* m_remoteSymbolBinaryEdit = nullptr;
  QPushButton* m_remoteSymbolBrowseBtn = nullptr;
  QComboBox* m_remoteGdbCombo = nullptr;
  QComboBox* m_remoteProtocolCombo = nullptr;
  QLabel* m_remoteHintLabel = nullptr;
  QLabel* m_remoteValidationLabel = nullptr;

  QPushButton* m_attachBtn = nullptr;
};

}  // namespace proc_attach

}  // namespace gridlock::ui

#define AttachProcessDialog proc_attach::AttachProcessDialog
