/**
 * @file AttachProcessDialog.cpp
 * @brief Implementation of the two-tab local `/proc` and remote `gdbserver`/QEMU attach dialog.
 */

#include "AttachProcessDialog.hpp"
#undef AttachProcessDialog

#include <unistd.h>

#include <algorithm>

#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMap>
#include <QPainter>
#include <QSet>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QTabBar>
#include <QTemporaryDir>
#include <QVBoxLayout>

#include "../../../core/managers/ConfigManager.hpp"
#include "../../MainWindow.hpp"

namespace gridlock::ui {

inline namespace proc_attach {

namespace {

constexpr int kRolePid = Qt::UserRole;
constexpr int kRoleIsGroup = Qt::UserRole + 1;
constexpr int kRoleJobTag = Qt::UserRole + 2;

RemoteTargetDialogState& fallbackRemoteDialogState() {
  static RemoteTargetDialogState s_state;
  return s_state;
}

MainWindow* resolveHostMainWindow(QWidget* w) {
  for (QWidget* cur = w; cur != nullptr; cur = cur->parentWidget()) {
    if (auto* mw = qobject_cast<MainWindow*>(cur)) return mw;
  }
  return nullptr;
}

}  // namespace

AttachProcessDialog::AttachProcessDialog(QWidget* parent,
                                         const QString& procRoot,
                                         const QString& sysctlPath)
    : QDialog(parent), m_procRoot(procRoot), m_sysctlPath(sysctlPath) {
  setWindowTitle(QStringLiteral("Attach to Running Process / Remote GDB Target"));
  setMinimumSize(740, 460);
  resize(840, 520);

  auto* rootLayout = new QVBoxLayout(this);
  rootLayout->setContentsMargins(12, 12, 12, 12);
  rootLayout->setSpacing(8);

  m_tabWidget = new QTabWidget(this);
  m_tabWidget->setObjectName(QStringLiteral("attachModeTabWidget"));
  m_tabWidget->tabBar()->setExpanding(false);
  m_tabWidget->tabBar()->setUsesScrollButtons(false);
  rootLayout->addWidget(m_tabWidget, 1);

  // ==========================================================================
  // Tab 1: Local Processes (/proc)
  // ==========================================================================
  auto* localPage = new QWidget(m_tabWidget);
  auto* localLayout = new QVBoxLayout(localPage);
  localLayout->setContentsMargins(8, 8, 8, 8);
  localLayout->setSpacing(8);

  m_ptraceBanner = new QWidget(localPage);
  m_ptraceBanner->setObjectName(QStringLiteral("attachPtraceWarningBanner"));
  m_ptraceBanner->setStyleSheet(QStringLiteral(
      "QWidget#attachPtraceWarningBanner { background-color: #2d2216; border: 1px solid #d19a66; border-radius: 4px; }"
      "QLabel { color: #e5c07b; font-size: 11px; }"));
  auto* bannerLayout = new QHBoxLayout(m_ptraceBanner);
  bannerLayout->setContentsMargins(10, 6, 10, 6);
  bannerLayout->setSpacing(8);
  m_ptraceLabel = new QLabel(m_ptraceBanner);
  m_ptraceLabel->setObjectName(QStringLiteral("attachPtraceWarningLabel"));
  m_ptraceLabel->setWordWrap(true);
  m_copySysctlBtn = new QPushButton(QStringLiteral("Copy sysctl Fix"), m_ptraceBanner);
  m_copySysctlBtn->setObjectName(QStringLiteral("attachCopySysctlFixBtn"));
  m_copySysctlBtn->setFixedHeight(24);
  bannerLayout->addWidget(m_ptraceLabel, 1);
  bannerLayout->addWidget(m_copySysctlBtn);
  localLayout->addWidget(m_ptraceBanner);

  connect(m_copySysctlBtn, &QPushButton::clicked, this, [this]() {
    if (QClipboard* cb = QGuiApplication::clipboard()) {
      cb->setText(QStringLiteral("echo 0 | sudo tee /proc/sys/kernel/yama/ptrace_scope"));
    }
    m_copySysctlBtn->setText(QStringLiteral("Copied!"));
  });

  auto* filterRow = new QHBoxLayout();
  filterRow->setContentsMargins(0, 0, 0, 0);
  filterRow->setSpacing(8);
  auto* filterLabel = new QLabel(QStringLiteral("Filter:"), localPage);
  m_filterEdit = new QLineEdit(localPage);
  m_filterEdit->setObjectName(QStringLiteral("attachProcessFilterEdit"));
  m_filterEdit->setPlaceholderText(
      QStringLiteral("Filter by PID, process name, command line, or MPI job..."));
  m_filterEdit->setFixedHeight(24);
  m_myProcessesCheck = new QCheckBox(QStringLiteral("My Processes Only"), localPage);
  m_myProcessesCheck->setObjectName(QStringLiteral("attachMyProcessesOnlyCheck"));
  m_myProcessesCheck->setChecked(true);
  m_refreshBtn = new QPushButton(QStringLiteral("Refresh"), localPage);
  m_refreshBtn->setObjectName(QStringLiteral("attachProcessRefreshBtn"));
  m_refreshBtn->setFixedHeight(24);
  filterRow->addWidget(filterLabel);
  filterRow->addWidget(m_filterEdit, 1);
  filterRow->addWidget(m_myProcessesCheck);
  filterRow->addWidget(m_refreshBtn);
  localLayout->addLayout(filterRow);

  m_processTree = new QTreeWidget(localPage);
  m_processTree->setObjectName(QStringLiteral("attachProcessTree"));
  m_processTree->setColumnCount(4);
  m_processTree->setHeaderLabels({QStringLiteral("PID"),
                                  QStringLiteral("Rank"),
                                  QStringLiteral("Process / Command"),
                                  QStringLiteral("Executable")});
  m_processTree->setAlternatingRowColors(true);
  m_processTree->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_processTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
  m_processTree->setUniformRowHeights(true);
  m_processTree->header()->setMinimumSectionSize(56);
  m_processTree->header()->setStretchLastSection(true);
  m_processTree->setColumnWidth(0, 120);
  m_processTree->setColumnWidth(1, 90);
  m_processTree->setColumnWidth(2, 340);
  localLayout->addWidget(m_processTree, 1);

  m_processTable = new QTableWidget(0, 4, localPage);
  m_processTable->setObjectName(QStringLiteral("attachProcessTable"));
  m_processTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_processTable->setSelectionMode(QAbstractItemView::SingleSelection);
  m_processTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_processTable->setHorizontalHeaderLabels({QStringLiteral("PID"),
                                             QStringLiteral("PROCESS"),
                                             QStringLiteral("USER"),
                                             QStringLiteral("COMMAND")});
  m_processTable->setVisible(false);

  m_tabWidget->addTab(localPage, QStringLiteral("Local Processes (/proc)"));

  // ==========================================================================
  // Tab 2: Remote Target (gdbserver / QEMU)
  // ==========================================================================
  auto* remotePage = new QWidget(m_tabWidget);
  auto* remoteLayout = new QVBoxLayout(remotePage);
  remoteLayout->setContentsMargins(12, 12, 12, 12);
  remoteLayout->setSpacing(10);

  auto* formLayout = new QFormLayout();
  formLayout->setSpacing(10);

  m_remoteEndpointEdit = new QLineEdit(QStringLiteral("localhost:1234"), remotePage);
  m_remoteEndpointEdit->setObjectName(QStringLiteral("remoteEndpointEdit"));
  m_remoteEndpointEdit->setPlaceholderText(
      QStringLiteral("localhost:1234 or localhost:1234..1237 or node0:1234, node1:1234"));
  formLayout->addRow(QStringLiteral("Endpoint(s):"), m_remoteEndpointEdit);

  auto* symRow = new QHBoxLayout();
  symRow->setContentsMargins(0, 0, 0, 0);
  symRow->setSpacing(6);
  const QString defaultBin = QString::fromStdString(
      gridlock::core::ConfigManager::instance().loadProjectSettings().targetBinary);
  m_remoteSymbolBinaryEdit = new QLineEdit(defaultBin, remotePage);
  m_remoteSymbolBinaryEdit->setObjectName(QStringLiteral("remoteSymbolBinaryEdit"));
  m_remoteSymbolBinaryEdit->setPlaceholderText(
      QStringLiteral("/path/to/local/unstripped/binary (for DWARF symbols)"));
  m_remoteSymbolBrowseBtn = new QPushButton(QStringLiteral("Browse..."), remotePage);
  m_remoteSymbolBrowseBtn->setObjectName(QStringLiteral("remoteSymbolBrowseBtn"));
  m_remoteSymbolBrowseBtn->setFixedHeight(24);
  symRow->addWidget(m_remoteSymbolBinaryEdit, 1);
  symRow->addWidget(m_remoteSymbolBrowseBtn);
  formLayout->addRow(QStringLiteral("Local Symbol Binary (ELF):"), symRow);

  connect(m_remoteSymbolBrowseBtn, &QPushButton::clicked, this, [this]() {
    const QString picked = QFileDialog::getOpenFileName(
        this, QStringLiteral("Select Local ELF Symbol Binary"),
        m_remoteSymbolBinaryEdit->text());
    if (!picked.isEmpty()) m_remoteSymbolBinaryEdit->setText(picked);
  });

  m_remoteGdbCombo = new QComboBox(remotePage);
  m_remoteGdbCombo->setObjectName(QStringLiteral("remoteGdbBinaryCombo"));
  m_remoteGdbCombo->setEditable(true);
  const QStringList gdbCandidates{QStringLiteral("gdb"),
                                  QStringLiteral("gdb-multiarch"),
                                  QStringLiteral("aarch64-linux-gnu-gdb"),
                                  QStringLiteral("riscv64-linux-gnu-gdb")};
  for (const QString& cand : gdbCandidates) {
    m_remoteGdbCombo->addItem(cand, cand);
  }
  const QString cfgGdb = gridlock::core::ConfigManager::instance().getGdbPath().trimmed();
  if (!cfgGdb.isEmpty() && m_remoteGdbCombo->findText(cfgGdb) < 0) {
    m_remoteGdbCombo->insertItem(0, cfgGdb, cfgGdb);
    m_remoteGdbCombo->setCurrentIndex(0);
  }
  formLayout->addRow(QStringLiteral("GDB Binary:"), m_remoteGdbCombo);

  m_remoteProtocolCombo = new QComboBox(remotePage);
  m_remoteProtocolCombo->setObjectName(QStringLiteral("remoteProtocolModeCombo"));
  m_remoteProtocolCombo->addItem(
      QStringLiteral("remote (qemu-user -g / gdbserver)"), false);
  m_remoteProtocolCombo->addItem(
      QStringLiteral("extended-remote (gdbserver --multi)"), true);
  formLayout->addRow(QStringLiteral("Protocol Mode:"), m_remoteProtocolCombo);

  remoteLayout->addLayout(formLayout);

  m_remoteValidationLabel = new QLabel(remotePage);
  m_remoteValidationLabel->setObjectName(QStringLiteral("remoteEndpointValidationLabel"));
  remoteLayout->addWidget(m_remoteValidationLabel);

  m_remoteHintLabel = new QLabel(
      QStringLiteral("Examples:\n"
                     "  • QEMU user-mode stub:  qemu-aarch64 -g 1234 ./guest_binary\n"
                     "  • Single gdbserver:     gdbserver :1234 ./target_binary\n"
                     "  • Multi-rank port range: localhost:1234..1237 (attaches Rank 0..3)"),
      remotePage);
  m_remoteHintLabel->setObjectName(QStringLiteral("remoteCommandHintLabel"));
  m_remoteHintLabel->setWordWrap(true);
  remoteLayout->addWidget(m_remoteHintLabel);
  remoteLayout->addStretch(1);

  m_tabWidget->addTab(remotePage, QStringLiteral("Remote Target (gdbserver / QEMU)"));

  // ==========================================================================
  // Footer Action Bar
  // ==========================================================================
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  m_attachBtn = buttons->button(QDialogButtonBox::Ok);
  if (m_attachBtn) {
    m_attachBtn->setObjectName(QStringLiteral("attachSelectedBtn"));
    m_attachBtn->setText(QStringLiteral("Attach to Selected"));
  }
  for (QAbstractButton* btn : buttons->buttons()) btn->setFixedHeight(26);
  rootLayout->addWidget(buttons);

  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(m_refreshBtn, &QPushButton::clicked, this, &AttachProcessDialog::refreshProcesses);
  connect(m_myProcessesCheck, &QCheckBox::toggled, this, [this](bool) { refreshProcesses(); });
  connect(m_filterEdit, &QLineEdit::textChanged, this, &AttachProcessDialog::filterProcesses);
  connect(m_processTree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem*, int) { accept(); });
  connect(m_processTree, &QTreeWidget::itemSelectionChanged, this, [this]() {
    m_tableExplicitlySelected = false;
  });
  connect(m_processTable, &QTableWidget::itemSelectionChanged, this, [this]() {
    m_tableExplicitlySelected = true;
  });
  connect(m_remoteEndpointEdit, &QLineEdit::textChanged, this, [this](const QString&) {
    updateRemoteEndpointValidation();
    persistRemoteDialogState();
  });
  connect(m_remoteSymbolBinaryEdit, &QLineEdit::textChanged, this, [this](const QString&) {
    persistRemoteDialogState();
  });
  connect(m_remoteGdbCombo, &QComboBox::currentTextChanged, this, [this](const QString&) {
    updateRemoteEndpointValidation();
    persistRemoteDialogState();
  });
  connect(m_remoteProtocolCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
    updateRemoteEndpointValidation();
    persistRemoteDialogState();
  });
  connect(m_tabWidget, &QTabWidget::currentChanged, this, [this](int) {
    updateRemoteEndpointValidation();
    persistRemoteDialogState();
  });

