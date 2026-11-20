/**
 * @file CheckpointDialog.cpp
 * @brief Implementation of the Copy-on-Write Fork Checkpoints manager and `/proc/[pid]/fd` + `/proc/[pid]/maps` safety inspector.
 */

#include "CheckpointDialog.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequence>
#include <QMessageBox>
#include <QRegularExpression>
#include <QShortcut>
#include <QTextStream>
#include <QVBoxLayout>

#include "../../core/hpc/GdbRankCoordinator.hpp"
#include "../../core/managers/ThemeManager.hpp"

namespace gridlock::ui {

namespace {

/** @brief Reads the raw POSIX symlink target of @p path without Qt path canonicalization. */
QString readRawSymlinkTarget(const QString& path) {
  char buf[4096];
  const QByteArray native = QFile::encodeName(path);
  const ssize_t n = ::readlink(native.constData(), buf, sizeof(buf) - 1);
  if (n > 0) {
    buf[n] = '\0';
    return QString::fromUtf8(buf, static_cast<int>(n));
  }
  return QFileInfo(path).symLinkTarget();
}

/** @brief Checks `<procRoot>/<pid>/fdinfo/<fd>` flags field for `O_WRONLY` or `O_RDWR` open mode. */
bool isFdOpenedWritable(const QString& fdInfoPath) {
  QFile f(fdInfoPath);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
  while (!f.atEnd()) {
    const QString ln = QString::fromUtf8(f.readLine()).trimmed();
    if (ln.startsWith(QStringLiteral("flags:"))) {
      bool ok = false;
      const unsigned int flags = ln.mid(6).trimmed().toUInt(&ok, 8);
      if (ok) {
        const unsigned int accMode = flags & static_cast<unsigned int>(O_ACCMODE);
        return accMode == static_cast<unsigned int>(O_WRONLY) || accMode == static_cast<unsigned int>(O_RDWR);
      }
    }
  }
  return false;
}

}  // namespace

CheckpointSafetyReport inspectProcessCheckpointSafety(qint64 pid, const QString& procRoot) {
  CheckpointSafetyReport report;
  report.pid = pid;
  if (pid <= 0) {
    report.level = SafetyLevel::Safe;
    report.badgeText = QStringLiteral("SAFE");
    report.summary = QStringLiteral("No live rank PID attached; safety scan runs automatically once a rank pauses.");
    return report;
  }

  const QString pidDir = QDir(procRoot).filePath(QString::number(pid));
  const QString fdDirPath = QDir(pidDir).filePath(QStringLiteral("fd"));
  const QString fdInfoDirPath = QDir(pidDir).filePath(QStringLiteral("fdinfo"));
  const QString mapsPath = QDir(pidDir).filePath(QStringLiteral("maps"));

  auto addBlocker = [&](const QString& entry) {
    if (!report.blockers.contains(entry)) {
      report.blockers.append(entry);
      report.details.append(QStringLiteral("[UNSAFE] %1").arg(entry));
    }
  };
  auto addWarning = [&](const QString& entry) {
    if (!report.warnings.contains(entry)) {
      report.warnings.append(entry);
      report.details.append(QStringLiteral("[WARN] %1").arg(entry));
    }
  };

  // 1. Scan `<procRoot>/<pid>/fd` symlink targets for RDMA/GPU device nodes, IPC sockets/pipes, and writable files.
  const QDir fdDir(fdDirPath);
  if (fdDir.exists()) {
    const QFileInfoList entries = fdDir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System, QDir::Name);
    for (const QFileInfo& fi : entries) {
      const QString fdName = fi.fileName();
      const QString target = readRawSymlinkTarget(fi.filePath());
      if (target.isEmpty()) continue;

      if (target.contains(QStringLiteral("/dev/infiniband/")) || target.contains(QStringLiteral("uverbs")) ||
          target.contains(QStringLiteral("/dev/nvidia")) || target.contains(QStringLiteral("/dev/kfd")) ||
          target.contains(QStringLiteral("/dev/dri/"))) {
        addBlocker(QStringLiteral("fd/%1 -> %2 (RDMA / GPU device handle)").arg(fdName, target));
      } else if (target.startsWith(QStringLiteral("socket:[")) || target.startsWith(QStringLiteral("pipe:[")) ||
                 target.startsWith(QStringLiteral("anon_inode:[eventfd")) || target.startsWith(QStringLiteral("anon_inode:[epoll")) ||
                 target.startsWith(QStringLiteral("/dev/shm/"))) {
        addWarning(QStringLiteral("fd/%1 -> %2").arg(fdName, target));
      } else if (!target.startsWith(QStringLiteral("/dev/pts/")) && target != QStringLiteral("/dev/null") &&
                 target != QStringLiteral("/dev/zero") && target != QStringLiteral("/dev/tty") &&
                 isFdOpenedWritable(QDir(fdInfoDirPath).filePath(fdName))) {
        addWarning(QStringLiteral("fd/%1 -> %2 (writable file descriptor)").arg(fdName, target));
      }
    }
  }

