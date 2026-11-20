/**
 * @file SignalCatchpointDialog.cpp
 * @brief Implementation of the Signal Disposition Matrix & Catchpoints manager dialog.
 */

#include "SignalCatchpointDialog.hpp"

#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QVBoxLayout>

#include "../../core/hpc/GdbRankCoordinator.hpp"
#include "../../core/managers/ThemeManager.hpp"

namespace gridlock::ui {

SignalCatchpointDialog::SignalCatchpointDialog(IBackendCoordinator* coordinator, QWidget* parent)
    : QDialog(parent) {
  setObjectName(QStringLiteral("SignalCatchpointDialog"));
  setWindowTitle(QStringLiteral("Signals & Catchpoints"));
  setMinimumSize(780, 560);
  resize(860, 620);

  seedDefaultSignals();
  setupUi();
  reloadStyle();
  setCoordinator(coordinator);
}

void SignalCatchpointDialog::seedDefaultSignals() {
  m_signals = {
      {QStringLiteral("SIGFPE"), true, true, false, QStringLiteral("Arithmetic exception / floating-point fault")},
      {QStringLiteral("SIGPIPE"), false, false, true, QStringLiteral("Broken pipe on socket or MPI fabric stream")},
      {QStringLiteral("SIGUSR1"), false, false, true, QStringLiteral("User-defined signal 1 (OpenMPI / PMIx fabric)")},
      {QStringLiteral("SIGUSR2"), false, false, true, QStringLiteral("User-defined signal 2 (OpenMPI / PMIx fabric)")},
      {QStringLiteral("SIGBUS"), true, true, false, QStringLiteral("Bus error / misaligned memory or UCX RMA fault")},
      {QStringLiteral("SIGSEGV"), true, true, false, QStringLiteral("Segmentation fault / invalid memory reference")},
      {QStringLiteral("SIGABRT"), true, true, false, QStringLiteral("Process abort signal (assert / MPI_Abort)")},
      {QStringLiteral("SIGALRM"), false, false, true, QStringLiteral("Timer alarm clock expiration")},
      {QStringLiteral("SIGINT"), true, true, false, QStringLiteral("Interactive interrupt signal")},
      {QStringLiteral("SIGTERM"), true, true, true, QStringLiteral("Termination request from launcher or SLURM")},
      {QStringLiteral("SIGCHLD"), false, false, true, QStringLiteral("Child process status changed")},
      {QStringLiteral("SIGURG"), false, false, true, QStringLiteral("Urgent out-of-band socket condition")},
      {QStringLiteral("SIGILL"), true, true, false, QStringLiteral("Illegal hardware instruction")},
      {QStringLiteral("SIGTRAP"), true, false, false, QStringLiteral("Trace / breakpoint hardware trap")}};
}

void SignalCatchpointDialog::setupUi() {
  auto* rootLayout = new QVBoxLayout(this);
  rootLayout->setContentsMargins(12, 10, 12, 12);
  rootLayout->setSpacing(8);

  // Header bar with title and explicit Wayland-friendly close button.
  auto* headerLayout = new QHBoxLayout();
  auto* titleLabel = new QLabel(QStringLiteral("SIGNAL DISPOSITION MATRIX & CATCHPOINTS"), this);
  titleLabel->setObjectName(QStringLiteral("signalDialogTitleLabel"));
  m_closeBtn = new QPushButton(QStringLiteral("✕ CLOSE"), this);
  m_closeBtn->setObjectName(QStringLiteral("signalDialogCloseBtn"));
  m_closeBtn->setCursor(Qt::PointingHandCursor);
  connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::close);
  headerLayout->addWidget(titleLabel);
  headerLayout->addStretch();
  headerLayout->addWidget(m_closeBtn);
  rootLayout->addLayout(headerLayout);

  m_tabWidget = new QTabWidget(this);
  m_tabWidget->setObjectName(QStringLiteral("signalCatchpointTabs"));
  rootLayout->addWidget(m_tabWidget, 1);

  // ==================== TAB 1: SIGNAL DISPOSITION MATRIX ====================
  auto* sigTab = new QWidget(this);
  sigTab->setObjectName(QStringLiteral("signalMatrixTab"));
  auto* sigLayout = new QVBoxLayout(sigTab);
  sigLayout->setContentsMargins(8, 8, 8, 8);
  sigLayout->setSpacing(8);