  restoreRemoteDialogState();
  refreshProcesses();
  updateRemoteEndpointValidation();
}

AttachProcessDialog::~AttachProcessDialog() { persistRemoteDialogState(); }

void AttachProcessDialog::done(int result) {
  persistRemoteDialogState();
  QDialog::done(result);
}

bool AttachProcessDialog::isRemoteTargetTabActive() const {
  return m_tabWidget && m_tabWidget->currentIndex() == 1;
}

void AttachProcessDialog::setRemoteTargetTabActive(bool active) {
  if (m_tabWidget) m_tabWidget->setCurrentIndex(active ? 1 : 0);
  persistRemoteDialogState();
}

QString AttachProcessDialog::remoteEndpointText() const {
  return m_remoteEndpointEdit ? m_remoteEndpointEdit->text() : QString();
}

QString AttachProcessDialog::localSymbolBinaryText() const {
  return m_remoteSymbolBinaryEdit ? m_remoteSymbolBinaryEdit->text() : QString();
}

QString AttachProcessDialog::gdbBinaryText() const {
  return m_remoteGdbCombo ? m_remoteGdbCombo->currentText() : QStringLiteral("gdb");
}

bool AttachProcessDialog::isExtendedRemoteProtocol() const {
  return m_remoteProtocolCombo && m_remoteProtocolCombo->currentData().toBool();
}

