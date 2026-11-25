/**
 * @file test_ui_plugins.cpp
 * @brief UI integration regression tests for `Preferences -> Plugins`, dual-scope staging, SHA-256 trust, and GitHub installer tab.
 */

#include <memory>

#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTreeView>
#include <QTreeWidget>
#include <QtTest/QtTest>

#include "../../src/core/hpc/gdb/GdbRankCoordinator.hpp"
#include "../../src/core/managers/ConfigManager.hpp"
#include "../../src/core/plugins/PluginInstaller.hpp"
#include "../../src/core/plugins/PluginManager.hpp"
#include "../../src/ui/MainWindow.hpp"
#include "../../src/ui/dialogs/preferences/PluginSettingsPage.hpp"
#include "../../src/ui/dialogs/preferences/PreferencesDialog.hpp"
#include "../../src/ui/docks/VariablesDockWidget.hpp"

void gridlockVerifyPluginSubsystemUi(gridlock::ui::MainWindow& mw) {
  QTemporaryDir plugCfgDir;
  QTemporaryDir plugWsDir;
  QVERIFY(plugCfgDir.isValid() && plugWsDir.isValid());

  const bool hadEnv = qEnvironmentVariableIsSet("GRIDLOCK_TEST_CONFIG_DIR");
  const QByteArray prevEnv = qgetenv("GRIDLOCK_TEST_CONFIG_DIR");
  const QString prevWs = gridlock::core::ConfigManager::instance().workspacePath();
  qputenv("GRIDLOCK_TEST_CONFIG_DIR", plugCfgDir.path().toUtf8());

  auto& cfg = gridlock::core::ConfigManager::instance();
  cfg.loadConfig();
  cfg.setWorkspace(plugWsDir.path());
  cfg.setDisabledPluginIds({});
  cfg.setWorkspaceDisabledPluginIds({});

  const QString globalPlugDir = plugCfgDir.path() + QStringLiteral("/plugins");
  const QString wsPlugDir = plugWsDir.path() + QStringLiteral("/.gridlock/plugins");
  QVERIFY(QDir().mkpath(globalPlugDir) && QDir().mkpath(wsPlugDir));
  QVERIFY(QFile::copy(QStringLiteral(GRIDLOCK_TEST_DUMMY_PLUGIN_SO),
                      QDir(globalPlugDir).filePath(QStringLiteral("01_dummy.so"))));
  QVERIFY(QFile::copy(QStringLiteral(GRIDLOCK_TEST_BAD_ABI_PLUGIN_SO),
                      QDir(globalPlugDir).filePath(QStringLiteral("02_bad_abi.so"))));

  // Create a distinct untrusted workspace binary (append 1 trailing zero byte so its SHA-256 differs from 01_dummy.so).
  const QString wsUntrustedPath = QDir(wsPlugDir).filePath(QStringLiteral("03_ws_probe.so"));
  QVERIFY(QFile::copy(QStringLiteral(GRIDLOCK_TEST_DUMMY_PLUGIN_SO), wsUntrustedPath));
  {
    QFile wf(wsUntrustedPath);
    QVERIFY(wf.open(QIODevice::Append));
    QCOMPARE(wf.write("\0", 1), qint64{1});
  }

  QVERIFY(mw.pluginManager() != nullptr);
  QCOMPARE(mw.pluginManager()->discoverAndLoad(), 1);
  mw.rebuildToolsPluginsMenu();
  auto* toolsPlugMenu = mw.findChild<QMenu*>(QStringLiteral("menuToolsPlugins"));
  QVERIFY(toolsPlugMenu != nullptr && mw.findChild<QAction*>(QStringLiteral("pluginCmd_dummy.ping_ranks")) != nullptr);

  // 1. Open Preferences -> Plugins and verify 7-column table, untrusted workspace button, and Cancel staging.
  gridlock::ui::PreferencesDialog pd(&mw);
  pd.show();
  QApplication::processEvents();
  QTreeWidgetItem* plugNode = nullptr;
  for (int i = 0; i < pd.categoryTree()->topLevelItemCount(); ++i) {
    if (pd.categoryTree()->topLevelItem(i)->text(0) == QStringLiteral("Plugins")) {
      plugNode = pd.categoryTree()->topLevelItem(i);
    }
  }
  QVERIFY(plugNode != nullptr && pd.pluginPage() != nullptr);
  pd.categoryTree()->setCurrentItem(plugNode);
  QApplication::processEvents();

  auto* page = pd.pluginPage();
  QVERIFY(page->tabWidget() != nullptr && page->tabWidget()->count() == 2);
  QCOMPARE(page->tabWidget()->tabText(0), QStringLiteral("Installed"));
  QCOMPARE(page->tabWidget()->tabText(1), QStringLiteral("Install from GitHub"));

  QTableWidget* tbl = page->pluginTable();
  QVERIFY(tbl != nullptr && tbl->rowCount() == 3 && tbl->columnCount() == 7);
  QCOMPARE(tbl->item(0, 3)->text(), QStringLiteral("1.0.0"));
  QVERIFY(tbl->item(0, 4)->text().contains(QStringLiteral("v1.0")));
  QCOMPARE(tbl->item(0, 5)->text(), QStringLiteral("Loaded"));
  QVERIFY(tbl->item(1, 5)->text().startsWith(QStringLiteral("ABI Rejected:")));
  QVERIFY(tbl->item(2, 5)->text().contains(QStringLiteral("Untrusted")));
  auto* trustBtn = pd.findChild<QPushButton*>(QStringLiteral("pluginTrustBtn_03_ws_probe"));
  QVERIFY(trustBtn != nullptr && trustBtn->text().startsWith(QStringLiteral("Trust & Approve")));

  auto* globalChk = pd.findChild<QCheckBox*>(QStringLiteral("pluginGlobalCheck_org.gridlock.test_dummy"));
  auto* wsChk = pd.findChild<QCheckBox*>(QStringLiteral("pluginWorkspaceCheck_org.gridlock.test_dummy"));
  QVERIFY(globalChk != nullptr && wsChk != nullptr && globalChk->isChecked() && wsChk->isChecked());
  globalChk->setChecked(false);
  wsChk->setChecked(false);
  trustBtn->click();
  QApplication::processEvents();
  // Before Apply: ConfigManager must remain completely untouched, and Cancel discards all staged edits.
  QVERIFY(cfg.isPluginGloballyEnabled(QStringLiteral("org.gridlock.test_dummy")));
  QVERIFY(cfg.isPluginWorkspaceEnabled(QStringLiteral("org.gridlock.test_dummy")));
  QVERIFY(tbl->item(0, 5)->text().contains(QStringLiteral("Staged")));
  pd.reject();
  QVERIFY(cfg.isPluginGloballyEnabled(QStringLiteral("org.gridlock.test_dummy")));
  QVERIFY(cfg.isPluginWorkspaceEnabled(QStringLiteral("org.gridlock.test_dummy")));

  // 2. Reopen Preferences -> Plugins, stage Workspace disable, click Apply, and verify Restart banners & dry-run restart.
  gridlock::ui::PreferencesDialog pd2(&mw);
  pd2.show();
  QApplication::processEvents();
  auto* wsChk2 = pd2.findChild<QCheckBox*>(QStringLiteral("pluginWorkspaceCheck_org.gridlock.test_dummy"));
  QVERIFY(wsChk2 != nullptr && wsChk2->isChecked());
  wsChk2->setChecked(false);
  QVERIFY(QMetaObject::invokeMethod(&pd2, "apply"));
  QApplication::processEvents();
  QVERIFY(!cfg.isPluginWorkspaceEnabled(QStringLiteral("org.gridlock.test_dummy")));
  QVERIFY(cfg.isPluginGloballyEnabled(QStringLiteral("org.gridlock.test_dummy")));
  QVERIFY(!pd2.pluginPage()->restartBanner()->isHidden());
  auto* mwBanner = mw.findChild<QWidget*>(QStringLiteral("mainWindowPluginRestartBanner"));
  QVERIFY(mwBanner != nullptr && !mwBanner->isHidden() && pd2.pluginPage()->restartNowButton() != nullptr);

  // Verify Tab 2 (Install from GitHub) controls and flag-injection rejection diagnostic.
  pd2.pluginPage()->tabWidget()->setCurrentIndex(1);
  QApplication::processEvents();
  QVERIFY(pd2.pluginPage()->githubUrlEdit() != nullptr && pd2.pluginPage()->githubRefEdit() != nullptr &&
          pd2.pluginPage()->installScopeCombo() != nullptr && pd2.pluginPage()->cloneAndBuildButton() != nullptr &&
          pd2.pluginPage()->compilerLogView() != nullptr);
  pd2.pluginPage()->githubUrlEdit()->setText(QStringLiteral("-oProxyCommand=evil"));
  pd2.pluginPage()->cloneAndBuildButton()->click();
  QApplication::processEvents();
  QVERIFY(pd2.pluginPage()->buildSummaryBadge()->text().contains(QStringLiteral("Build Failed")));
  QVERIFY(pd2.pluginPage()->compilerLogView()->toPlainText().contains(QStringLiteral("must not begin with '-'")));

  // Re-enable workspace flag, dry-run Restart GridLock Now button, and reload plugins cleanly.
  pd2.pluginPage()->tabWidget()->setCurrentIndex(0);
  wsChk2 = pd2.findChild<QCheckBox*>(QStringLiteral("pluginWorkspaceCheck_org.gridlock.test_dummy"));
  QVERIFY(wsChk2 != nullptr);
  wsChk2->setChecked(true);
  qputenv("GRIDLOCK_DRY_RUN_SELF_RESTART", "1");
  qApp->setProperty("gridlock_restart_triggered", false);
  pd2.pluginPage()->restartNowButton()->click();
  qunsetenv("GRIDLOCK_DRY_RUN_SELF_RESTART");
  QVERIFY(qApp->property("gridlock_restart_triggered").toBool());
  QVERIFY(cfg.isPluginWorkspaceEnabled(QStringLiteral("org.gridlock.test_dummy")));
  QFile::remove(wsUntrustedPath);
  QCOMPARE(mw.pluginManager()->discoverAndLoad(), 1);

  // 3. Bind stopped GdbRankCoordinator and verify VariablesDockWidget Plugin Actions -> Inspect Domain Field.
  gridlock::GdbRankCoordinator coord;
  coord.initializeMockSession(2);
  mw.setCoordinator(&coord);
  coord.processGdbOutput(
      0, QStringLiteral("*stopped,reason=\"breakpoint-hit\",frame={addr=\"0x401120\",func=\"solve\",fullname=\"/tmp/sim.c\",line=\"12\",args=[]}\n"));
  const auto evalConn = QObject::connect(
      &coord, &gridlock::GdbRankCoordinator::commandSentToGdb, &coord, [&](int r, const QString& cmd) {
        if (cmd.contains(QStringLiteral("-data-evaluate-expression"))) {
          const QString tok = cmd.section(QLatin1Char('-'), 0, 0);
          coord.processGdbOutput(r, QStringLiteral("%1^done,value=\"petsc_vec_norm_3.14159\"\n").arg(tok));
        }
      });
  QSignalSpy evSpy(&mw, &gridlock::ui::MainWindow::pluginStructuredEventEmitted);
  auto* varDock = mw.variablesDockWidget();
  QVERIFY(varDock != nullptr);
  varDock->setFocusedRank(0, 2);
  varDock->applyLocalsForRank(
      0, QJsonArray{QJsonObject{{"name", "solution_vec"}, {"value", "0x5000"}, {"type", "PetscVec"}}});
  auto* varTree = varDock->findChild<QTreeView*>();
  QVERIFY(varTree && varTree->model()->rowCount() >= 1);
  std::unique_ptr<QMenu> ctxMenu(varDock->createContextMenuForIndex(varTree->model()->index(0, 0), varDock));
  QVERIFY(ctxMenu != nullptr);
  auto* plugSub = ctxMenu->findChild<QMenu*>(QStringLiteral("variablePluginActionsMenu"));
  auto* varPlugAct = ctxMenu->findChild<QAction*>(QStringLiteral("pluginVarAct_dummy.inspect_field"));
  QVERIFY(plugSub != nullptr && varPlugAct != nullptr);
  varPlugAct->trigger();
  QApplication::processEvents();
  QVERIFY(!evSpy.isEmpty() && evSpy.last().at(0).toString() == QStringLiteral("dummy.var_eval") &&
          evSpy.last().at(1).toString() == QStringLiteral("petsc_vec_norm_3.14159"));

  QObject::disconnect(evalConn);
  mw.setCoordinator(nullptr);
  mw.pluginManager()->unloadAll();
  if (hadEnv) qputenv("GRIDLOCK_TEST_CONFIG_DIR", prevEnv);
  else qunsetenv("GRIDLOCK_TEST_CONFIG_DIR");
  cfg.loadConfig();
  cfg.setWorkspace(prevWs);
}