  auto* sigTopBar = new QHBoxLayout();
  m_signalFilterEdit = new QLineEdit(sigTab);
  m_signalFilterEdit->setObjectName(QStringLiteral("signalFilterEdit"));
  m_signalFilterEdit->setPlaceholderText(QStringLiteral("Filter signals by name or description (e.g., SIGFPE, MPI, fault)..."));
  m_signalFilterEdit->setClearButtonEnabled(true);
  connect(m_signalFilterEdit, &QLineEdit::textChanged, this, &SignalCatchpointDialog::filterSignalTable);

  m_hpcQuietPresetBtn = new QPushButton(QStringLiteral("HPC Quiet Defaults"), sigTab);
  m_hpcQuietPresetBtn->setObjectName(QStringLiteral("hpcQuietPresetBtn"));
  m_hpcQuietPresetBtn->setToolTip(QStringLiteral("Set SIGUSR1, SIGUSR2, and SIGPIPE to nostop / noprint / pass"));
  connect(m_hpcQuietPresetBtn, &QPushButton::clicked, this, &SignalCatchpointDialog::applyHpcQuietDefaultsPreset);

  m_trapFaultsPresetBtn = new QPushButton(QStringLiteral("Trap Arithmetic/Memory Faults"), sigTab);
  m_trapFaultsPresetBtn->setObjectName(QStringLiteral("trapFaultsPresetBtn"));
  m_trapFaultsPresetBtn->setToolTip(QStringLiteral("Set SIGFPE, SIGBUS, and SIGSEGV to stop / print / nopass"));
  connect(m_trapFaultsPresetBtn, &QPushButton::clicked, this, &SignalCatchpointDialog::applyTrapFaultsPreset);

  m_refreshSignalsBtn = new QPushButton(QStringLiteral("Query GDB"), sigTab);
  m_refreshSignalsBtn->setObjectName(QStringLiteral("refreshSignalsBtn"));
  connect(m_refreshSignalsBtn, &QPushButton::clicked, this, [this]() {
    if (m_coordinator) m_coordinator->requestSignalDispositions();
  });

  sigTopBar->addWidget(m_signalFilterEdit, 1);
  sigTopBar->addWidget(m_hpcQuietPresetBtn);
  sigTopBar->addWidget(m_trapFaultsPresetBtn);
  sigTopBar->addWidget(m_refreshSignalsBtn);
  sigLayout->addLayout(sigTopBar);

  m_signalTable = new QTableWidget(0, 5, sigTab);
  m_signalTable->setObjectName(QStringLiteral("signalDispositionTable"));
  m_signalTable->setHorizontalHeaderLabels({QStringLiteral("Signal"), QStringLiteral("Stop"),
                                            QStringLiteral("Print"), QStringLiteral("Pass to App"),
                                            QStringLiteral("Description")});
  m_signalTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  m_signalTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  m_signalTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  m_signalTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
  m_signalTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
  m_signalTable->verticalHeader()->setVisible(false);
  m_signalTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_signalTable->setAlternatingRowColors(true);
  sigLayout->addWidget(m_signalTable, 1);