void AttachProcessDialog::setRemoteTargetFields(const QString& endpoints,
                                                const QString& localSymbolBinary,
                                                const QString& gdbBinary,
                                                bool extendedRemote) {
  m_restoringRemoteState = true;
  if (m_remoteEndpointEdit) m_remoteEndpointEdit->setText(endpoints);
  if (m_remoteSymbolBinaryEdit) m_remoteSymbolBinaryEdit->setText(localSymbolBinary);
  if (m_remoteGdbCombo && !gdbBinary.trimmed().isEmpty()) {
    const int idx = m_remoteGdbCombo->findText(gdbBinary.trimmed());
    if (idx >= 0) m_remoteGdbCombo->setCurrentIndex(idx);
    else m_remoteGdbCombo->setEditText(gdbBinary.trimmed());
  }
  if (m_remoteProtocolCombo) m_remoteProtocolCombo->setCurrentIndex(extendedRemote ? 1 : 0);
  m_restoringRemoteState = false;
  updateRemoteEndpointValidation();
  persistRemoteDialogState();
}

void AttachProcessDialog::restoreRemoteDialogState() {
  MainWindow* mw = resolveHostMainWindow(parentWidget());
  const RemoteTargetDialogState& saved = mw ? mw->remoteTargetDialogState() : fallbackRemoteDialogState();
  if (!saved.initialized) return;
  m_restoringRemoteState = true;
  if (m_remoteEndpointEdit) { QSignalBlocker b(m_remoteEndpointEdit); m_remoteEndpointEdit->setText(saved.endpoints); }
  if (m_remoteSymbolBinaryEdit) { QSignalBlocker b(m_remoteSymbolBinaryEdit); m_remoteSymbolBinaryEdit->setText(saved.localSymbolBinary); }
  if (m_remoteGdbCombo && !saved.gdbBinary.trimmed().isEmpty()) {
    QSignalBlocker b(m_remoteGdbCombo);
    const int idx = m_remoteGdbCombo->findText(saved.gdbBinary.trimmed());
    if (idx >= 0) m_remoteGdbCombo->setCurrentIndex(idx);
    else m_remoteGdbCombo->setEditText(saved.gdbBinary.trimmed());
  }
  if (m_remoteProtocolCombo) { QSignalBlocker b(m_remoteProtocolCombo); m_remoteProtocolCombo->setCurrentIndex(saved.extendedRemote ? 1 : 0); }
  if (m_tabWidget) { QSignalBlocker b(m_tabWidget); m_tabWidget->setCurrentIndex(saved.remoteTabActive ? 1 : 0); }
  m_restoringRemoteState = false;
}

