/**
 * @file SignalCatchpointDialog.hpp
 * @brief Signal Disposition Matrix (`handle <SIG>`) and C++/Syscall/dlopen Catchpoints manager dialog.
 *
 * Provides a modeless/modal two-tab workstation dialog for configuring per-signal
 * Stop / Print / Pass dispositions across standard POSIX and HPC fabric signals,
 * and installing/removing GDB catchpoints for C++ exceptions (`throw` / `rethrow` / `catch`),
 * Linux syscalls (`mmap`, `openat`, `clone3`, `ioctl`, etc.), and shared-library
 * `dlopen` / `dlclose` events.
 */

#pragma once

#include <QComboBox>
#include <QDialog>
#include <QLineEdit>
#include <QMap>
#include <QPointer>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QVector>

#include "../../core/hpc/IBackendCoordinator.hpp"

namespace gridlock::ui {

/**
 * @class SignalCatchpointDialog
 * @brief Floating/modal Signal Disposition Matrix and Catchpoints manager (`Ctrl+Alt+S`).
 */
class SignalCatchpointDialog : public QDialog {
  Q_OBJECT

 public:
  /**
   * @brief Constructs the Signal Disposition Matrix & Catchpoints dialog.
   * @param coordinator Optional initial backend coordinator pointer.
   * @param parent Optional parent widget.
   */
  explicit SignalCatchpointDialog(IBackendCoordinator* coordinator = nullptr,
                                  QWidget* parent = nullptr);

  /** @brief Virtual destructor. */
  ~SignalCatchpointDialog() override = default;

  /** @brief Binds or switches the active backend coordinator and connects live signal/catchpoint updates. */
  void setCoordinator(IBackendCoordinator* coordinator);

  /** @brief Re-applies active ThemeManager palette colors across tabs, tables, and header buttons. */
  void reloadStyle();

  /** @brief Returns the active `QTabWidget` hosting the Signal Matrix (`0`) and Catchpoints (`1`) tabs. */
  QTabWidget* tabWidget() const { return m_tabWidget; }

  /** @brief Returns the Signal Disposition Matrix table widget. */
  QTableWidget* signalTable() const { return m_signalTable; }

  /** @brief Returns the Active Catchpoints table widget. */
  QTableWidget* catchpointTable() const { return m_catchpointTable; }

  /** @brief Returns the current signal disposition row for @p signalName, or default if absent. */
  SignalDisposition dispositionForSignal(const QString& signalName) const;

  /** @brief Programmatically toggles Stop/Print/Pass for @p signalName and notifies the coordinator. */
  void setSignalDispositionRow(const QString& signalName, bool stop, bool print, bool pass);

  /** @brief Applies the HPC Quiet Defaults preset (`SIGUSR1`, `SIGUSR2`, `SIGPIPE` -> nostop, noprint, pass). */
  void applyHpcQuietDefaultsPreset();

  /** @brief Applies the Trap Arithmetic/Memory Faults preset (`SIGFPE`, `SIGBUS`, `SIGSEGV` -> stop, print, nopass). */
  void applyTrapFaultsPreset();

  /** @brief Returns the list of currently installed catchpoints displayed in the table. */
  QVector<CatchpointSpec> catchpoints() const { return m_catchpoints; }

 public slots:
  /** @brief Updates the Signal Disposition Matrix when `signalDispositionsReady` arrives from the backend. */
  void onSignalDispositionsReady(int rank, const QVector<SignalDisposition>& dispositions);

  /** @brief Rebuilds the Active Catchpoints table when `catchpointsUpdated` arrives or local rules change. */
  void onCatchpointsUpdated(const QVector<CatchpointSpec>& catchpoints);

  /** @brief Adds a C++ exception catchpoint (`throw`, `rethrow`, or `catch`) with optional regex filter. */
  void addCppCatchpoint();

  /** @brief Adds a Linux syscall catchpoint from selected quick-toggle preset chips and custom syscall list. */
  void addSyscallCatchpoint();

  /** @brief Adds a dynamic library (`dlopen`, `dlclose`, or both) catchpoint with optional library regex. */
  void addLibraryCatchpoint();

  /** @brief Removes an installed catchpoint by its assigned ID @p gdbId. */
  void removeCatchpointById(int gdbId);

 signals:
  /** @brief Emitted whenever any signal's Stop, Print, or Pass disposition is toggled in the matrix. */
  void signalDispositionChanged(const QString& signal, bool stop, bool print, bool pass);

  /** @brief Emitted whenever a catchpoint is added via the dialog. */
  void catchpointAdded(const CatchpointSpec& spec);

  /** @brief Emitted whenever a catchpoint is removed via the dialog. */
  void catchpointRemoved(int gdbId);

 private:
  void setupUi();
  void seedDefaultSignals();
  void rebuildSignalTable();
  void filterSignalTable(const QString& filterText);
  void dispatchCatchpoint(const CatchpointSpec& spec);

  QPointer<IBackendCoordinator> m_coordinator;
  QTabWidget* m_tabWidget = nullptr;
  QPushButton* m_closeBtn = nullptr;

  // Tab 1: Signal Disposition Matrix
  QLineEdit* m_signalFilterEdit = nullptr;
  QPushButton* m_hpcQuietPresetBtn = nullptr;
  QPushButton* m_trapFaultsPresetBtn = nullptr;
  QPushButton* m_refreshSignalsBtn = nullptr;
  QTableWidget* m_signalTable = nullptr;
  QVector<SignalDisposition> m_signals;
  bool m_updatingSignalTable = false;

  // Tab 2: Catchpoints
  QComboBox* m_cppEventCombo = nullptr;
  QLineEdit* m_cppRegexEdit = nullptr;
  QPushButton* m_addCppCatchBtn = nullptr;

  QVector<QPushButton*> m_syscallChipBtns;
  QLineEdit* m_syscallCustomEdit = nullptr;
  QPushButton* m_addSyscallCatchBtn = nullptr;

  QComboBox* m_libEventCombo = nullptr;
  QLineEdit* m_libRegexEdit = nullptr;
  QPushButton* m_addLibCatchBtn = nullptr;

  QTableWidget* m_catchpointTable = nullptr;
  QVector<CatchpointSpec> m_catchpoints;
  int m_localNextCatchId = 1;
};

}  // namespace gridlock::ui