  connect(m_signalTable, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
    if (m_updatingSignalTable || !item) return;
    const int row = item->row();
    const int col = item->column();
    if (col < 1 || col > 3 || row < 0 || row >= m_signalTable->rowCount()) return;
    const QTableWidgetItem* sigItem = m_signalTable->item(row, 0);
    const QTableWidgetItem* stopItem = m_signalTable->item(row, 1);
    const QTableWidgetItem* printItem = m_signalTable->item(row, 2);
    const QTableWidgetItem* passItem = m_signalTable->item(row, 3);
    if (!sigItem || !stopItem || !printItem || !passItem) return;

    const QString sig = sigItem->text().trimmed().toUpper();
    const bool stop = (stopItem->checkState() == Qt::Checked);
    const bool print = (printItem->checkState() == Qt::Checked);
    const bool pass = (passItem->checkState() == Qt::Checked);

    for (auto& entry : m_signals) {
      if (entry.signal == sig) {
        entry.stop = stop;
        entry.print = print;
        entry.pass = pass;
        break;
      }
    }
    if (m_coordinator) m_coordinator->setSignalDisposition(sig, stop, print, pass);
    emit signalDispositionChanged(sig, stop, print, pass);
  });

  rebuildSignalTable();
  m_tabWidget->addTab(sigTab, QStringLiteral("Signal Disposition Matrix"));

  // ==================== TAB 2: CATCHPOINTS ====================
  auto* catchTab = new QWidget(this);
  catchTab->setObjectName(QStringLiteral("catchpointsTab"));
  auto* catchLayout = new QVBoxLayout(catchTab);
  catchLayout->setContentsMargins(8, 8, 8, 8);
  catchLayout->setSpacing(10);

  // 1. C++ Exceptions Group
  auto* cppGroup = new QGroupBox(QStringLiteral("C++ Exception Catchpoints (throw / rethrow / catch)"), catchTab);
  auto* cppLayout = new QHBoxLayout(cppGroup);
  m_cppEventCombo = new QComboBox(cppGroup);
  m_cppEventCombo->setObjectName(QStringLiteral("cppEventCombo"));
  m_cppEventCombo->addItem(QStringLiteral("throw"), static_cast<int>(CatchpointSpec::Kind::CppThrow));
  m_cppEventCombo->addItem(QStringLiteral("rethrow"), static_cast<int>(CatchpointSpec::Kind::CppRethrow));
  m_cppEventCombo->addItem(QStringLiteral("catch"), static_cast<int>(CatchpointSpec::Kind::CppCatch));

  m_cppRegexEdit = new QLineEdit(cppGroup);
  m_cppRegexEdit->setObjectName(QStringLiteral("cppRegexEdit"));
  m_cppRegexEdit->setPlaceholderText(QStringLiteral("Optional Exception Type Regex (e.g., std::bad_alloc|std::runtime_error)"));

  m_addCppCatchBtn = new QPushButton(QStringLiteral("Add C++ Catchpoint"), cppGroup);
  m_addCppCatchBtn->setObjectName(QStringLiteral("addCppCatchpointBtn"));
  connect(m_addCppCatchBtn, &QPushButton::clicked, this, &SignalCatchpointDialog::addCppCatchpoint);

  cppLayout->addWidget(new QLabel(QStringLiteral("Event:"), cppGroup));
  cppLayout->addWidget(m_cppEventCombo);
  cppLayout->addWidget(m_cppRegexEdit, 1);
  cppLayout->addWidget(m_addCppCatchBtn);
  catchLayout->addWidget(cppGroup);

  // 2. Linux Syscalls Group
  auto* sysGroup = new QGroupBox(QStringLiteral("Linux Syscall Catchpoints"), catchTab);
  auto* sysLayout = new QVBoxLayout(sysGroup);
  auto* chipsLayout = new QHBoxLayout();
  chipsLayout->addWidget(new QLabel(QStringLiteral("Presets:"), sysGroup));
  for (const QString& chipName : {QStringLiteral("mmap"), QStringLiteral("openat"), QStringLiteral("clone3"),
                                  QStringLiteral("ioctl"), QStringLiteral("futex"), QStringLiteral("socket")}) {
    auto* chipBtn = new QPushButton(chipName, sysGroup);
    chipBtn->setObjectName(QStringLiteral("syscallChip_") + chipName);
    chipBtn->setCheckable(true);
    chipBtn->setCursor(Qt::PointingHandCursor);
    m_syscallChipBtns.append(chipBtn);
    chipsLayout->addWidget(chipBtn);
  }
  chipsLayout->addStretch();
  sysLayout->addLayout(chipsLayout);

  auto* sysInputRow = new QHBoxLayout();
  m_syscallCustomEdit = new QLineEdit(sysGroup);
  m_syscallCustomEdit->setObjectName(QStringLiteral("syscallCustomEdit"));
  m_syscallCustomEdit->setPlaceholderText(QStringLiteral("Custom syscall names (comma or space separated, e.g., write, read, munmap)"));

  m_addSyscallCatchBtn = new QPushButton(QStringLiteral("Add Syscall Catchpoint"), sysGroup);
  m_addSyscallCatchBtn->setObjectName(QStringLiteral("addSyscallCatchpointBtn"));
  connect(m_addSyscallCatchBtn, &QPushButton::clicked, this, &SignalCatchpointDialog::addSyscallCatchpoint);

  sysInputRow->addWidget(m_syscallCustomEdit, 1);
  sysInputRow->addWidget(m_addSyscallCatchBtn);
  sysLayout->addLayout(sysInputRow);
  catchLayout->addWidget(sysGroup);

  // 3. Dynamic Libraries (dlopen / dlclose) Group
  auto* libGroup = new QGroupBox(QStringLiteral("Dynamic Library Catchpoints (dlopen / dlclose)"), catchTab);
  auto* libLayout = new QHBoxLayout(libGroup);
  m_libEventCombo = new QComboBox(libGroup);
  m_libEventCombo->setObjectName(QStringLiteral("libEventCombo"));
  m_libEventCombo->addItem(QStringLiteral("Load (dlopen)"), 0);
  m_libEventCombo->addItem(QStringLiteral("Unload (dlclose)"), 1);
  m_libEventCombo->addItem(QStringLiteral("Both"), 2);

  m_libRegexEdit = new QLineEdit(libGroup);
  m_libRegexEdit->setObjectName(QStringLiteral("libRegexEdit"));
  m_libRegexEdit->setPlaceholderText(QStringLiteral("Optional Library Regex (e.g., libmpi.*\\.so|libucp.*\\.so)"));

  m_addLibCatchBtn = new QPushButton(QStringLiteral("Add Library Catchpoint"), libGroup);
  m_addLibCatchBtn->setObjectName(QStringLiteral("addLibCatchpointBtn"));
  connect(m_addLibCatchBtn, &QPushButton::clicked, this, &SignalCatchpointDialog::addLibraryCatchpoint);

  libLayout->addWidget(new QLabel(QStringLiteral("Event:"), libGroup));
  libLayout->addWidget(m_libEventCombo);
  libLayout->addWidget(m_libRegexEdit, 1);
  libLayout->addWidget(m_addLibCatchBtn);
  catchLayout->addWidget(libGroup);

  // 4. Active Catchpoints Table
  m_catchpointTable = new QTableWidget(0, 4, catchTab);
  m_catchpointTable->setObjectName(QStringLiteral("activeCatchpointsTable"));
  m_catchpointTable->setHorizontalHeaderLabels(
      {QStringLiteral("ID"), QStringLiteral("Type"), QStringLiteral("Filter / Syscalls"), QStringLiteral("Action")});
  m_catchpointTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  m_catchpointTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  m_catchpointTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
  m_catchpointTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
  m_catchpointTable->verticalHeader()->setVisible(false);
  m_catchpointTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_catchpointTable->setAlternatingRowColors(true);
  catchLayout->addWidget(m_catchpointTable, 1);

  m_tabWidget->addTab(catchTab, QStringLiteral("Catchpoints"));
}