  // 2. Scan `<procRoot>/<pid>/maps` for shared-memory IPC, RDMA fabric, and GPU driver VMAs.
  QFile mapsFile(mapsPath);
  if (mapsFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QTextStream ts(&mapsFile);
    static const QRegularExpression wsRe(QStringLiteral("\\s+"));
    while (!ts.atEnd()) {
      const QString line = ts.readLine().trimmed();
      if (line.isEmpty()) continue;
      const QStringList parts = line.split(wsRe, Qt::SkipEmptyParts);
      if (parts.size() < 2) continue;
      const QString perms = parts.at(1);
      const QString path = (parts.size() >= 6) ? parts.mid(5).join(QLatin1Char(' ')) : QString();
      const bool isSharedWrite = perms.contains(QLatin1Char('s')) && perms.contains(QLatin1Char('w'));

      if (path.contains(QStringLiteral("/dev/infiniband/")) || path.contains(QStringLiteral("uverbs")) ||
          path.contains(QStringLiteral("mlx5")) || path.contains(QStringLiteral("/dev/nvidia")) ||
          path.contains(QStringLiteral("/dev/kfd")) || path.contains(QStringLiteral("/dev/dri/"))) {
        addBlocker(QStringLiteral("maps (%1) -> %2 (RDMA / GPU VMA)").arg(perms, path));
      } else if (path.contains(QStringLiteral("/dev/shm/")) || path.contains(QStringLiteral("SYSV")) ||
                 path.contains(QStringLiteral("psm"), Qt::CaseInsensitive) ||
                 path.contains(QStringLiteral("ucx"), Qt::CaseInsensitive) ||
                 isSharedWrite) {
        const QString desc = path.isEmpty() ? QStringLiteral("[anonymous shared mapping]") : path;
        addWarning(QStringLiteral("maps (%1) -> %2").arg(perms, desc));
      }
    }
  }

  if (!report.blockers.isEmpty()) {
    report.level = SafetyLevel::BlockedOrUnsafe;
    report.badgeText = QStringLiteral("UNSAFE: RDMA / GPU Device Mapping Detected");
    report.summary = QStringLiteral(
        "PID %1 holds %2 RDMA/GPU device mapping(s). Forking a checkpoint can trigger EFAULT or driver faults.")
        .arg(pid).arg(report.blockers.size());
  } else if (!report.warnings.isEmpty()) {
    report.level = SafetyLevel::Warning;
    report.badgeText = QStringLiteral("WARNING: Active Sockets / Pipes / SHM");
    report.summary = QStringLiteral(
        "PID %1 holds %2 IPC socket/pipe/SHM handle(s) (typical for OpenMPI/MPICH). Safe for local single-rank forks if collective barriers are not crossed.")
        .arg(pid).arg(report.warnings.size());
  } else {
    report.level = SafetyLevel::Safe;
    report.badgeText = QStringLiteral("SAFE");
    report.summary = QStringLiteral("PID %1 has no external sockets, shared-memory IPC, RDMA verbs, or GPU device mappings.").arg(pid);
  }
  return report;
}