void AttachProcessDialog::persistRemoteDialogState() const {
  if (m_restoringRemoteState || !m_remoteEndpointEdit) return;
  RemoteTargetDialogState st;
  st.endpoints = remoteEndpointText();
  st.localSymbolBinary = localSymbolBinaryText();
  st.gdbBinary = gdbBinaryText();
  st.extendedRemote = isExtendedRemoteProtocol();
  st.remoteTabActive = isRemoteTargetTabActive();
  st.initialized = true;
  if (MainWindow* mw = resolveHostMainWindow(parentWidget())) mw->setRemoteTargetDialogState(st);
  else fallbackRemoteDialogState() = st;
}

QList<RemoteTargetSpec> AttachProcessDialog::selectedRemoteTargets() const {
  if (!m_remoteEndpointEdit) return {};
  const QString symBin = m_remoteSymbolBinaryEdit ? m_remoteSymbolBinaryEdit->text().trimmed() : QString();
  const QString gdbBin = m_remoteGdbCombo ? m_remoteGdbCombo->currentText().trimmed() : QStringLiteral("gdb");
  const bool extRemote = m_remoteProtocolCombo && m_remoteProtocolCombo->currentData().toBool();
  return ProcessScanner::expandRemoteEndpoints(
      m_remoteEndpointEdit->text(), symBin, gdbBin, extRemote);
}