void SignalCatchpointDialog::setCoordinator(IBackendCoordinator* coordinator) {
  if (m_coordinator == coordinator) return;
  if (m_coordinator) disconnect(m_coordinator, nullptr, this, nullptr);
  m_coordinator = coordinator;
  if (m_coordinator) {
    connect(m_coordinator, &IBackendCoordinator::signalDispositionsReady,
            this, &SignalCatchpointDialog::onSignalDispositionsReady, Qt::UniqueConnection);
    connect(m_coordinator, &IBackendCoordinator::catchpointsUpdated,
            this, &SignalCatchpointDialog::onCatchpointsUpdated, Qt::UniqueConnection);
    if (auto* gdb = qobject_cast<GdbRankCoordinator*>(m_coordinator)) {
      if (!gdb->activeCatchpoints().isEmpty()) onCatchpointsUpdated(gdb->activeCatchpoints());
    }
    m_coordinator->requestSignalDispositions();
  }
}

void SignalCatchpointDialog::rebuildSignalTable() {
  if (!m_signalTable) return;
  m_updatingSignalTable = true;
  m_signalTable->setRowCount(m_signals.size());
  for (int i = 0; i < m_signals.size(); ++i) {
    const auto& s = m_signals.at(i);
    auto* sigItem = new QTableWidgetItem(s.signal);
    sigItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);

    auto makeCheckItem = [](bool checked) {
      auto* it = new QTableWidgetItem();
      it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
      it->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
      return it;
    };

    auto* descItem = new QTableWidgetItem(s.description);
    descItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);

    m_signalTable->setItem(i, 0, sigItem);
    m_signalTable->setItem(i, 1, makeCheckItem(s.stop));
    m_signalTable->setItem(i, 2, makeCheckItem(s.print));
    m_signalTable->setItem(i, 3, makeCheckItem(s.pass));
    m_signalTable->setItem(i, 4, descItem);
  }
  m_updatingSignalTable = false;
  if (m_signalFilterEdit && !m_signalFilterEdit->text().isEmpty())
    filterSignalTable(m_signalFilterEdit->text());
}

