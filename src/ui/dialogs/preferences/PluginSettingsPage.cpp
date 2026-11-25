/**
 * @file PluginSettingsPage.cpp
 * @brief Implementation of Preferences -> Plugins (`Installed`, multi-forge `Install from Git`, and `plugins.toml` sync).
 */

#include "PluginSettingsPage.hpp"

#include <algorithm>

#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMetaObject>
#include <QProcess>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QSyntaxHighlighter>
#include <QTableWidgetItem>
#include <QTextCharFormat>
#include <QUrl>
#include <QVBoxLayout>

#include "../../../core/managers/ConfigManager.hpp"
#include "../../../core/managers/ThemeManager.hpp"
#include "../../../core/plugins/PluginInstaller.hpp"
#include "../../../core/plugins/PluginManager.hpp"
#include "../../../core/plugins/PluginManifest.hpp"
#include "../../MainWindow.hpp"

namespace gridlock::ui {

namespace {

using gridlock::core::plugins::GitForgeHost;
using gridlock::core::plugins::LoadedPluginInfo;
using gridlock::core::plugins::PluginBuildResult;
using gridlock::core::plugins::PluginInstaller;
using gridlock::core::plugins::PluginManager;
using gridlock::core::plugins::PluginManifest;
using gridlock::core::plugins::PluginScope;
using gridlock::core::plugins::PluginUpdateTarget;

class CompilerLogHighlighter final : public QSyntaxHighlighter {
 public:
  explicit CompilerLogHighlighter(QTextDocument* doc) : QSyntaxHighlighter(doc) {
    m_errFmt.setForeground(QColor(QStringLiteral("#ef4444")));
    m_errFmt.setFontWeight(QFont::Bold);
    m_warnFmt.setForeground(QColor(QStringLiteral("#f59e0b")));
    m_warnFmt.setFontWeight(QFont::DemiBold);
    m_stepFmt.setForeground(QColor(QStringLiteral("#38bdf8")));
    m_stepFmt.setFontWeight(QFont::Bold);
  }