void AttachProcessDialog::updateRemoteEndpointValidation() {
  const QList<RemoteTargetSpec> specs = selectedRemoteTargets();
  if (m_remoteValidationLabel) {
    if (specs.isEmpty()) {
      m_remoteValidationLabel->setText(
          QStringLiteral("Invalid endpoint format (expected host:port or host:start..end)"));
      m_remoteValidationLabel->setStyleSheet(QStringLiteral("color: #e06c75; font-weight: bold;"));
    } else {
      m_remoteValidationLabel->setText(
          QStringLiteral("Resolved %1 remote rank endpoint(s): %2%3")
              .arg(specs.size())
              .arg(specs.first().endpoint)
              .arg(specs.size() > 1 ? QStringLiteral(" .. %1").arg(specs.last().endpoint) : QString()));
      m_remoteValidationLabel->setStyleSheet(QStringLiteral("color: #98c379;"));
    }
  }
  if (m_attachBtn) {
    if (isRemoteTargetTabActive()) {
      m_attachBtn->setText(QStringLiteral("Connect Remote Target"));
      m_attachBtn->setEnabled(!specs.isEmpty());
    } else {
      m_attachBtn->setText(QStringLiteral("Attach to Selected"));
      m_attachBtn->setEnabled(true);
    }
  }
}

void AttachProcessDialog::setProcPaths(const QString& procRoot,
                                       const QString& sysctlPath) {
  m_procRoot = procRoot;
  m_sysctlPath = sysctlPath;
  refreshProcesses();
}

void AttachProcessDialog::setProcRoot(const QString& procRoot) {
  setProcPaths(procRoot, m_sysctlPath);
}