void SignalCatchpointDialog::filterSignalTable(const QString& filterText) {
  if (!m_signalTable) return;
  const QString q = filterText.trimmed();
  for (int r = 0; r < m_signalTable->rowCount(); ++r) {
    const QString sig = m_signalTable->item(r, 0) ? m_signalTable->item(r, 0)->text() : QString();
    const QString desc = m_signalTable->item(r, 4) ? m_signalTable->item(r, 4)->text() : QString();
    const bool match = q.isEmpty() || sig.contains(q, Qt::CaseInsensitive) || desc.contains(q, Qt::CaseInsensitive);
    m_signalTable->setRowHidden(r, !match);
  }
}

SignalDisposition SignalCatchpointDialog::dispositionForSignal(const QString& signalName) const {
  const QString sig = signalName.trimmed().toUpper();
  for (const auto& s : m_signals)
    if (s.signal == sig) return s;
  return {sig, true, true, false, QString()};
}

void SignalCatchpointDialog::setSignalDispositionRow(const QString& signalName, bool stop, bool print, bool pass) {
  const QString sig = signalName.trimmed().toUpper();
  if (sig.isEmpty()) return;
  bool found = false;
  for (auto& s : m_signals) {
    if (s.signal == sig) {
      s.stop = stop; s.print = print; s.pass = pass;
      found = true; break;
    }
  }
  if (!found) m_signals.append({sig, stop, print, pass, QStringLiteral("Custom POSIX signal")});
  rebuildSignalTable();
  if (m_coordinator) m_coordinator->setSignalDisposition(sig, stop, print, pass);
  emit signalDispositionChanged(sig, stop, print, pass);
}

void SignalCatchpointDialog::applyHpcQuietDefaultsPreset() {
  for (const QString& sig : {QStringLiteral("SIGUSR1"), QStringLiteral("SIGUSR2"), QStringLiteral("SIGPIPE")})
    setSignalDispositionRow(sig, false, false, true);
}

void SignalCatchpointDialog::applyTrapFaultsPreset() {
  for (const QString& sig : {QStringLiteral("SIGFPE"), QStringLiteral("SIGBUS"), QStringLiteral("SIGSEGV")})
    setSignalDispositionRow(sig, true, true, false);
}

void SignalCatchpointDialog::onSignalDispositionsReady(int rank, const QVector<SignalDisposition>& dispositions) {
  Q_UNUSED(rank);
  if (dispositions.isEmpty()) return;
  for (const auto& incoming : dispositions) {
    const QString sig = incoming.signal.trimmed().toUpper();
    bool updated = false;
    for (auto& existing : m_signals) {
      if (existing.signal == sig) {
        existing.stop = incoming.stop;
        existing.print = incoming.print;
        existing.pass = incoming.pass;
        if (!incoming.description.isEmpty()) existing.description = incoming.description;
        updated = true;
        break;
      }
    }
    if (!updated) m_signals.append(incoming);
  }
  rebuildSignalTable();
}

void SignalCatchpointDialog::dispatchCatchpoint(const CatchpointSpec& spec) {
  CatchpointSpec entry = spec;
  entry.filter = entry.filter.trimmed();
  if (m_coordinator) {
    const int beforeSize = m_catchpoints.size();
    m_coordinator->insertCatchpoint(entry);
    if (m_catchpoints.size() == beforeSize) {
      if (entry.gdbId <= 0) entry.gdbId = m_localNextCatchId++;
      m_catchpoints.append(entry);
      onCatchpointsUpdated(m_catchpoints);
    }
  } else {
    if (entry.gdbId <= 0) entry.gdbId = m_localNextCatchId++;
    m_catchpoints.append(entry);
    onCatchpointsUpdated(m_catchpoints);
  }
  emit catchpointAdded(m_catchpoints.isEmpty() ? entry : m_catchpoints.last());
}

void SignalCatchpointDialog::addCppCatchpoint() {
  const auto kind = static_cast<CatchpointSpec::Kind>(
      m_cppEventCombo ? m_cppEventCombo->currentData().toInt() : static_cast<int>(CatchpointSpec::Kind::CppThrow));
  const QString regex = m_cppRegexEdit ? m_cppRegexEdit->text().trimmed() : QString();
  dispatchCatchpoint({kind, regex, -1, true});
}

