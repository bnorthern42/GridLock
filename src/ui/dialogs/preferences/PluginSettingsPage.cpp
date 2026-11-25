/**
 * @file PluginSettingsPage.cpp
 * @brief Implementation of the Preferences -> Plugins management page.
 */

#include "PluginSettingsPage.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QDesktopServices>
#include <QDir>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QSignalBlocker>
#include <QTableWidgetItem>
#include <QUrl>
#include <QVBoxLayout>

#include "../../../core/managers/ConfigManager.hpp"
#include "../../../core/plugins/PluginManager.hpp"
#include "../../MainWindow.hpp"

namespace gridlock::ui {

PluginSettingsPage::PluginSettingsPage(gridlock::core::plugins::PluginManager* pluginManager,
                                       QWidget* parent)
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
         "GridLock Plugin ABI v1.0."),
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
  m_reloadBtn->setToolTip(tr("Unload active plugins and rescan plugin directories immediately."));
  dirRow->addWidget(m_reloadBtn);
  root->addLayout(dirRow);

  // Discovered plugins table: Enabled | Name | Version | ABI | Status.
  m_pluginTable = new QTableWidget(0, 5, this);
  m_pluginTable->setObjectName(QStringLiteral("pluginsTableWidget"));
  m_pluginTable->setHorizontalHeaderLabels(
      {tr("Enabled"), tr("Name"), tr("Version"), tr("ABI"), tr("Status")});
  m_pluginTable->verticalHeader()->setVisible(false);
  m_pluginTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_pluginTable->setSelectionMode(QAbstractItemView::SingleSelection);
  m_pluginTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_pluginTable->setAlternatingRowColors(true);
  m_pluginTable->setMinimumHeight(260);

  QHeaderView* hdr = m_pluginTable->horizontalHeader();
  hdr->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  hdr->setSectionResizeMode(1, QHeaderView::Interactive);
  hdr->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  hdr->setSectionResizeMode(3, QHeaderView::ResizeToContents);
  hdr->setSectionResizeMode(4, QHeaderView::Stretch);
  m_pluginTable->setColumnWidth(1, 220);
  root->addWidget(m_pluginTable, 1);

  connect(m_openFolderBtn, &QPushButton::clicked, this, [this]() {
    if (!m_searchDirsCombo) return;
    const QString targetDir = m_searchDirsCombo->currentText().trimmed();
    if (targetDir.isEmpty()) return;
    QDir().mkpath(targetDir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(targetDir));
  });

  connect(m_reloadBtn, &QPushButton::clicked, this, &PluginSettingsPage::reloadPlugins);

  refreshTable();
}

gridlock::core::plugins::PluginManager* PluginSettingsPage::resolvePluginManager() {
  if (!m_pluginManager.isNull()) return m_pluginManager.data();

  for (QWidget* w = parentWidget(); w != nullptr; w = w->parentWidget()) {
    if (auto* mw = qobject_cast<MainWindow*>(w)) {
      if (mw->pluginManager() != nullptr) {
        m_pluginManager = mw->pluginManager();
        return m_pluginManager.data();
      }
    }
  }
  for (QWidget* top : QApplication::topLevelWidgets()) {
    if (auto* mw = qobject_cast<MainWindow*>(top)) {
      if (mw->pluginManager() != nullptr) {
        m_pluginManager = mw->pluginManager();
        return m_pluginManager.data();
      }
    }
  }

  if (m_ownedFallbackManager == nullptr) {
    m_ownedFallbackManager = new gridlock::core::plugins::PluginManager(nullptr, this);
    m_ownedFallbackManager->discoverAndLoad();
  }
  m_pluginManager = m_ownedFallbackManager;
  return m_ownedFallbackManager;
}

void PluginSettingsPage::reloadPlugins() {
  if (auto* mgr = resolvePluginManager()) {
    mgr->discoverAndLoad();
  }
  refreshTable();
}

void PluginSettingsPage::refreshTable() {
  auto* mgr = resolvePluginManager();
  if (mgr == nullptr || m_pluginTable == nullptr || m_searchDirsCombo == nullptr) return;

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

  m_pluginTable->setRowCount(0);
  const auto& list = mgr->plugins();
  m_pluginTable->setRowCount(list.size());

  for (int row = 0; row < list.size(); ++row) {
    const auto& info = list.at(row);
    const bool rejected = !info.errorMessage.isEmpty();
    const QString abiText = (info.abiMajor > 0u || info.abiMinor > 0u)
                                ? QStringLiteral("v%1.%2").arg(info.abiMajor).arg(info.abiMinor)
                                : QStringLiteral("v1.0");
    const QString statusText =
        rejected ? QStringLiteral("ABI Rejected: %1").arg(info.errorMessage)
                 : (info.enabled ? tr("Loaded") : tr("Disabled"));

    auto* chkHolder = new QWidget(m_pluginTable);
    auto* chkLay = new QHBoxLayout(chkHolder);
    chkLay->setContentsMargins(6, 0, 6, 0);
    chkLay->setAlignment(Qt::AlignCenter);
    auto* chk = new QCheckBox(chkHolder);
    chk->setObjectName(QStringLiteral("pluginEnableCheck_") + info.id);
    chk->setProperty("pluginId", info.id);
    chk->setChecked(info.enabled && !rejected);
    chk->setEnabled(!rejected && !info.id.isEmpty());
    chkLay->addWidget(chk);
    m_pluginTable->setCellWidget(row, 0, chkHolder);

    auto makeCell = [&](const QString& text) {
      auto* item = new QTableWidgetItem(text);
      item->setToolTip(info.libraryPath);
      return item;
    };
    auto* nameItem = makeCell(info.name.isEmpty() ? info.id : info.name);
    nameItem->setData(Qt::UserRole, info.id);
    m_pluginTable->setItem(row, 1, nameItem);
    m_pluginTable->setItem(row, 2, makeCell(info.version.isEmpty() ? QStringLiteral("-") : info.version));
    m_pluginTable->setItem(row, 3, makeCell(abiText));
    m_pluginTable->setItem(row, 4, makeCell(statusText));

    const QString plugId = info.id;
    connect(chk, &QCheckBox::toggled, this, [this, plugId](bool checked) {
      if (auto* pm = resolvePluginManager()) {
        pm->setPluginEnabled(plugId, checked);
      }
      refreshTable();
    });
  }
}

void PluginSettingsPage::applyChanges() {
  auto* mgr = resolvePluginManager();
  if (mgr == nullptr || m_pluginTable == nullptr) return;
  for (int row = 0; row < m_pluginTable->rowCount(); ++row) {
    QWidget* holder = m_pluginTable->cellWidget(row, 0);
    auto* chk = holder ? holder->findChild<QCheckBox*>() : nullptr;
    if (chk != nullptr && chk->isEnabled()) {
      const QString id = chk->property("pluginId").toString();
      if (!id.isEmpty()) {
        gridlock::core::ConfigManager::instance().setPluginDisabled(id, !chk->isChecked());
      }
    }
  }
}

}  // namespace gridlock::ui