bool AttachProcessDialog::captureDocumentationPreviews(
    QWidget* parentWindow, const QString& outputDir) {
  if (!parentWindow) return false;
  QDir outDir(outputDir);
  if (!outDir.exists() && !outDir.mkpath(QStringLiteral("."))) return false;

  QTemporaryDir fakeProcDir;
  if (!fakeProcDir.isValid()) return false;
  const uid_t myUid = ::geteuid();

  auto writeFakeProc = [&](int pid, int ppid, const QString& comm,
                           const QStringList& argv, const QStringList& env) {
    const QString pidDir = fakeProcDir.filePath(QString::number(pid));
    QDir().mkpath(pidDir);
    QFile commF(pidDir + QStringLiteral("/comm"));
    if (commF.open(QIODevice::WriteOnly)) {
      commF.write((comm + QStringLiteral("\n")).toUtf8());
    }
    QFile statusF(pidDir + QStringLiteral("/status"));
    if (statusF.open(QIODevice::WriteOnly)) {
      statusF.write(QStringLiteral("Name:\t%1\nPPid:\t%2\nUid:\t%3\t%3\t%3\t%3\n")
                        .arg(comm)
                        .arg(ppid)
                        .arg(myUid)
                        .toUtf8());
    }
    QFile cmdF(pidDir + QStringLiteral("/cmdline"));
    if (cmdF.open(QIODevice::WriteOnly)) {
      QByteArrayList parts;
      for (const QString& a : argv) parts.append(a.toUtf8());
      cmdF.write(parts.join('\0') + '\0');
    }
    QFile envF(pidDir + QStringLiteral("/environ"));
    if (envF.open(QIODevice::WriteOnly)) {
      QByteArrayList parts;
      for (const QString& e : env) parts.append(e.toUtf8());
      envF.write(parts.join('\0') + '\0');
    }
  };

  for (int r = 0; r < 4; ++r) {
    writeFakeProc(
        41010 + r, 41000, QStringLiteral("wave_solver"),
        {QStringLiteral("/workspace/hpc/build/wave_solver"),
         QStringLiteral("--grid"), QStringLiteral("512x512"),
         QStringLiteral("--cfl"), QStringLiteral("0.45")},
        {QStringLiteral("OMPI_COMM_WORLD_RANK=%1").arg(r),
         QStringLiteral("OMPI_COMM_WORLD_SIZE=4"),
         QStringLiteral("OMPI_MCA_ess_base_jobid=41000"),
         QStringLiteral("USER=researcher")});
  }
  writeFakeProc(
      41088, 39200, QStringLiteral("mesh_partitioner"),
      {QStringLiteral("/workspace/hpc/build/mesh_partitioner"),
       QStringLiteral("--domains"), QStringLiteral("4"),
       QStringLiteral("--input"), QStringLiteral("wing_3d.msh")},
      {QStringLiteral("USER=researcher")});

  const QString sysctlPath = fakeProcDir.filePath(QStringLiteral("ptrace_scope"));
  {
    QFile sysctlF(sysctlPath);
    if (sysctlF.open(QIODevice::WriteOnly)) sysctlF.write("1\n");
  }

  MainWindow* hostMw = resolveHostMainWindow(parentWindow);
  const RemoteTargetDialogState savedBeforePreview =
      hostMw ? hostMw->remoteTargetDialogState() : fallbackRemoteDialogState();
  struct PreviewStateGuard {
    MainWindow* mw;
    RemoteTargetDialogState prev;
    ~PreviewStateGuard() {
      if (mw) mw->setRemoteTargetDialogState(prev);
      else fallbackRemoteDialogState() = prev;
    }
  } previewGuard{hostMw, savedBeforePreview};

  AttachProcessDialog dlg(parentWindow, fakeProcDir.path(), sysctlPath);
  dlg.resize(860, 520);
  dlg.show();
  if (dlg.m_processTable) {
    for (int r = 0; r < dlg.m_processTable->rowCount(); ++r) {
      if (QTableWidgetItem* uItem = dlg.m_processTable->item(r, 2)) {
        uItem->setText(QStringLiteral("researcher"));
      }
    }
  }
  if (dlg.m_processTree && dlg.m_processTree->topLevelItemCount() > 0) {
    QTreeWidgetItem* topGroup = dlg.m_processTree->topLevelItem(0);
    topGroup->setText(3, QStringLiteral("/workspace/hpc/build/wave_solver"));
    for (int i = 0; i < topGroup->childCount(); ++i) {
      topGroup->child(i)->setText(3, QStringLiteral("/workspace/hpc/build/wave_solver"));
    }
    if (dlg.m_processTree->topLevelItemCount() > 1) {
      dlg.m_processTree->topLevelItem(1)->setText(
          3, QStringLiteral("/workspace/hpc/build/mesh_partitioner"));
    }
    dlg.m_processTree->setCurrentItem(topGroup);
    topGroup->setSelected(true);
  }
  QApplication::processEvents();

  auto compositeAndSave = [&](const QString& fileName) -> bool {
    QPixmap basePix = parentWindow->grab();
    const QPixmap dlgPix = dlg.grab();
    QPainter painter(&basePix);
    painter.setPen(QColor(QStringLiteral("#3e4451")));
    const int dx = (parentWindow->width() - dlgPix.width()) / 2;
    const int dy = (parentWindow->height() - dlgPix.height()) / 2;
    painter.drawPixmap(dx, dy, dlgPix);
    painter.drawRect(dx, dy, dlgPix.width() - 1, dlgPix.height() - 1);
    painter.end();
    return basePix.save(outDir.filePath(fileName), "PNG", 88);
  };

  if (!compositeAndSave(QStringLiteral("attach_process.png"))) {
    dlg.hide();
    return false;
  }

  dlg.setRemoteTargetTabActive(true);
  if (dlg.m_remoteEndpointEdit) {
    dlg.m_remoteEndpointEdit->setText(QStringLiteral("localhost:1234..1237"));
  }
  if (dlg.m_remoteSymbolBinaryEdit) {
    dlg.m_remoteSymbolBinaryEdit->setText(
        QStringLiteral("/workspace/hpc/build/wave_solver"));
  }
  if (dlg.m_remoteGdbCombo) {
    const int idx = dlg.m_remoteGdbCombo->findText(QStringLiteral("gdb-multiarch"));
    if (idx >= 0) dlg.m_remoteGdbCombo->setCurrentIndex(idx);
  }
  if (dlg.m_remoteProtocolCombo) {
    dlg.m_remoteProtocolCombo->setCurrentIndex(0);
  }
  dlg.updateRemoteEndpointValidation();
  QApplication::processEvents();

  const bool ok = compositeAndSave(QStringLiteral("attach_remote.png"));
  dlg.hide();
  return ok;
}