void SignalCatchpointDialog::addSyscallCatchpoint() {
  QStringList selected;
  for (QPushButton* chip : m_syscallChipBtns)
    if (chip && chip->isChecked()) selected << chip->text().trimmed();
  if (m_syscallCustomEdit && !m_syscallCustomEdit->text().trimmed().isEmpty()) {
    for (const QString& part : m_syscallCustomEdit->text().split(QRegularExpression(QStringLiteral("[,\\s]+")), Qt::SkipEmptyParts))
      if (!selected.contains(part)) selected << part;
  }
  dispatchCatchpoint({CatchpointSpec::Kind::Syscall, selected.join(QLatin1Char(' ')), -1, true});
}

void SignalCatchpointDialog::addLibraryCatchpoint() {
  const int mode = m_libEventCombo ? m_libEventCombo->currentData().toInt() : 0;
  const QString regex = m_libRegexEdit ? m_libRegexEdit->text().trimmed() : QString();
  if (mode == 0 || mode == 2) dispatchCatchpoint({CatchpointSpec::Kind::LoadLib, regex, -1, true});
  if (mode == 1 || mode == 2) dispatchCatchpoint({CatchpointSpec::Kind::UnloadLib, regex, -1, true});
}

void SignalCatchpointDialog::removeCatchpointById(int gdbId) {
  if (gdbId <= 0) return;
  if (m_coordinator) m_coordinator->removeCatchpoint(gdbId);
  for (int i = m_catchpoints.size() - 1; i >= 0; --i)
    if (m_catchpoints[i].gdbId == gdbId) m_catchpoints.removeAt(i);
  onCatchpointsUpdated(m_catchpoints);
  emit catchpointRemoved(gdbId);
}

void SignalCatchpointDialog::onCatchpointsUpdated(const QVector<CatchpointSpec>& catchpoints) {
  m_catchpoints = catchpoints;
  for (const auto& cp : m_catchpoints)
    if (cp.gdbId >= m_localNextCatchId) m_localNextCatchId = cp.gdbId + 1;
  if (!m_catchpointTable) return;
  m_catchpointTable->setRowCount(m_catchpoints.size());
  for (int i = 0; i < m_catchpoints.size(); ++i) {
    const auto& cp = m_catchpoints.at(i);
    const QString typeStr =
        (cp.kind == CatchpointSpec::Kind::CppThrow)    ? QStringLiteral("C++ throw")
        : (cp.kind == CatchpointSpec::Kind::CppRethrow) ? QStringLiteral("C++ rethrow")
        : (cp.kind == CatchpointSpec::Kind::CppCatch)   ? QStringLiteral("C++ catch")
        : (cp.kind == CatchpointSpec::Kind::Syscall)    ? QStringLiteral("Syscall")
        : (cp.kind == CatchpointSpec::Kind::LoadLib)    ? QStringLiteral("Library Load (dlopen)")
                                                        : QStringLiteral("Library Unload (dlclose)");
    auto* idItem = new QTableWidgetItem(QString::number(cp.gdbId));
    idItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    auto* typeItem = new QTableWidgetItem(typeStr);
    typeItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    auto* filterItem = new QTableWidgetItem(cp.filter.isEmpty() ? QStringLiteral("<all>") : cp.filter);
    filterItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);

    m_catchpointTable->setItem(i, 0, idItem);
    m_catchpointTable->setItem(i, 1, typeItem);
    m_catchpointTable->setItem(i, 2, filterItem);

    auto* removeBtn = new QPushButton(QStringLiteral("Remove"), m_catchpointTable);
    removeBtn->setObjectName(QStringLiteral("removeCatchpointBtn_%1").arg(cp.gdbId));
    const int idToRemove = cp.gdbId;
    connect(removeBtn, &QPushButton::clicked, this, [this, idToRemove]() { removeCatchpointById(idToRemove); });
    m_catchpointTable->setCellWidget(i, 3, removeBtn);
  }
}

void SignalCatchpointDialog::reloadStyle() {
  const auto pal = gridlock::core::ThemeManager::paletteForTheme(
      gridlock::core::ThemeManager::instance().activeThemeId());
  setStyleSheet(QStringLiteral(
      "QDialog#SignalCatchpointDialog { background-color: %1; color: %2; }"
      "QLabel#signalDialogTitleLabel { font-weight: 700; font-size: 13px; color: %3; }"
      "QPushButton#signalDialogCloseBtn { padding: 4px 10px; font-weight: 600; }"
      "QTableWidget { background-color: %4; color: %2; gridline-color: %5; }")
                    .arg(pal.canvasHex, pal.textHex, pal.accentHex, pal.headerHex, pal.borderHex));
}

}  // namespace gridlock::ui