 protected:
  void highlightBlock(const QString& text) override {
    static const QRegularExpression kErrRe(
        QStringLiteral("(?:(?:^|\\s|:)(?:fatal )?error:\\s|undefined reference to\\b|CMake Error\\b)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression kWarnRe(QStringLiteral("(?:^|\\s|:)warning:\\s"),
                                           QRegularExpression::CaseInsensitiveOption);
    if (text.startsWith(QStringLiteral("==>"))) {
      setFormat(0, text.size(), m_stepFmt);
    } else if (kErrRe.match(text).hasMatch()) {
      setFormat(0, text.size(), m_errFmt);
    } else if (kWarnRe.match(text).hasMatch()) {
      setFormat(0, text.size(), m_warnFmt);
    }
  }

 private:
  QTextCharFormat m_errFmt;
  QTextCharFormat m_warnFmt;
  QTextCharFormat m_stepFmt;
};

QString pluginRowKey(const LoadedPluginInfo& info) {
  return !info.id.isEmpty() ? info.id : info.libraryPath;
}

QString formatVersionCellText(const LoadedPluginInfo& info) {
  if (!info.gitCommitShort.isEmpty()) {
    return info.version.isEmpty() ? QStringLiteral("(%1)").arg(info.gitCommitShort)
                                  : QStringLiteral("%1 (%2)").arg(info.version, info.gitCommitShort);
  }
  return info.version.isEmpty() ? QStringLiteral("-") : info.version;
}

QString formatRowProvenanceTooltip(const LoadedPluginInfo& info) {
  const bool hasGit = !info.gitRepo.isEmpty();
  const QString refStr = !info.gitRef.isEmpty() ? info.gitRef : (hasGit ? QStringLiteral("HEAD") : QStringLiteral("-"));
  return QStringLiteral("Path: %1\nSHA-256: %2\nGit Source: %3\nRef: %4\nCommit: %5")
      .arg(info.libraryPath, info.sha256Hex.isEmpty() ? QStringLiteral("unknown") : info.sha256Hex,
           hasGit ? info.gitRepo : QStringLiteral("Local binary (no Git source)"), refStr,
           info.gitCommitSha.isEmpty() ? QStringLiteral("-") : info.gitCommitSha);
}

PluginUpdateTarget makeUpdateTarget(const LoadedPluginInfo& info) {
  const QFileInfo fi(info.libraryPath);
  return PluginUpdateTarget{info.id, info.gitRepo, info.gitRef,
                            info.buildSystem.isEmpty() ? QStringLiteral("auto") : info.buildSystem,
                            info.scope, fi.absolutePath(), fi.fileName(),
                            info.scope != PluginScope::Workspace || info.trusted};
}

void applyPrimaryAccentButtonStyle(QPushButton* btn) {
  if (!btn) return;
  auto& tm = gridlock::core::managers::ThemeManager::instance();
  const auto pal = gridlock::core::managers::ThemeManager::paletteForTheme(tm.activeThemeId());
  const QString fgHex =
      (QColor(pal.accentHex).lightness() < 165) ? QStringLiteral("#ffffff") : QStringLiteral("#16181d");
  const QString mutedFg = pal.isDark ? QStringLiteral("#6b7280") : QStringLiteral("#9ca3af");
  btn->setStyleSheet(QStringLiteral(
      "QPushButton { background-color: %1; color: %2; border: 1px solid %1; "
      "font-size: 13px; font-weight: 600; text-transform: none; padding: 2px 16px; border-radius: 4px; "
      "min-height: 22px; max-height: 26px; }"
      "QPushButton:hover { background-color: %1; color: %2; border: 1px solid %3; }"
      "QPushButton:disabled { background-color: %4; color: %5; border: 1px solid %4; }")
                         .arg(pal.accentHex, fgHex, pal.textHex, pal.headerHex, mutedFg));
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
    : QWidget(parent), m_pluginManager(pluginManager), m_installer(new PluginInstaller(this)) {
  setObjectName(QStringLiteral("preferencesPluginsPage"));

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(14, 14, 14, 16);
  root->setSpacing(10);

  auto* heading = new QLabel(QStringLiteral("<b>Native C99 Debugger Plugins</b>"), this);
  heading->setObjectName(QStringLiteral("heading"));
  root->addWidget(heading);

  auto* sep = new QLabel(this);
  sep->setFixedHeight(1);
  sep->setObjectName(QStringLiteral("separator"));
  root->addWidget(sep);

  m_tabs = new QTabWidget(this);
  m_tabs->setObjectName(QStringLiteral("pluginSettingsTabs"));
  m_tabs->addTab(createInstalledTab(m_tabs), tr("Installed"));
  m_tabs->addTab(createGitInstallTab(m_tabs), tr("Install from Git"));
  root->addWidget(m_tabs, 1);

  connect(m_installer, &PluginInstaller::stepStarted, this, [this](const QString& desc) {
    if (m_buildSummaryBadge) m_buildSummaryBadge->setText(desc);
    if (m_installedStatusLabel && !m_installedStatusLabel->isHidden()) m_installedStatusLabel->setText(desc);
  });
  connect(m_installer, &PluginInstaller::logOutputReceived, this, [this](const QString& chunk) {
    if (!m_compilerLogView) return;
    m_compilerLogView->moveCursor(QTextCursor::End);
    m_compilerLogView->insertPlainText(chunk);
    if (QScrollBar* sb = m_compilerLogView->verticalScrollBar()) sb->setValue(sb->maximum());
  });
  connect(m_installer, &PluginInstaller::installFinished, this, &PluginSettingsPage::onInstallFinished);

  refreshTable();
}

QWidget* PluginSettingsPage::createInstalledTab(QWidget* parent) {
  auto* tab = new QWidget(parent);
  tab->setObjectName(QStringLiteral("pluginInstalledTab"));
  auto* lay = new QVBoxLayout(tab);
  lay->setContentsMargins(8, 10, 8, 8);
  lay->setSpacing(10);

  auto* desc = new QLabel(
      tr("Manage compiled native extensions (.so / .dylib / .dll) conforming to the frozen "
         "GridLock Plugin ABI v1.x across Global and Workspace scopes."),
      tab);
  desc->setWordWrap(true);
  lay->addWidget(desc);

  auto* dirRow = new QHBoxLayout();
  dirRow->setSpacing(8);
  dirRow->addWidget(new QLabel(tr("Plugin Directories:"), tab));

  m_searchDirsCombo = new QComboBox(tab);
  m_searchDirsCombo->setObjectName(QStringLiteral("pluginSearchDirsCombo"));
  m_searchDirsCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  m_searchDirsCombo->setMinimumContentsLength(24);
  m_searchDirsCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  dirRow->addWidget(m_searchDirsCombo, 1);

  m_openFolderBtn = new QPushButton(tr("Open Folder"), tab);
  m_openFolderBtn->setObjectName(QStringLiteral("openPluginFolderBtn"));
  m_openFolderBtn->setToolTip(tr("Create (if missing) and open the selected plugin directory."));
  dirRow->addWidget(m_openFolderBtn);
  lay->addLayout(dirRow);

  auto* actionRow = new QHBoxLayout();
  actionRow->setSpacing(8);
  actionRow->addStretch(1);
  m_reloadBtn = new QPushButton(tr("Reload Plugins"), tab);
  m_reloadBtn->setObjectName(QStringLiteral("reloadPluginsBtn"));
  m_reloadBtn->setToolTip(tr("Discard staged edits, unload active plugins, and rescan plugin directories."));
  actionRow->addWidget(m_reloadBtn);

  m_updateAllBtn = new QPushButton(tr("Update All"), tab);
  m_updateAllBtn->setObjectName(QStringLiteral("pluginUpdateAllBtn"));
  m_updateAllBtn->setToolTip(tr("Re-clone and rebuild all Git-installed plugins into their existing scopes."));
  m_updateAllBtn->setMinimumWidth(108);
  applyPrimaryAccentButtonStyle(m_updateAllBtn);
  m_updateAllBtn->setEnabled(false);
  actionRow->addWidget(m_updateAllBtn);
  lay->addLayout(actionRow);

  m_installedStatusLabel = new QLabel(tab);
  m_installedStatusLabel->setObjectName(QStringLiteral("pluginInstalledStatusLabel"));
  m_installedStatusLabel->setWordWrap(true);
  m_installedStatusLabel->setVisible(false);
  lay->addWidget(m_installedStatusLabel);

  m_restartBanner = new QFrame(tab);
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
  bannerLay->addWidget(m_restartNowBtn);
  m_restartBanner->setVisible(false);
  lay->addWidget(m_restartBanner);

  m_pluginTable = new QTableWidget(0, 8, tab);
  m_pluginTable->setObjectName(QStringLiteral("pluginsTableWidget"));
  m_pluginTable->setHorizontalHeaderLabels({tr("Global"), tr("Workspace"), tr("Name"), tr("Version"),
                                           tr("Scope / ABI"), tr("Status"), tr("Trust"), tr("Update")});
  m_pluginTable->verticalHeader()->setVisible(false);
  m_pluginTable->verticalHeader()->setDefaultSectionSize(32);
  m_pluginTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_pluginTable->setSelectionMode(QAbstractItemView::SingleSelection);
  m_pluginTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_pluginTable->setAlternatingRowColors(true);
  m_pluginTable->setWordWrap(false);
  m_pluginTable->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
  m_pluginTable->setStyleSheet(QStringLiteral(
      "QTableWidget#pluginsTableWidget { font-size: 12px; } "
      "QTableWidget#pluginsTableWidget::item { padding: 2px 5px; } "
      "QTableWidget#pluginsTableWidget QHeaderView::section { padding: 3px 5px; font-size: 11px; }"));
  m_pluginTable->setMinimumHeight(240);

  QHeaderView* hdr = m_pluginTable->horizontalHeader();
  hdr->setMinimumSectionSize(44);
  for (int col : {0, 1, 3, 4, 5, 6, 7}) hdr->setSectionResizeMode(col, QHeaderView::ResizeToContents);
  hdr->setSectionResizeMode(2, QHeaderView::Stretch);
  lay->addWidget(m_pluginTable, 1);

  connect(m_openFolderBtn, &QPushButton::clicked, this, [this]() {
    if (!m_searchDirsCombo) return;
    if (const QString d = m_searchDirsCombo->currentText().trimmed(); !d.isEmpty()) {
      QDir().mkpath(d); QDesktopServices::openUrl(QUrl::fromLocalFile(d));
    }
  });
  connect(m_reloadBtn, &QPushButton::clicked, this, &PluginSettingsPage::reloadPlugins);
  connect(m_updateAllBtn, &QPushButton::clicked, this, &PluginSettingsPage::onUpdateAllClicked);
  connect(m_restartNowBtn, &QPushButton::clicked, this, &PluginSettingsPage::triggerRestartNow);
  return tab;
}

QWidget* PluginSettingsPage::createGitInstallTab(QWidget* parent) {
  auto* tab = new QWidget(parent);
  tab->setObjectName(QStringLiteral("pluginInstallTab"));
  auto* lay = new QVBoxLayout(tab);
  lay->setContentsMargins(8, 10, 8, 8);
  lay->setSpacing(10);

  auto* form = new QFormLayout();
  form->setSpacing(8);

  auto* hostRepoRow = new QHBoxLayout();
  hostRepoRow->setSpacing(8);

  m_gitHostCombo = new QComboBox(tab);
  m_gitHostCombo->setObjectName(QStringLiteral("pluginGitHostCombo"));
  m_gitHostCombo->addItem(tr("GitHub (github.com)"), static_cast<int>(GitForgeHost::GitHub));
  m_gitHostCombo->addItem(tr("GitLab (gitlab.com)"), static_cast<int>(GitForgeHost::GitLab));
  m_gitHostCombo->addItem(tr("Codeberg (codeberg.org)"), static_cast<int>(GitForgeHost::Codeberg));
  m_gitHostCombo->addItem(tr("Custom Git URL (https:// / git@)"), static_cast<int>(GitForgeHost::CustomUrl));
  hostRepoRow->addWidget(m_gitHostCombo);

  m_repoUrlEdit = new QLineEdit(tab);
  m_repoUrlEdit->setObjectName(QStringLiteral("pluginGitRepoEdit"));
  m_repoUrlEdit->setToolTip(
      tr("Accepts owner/repo shorthand for the selected forge, github:/gitlab:/codeberg: prefixes, "
         "or any full https:// / git@ repository URL."));
  hostRepoRow->addWidget(m_repoUrlEdit, 1);
  form->addRow(tr("Git Host & Repository:"), hostRepoRow);

  auto* refScopeRow = new QHBoxLayout();
  refScopeRow->setSpacing(8);
  m_gitRefEdit = new QLineEdit(tab);
  m_gitRefEdit->setObjectName(QStringLiteral("pluginGithubRefEdit"));
  m_gitRefEdit->setPlaceholderText(tr("Optional branch/tag..."));
  m_gitRefEdit->setMinimumWidth(180);
  m_gitRefEdit->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
  refScopeRow->addWidget(m_gitRefEdit, 1);

  m_installScopeCombo = new QComboBox(tab);
  m_installScopeCombo->setObjectName(QStringLiteral("pluginInstallScopeCombo"));
  m_installScopeCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  m_installScopeCombo->setMinimumContentsLength(18);
  m_installScopeCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  m_installScopeCombo->addItem(tr("Global (~/.config/gridlock/plugins)"), static_cast<int>(PluginScope::UserGlobal));
  m_installScopeCombo->addItem(tr("This Workspace (<workspace>/.gridlock/plugins)"),
                               static_cast<int>(PluginScope::Workspace));
  refScopeRow->addWidget(m_installScopeCombo, 1);

  m_cloneBuildBtn = new QPushButton(tr("Install"), tab);
  m_cloneBuildBtn->setObjectName(QStringLiteral("pluginCloneAndBuildBtn"));
  m_cloneBuildBtn->setMinimumWidth(96);
  applyPrimaryAccentButtonStyle(m_cloneBuildBtn);
  refScopeRow->addWidget(m_cloneBuildBtn);
  form->addRow(tr("Ref & Install Scope:"), refScopeRow);

  auto* manifestField = new QWidget(tab);
  manifestField->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  auto* manifestRow = new QHBoxLayout(manifestField);
  manifestRow->setContentsMargins(0, 2, 0, 2);
  manifestRow->setSpacing(8);
  auto* manifestHint = new QLabel(
      tr("Declarative manifest (plugins.toml + user-global plugins-lock.toml):"), manifestField);
  manifestHint->setObjectName(QStringLiteral("pluginManifestSyncHintLabel"));
  manifestHint->setWordWrap(true);
  manifestHint->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  manifestRow->addWidget(manifestHint, 1);

  m_openManifestBtn = new QPushButton(tr("Open plugins.toml"), manifestField);
  m_openManifestBtn->setObjectName(QStringLiteral("pluginOpenManifestBtn"));
  m_openManifestBtn->setToolTip(tr("Create (if missing) and open plugins.toml for the selected scope."));
  manifestRow->addWidget(m_openManifestBtn);

  m_syncManifestBtn = new QPushButton(tr("Sync plugins.toml"), manifestField);
  m_syncManifestBtn->setObjectName(QStringLiteral("pluginSyncManifestBtn"));
  m_syncManifestBtn->setToolTip(tr("Clone and compile any missing or outdated plugins declared in plugins.toml."));
  manifestRow->addWidget(m_syncManifestBtn);
  form->addRow(tr("Manifest Sync:"), manifestField);
  lay->addLayout(form);

  m_buildSummaryBadge = new QLabel(
      tr("Ready to install from GitHub, GitLab, Codeberg, Gitea, or custom Git URLs."), tab);
  m_buildSummaryBadge->setObjectName(QStringLiteral("pluginBuildSummaryBadge"));
  m_buildSummaryBadge->setWordWrap(true);
  lay->addWidget(m_buildSummaryBadge);

  m_installRestartBtn = new QPushButton(tr("Restart GridLock to Load Installed Plugin"), tab);
  m_installRestartBtn->setObjectName(QStringLiteral("pluginInstallRestartBtn"));
  m_installRestartBtn->setVisible(false);
  lay->addWidget(m_installRestartBtn);

  m_compilerLogView = new QPlainTextEdit(tab);
  m_compilerLogView->setObjectName(QStringLiteral("pluginCompilerLogView"));
  m_compilerLogView->setReadOnly(true);
  m_compilerLogView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  m_compilerLogView->setMinimumHeight(200);
  m_compilerLogView->setPlaceholderText(tr("Compiler output, warnings, and errors will stream here..."));
  (void)new CompilerLogHighlighter(m_compilerLogView->document());
  lay->addWidget(m_compilerLogView, 1);

  updateGitRepoPlaceholder();
  connect(&gridlock::core::managers::ThemeManager::instance(),
          &gridlock::core::managers::ThemeManager::themeModeChanged, this, [this](bool) {
            applyPrimaryAccentButtonStyle(m_cloneBuildBtn);
            applyPrimaryAccentButtonStyle(m_updateAllBtn);
          });
  connect(m_gitHostCombo, &QComboBox::currentIndexChanged, this, [this](int) { updateGitRepoPlaceholder(); });
  connect(m_cloneBuildBtn, &QPushButton::clicked, this, &PluginSettingsPage::onCloneOrCancelClicked);
  connect(m_syncManifestBtn, &QPushButton::clicked, this, &PluginSettingsPage::onSyncManifestClicked);
  connect(m_openManifestBtn, &QPushButton::clicked, this, &PluginSettingsPage::onOpenManifestClicked);
  connect(m_installRestartBtn, &QPushButton::clicked, this, &PluginSettingsPage::triggerRestartNow);
  return tab;
}

void PluginSettingsPage::updateGitRepoPlaceholder() {
  if (!m_repoUrlEdit || !m_gitHostCombo) return;
  switch (static_cast<GitForgeHost>(m_gitHostCombo->currentData().toInt())) {
    case GitForgeHost::GitHub:
      m_repoUrlEdit->setPlaceholderText(QStringLiteral("owner/repo (e.g. bnorthern42/gridlock-stl-inspector)")); break;
    case GitForgeHost::GitLab:
      m_repoUrlEdit->setPlaceholderText(QStringLiteral("owner/repo or group/sub/repo (gitlab.com)")); break;
    case GitForgeHost::Codeberg:
      m_repoUrlEdit->setPlaceholderText(QStringLiteral("owner/repo (codeberg.org)")); break;
    case GitForgeHost::CustomUrl:
      m_repoUrlEdit->setPlaceholderText(QStringLiteral("https://gitea.example.org/owner/repo.git or git@host:owner/repo.git")); break;
  }
}

void PluginSettingsPage::onCloneOrCancelClicked() {
  if (!m_installer) return;
  if (m_installer->isRunning()) { m_installer->cancelInstall(); return; }
  if (m_compilerLogView) m_compilerLogView->clear();
  if (m_installRestartBtn) m_installRestartBtn->setVisible(false);
  const QString repoSpec = m_repoUrlEdit ? m_repoUrlEdit->text().trimmed() : QString();
  const QString ref = m_gitRefEdit ? m_gitRefEdit->text().trimmed() : QString();
  const auto forge = m_gitHostCombo ? static_cast<GitForgeHost>(m_gitHostCombo->currentData().toInt()) : GitForgeHost::GitHub;
  const auto scope = (m_installScopeCombo && m_installScopeCombo->currentData().toInt() == static_cast<int>(PluginScope::Workspace))
                         ? PluginScope::Workspace : PluginScope::UserGlobal;
  if (m_installer->startInstallFromGit(repoSpec, ref, scope, forge)) {
    if (m_cloneBuildBtn) m_cloneBuildBtn->setText(tr("Cancel"));
    if (m_syncManifestBtn) m_syncManifestBtn->setEnabled(false);
    if (m_updateAllBtn) m_updateAllBtn->setEnabled(false);
  }
}

void PluginSettingsPage::onSyncManifestClicked() {
  if (!m_installer || m_installer->isRunning()) return;
  if (m_tabs) m_tabs->setCurrentIndex(1);
  if (m_compilerLogView) m_compilerLogView->clear();
  if (m_installRestartBtn) m_installRestartBtn->setVisible(false);
  const auto scope = (m_installScopeCombo && m_installScopeCombo->currentData().toInt() == static_cast<int>(PluginScope::Workspace))
                         ? PluginScope::Workspace : PluginScope::UserGlobal;
  if (m_installer->syncFromManifests(PluginManifest::userManifestPath(), PluginManifest::workspaceManifestPath(), scope) &&
      m_installer->isRunning()) {
    if (m_cloneBuildBtn) m_cloneBuildBtn->setText(tr("Cancel"));
    if (m_syncManifestBtn) m_syncManifestBtn->setEnabled(false);
    if (m_updateAllBtn) m_updateAllBtn->setEnabled(false);
  }
}

void PluginSettingsPage::onOpenManifestClicked() {
  const bool wantWs = m_installScopeCombo && m_installScopeCombo->currentData().toInt() == static_cast<int>(PluginScope::Workspace);
  QString path = wantWs ? PluginManifest::workspaceManifestPath() : PluginManifest::userManifestPath();
  if (path.isEmpty()) path = PluginManifest::userManifestPath();
  if (!PluginManifest::ensureManifestTemplate(path)) return;
  if (MainWindow* mw = resolveMainWindow()) mw->loadSourceFile(path);
  else QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void PluginSettingsPage::onUpdateAllClicked() {
  auto* mgr = resolvePluginManager();
  if (!mgr) return;
  QVector<PluginUpdateTarget> targets;
  for (const LoadedPluginInfo& info : mgr->plugins()) {
    if (!info.gitRepo.isEmpty()) targets.append(makeUpdateTarget(info));
  }
  startPluginUpdates(targets);
}

void PluginSettingsPage::startPluginUpdates(const QVector<PluginUpdateTarget>& targets) {
  if (!m_installer || m_installer->isRunning() || targets.isEmpty()) return;
  if (m_compilerLogView) m_compilerLogView->clear();
  if (m_installRestartBtn) m_installRestartBtn->setVisible(false);
  if (m_installedStatusLabel) {
    m_installedStatusLabel->setText(tr("Updating %1 plugin(s)...").arg(targets.size()));
    m_installedStatusLabel->setVisible(true);
  }
  if (!m_installer->startUpdatePlugins(targets)) return;
  if (m_cloneBuildBtn) m_cloneBuildBtn->setText(tr("Cancel"));
  if (m_syncManifestBtn) m_syncManifestBtn->setEnabled(false);
  if (m_updateAllBtn) m_updateAllBtn->setEnabled(false);
  if (m_reloadBtn) m_reloadBtn->setEnabled(false);
  if (m_pluginTable) {
    for (int r = 0; r < m_pluginTable->rowCount(); ++r) {
      if (QWidget* w = m_pluginTable->cellWidget(r, 7)) w->setEnabled(false);
    }
  }
}

void PluginSettingsPage::onInstallFinished(const PluginBuildResult& result) {
  if (m_cloneBuildBtn) m_cloneBuildBtn->setText(tr("Install"));
  if (m_syncManifestBtn) m_syncManifestBtn->setEnabled(true);
  if (m_reloadBtn) m_reloadBtn->setEnabled(true);
  auto* mgr = resolvePluginManager();
  const bool mgrShaChanged = (mgr != nullptr) ? mgr->refreshInstalledLockMetadata() : false;
  const int warnCount = static_cast<int>(result.warnings.size()), errCount = static_cast<int>(result.errors.size());
  QString summary;
  if (result.success) {
    const bool builtAny = (result.installedCount > 0) || !result.installedPath.isEmpty();
    summary = builtAny ? tr("Installed %1 (%2 built, %3 warning(s), %4 error(s)).")
                             .arg(result.installedPath).arg(std::max(1, result.installedCount)).arg(warnCount).arg(errCount)
                       : tr("All declared plugins in plugins.toml are up to date (0 built).");
    if (mgrShaChanged || result.anyBinaryChanged) {
      if (m_installRestartBtn) m_installRestartBtn->setVisible(true);
      setRestartBannerVisible(true);
      if (mgr != nullptr) { mgr->setProperty("gl_restart_required", true); emit mgr->pluginsChanged(); }
      emit restartRequiredChanged(true);
    }
  } else {
    const QString detail = result.errors.isEmpty() ? tr("Build failed.") : result.errors.last();
    summary = tr("Build Failed (%1 warning(s), %2 error(s)): %3").arg(warnCount).arg(errCount).arg(detail);
  }
  if (m_buildSummaryBadge) m_buildSummaryBadge->setText(summary);
  if (m_installedStatusLabel && !m_installedStatusLabel->isHidden()) m_installedStatusLabel->setText(summary);
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

  const bool isDirty = (staged.globalEnabled != baseGlobal) || (staged.workspaceEnabled != baseWorkspace) ||
                       (isWs && staged.workspaceTrusted != baseTrusted);
  QString statusText;
  if (abiRejected) {
    statusText = (info.abiMajor > 0u) ? QStringLiteral("ABI Rejected: v%1.%2").arg(info.abiMajor).arg(info.abiMinor)
                                      : QStringLiteral("ABI Rejected: invalid");
  } else if (isDirty) {
    statusText = (isWs && !staged.workspaceTrusted)
                     ? tr("Staged: Untrusted (Apply to save)")
                     : (!staged.globalEnabled ? tr("Staged: Disabled Globally (Apply to save)")
                                              : (!staged.workspaceEnabled ? tr("Staged: Disabled in Workspace (Apply to save)")
                                                                          : tr("Staged: Enabled (Apply to save)")));
  } else if (info.requiresRestart) {
    statusText = tr("Restart Required");
  } else if (isUntrustedWs) {
    statusText = info.errorMessage.isEmpty() ? tr("Untrusted Workspace Plugin (Approval Required)") : info.errorMessage;
  } else if (info.enabled) {
    statusText = tr("Loaded");
  } else {
    statusText = !baseGlobal ? tr("Disabled (Global)") : (!baseWorkspace ? tr("Disabled (Workspace)") : tr("Disabled"));
  }
  if (QTableWidgetItem* statusItem = m_pluginTable->item(row, 5)) statusItem->setText(statusText);

  if (isWs && !abiRejected) {
    if (auto* trustBtn = qobject_cast<QPushButton*>(m_pluginTable->cellWidget(row, 6))) {
      const QString shaPrefix = info.sha256Hex.isEmpty() ? QStringLiteral("unknown") : info.sha256Hex.left(8);
      trustBtn->setText(staged.workspaceTrusted ? tr("Trusted (%1)").arg(shaPrefix)
                                                : tr("Trust & Approve (%1)").arg(shaPrefix));
      trustBtn->setToolTip(staged.workspaceTrusted
                               ? tr("Approved SHA-256: %1. Click to stage trust revocation.").arg(info.sha256Hex)
                               : tr("Click to stage SHA-256 approval (%1); committed on Apply.").arg(info.sha256Hex));
    }
  }
}

void PluginSettingsPage::refreshTable() {
  auto* mgr = resolvePluginManager();
  if (m_pluginTable == nullptr || m_searchDirsCombo == nullptr) return;
  if (mgr == nullptr) {
    m_pluginTable->setRowCount(0);
    if (m_updateAllBtn) m_updateAllBtn->setEnabled(false);
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
  if (mgr->property("gl_restart_required").toBool() ||
      std::any_of(list.cbegin(), list.cend(), [](const LoadedPluginInfo& i) { return i.requiresRestart; })) {
    setRestartBannerVisible(true);
  }
  const bool installerBusy = (m_installer != nullptr) && m_installer->isRunning();
  const bool anyGitPlugin =
      std::any_of(list.cbegin(), list.cend(), [](const LoadedPluginInfo& i) { return !i.gitRepo.isEmpty(); });
  if (m_updateAllBtn) m_updateAllBtn->setEnabled(anyGitPlugin && !installerBusy);

  m_pluginTable->setRowCount(0);
  m_pluginTable->setRowCount(list.size());
  const bool hasWorkspace = !gridlock::core::ConfigManager::instance().workspacePath().trimmed().isEmpty();

  for (int row = 0; row < list.size(); ++row) {
    const auto& info = list.at(row);
    const QString rowKey = pluginRowKey(info);
    const bool isWs = (info.scope == PluginScope::Workspace);
    const bool abiRejected = !info.errorMessage.isEmpty() && !(isWs && !info.trusted);
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

    const QString rowTip = formatRowProvenanceTooltip(info);
    auto makeCell = [&](const QString& text) {
      auto* item = new QTableWidgetItem(text);
      item->setToolTip(rowTip);
      return item;
    };
    const QString abiText = (info.abiMajor > 0u || info.abiMinor > 0u)
                                ? QStringLiteral("v%1.%2").arg(info.abiMajor).arg(info.abiMinor)
                                : QStringLiteral("v1.0");
    auto* nameItem = makeCell(info.name.isEmpty() ? info.id : info.name);
    nameItem->setData(Qt::UserRole, rowKey);
    m_pluginTable->setItem(row, 2, nameItem);
    m_pluginTable->setItem(row, 3, makeCell(formatVersionCellText(info)));
    m_pluginTable->setItem(row, 4, makeCell(QStringLiteral("%1 · %2").arg(isWs ? tr("Workspace") : tr("Global"), abiText)));
    m_pluginTable->setItem(row, 5, makeCell(QString()));

    if (isWs && !abiRejected) {
      auto* trustBtn = new QPushButton(m_pluginTable);
      trustBtn->setObjectName(QStringLiteral("pluginTrustBtn_") + info.id);
      trustBtn->setMinimumHeight(24);
      m_pluginTable->setCellWidget(row, 6, trustBtn);
      connect(trustBtn, &QPushButton::clicked, this, [this, row, rowKey]() {
        if (m_stagedStates.contains(rowKey)) {
          m_stagedStates[rowKey].workspaceTrusted = !m_stagedStates[rowKey].workspaceTrusted;
          updateRowPreview(row, rowKey);
        }
      });
    } else {
      m_pluginTable->setItem(row, 6, makeCell(isWs ? tr("Rejected") : tr("User Global")));
    }

    const QString btnSuffix = !info.id.isEmpty() ? info.id : QFileInfo(info.libraryPath).completeBaseName();
    auto* updateBtn = new QPushButton(tr("Update"), m_pluginTable);
    updateBtn->setObjectName(QStringLiteral("pluginUpdateBtn_") + btnSuffix);
    updateBtn->setMinimumSize(82, 24);
    updateBtn->setStyleSheet(QStringLiteral("QPushButton { padding: 2px 8px; }"));
    const bool hasGitSource = !info.gitRepo.isEmpty();
    updateBtn->setEnabled(hasGitSource && !installerBusy);
    updateBtn->setToolTip(hasGitSource ? tr("Re-clone and rebuild from %1").arg(info.gitRepo)
                                       : QStringLiteral("Local binary (no Git source)"));
    m_pluginTable->setCellWidget(row, 7, updateBtn);
    if (hasGitSource) {
      connect(updateBtn, &QPushButton::clicked, this,
              [this, target = makeUpdateTarget(info)]() { startPluginUpdates({target}); });
    }

    connect(globalChk, &QCheckBox::toggled, this, [this, row, rowKey](bool checked) {
      if (m_stagedStates.contains(rowKey)) {
        m_stagedStates[rowKey].globalEnabled = checked;
        updateRowPreview(row, rowKey);
      }
    });
    connect(wsChk, &QCheckBox::toggled, this, [this, row, rowKey](bool checked) {
      if (m_stagedStates.contains(rowKey)) {
        m_stagedStates[rowKey].workspaceEnabled = checked;
        updateRowPreview(row, rowKey);
      }
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
    if (staged.globalEnabled == curGlobal && staged.workspaceEnabled == curWorkspace &&
        (!isWs || staged.workspaceTrusted == curTrusted)) {
      continue;
    }
    mgr->setProperty("gl_restart_required", true);
    anyChanged = true;
    if (staged.globalEnabled != curGlobal) mgr->setPluginGloballyEnabled(info.id, staged.globalEnabled);
    if (staged.workspaceEnabled != curWorkspace) mgr->setPluginWorkspaceEnabled(info.id, staged.workspaceEnabled);
    if (isWs && staged.workspaceTrusted != curTrusted) {
      if (staged.workspaceTrusted) mgr->trustWorkspacePlugin(info.id);
      else mgr->revokeWorkspacePluginTrust(info.id);
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
  if (MainWindow* mw = resolveMainWindow(); mw != nullptr && !mw->activeWorkspaceDirectory().isEmpty()) {
    mw->saveSession(false);
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
