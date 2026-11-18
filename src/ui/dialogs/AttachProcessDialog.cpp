/**
 * @file AttachProcessDialog.cpp
 * @brief Implementation of the `/proc` process & MPI job attach dialog.
 */

#include "AttachProcessDialog.hpp"
#undef AttachProcessDialog

#include <algorithm>

#include <QClipboard>
#include <QColor>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMap>
#include <QSet>
#include <QVBoxLayout>

namespace gridlock::ui {

inline namespace proc_attach {

namespace {

constexpr int kRolePid = Qt::UserRole;
constexpr int kRoleIsGroup = Qt::UserRole + 1;
constexpr int kRoleJobTag = Qt::UserRole + 2;

}  // namespace

AttachProcessDialog::AttachProcessDialog(QWidget* parent,
                                         const QString& procRoot,
                                         const QString& sysctlPath)
    : QDialog(parent), m_procRoot(procRoot), m_sysctlPath(sysctlPath) {
  setWindowTitle(QStringLiteral("Attach to Running Process / MPI Job"));
  setMinimumSize(720, 440);
  resize(820, 500);

  auto* rootLayout = new QVBoxLayout(this);
  rootLayout->setContentsMargins(12, 12, 12, 12);
  rootLayout->setSpacing(8);

  // 1. Yama ptrace_scope warning banner (shown when checkPtraceScope() > 0).
  m_ptraceBanner = new QWidget(this);
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
  rootLayout->addWidget(m_ptraceBanner);

  connect(m_copySysctlBtn, &QPushButton::clicked, this, [this]() {
    if (QClipboard* cb = QGuiApplication::clipboard()) {
      cb->setText(QStringLiteral("echo 0 | sudo tee /proc/sys/kernel/yama/ptrace_scope"));
    }
    m_copySysctlBtn->setText(QStringLiteral("Copied!"));
  });

  // 2. Filter strip: instant search input, "My Processes Only" checkbox, and "Refresh" button.
  auto* filterRow = new QHBoxLayout();
  filterRow->setContentsMargins(0, 0, 0, 0);
  filterRow->setSpacing(8);
  auto* filterLabel = new QLabel(QStringLiteral("Filter:"), this);
  m_filterEdit = new QLineEdit(this);
  m_filterEdit->setObjectName(QStringLiteral("attachProcessFilterEdit"));
  m_filterEdit->setPlaceholderText(
      QStringLiteral("Filter by PID, process name, command line, or MPI job..."));
  m_filterEdit->setFixedHeight(24);
  m_myProcessesCheck = new QCheckBox(QStringLiteral("My Processes Only"), this);
  m_myProcessesCheck->setObjectName(QStringLiteral("attachMyProcessesOnlyCheck"));
  m_myProcessesCheck->setChecked(true);
  m_refreshBtn = new QPushButton(QStringLiteral("Refresh"), this);
  m_refreshBtn->setObjectName(QStringLiteral("attachProcessRefreshBtn"));
  m_refreshBtn->setFixedHeight(24);
  filterRow->addWidget(filterLabel);
  filterRow->addWidget(m_filterEdit, 1);
  filterRow->addWidget(m_myProcessesCheck);
  filterRow->addWidget(m_refreshBtn);
  rootLayout->addLayout(filterRow);

  // 3. Primary MPI Job grouped QTreeWidget (`PID`, `Rank`, `Process / Command`, `Executable`).
  m_processTree = new QTreeWidget(this);
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
  rootLayout->addWidget(m_processTree, 1);

  // 4. Companion 4-column flat QTableWidget (`attachProcessTable`) for flat table inspection/selection.
  m_processTable = new QTableWidget(0, 4, this);
  m_processTable->setObjectName(QStringLiteral("attachProcessTable"));
  m_processTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_processTable->setSelectionMode(QAbstractItemView::SingleSelection);
  m_processTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_processTable->setHorizontalHeaderLabels({QStringLiteral("PID"),
                                             QStringLiteral("PROCESS"),
                                             QStringLiteral("USER"),
                                             QStringLiteral("COMMAND")});
  m_processTable->setVisible(false);

  // 5. Footer action bar with "Attach to Selected" and "Cancel".
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

  refreshProcesses();
}

void AttachProcessDialog::setProcPaths(const QString& procRoot,
                                       const QString& sysctlPath) {
  m_procRoot = procRoot;
  m_sysctlPath = sysctlPath;
  refreshProcesses();
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

  // Populate flat companion table (`attachProcessTable`).
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

  // Populate hierarchical MPI job + standalone process tree (`attachProcessTree`).
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