void AttachProcessDialog::refreshProcesses() {
  m_ptraceScope = ProcessScanner::checkPtraceScope(m_sysctlPath);
  if (m_ptraceBanner && m_ptraceLabel) {
    const bool restricted = (m_ptraceScope > 0);
    m_ptraceBanner->setVisible(restricted);
    if (restricted) {
      m_ptraceLabel->setText(
          QStringLiteral("Warning: kernel.yama.ptrace_scope = %1. Attaching to non-child processes "
                         "may fail with EPERM unless ptrace_scope is set to 0.")
              .arg(m_ptraceScope));
    }
  }

  const bool userOnly = m_myProcessesCheck ? m_myProcessesCheck->isChecked() : true;
  m_entries = ProcessScanner::scanRunningProcesses(m_procRoot, userOnly);
  m_tableExplicitlySelected = false;
  m_mpiJobGroupCount = 0;

  if (m_processTable) {
    m_processTable->blockSignals(true);
    m_processTable->setRowCount(m_entries.size());
    for (int i = 0; i < m_entries.size(); ++i) {
      const ProcessEntry& e = m_entries[i];
      m_processTable->setItem(i, 0, new QTableWidgetItem(QString::number(e.pid)));
      m_processTable->setItem(i, 1, new QTableWidgetItem(e.name));
      m_processTable->setItem(i, 2, new QTableWidgetItem(e.user));
      m_processTable->setItem(i, 3, new QTableWidgetItem(e.cmdline));
    }
    m_processTable->blockSignals(false);
  }

  if (!m_processTree) return;
  m_processTree->blockSignals(true);
  m_processTree->clear();

  QMap<QString, QList<ProcessEntry>> mpiGroups;
  QList<ProcessEntry> standalone;
  for (const ProcessEntry& e : m_entries) {
    if (e.isMpiProcess() && !e.mpiJobTag.isEmpty()) {
      mpiGroups[e.mpiJobTag].append(e);
    } else {
      standalone.append(e);
    }
  }

  QFont boldFont = m_processTree->font();
  boldFont.setBold(true);

  for (auto it = mpiGroups.cbegin(); it != mpiGroups.cend(); ++it) {
    const QList<ProcessEntry>& ranks = it.value();
    if (ranks.isEmpty()) continue;
    ++m_mpiJobGroupCount;
    const QString binName = !ranks.first().executablePath.isEmpty()
                                ? QFileInfo(ranks.first().executablePath).fileName()
                                : ranks.first().name;
    auto* groupItem = new QTreeWidgetItem(m_processTree);
    groupItem->setText(0, QStringLiteral("MPI Job (%1 ranks)").arg(ranks.size()));
    groupItem->setText(1, QStringLiteral("0..%1").arg(ranks.size() - 1));
    groupItem->setText(2, QStringLiteral("MPI Job (%1 ranks) — %2").arg(ranks.size()).arg(binName));
    groupItem->setText(3, ranks.first().executablePath);
    groupItem->setData(0, kRolePid, static_cast<qlonglong>(ranks.first().pid));
    groupItem->setData(0, kRoleIsGroup, true);
    groupItem->setData(0, kRoleJobTag, it.key());
    for (int c = 0; c < 4; ++c) {
      groupItem->setFont(c, boldFont);
      groupItem->setForeground(c, QColor(QStringLiteral("#61afef")));
    }

    for (const ProcessEntry& r : ranks) {
      auto* rankItem = new QTreeWidgetItem(groupItem);
      rankItem->setText(0, QString::number(r.pid));
      rankItem->setText(1, r.mpiWorldSize > 0
                               ? QStringLiteral("Rank %1/%2").arg(r.mpiRank).arg(r.mpiWorldSize)
                               : QStringLiteral("Rank %1").arg(r.mpiRank));
      rankItem->setText(2, r.cmdline);
      rankItem->setText(3, r.executablePath);
      rankItem->setData(0, kRolePid, static_cast<qlonglong>(r.pid));
      rankItem->setData(0, kRoleIsGroup, false);
      rankItem->setData(0, kRoleJobTag, it.key());
    }
    groupItem->setExpanded(true);
  }

  for (const ProcessEntry& e : standalone) {
    auto* item = new QTreeWidgetItem(m_processTree);
    item->setText(0, QString::number(e.pid));
    item->setText(1, QStringLiteral("-"));
    item->setText(2, QStringLiteral("%1 — %2").arg(e.name, e.cmdline));
    item->setText(3, e.executablePath);
    item->setData(0, kRolePid, static_cast<qlonglong>(e.pid));
    item->setData(0, kRoleIsGroup, false);
  }

  m_processTree->blockSignals(false);
  if (m_filterEdit && !m_filterEdit->text().isEmpty()) {
    filterProcesses(m_filterEdit->text());
  }
  if (m_processTree->topLevelItemCount() > 0) {
    m_processTree->setCurrentItem(m_processTree->topLevelItem(0));
  }
}