CheckpointDialog::CheckpointDialog(IBackendCoordinator* coordinator, QWidget* parent)
    : QDialog(parent) {
  setObjectName(QStringLiteral("CheckpointDialog"));
  setWindowTitle(QStringLiteral("Process Checkpoints (Copy-on-Write)"));
  setWindowFlags(windowFlags() | Qt::Window);
  setModal(false);
  setMinimumSize(780, 480);
  resize(880, 560);

  setupUi();
  reloadStyle();
  setCoordinator(coordinator);
}

void CheckpointDialog::setupUi() {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(12, 10, 12, 12);
  root->setSpacing(8);

  // Header bar with rank selector, refresh button, and explicit Wayland-friendly close button.
  auto* header = new QHBoxLayout();
  auto* title = new QLabel(QStringLiteral("COPY-ON-WRITE PROCESS CHECKPOINTS"), this);
  title->setObjectName(QStringLiteral("checkpointDialogTitleLabel"));

  auto* rankLbl = new QLabel(QStringLiteral("Rank:"), this);
  m_rankSpin = new QSpinBox(this);
  m_rankSpin->setObjectName(QStringLiteral("checkpointRankSpin"));
  m_rankSpin->setRange(0, 4096);
  m_rankSpin->setValue(0);
  connect(m_rankSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int r) { setFocusedRank(r); });

  m_refreshBtn = new QPushButton(QStringLiteral("Rescan && Refresh"), this);
  m_refreshBtn->setObjectName(QStringLiteral("checkpointRefreshBtn"));
  connect(m_refreshBtn, &QPushButton::clicked, this, [this]() {
    refreshSafetyScan();
    if (m_coordinator) m_coordinator->requestCheckpoints(m_focusedRank);
  });

  m_closeBtn = new QPushButton(QStringLiteral("✕ CLOSE"), this);
  m_closeBtn->setObjectName(QStringLiteral("checkpointCloseBtn"));
  m_closeBtn->setCursor(Qt::PointingHandCursor);
  connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::close);

  header->addWidget(title);
  header->addStretch();
  header->addWidget(rankLbl);
  header->addWidget(m_rankSpin);
  header->addWidget(m_refreshBtn);
  header->addWidget(m_closeBtn);
  root->addLayout(header);

  // Pre-checkpoint `/proc/[pid]/fd` & `/proc/[pid]/maps` safety status banner + collapsible diagnostics.
  m_safetyBanner = new QWidget(this);
  m_safetyBanner->setObjectName(QStringLiteral("checkpointSafetyBanner"));
  auto* bannerVBox = new QVBoxLayout(m_safetyBanner);
  bannerVBox->setContentsMargins(10, 8, 10, 8);
  bannerVBox->setSpacing(6);

  auto* bannerTop = new QHBoxLayout();
  m_safetyBadgeLabel = new QLabel(QStringLiteral("SAFE"), m_safetyBanner);
  m_safetyBadgeLabel->setObjectName(QStringLiteral("checkpointSafetyBadgeLabel"));
  m_safetySummaryLabel = new QLabel(QStringLiteral("Ready to inspect target rank /proc handles."), m_safetyBanner);
  m_safetySummaryLabel->setObjectName(QStringLiteral("checkpointSafetySummaryLabel"));
  m_safetySummaryLabel->setWordWrap(true);

  m_diagToggleBtn = new QPushButton(QStringLiteral("Show /proc Details"), m_safetyBanner);
  m_diagToggleBtn->setObjectName(QStringLiteral("checkpointDiagnosticsToggleBtn"));
  m_diagToggleBtn->setCheckable(true);
  connect(m_diagToggleBtn, &QPushButton::toggled, this, [this](bool expanded) {
    if (m_diagText) m_diagText->setVisible(expanded);
    m_diagToggleBtn->setText(expanded ? QStringLiteral("Hide /proc Details") : QStringLiteral("Show /proc Details"));
  });

  bannerTop->addWidget(m_safetyBadgeLabel);
  bannerTop->addWidget(m_safetySummaryLabel, 1);
  bannerTop->addWidget(m_diagToggleBtn);
  bannerVBox->addLayout(bannerTop);

  m_diagText = new QPlainTextEdit(m_safetyBanner);
  m_diagText->setObjectName(QStringLiteral("checkpointDiagnosticsView"));
  m_diagText->setReadOnly(true);
  m_diagText->setMaximumHeight(110);
  m_diagText->setVisible(false);
  bannerVBox->addWidget(m_diagText);
  root->addWidget(m_safetyBanner);

  // Snapshot creation bar: optional note + Create CoW Checkpoint button (`Ctrl+Shift+K`).
  auto* createRow = new QHBoxLayout();
  m_noteEdit = new QLineEdit(this);
  m_noteEdit->setObjectName(QStringLiteral("checkpointNoteEdit"));
  m_noteEdit->setPlaceholderText(QStringLiteral("Optional note (e.g., before halo exchange iter 42)..."));
  m_noteEdit->setClearButtonEnabled(true);

  m_createBtn = new QPushButton(QStringLiteral("Create CoW Checkpoint (Ctrl+Shift+K)"), this);
  m_createBtn->setObjectName(QStringLiteral("createCheckpointBtn"));
  m_createBtn->setCursor(Qt::PointingHandCursor);
  connect(m_createBtn, &QPushButton::clicked, this, [this]() { createCheckpointNow(false); });

  auto* createShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_K), this);
  createShortcut->setContext(Qt::WidgetWithChildrenShortcut);
  connect(createShortcut, &QShortcut::activated, this, [this]() { createCheckpointNow(false); });

  createRow->addWidget(m_noteEdit, 1);
  createRow->addWidget(m_createBtn);
  root->addLayout(createRow);

  // Checkpoint history table (`ID`, `Rank`, `Fork PID`, `Location`, `Created`, `Note`, `Restore`, `Delete`).
  m_checkpointTable = new QTableWidget(0, 8, this);
  m_checkpointTable->setObjectName(QStringLiteral("checkpointTable"));
  m_checkpointTable->setHorizontalHeaderLabels({
      QStringLiteral("ID"), QStringLiteral("Rank"), QStringLiteral("Fork PID"),
      QStringLiteral("Location"), QStringLiteral("Created"), QStringLiteral("Note"),
      QStringLiteral("Restore"), QStringLiteral("Delete")});
  m_checkpointTable->verticalHeader()->setVisible(false);
  m_checkpointTable->setWordWrap(false);
  m_checkpointTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_checkpointTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_checkpointTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
  m_checkpointTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
  m_checkpointTable->setColumnWidth(0, 42);
  m_checkpointTable->setColumnWidth(1, 52);
  m_checkpointTable->setColumnWidth(2, 76);
  m_checkpointTable->setColumnWidth(4, 92);
  m_checkpointTable->setColumnWidth(5, 210);
  m_checkpointTable->setColumnWidth(6, 98);
  m_checkpointTable->setColumnWidth(7, 82);
  root->addWidget(m_checkpointTable, 1);
}

