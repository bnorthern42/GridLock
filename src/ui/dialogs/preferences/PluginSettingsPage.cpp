/**
 * @file PluginSettingsPage.cpp
 * @brief Implementation of the Preferences -> Plugins page with staged dual-scope enablement, SHA-256 trust, and self-restart.
 */

#include "PluginSettingsPage.hpp"

#include <algorithm>

#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMetaObject>
#include <QProcess>
#include <QSettings>
#include <QSignalBlocker>
#include <QTableWidgetItem>
#include <QUrl>
#include <QVBoxLayout>

#include "../../../core/managers/ConfigManager.hpp"
#include "../../../core/plugins/PluginManager.hpp"
#include "../../MainWindow.hpp"

namespace gridlock::ui {

namespace {

using gridlock::core::plugins::LoadedPluginInfo;
using gridlock::core::plugins::PluginManager;
using gridlock::core::plugins::PluginScope;

QString pluginRowKey(const LoadedPluginInfo& info) {
  return !info.id.isEmpty() ? info.id : info.libraryPath;
}

QWidget* makeCenteredCheckboxCell(QTableWidget* parent, QCheckBox** outCheck, const QString& objName,
                                  bool checked, bool enabled, const QString& tooltip) {
  auto* holder = new QWidget(parent);
  auto* lay = new QHBoxLayout(holder);
  lay->setContentsMargins(6, 0, 6, 0);
  lay->setAlignment(Qt::AlignCenter);
  auto* chk = new QCheckBox(holder);
  chk->setObjectName(objName);
  chk->setChecked(checked);
  chk->setEnabled(enabled);
  chk->setToolTip(tooltip);
  lay->addWidget(chk);
  *outCheck = chk;
  return holder;
}

}  // namespace

PluginSettingsPage::PluginSettingsPage(PluginManager* pluginManager, QWidget* parent)
    : QWidget(parent), m_pluginManager(pluginManager) {
  setObjectName(QStringLiteral("preferencesPluginsPage"));

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(20, 16, 20, 24);
  root->setSpacing(12);

  auto* heading = new QLabel(QStringLiteral("<b>Native C99 Debugger Plugins</b>"), this);
  heading->setObjectName(QStringLiteral("heading"));
  root->addWidget(heading);

  auto* sep = new QLabel(this);
  sep->setFixedHeight(1);
  sep->setObjectName(QStringLiteral("separator"));
  root->addWidget(sep);

  auto* desc = new QLabel(
      tr("Manage compiled native extensions (.so / .dylib / .dll) conforming to the frozen "
         "GridLock Plugin ABI v1.x across Global and Workspace scopes."),
      this);
  desc->setWordWrap(true);
  root->addWidget(desc);

  // Search directories bar with Open Folder and Reload Plugins actions.
  auto* dirRow = new QHBoxLayout();
  dirRow->setSpacing(8);
  dirRow->addWidget(new QLabel(tr("Plugin Directories:"), this));

  m_searchDirsCombo = new QComboBox(this);
  m_searchDirsCombo->setObjectName(QStringLiteral("pluginSearchDirsCombo"));
  m_searchDirsCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  m_searchDirsCombo->setMinimumContentsLength(16);
  m_searchDirsCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  dirRow->addWidget(m_searchDirsCombo, 1);

  m_openFolderBtn = new QPushButton(tr("Open Folder"), this);
  m_openFolderBtn->setObjectName(QStringLiteral("openPluginFolderBtn"));
  m_openFolderBtn->setToolTip(tr("Create (if missing) and open the selected plugin directory."));
  dirRow->addWidget(m_openFolderBtn);

  m_reloadBtn = new QPushButton(tr("Reload Plugins"), this);
  m_reloadBtn->setObjectName(QStringLiteral("reloadPluginsBtn"));
  m_reloadBtn->setToolTip(tr("Discard staged edits, unload active plugins, and rescan plugin directories."));
  dirRow->addWidget(m_reloadBtn);
  root->addLayout(dirRow);

  // Inline restart notification banner shown when applied plugin changes require restarting GridLock.
  m_restartBanner = new QFrame(this);
  m_restartBanner->setObjectName(QStringLiteral("pluginRestartBanner"));
  m_restartBanner->setStyleSheet(QStringLiteral(
      "QFrame#pluginRestartBanner { background-color: rgba(217, 119, 6, 0.16); "
      "border: 1px solid #d97706; border-radius: 5px; }"));
  auto* bannerLay = new QHBoxLayout(m_restartBanner);
  bannerLay->setContentsMargins(12, 8, 12, 8);
  bannerLay->setSpacing(10);
  m_restartBannerLabel = new QLabel(
      tr("Plugin enablement or workspace trust changes were saved. Restart GridLock to apply all updates cleanly."),
      m_restartBanner);
  m_restartBannerLabel->setObjectName(QStringLiteral("pluginRestartBannerLabel"));
  m_restartBannerLabel->setWordWrap(true);
  bannerLay->addWidget(m_restartBannerLabel, 1);

  m_restartNowBtn = new QPushButton(tr("Restart GridLock Now"), m_restartBanner);
  m_restartNowBtn->setObjectName(QStringLiteral("restartGridLockNowBtn"));
  m_restartNowBtn->setMinimumWidth(164);
  m_restartNowBtn->setToolTip(tr("Save session state and relaunch GridLock immediately."));
  bannerLay->addWidget(m_restartNowBtn);
  m_restartBanner->setVisible(false);
  root->addWidget(m_restartBanner);

  // 7-column plugins table: Global | Workspace | Name | Version | Scope / ABI | Status | Trust.
  m_pluginTable = new QTableWidget(0, 7, this);
  m_pluginTable->setObjectName(QStringLiteral("pluginsTableWidget"));
  m_pluginTable->setHorizontalHeaderLabels({tr("Global"), tr("Workspace"), tr("Name"), tr("Version"),
                                           tr("Scope / ABI"), tr("Status"), tr("Trust")});
  m_pluginTable->verticalHeader()->setVisible(false);
  m_pluginTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_pluginTable->setSelectionMode(QAbstractItemView::SingleSelection);
  m_pluginTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_pluginTable->setAlternatingRowColors(true);
  m_pluginTable->setMinimumHeight(260);

  QHeaderView* hdr = m_pluginTable->horizontalHeader();
  hdr->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  hdr->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  hdr->setSectionResizeMode(2, QHeaderView::Interactive);
  hdr->setSectionResizeMode(3, QHeaderView::ResizeToContents);
  hdr->setSectionResizeMode(4, QHeaderView::ResizeToContents);
  hdr->setSectionResizeMode(5, QHeaderView::Stretch);
  hdr->setSectionResizeMode(6, QHeaderView::ResizeToContents);
  m_pluginTable->setColumnWidth(2, 180);
  root->addWidget(m_pluginTable, 1);

  connect(m_openFolderBtn, &QPushButton::clicked, this, [this]() {
    if (!m_searchDirsCombo) return;
    const QString targetDir = m_searchDirsCombo->currentText().trimmed();
    if (targetDir.isEmpty()) return;
    QDir().mkpath(targetDir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(targetDir));
  });

  connect(m_reloadBtn, &QPushButton::clicked, this, &PluginSettingsPage::reloadPlugins);
  connect(m_restartNowBtn, &QPushButton::clicked, this, &PluginSettingsPage::triggerRestartNow);

  refreshTable();
}

MainWindow* PluginSettingsPage::resolveMainWindow() const {
  for (QWidget* w = parentWidget(); w != nullptr; w = w->parentWidget()) {
    if (auto* mw = qobject_cast<MainWindow*>(w)) return mw;
  }
  for (QWidget* top : QApplication::topLevelWidgets()) {
    if (auto* mw = qobject_cast<MainWindow*>(top)) return mw;
  }
  return nullptr;
}

PluginManager* PluginSettingsPage::resolvePluginManager() {
  if (!m_pluginManager.isNull()) return m_pluginManager.data();
  if (MainWindow* mw = resolveMainWindow(); mw != nullptr && mw->pluginManager() != nullptr) {
    m_pluginManager = mw->pluginManager();
    return m_pluginManager.data();
  }
  return nullptr;
}

void PluginSettingsPage::setRestartBannerVisible(bool visible) {
  m_restartRequired = visible;
  if (m_restartBanner != nullptr) m_restartBanner->setVisible(visible);
}

void PluginSettingsPage::reloadPlugins() {
  m_stagedStates.clear();
  if (auto* mgr = resolvePluginManager()) {
    mgr->setProperty("gl_restart_required", false);
    mgr->discoverAndLoad();
  }
  setRestartBannerVisible(false);
  refreshTable();
}

void PluginSettingsPage::updateRowPreview(int row, const QString& rowKey) {
  auto* mgr = resolvePluginManager();
  if (mgr == nullptr || m_pluginTable == nullptr || row < 0 || row >= m_pluginTable->rowCount()) return;

  const auto& list = mgr->plugins();
  const auto it = std::find_if(list.cbegin(), list.cend(),
                               [&](const LoadedPluginInfo& cand) { return pluginRowKey(cand) == rowKey; });
  if (it == list.cend()) return;
  const LoadedPluginInfo& info = *it;

  const bool isWs = (info.scope == PluginScope::Workspace);
  const bool isUntrustedWs = isWs && !info.trusted;
  const bool abiRejected = !info.errorMessage.isEmpty() && !isUntrustedWs;

  const bool baseGlobal = !info.id.isEmpty() && mgr->isPluginGloballyEnabled(info.id);
  const bool baseWorkspace = !info.id.isEmpty() && mgr->isPluginWorkspaceEnabled(info.id);
  const bool baseTrusted = !isWs || info.trusted;
  const StagedPluginState staged = m_stagedStates.value(
      rowKey, StagedPluginState{info.id, info.libraryPath, info.sha256Hex, isWs, baseGlobal, baseWorkspace, baseTrusted});

  const bool isDirty = (staged.globalEnabled != baseGlobal) ||
                       (staged.workspaceEnabled != baseWorkspace) ||
                       (isWs && staged.workspaceTrusted != baseTrusted);

  QString statusText;
  if (abiRejected) {
    statusText = QStringLiteral("ABI Rejected: %1").arg(info.errorMessage);
  } else if (isDirty) {
    if (isWs && !staged.workspaceTrusted) {
      statusText = tr("Staged: Untrusted (Apply to save)");
    } else if (!staged.globalEnabled) {
      statusText = tr("Staged: Disabled Globally (Apply to save)");
    } else if (!staged.workspaceEnabled) {
      statusText = tr("Staged: Disabled in Workspace (Apply to save)");
    } else {
      statusText = tr("Staged: Enabled (Apply to save)");
    }
  } else if (info.requiresRestart) {
    statusText = tr("Restart Required");
  } else if (isUntrustedWs) {
    statusText = info.errorMessage.isEmpty() ? tr("Untrusted Workspace Plugin (Approval Required)") : info.errorMessage;
  } else if (info.enabled) {
    statusText = tr("Loaded");
  } else if (!baseGlobal) {
    statusText = tr("Disabled (Global)");
  } else if (!baseWorkspace) {
    statusText = tr("Disabled (Workspace)");
  } else {
    statusText = tr("Disabled");
  }

  if (QTableWidgetItem* statusItem = m_pluginTable->item(row, 5)) {
    statusItem->setText(statusText);
  }

  if (isWs && !abiRejected) {
    if (auto* trustBtn = qobject_cast<QPushButton*>(m_pluginTable->cellWidget(row, 6))) {
      const QString shaPrefix = info.sha256Hex.isEmpty() ? QStringLiteral("unknown") : info.sha256Hex.left(8);
      if (staged.workspaceTrusted) {
        trustBtn->setText(tr("Trusted (%1)").arg(shaPrefix));
        trustBtn->setToolTip(tr("Approved SHA-256: %1. Click to stage trust revocation.").arg(info.sha256Hex));
      } else {
        trustBtn->setText(tr("Trust & Approve (%1)").arg(shaPrefix));
        trustBtn->setToolTip(tr("Click to stage SHA-256 approval (%1); committed on Apply.").arg(info.sha256Hex));
      }
    }
  }
}

void PluginSettingsPage::refreshTable() {
  auto* mgr = resolvePluginManager();
  if (m_pluginTable == nullptr || m_searchDirsCombo == nullptr) return;
  if (mgr == nullptr) {
    m_pluginTable->setRowCount(0);
    return;
  }

  {
    QSignalBlocker sb(m_searchDirsCombo);
    const QString prev = m_searchDirsCombo->currentText();
    m_searchDirsCombo->clear();
    for (const QString& dir : mgr->searchDirectories()) {
      m_searchDirsCombo->addItem(dir, dir);
      m_searchDirsCombo->setItemData(m_searchDirsCombo->count() - 1, dir, Qt::ToolTipRole);
    }
    const int idx = m_searchDirsCombo->findText(prev);
    if (idx >= 0) m_searchDirsCombo->setCurrentIndex(idx);
  }

  const auto& list = mgr->plugins();
  const bool anyPersistedRestart =
      mgr->property("gl_restart_required").toBool() ||
      std::any_of(list.cbegin(), list.cend(), [](const LoadedPluginInfo& i) { return i.requiresRestart; });
  if (anyPersistedRestart) setRestartBannerVisible(true);

  m_pluginTable->setRowCount(0);
  m_pluginTable->setRowCount(list.size());
  const bool hasWorkspace = !gridlock::core::ConfigManager::instance().workspacePath().trimmed().isEmpty();

  for (int row = 0; row < list.size(); ++row) {
    const auto& info = list.at(row);
    const QString rowKey = pluginRowKey(info);
    const bool isWs = (info.scope == PluginScope::Workspace);
    const bool isUntrustedWs = isWs && !info.trusted;
    const bool abiRejected = !info.errorMessage.isEmpty() && !isUntrustedWs;

    const bool baseGlobal = !abiRejected && !info.id.isEmpty() && mgr->isPluginGloballyEnabled(info.id);
    const bool baseWorkspace = !abiRejected && !info.id.isEmpty() && mgr->isPluginWorkspaceEnabled(info.id);
    const bool baseTrusted = !isWs || info.trusted;

    if (!m_stagedStates.contains(rowKey)) {
      m_stagedStates.insert(rowKey, StagedPluginState{info.id, info.libraryPath, info.sha256Hex, isWs,
                                                      baseGlobal, baseWorkspace, baseTrusted});
    }
    const StagedPluginState staged = m_stagedStates.value(rowKey);

    QCheckBox* globalChk = nullptr;
    QCheckBox* wsChk = nullptr;
    m_pluginTable->setCellWidget(
        row, 0,
        makeCenteredCheckboxCell(m_pluginTable, &globalChk, QStringLiteral("pluginGlobalCheck_") + info.id,
                                 staged.globalEnabled && !abiRejected, !abiRejected && !info.id.isEmpty(),
                                 tr("Enable or disable this plugin globally (staged until Apply).")));
    m_pluginTable->setCellWidget(
        row, 1,
        makeCenteredCheckboxCell(m_pluginTable, &wsChk, QStringLiteral("pluginWorkspaceCheck_") + info.id,
                                 staged.workspaceEnabled && !abiRejected,
                                 !abiRejected && !info.id.isEmpty() && hasWorkspace,
                                 tr("Enable or disable this plugin in the active workspace (staged until Apply).")));

    auto makeCell = [&](const QString& text) {
      auto* item = new QTableWidgetItem(text);
      item->setToolTip(info.libraryPath);
      return item;
    };

    const QString scopeLabel = isWs ? tr("Workspace") : tr("Global");
    const QString abiText = (info.abiMajor > 0u || info.abiMinor > 0u)
                                ? QStringLiteral("v%1.%2").arg(info.abiMajor).arg(info.abiMinor)
                                : QStringLiteral("v1.0");

    auto* nameItem = makeCell(info.name.isEmpty() ? info.id : info.name);
    nameItem->setData(Qt::UserRole, rowKey);
    m_pluginTable->setItem(row, 2, nameItem);
    m_pluginTable->setItem(row, 3, makeCell(info.version.isEmpty() ? QStringLiteral("-") : info.version));
    m_pluginTable->setItem(row, 4, makeCell(QStringLiteral("%1 · %2").arg(scopeLabel, abiText)));
    m_pluginTable->setItem(row, 5, makeCell(QString()));

    if (isWs && !abiRejected) {
      auto* trustBtn = new QPushButton(m_pluginTable);
      trustBtn->setObjectName(QStringLiteral("pluginTrustBtn_") + info.id);
      trustBtn->setMinimumHeight(24);
      m_pluginTable->setCellWidget(row, 6, trustBtn);
      connect(trustBtn, &QPushButton::clicked, this, [this, row, rowKey]() {
        if (!m_stagedStates.contains(rowKey)) return;
        m_stagedStates[rowKey].workspaceTrusted = !m_stagedStates[rowKey].workspaceTrusted;
        updateRowPreview(row, rowKey);
      });
    } else {
      m_pluginTable->setItem(row, 6, makeCell(isWs ? tr("Rejected") : tr("User Global")));
    }

    connect(globalChk, &QCheckBox::toggled, this, [this, row, rowKey](bool checked) {
      if (!m_stagedStates.contains(rowKey)) return;
      m_stagedStates[rowKey].globalEnabled = checked;
      updateRowPreview(row, rowKey);
    });
    connect(wsChk, &QCheckBox::toggled, this, [this, row, rowKey](bool checked) {
      if (!m_stagedStates.contains(rowKey)) return;
      m_stagedStates[rowKey].workspaceEnabled = checked;
      updateRowPreview(row, rowKey);
    });

    updateRowPreview(row, rowKey);
  }
}

bool PluginSettingsPage::applyChanges() {
  auto* mgr = resolvePluginManager();
  if (mgr == nullptr) return false;

  const QVector<LoadedPluginInfo> snapshot = mgr->plugins();
  bool anyChanged = false;

  for (const LoadedPluginInfo& info : snapshot) {
    const QString rowKey = pluginRowKey(info);
    if (!m_stagedStates.contains(rowKey) || info.id.isEmpty()) continue;

    const StagedPluginState staged = m_stagedStates.value(rowKey);
    const bool isWs = (info.scope == PluginScope::Workspace);
    const bool curGlobal = mgr->isPluginGloballyEnabled(info.id);
    const bool curWorkspace = mgr->isPluginWorkspaceEnabled(info.id);
    const bool curTrusted = !isWs || info.trusted;

    const bool rowDiff = (staged.globalEnabled != curGlobal) ||
                         (staged.workspaceEnabled != curWorkspace) ||
                         (isWs && staged.workspaceTrusted != curTrusted);
    if (!rowDiff) continue;

    mgr->setProperty("gl_restart_required", true);
    anyChanged = true;

    if (staged.globalEnabled != curGlobal) {
      mgr->setPluginGloballyEnabled(info.id, staged.globalEnabled);
    }
    if (staged.workspaceEnabled != curWorkspace) {
      mgr->setPluginWorkspaceEnabled(info.id, staged.workspaceEnabled);
    }
    if (isWs && staged.workspaceTrusted != curTrusted) {
      if (staged.workspaceTrusted) {
        mgr->trustWorkspacePlugin(info.id);
      } else {
        mgr->revokeWorkspacePluginTrust(info.id);
      }
    }
  }

  m_stagedStates.clear();
  refreshTable();

  if (anyChanged) {
    setRestartBannerVisible(true);
    emit restartRequiredChanged(true);
  }
  return anyChanged;
}

void PluginSettingsPage::triggerRestartNow() {
  if (auto* dlg = qobject_cast<QDialog*>(window())) {
    (void)QMetaObject::invokeMethod(dlg, "apply");
  } else {
    (void)applyChanges();
  }

  if (MainWindow* mw = resolveMainWindow()) {
    if (!mw->activeWorkspaceDirectory().isEmpty()) mw->saveSession(false);
  }
  gridlock::core::ConfigManager::instance().saveGlobalConfig();
  QSettings(QStringLiteral("gridlock"), QStringLiteral("debugger")).sync();

  emit restartRequested();
  if (auto* dlg = qobject_cast<QDialog*>(window())) dlg->accept();

  if (qEnvironmentVariableIntValue("GRIDLOCK_DRY_RUN_SELF_RESTART") == 1) {
    if (qApp != nullptr) qApp->setProperty("gridlock_restart_triggered", true);
    return;
  }
  const QString appPath = QCoreApplication::applicationFilePath();
  const QStringList appArgs = QCoreApplication::arguments().mid(1);
  if (!appPath.isEmpty()) QProcess::startDetached(appPath, appArgs);
  QCoreApplication::quit();
}

}  // namespace gridlock::ui