void AttachProcessDialog::filterProcesses(const QString& filterText) {
  const QString query = filterText.trimmed();
  if (m_processTable) {
    for (int r = 0; r < m_processTable->rowCount(); ++r) {
      bool match = query.isEmpty();
      for (int c = 0; c < m_processTable->columnCount() && !match; ++c) {
        if (const QTableWidgetItem* it = m_processTable->item(r, c)) {
          if (it->text().contains(query, Qt::CaseInsensitive)) match = true;
        }
      }
      m_processTable->setRowHidden(r, !match);
    }
  }
  if (!m_processTree) return;
  auto matchesItem = [&query](const QTreeWidgetItem* item) {
    if (query.isEmpty()) return true;
    for (int c = 0; c < 4; ++c) {
      if (item->text(c).contains(query, Qt::CaseInsensitive)) return true;
    }
    return false;
  };
  for (int i = 0; i < m_processTree->topLevelItemCount(); ++i) {
    QTreeWidgetItem* top = m_processTree->topLevelItem(i);
    bool anyChildMatch = false;
    for (int j = 0; j < top->childCount(); ++j) {
      QTreeWidgetItem* child = top->child(j);
      const bool cm = matchesItem(child) || matchesItem(top);
      child->setHidden(!cm);
      if (cm) anyChildMatch = true;
    }
    const bool topMatch = matchesItem(top) || anyChildMatch;
    top->setHidden(!topMatch);
  }
}

QList<ProcessEntry> AttachProcessDialog::selectedProcesses() const {
  QList<ProcessEntry> result;
  QSet<pid_t> seenPids;
  auto appendByPid = [&](qint64 pid) {
    for (const ProcessEntry& e : m_entries) {
      if (static_cast<qint64>(e.pid) == pid && !seenPids.contains(e.pid)) {
        seenPids.insert(e.pid);
        result.append(e);
      }
    }
  };

  if (m_tableExplicitlySelected && m_processTable && m_processTable->currentRow() >= 0) {
    if (const QTableWidgetItem* it = m_processTable->item(m_processTable->currentRow(), 0)) {
      appendByPid(it->text().toLongLong());
      return result;
    }
  }

  if (m_processTree) {
    QList<QTreeWidgetItem*> items = m_processTree->selectedItems();
    if (items.isEmpty() && m_processTree->currentItem()) {
      items.append(m_processTree->currentItem());
    }
    for (const QTreeWidgetItem* item : items) {
      if (item->data(0, kRoleIsGroup).toBool()) {
        for (int i = 0; i < item->childCount(); ++i) {
          appendByPid(item->child(i)->data(0, kRolePid).toLongLong());
        }
      } else {
        appendByPid(item->data(0, kRolePid).toLongLong());
      }
    }
  }
  std::sort(result.begin(), result.end(), [](const ProcessEntry& a, const ProcessEntry& b) {
    if (a.mpiRank >= 0 && b.mpiRank >= 0 && a.mpiRank != b.mpiRank) return a.mpiRank < b.mpiRank;
    return a.pid < b.pid;
  });
  return result;
}

QList<qint64> AttachProcessDialog::selectedPids() const {
  QList<qint64> pids;
  for (const ProcessEntry& e : selectedProcesses()) {
    pids.append(static_cast<qint64>(e.pid));
  }
  return pids;
}

qint64 AttachProcessDialog::selectedPid() const {
  const QList<qint64> pids = selectedPids();
  return pids.isEmpty() ? -1 : pids.first();
}

}  // namespace proc_attach

}  // namespace gridlock::ui