void CheckpointDialog::setCoordinator(IBackendCoordinator* coordinator) {
  if (m_coordinator == coordinator) {
    refreshSafetyScan();
    return;
  }
  if (m_coordinator) disconnect(m_coordinator, nullptr, this, nullptr);
  m_coordinator = coordinator;
  if (m_coordinator) {
    connect(m_coordinator, &IBackendCoordinator::checkpointsReady,
            this, &CheckpointDialog::onCheckpointsReady, Qt::UniqueConnection);
    if (auto* gdb = dynamic_cast<gridlock::GdbRankCoordinator*>(m_coordinator.data())) {
      m_checkpoints = gdb->checkpointsForRank(m_focusedRank);
      rebuildCheckpointTable();
    }
    m_coordinator->requestCheckpoints(m_focusedRank);
  }
  refreshSafetyScan();
}

void CheckpointDialog::setFocusedRank(int rank) {
  m_focusedRank = qMax(0, rank);
  if (m_rankSpin && m_rankSpin->value() != m_focusedRank) {
    m_rankSpin->blockSignals(true);
    m_rankSpin->setValue(m_focusedRank);
    m_rankSpin->blockSignals(false);
  }
  refreshSafetyScan();
  if (auto* gdb = dynamic_cast<gridlock::GdbRankCoordinator*>(m_coordinator.data())) {
    m_checkpoints = gdb->checkpointsForRank(m_focusedRank);
    rebuildCheckpointTable();
  }
  if (m_coordinator) m_coordinator->requestCheckpoints(m_focusedRank);
}

void CheckpointDialog::setProcRootForTesting(const QString& procRoot) {
  m_procRoot = procRoot.trimmed().isEmpty() ? QStringLiteral("/proc") : procRoot;
  refreshSafetyScan();
}

qint64 CheckpointDialog::activeRankPid() const {
  if (!m_coordinator) return 0;
  return m_coordinator->getPidForRank(m_focusedRank);
}

CheckpointSafetyReport CheckpointDialog::refreshSafetyScan() {
  m_lastReport = inspectProcessCheckpointSafety(activeRankPid(), m_procRoot);
  applySafetyReportToBanner(m_lastReport);
  return m_lastReport;
}

void CheckpointDialog::applySafetyReportToBanner(const CheckpointSafetyReport& report) {
  if (!m_safetyBadgeLabel || !m_safetySummaryLabel || !m_diagText) return;
  m_safetyBadgeLabel->setText(report.badgeText);
  m_safetySummaryLabel->setText(report.summary);

  QString badgeBg = QStringLiteral("#15803d");
  QString borderCol = QStringLiteral("#22c55e");
  if (report.level == SafetyLevel::Warning) {
    badgeBg = QStringLiteral("#b45309");
    borderCol = QStringLiteral("#f59e0b");
  } else if (report.level == SafetyLevel::BlockedOrUnsafe) {
    badgeBg = QStringLiteral("#b91c1c");
    borderCol = QStringLiteral("#ef4444");
  }
  m_safetyBadgeLabel->setStyleSheet(QStringLiteral(
      "background-color: %1; color: #ffffff; font-weight: 700; padding: 3px 8px; border-radius: 4px;").arg(badgeBg));
  if (m_safetyBanner) {
    m_safetyBanner->setStyleSheet(QStringLiteral(
        "QWidget#checkpointSafetyBanner { border: 1px solid %1; border-radius: 6px; }").arg(borderCol));
  }
  if (report.details.isEmpty()) {
    m_diagText->setPlainText(QStringLiteral("No hazardous /proc/%1/fd or /proc/%1/maps entries detected.").arg(report.pid));
  } else {
    m_diagText->setPlainText(report.details.join(QLatin1Char('\n')));
  }
}

bool CheckpointDialog::createCheckpointNow(bool bypassConfirm) {
  const CheckpointSafetyReport report = refreshSafetyScan();
  if (!bypassConfirm && report.level != SafetyLevel::Safe) {
    const QString detailPreview = report.details.mid(0, 8).join(QLatin1Char('\n'));
    const QString prompt = QStringLiteral("%1\n\n%2\n\nProceed with GDB copy-on-write fork checkpoint on Rank %3?")
                               .arg(report.summary, detailPreview)
                               .arg(m_focusedRank);
    if (QMessageBox::warning(this, tr("Checkpoint Safety Confirmation"), prompt,
                             QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
      return false;
    }
  }
  if (!m_coordinator) return false;
  const QString note = m_noteEdit ? m_noteEdit->text().trimmed() : QString();
  m_coordinator->createCheckpoint(m_focusedRank, note);
  if (m_noteEdit) m_noteEdit->clear();
  return true;
}

void CheckpointDialog::onCheckpointsReady(int rank, const QVector<ProcessCheckpoint>& checkpoints) {
  if (rank != m_focusedRank) return;
  m_checkpoints = checkpoints;
  rebuildCheckpointTable();
}

void CheckpointDialog::rebuildCheckpointTable() {
  if (!m_checkpointTable) return;
  m_checkpointTable->setRowCount(0);
  for (const auto& cp : m_checkpoints) {
    const int row = m_checkpointTable->rowCount();
    m_checkpointTable->insertRow(row);
    m_checkpointTable->setItem(row, 0, new QTableWidgetItem(QString::number(cp.id)));
    m_checkpointTable->setItem(row, 1, new QTableWidgetItem(QString::number(cp.rank)));
    m_checkpointTable->setItem(row, 2, new QTableWidgetItem(QString::number(cp.forkPid)));
    m_checkpointTable->setItem(row, 3, new QTableWidgetItem(cp.location));
    m_checkpointTable->setItem(row, 4, new QTableWidgetItem(cp.timestamp));
    m_checkpointTable->setItem(row, 5, new QTableWidgetItem(cp.note));

    auto* restoreBtn = new QPushButton(QStringLiteral("Restore"), m_checkpointTable);
    restoreBtn->setObjectName(QStringLiteral("checkpointRestoreBtn_%1").arg(cp.id));
    restoreBtn->setToolTip(QStringLiteral("Restore checkpoint %1 (restart %1)").arg(cp.id));
    restoreBtn->setCursor(Qt::PointingHandCursor);
    connect(restoreBtn, &QPushButton::clicked, this, [this, r = cp.rank, cid = cp.id]() {
      if (m_coordinator) m_coordinator->restoreCheckpoint(r, cid);
    });
    m_checkpointTable->setCellWidget(row, 6, restoreBtn);

    auto* delBtn = new QPushButton(QStringLiteral("Delete"), m_checkpointTable);
    delBtn->setObjectName(QStringLiteral("checkpointDeleteBtn_%1").arg(cp.id));
    delBtn->setCursor(Qt::PointingHandCursor);
    delBtn->setEnabled(cp.id > 0);
    connect(delBtn, &QPushButton::clicked, this, [this, r = cp.rank, cid = cp.id]() {
      if (m_coordinator) m_coordinator->deleteCheckpoint(r, cid);
    });
    m_checkpointTable->setCellWidget(row, 7, delBtn);
  }
}

void CheckpointDialog::reloadStyle() {
  const auto pal = core::ThemeManager::paletteForTheme(core::ThemeManager::instance().activeThemeId());
  setStyleSheet(QStringLiteral(
                    "QDialog#CheckpointDialog { background-color: %1; color: %2; }"
                    "QLabel#checkpointDialogTitleLabel { font-weight: 700; font-size: 13px; color: %3; }"
                    "QTableWidget#checkpointTable { background-color: %4; color: %2; gridline-color: %5; border: 1px solid %5; }"
                    "QHeaderView::section { background-color: %4; color: %2; padding: 4px 6px; border: 1px solid %5; }"
                    "QLineEdit, QSpinBox, QPlainTextEdit { background-color: %4; color: %2; border: 1px solid %5; border-radius: 4px; padding: 3px 6px; }"
                    "QPushButton { background-color: %4; color: %2; border: 1px solid %5; border-radius: 4px; padding: 4px 10px; }"
                    "QPushButton:hover { border-color: %3; }"
                    "QPushButton#createCheckpointBtn { background-color: %3; color: #ffffff; font-weight: 700; border: none; }"
                    "QPushButton#checkpointCloseBtn { background-color: #991b1b; color: #ffffff; font-weight: 700; border: none; }")
                    .arg(pal.canvasHex, pal.textHex, pal.accentHex, pal.headerHex, pal.borderHex));
  applySafetyReportToBanner(m_lastReport);
}

}  // namespace gridlock::ui
