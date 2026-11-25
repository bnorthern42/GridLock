/**
 * @file test_ui_plugins.cpp
 * @brief UI integration tests for Preferences -> Plugins, nested menus, status items, 5 context surfaces, annotations, themes, and report dock.
 */

#include <memory>

#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QPoint>
#include <QPushButton>
#include <QSignalSpy>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QToolButton>
#include <QTreeView>
#include <QTreeWidget>
#include <QtTest/QtTest>

#include "../../src/core/hpc/gdb/GdbRankCoordinator.hpp"
#include "../../src/core/managers/ConfigManager.hpp"
#include "../../src/core/managers/ThemeManager.hpp"
#include "../../src/core/plugins/PluginInstaller.hpp"
#include "../../src/core/plugins/PluginManager.hpp"
#include "../../src/ui/EditorTabManager.hpp"
#include "../../src/ui/MainWindow.hpp"
#include "../../src/ui/dialogs/preferences/PluginSettingsPage.hpp"
#include "../../src/ui/dialogs/preferences/PreferencesDialog.hpp"
#include "../../src/ui/docks/CallStackDockWidget.hpp"
#include "../../src/ui/docks/PluginReportDockWidget.hpp"
#include "../../src/ui/docks/VariablesDockWidget.hpp"
#include "../../src/ui/views/ServerRackView.hpp"
#include "../../src/ui/views/SourceCodeView.hpp"

void gridlockVerifyPluginSubsystemUi(gridlock::ui::MainWindow& mw) {
  QTemporaryDir plugCfgDir;
  QTemporaryDir plugWsDir;
  QVERIFY(plugCfgDir.isValid() && plugWsDir.isValid());

  const bool hadEnv = qEnvironmentVariableIsSet("GRIDLOCK_TEST_CONFIG_DIR");
  const QByteArray prevEnv = qgetenv("GRIDLOCK_TEST_CONFIG_DIR");
  const QString prevWs = gridlock::core::ConfigManager::instance().workspacePath();
  qputenv("GRIDLOCK_TEST_CONFIG_DIR", plugCfgDir.path().toUtf8());

  struct PluginUiScopeCleanup {
    gridlock::ui::MainWindow& win;
    bool restoreEnv;
    QByteArray savedEnv;
    QString savedWs;
    ~PluginUiScopeCleanup() {
      win.setCoordinator(nullptr);
      if (win.pluginManager()) win.pluginManager()->unloadAll();
      if (restoreEnv) qputenv("GRIDLOCK_TEST_CONFIG_DIR", savedEnv);
      else qunsetenv("GRIDLOCK_TEST_CONFIG_DIR");
      gridlock::core::ConfigManager::instance().loadConfig();
      gridlock::core::ConfigManager::instance().setWorkspace(savedWs);
    }
  } scopeCleanup{mw, hadEnv, prevEnv, prevWs};

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

  // Create a distinct untrusted workspace binary by appending 1 trailing zero byte so its SHA-256 differs.
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

  // Verify nested "/"-delimited submenus under Tools -> Plugins ("Mesh Tools/Refinement/Export VTK").
  auto* meshSubMenu = mw.findChild<QMenu*>(QStringLiteral("pluginSubMenu_Mesh Tools"));
  auto* refineSubMenu = mw.findChild<QMenu*>(QStringLiteral("pluginSubMenu_Mesh Tools/Refinement"));
  auto* exportMenuAct = mw.findChild<QAction*>(QStringLiteral("pluginMenuAct_dummy.export_vtk"));
  QVERIFY(meshSubMenu != nullptr && refineSubMenu != nullptr && exportMenuAct != nullptr);
  QCOMPARE(exportMenuAct->text(), QStringLiteral("Export VTK"));

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
  QCOMPARE(page->tabWidget()->tabText(1), QStringLiteral("Install from Git"));

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
  QVERIFY(cfg.isPluginGloballyEnabled(QStringLiteral("org.gridlock.test_dummy")));
  QVERIFY(cfg.isPluginWorkspaceEnabled(QStringLiteral("org.gridlock.test_dummy")));
  QVERIFY(tbl->item(0, 5)->text().contains(QStringLiteral("Staged")));
  pd.reject();
  QVERIFY(cfg.isPluginGloballyEnabled(QStringLiteral("org.gridlock.test_dummy")));
  QVERIFY(cfg.isPluginWorkspaceEnabled(QStringLiteral("org.gridlock.test_dummy")));

  // 2. Reopen Preferences -> Plugins, stage Workspace disable, click Apply, and verify Restart banners + GitHub tab.
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

  pd2.pluginPage()->tabWidget()->setCurrentIndex(1);
  QApplication::processEvents();
  auto* hostCombo = pd2.findChild<QComboBox*>(QStringLiteral("pluginGitHostCombo"));
  auto* repoEdit = pd2.findChild<QLineEdit*>(QStringLiteral("pluginGitRepoEdit"));
  QVERIFY(pd2.findChild<QWidget*>(QStringLiteral("pluginInstallTab")) != nullptr);
  QVERIFY(hostCombo != nullptr && hostCombo->count() == 4 && repoEdit != nullptr);
  QVERIFY(pd2.pluginPage()->gitRepoEdit() == repoEdit && pd2.pluginPage()->githubRefEdit() != nullptr &&
          pd2.pluginPage()->installScopeCombo() != nullptr && pd2.pluginPage()->cloneAndBuildButton() != nullptr &&
          pd2.pluginPage()->syncManifestButton() != nullptr && pd2.pluginPage()->openManifestButton() != nullptr &&
          pd2.pluginPage()->compilerLogView() != nullptr);
  QVERIFY(repoEdit->placeholderText().contains(QStringLiteral("owner/repo")));
  hostCombo->setCurrentIndex(1);
  QApplication::processEvents();
  QVERIFY(repoEdit->placeholderText().contains(QStringLiteral("gitlab.com")));
  hostCombo->setCurrentIndex(2);
  QApplication::processEvents();
  QVERIFY(repoEdit->placeholderText().contains(QStringLiteral("codeberg.org")));
  hostCombo->setCurrentIndex(3);
  QApplication::processEvents();
  QVERIFY(repoEdit->placeholderText().contains(QStringLiteral("gitea.example.org")));
  hostCombo->setCurrentIndex(0);
  repoEdit->setText(QStringLiteral("-oProxyCommand=evil"));
  pd2.pluginPage()->cloneAndBuildButton()->click();
  QApplication::processEvents();
  QVERIFY(pd2.pluginPage()->buildSummaryBadge()->text().contains(QStringLiteral("Build Failed")));
  QVERIFY(pd2.pluginPage()->compilerLogView()->toPlainText().contains(QStringLiteral("must not begin with '-'")));

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
  QApplication::processEvents();

  // 3. Verify Status-Bar badge mounting, clicking, message-box + two-pass promptInput, and toast + PluginReportDockWidget.
  QSignalSpy evSpy(&mw, &gridlock::ui::MainWindow::pluginStructuredEventEmitted);
  auto* statusBtn = mw.findChild<QToolButton*>(QStringLiteral("pluginStatusItem_dummy.mesh_status"));
  QVERIFY(statusBtn != nullptr && !statusBtn->isHidden());
  QCOMPARE(statusBtn->text(), QStringLiteral("Mesh: Ready"));

  mw.setProperty("gl_plugin_msgbox_auto_result", static_cast<int>(GRIDLOCK_RESULT_YES));
  mw.setProperty("gl_plugin_prompt_auto_accept", true);
  mw.setProperty("gl_plugin_prompt_auto_value", QStringLiteral("ui_mesh_probe_tag_99"));
  statusBtn->click();
  QApplication::processEvents();
  QCOMPARE(statusBtn->text(), QStringLiteral("Mesh: Active"));
  QVERIFY(!evSpy.isEmpty() && evSpy.last().at(0).toString() == QStringLiteral("dummy.status_click") &&
          evSpy.last().at(1).toString().contains(QStringLiteral("ui_mesh_probe_tag_99")));
  mw.setProperty("gl_plugin_msgbox_auto_result", QVariant());
  mw.setProperty("gl_plugin_prompt_auto_accept", QVariant());
  mw.setProperty("gl_plugin_prompt_auto_value", QVariant());

  exportMenuAct = mw.findChild<QAction*>(QStringLiteral("pluginMenuAct_dummy.export_vtk"));
  QVERIFY(exportMenuAct != nullptr);
  exportMenuAct->trigger();
  QApplication::processEvents();

  auto* reportDock = mw.pluginReportDockWidget();
  QVERIFY(reportDock != nullptr && !reportDock->isHidden() && reportDock->panelCount() == 3);
  auto* mdBrowser = qobject_cast<QTextBrowser*>(reportDock->panelWidget(QStringLiteral("dummy.report.md")));
  auto* kvTable = qobject_cast<QTableWidget*>(reportDock->panelWidget(QStringLiteral("dummy.report.kv")));
  auto* jsonTree = qobject_cast<QTreeWidget*>(reportDock->panelWidget(QStringLiteral("dummy.report.json")));
  QVERIFY(mdBrowser != nullptr && mdBrowser->toPlainText().contains(QStringLiteral("All ranks converged")));
  QVERIFY(kvTable != nullptr && kvTable->rowCount() == 2);
  QVERIFY(jsonTree != nullptr && jsonTree->topLevelItemCount() == 2);
  auto* toastBanner = mw.findChild<QWidget*>(QStringLiteral("pluginToastBanner"));
  QVERIFY(toastBanner != nullptr && !toastBanner->isHidden());

  // 4. Verify all 5 multi-surface context menus, editor line annotations, call-stack navigation, and theme propagation.
  const QString demoSrc = QFileInfo::exists(QStringLiteral("tutorial/inspection_demo.cpp"))
                              ? QFileInfo(QStringLiteral("tutorial/inspection_demo.cpp")).canonicalFilePath()
                              : QFileInfo(QStringLiteral("../tutorial/inspection_demo.cpp")).canonicalFilePath();
  QVERIFY(!demoSrc.isEmpty());
  mw.loadSourceFile(demoSrc);
  QApplication::processEvents();

  auto* srcView = mw.sourceCodeView();
  QVERIFY(srcView != nullptr);
  const QPoint edMenuPos(srcView->lineNumberAreaWidth() + 24, 12);
  std::unique_ptr<QMenu> edMenu(srcView->createEditorContextMenuAt(edMenuPos));
  QVERIFY(edMenu != nullptr && edMenu->findChild<QMenu*>(QStringLiteral("codeEditorPluginActionsMenu")) != nullptr);
  auto* edCtxAct = edMenu->findChild<QAction*>(QStringLiteral("pluginCtxAct_dummy.ctx.editor"));
  QVERIFY(edCtxAct != nullptr);
  edCtxAct->trigger();
  QApplication::processEvents();
  QVERIFY(!srcView->pluginAnnotations().isEmpty());
  QCOMPARE(srcView->pluginAnnotations().first().badgeText, QStringLiteral("HOTSPOT"));

  std::unique_ptr<QMenu> bpMenu(srcView->createBreakpointContextMenuAtLine(5, srcView));
  QVERIFY(bpMenu != nullptr && bpMenu->findChild<QMenu*>(QStringLiteral("breakpointPluginActionsMenu")) != nullptr);
  auto* bpCtxAct = bpMenu->findChild<QAction*>(QStringLiteral("pluginCtxAct_dummy.ctx.bp"));
  QVERIFY(bpCtxAct != nullptr);
  bpCtxAct->trigger();
  QApplication::processEvents();
  QVERIFY(!evSpy.isEmpty() && evSpy.last().at(0).toString() == QStringLiteral("dummy.ctx") &&
          evSpy.last().at(1).toString().contains(QStringLiteral("\"surface\":4")));

  auto* csDock = mw.callStackDockWidget();
  QVERIFY(csDock != nullptr);
  StackFrameEntry frameEntry;
  frameEntry.level = 0;
  frameEntry.function = QStringLiteral("main");
  frameEntry.file = demoSrc;
  frameEntry.line = 12;
  frameEntry.address = 0x401120ULL;
  csDock->setStackFrames({frameEntry}, 0);
  std::unique_ptr<QMenu> csMenu(csDock->createContextMenuForRow(0, csDock));
  QVERIFY(csMenu != nullptr && csMenu->findChild<QMenu*>(QStringLiteral("callStackPluginActionsMenu")) != nullptr);
  auto* csCtxAct = csMenu->findChild<QAction*>(QStringLiteral("pluginCtxAct_dummy.ctx.stack"));
  QVERIFY(csCtxAct != nullptr);
  csCtxAct->trigger();
  QApplication::processEvents();
  QCOMPARE(srcView->getCurrentLineNumber(), 12);

  auto* rackView = mw.serverRackView();
  QVERIFY(rackView != nullptr && rackView->rankNodeCount() >= 1);
  std::unique_ptr<QMenu> rackMenu(rackView->createContextMenuForRank(0, rackView));
  QVERIFY(rackMenu != nullptr && rackMenu->findChild<QMenu*>(QStringLiteral("rankListPluginActionsMenu")) != nullptr);
  auto* rackCtxAct = rackMenu->findChild<QAction*>(QStringLiteral("pluginCtxAct_dummy.ctx.rank"));
  QVERIFY(rackCtxAct != nullptr);
  rackCtxAct->trigger();
  QApplication::processEvents();
  QVERIFY(!evSpy.isEmpty() && evSpy.last().at(0).toString() == QStringLiteral("dummy.ctx") &&
          evSpy.last().at(1).toString().contains(QStringLiteral("\"surface\":5")));

  // Verify live ThemeManager switch broadcasts on_theme_changed to active C99 plugins.
  auto& tm = gridlock::core::ThemeManager::instance();
  const QString prevTheme = tm.activeThemeId();
  tm.applyTheme(QStringLiteral("daylight_light"), false);
  QApplication::processEvents();
  bool sawThemeBroadcast = false;
  for (int i = 0; i < evSpy.count(); ++i) {
    if (evSpy.at(i).at(0).toString() == QStringLiteral("dummy.theme_changed") &&
        evSpy.at(i).at(1).toString().contains(QStringLiteral("daylight_light"))) {
      sawThemeBroadcast = true;
      break;
    }
  }
  QVERIFY(sawThemeBroadcast);
  tm.applyTheme(prevTheme.isEmpty() ? QStringLiteral("gridlock_slate") : prevTheme, false);
  QApplication::processEvents();

  // 5. Bind stopped GdbRankCoordinator and verify VariablesDockWidget Plugin Actions (both variable + surface actions).
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
  auto* varSurfAct = ctxMenu->findChild<QAction*>(QStringLiteral("pluginCtxAct_dummy.ctx.vars"));
  QVERIFY(plugSub != nullptr && varPlugAct != nullptr && varSurfAct != nullptr);
  varPlugAct->trigger();
  QApplication::processEvents();
  QVERIFY(!evSpy.isEmpty() && evSpy.last().at(0).toString() == QStringLiteral("dummy.var_eval") &&
          evSpy.last().at(1).toString() == QStringLiteral("petsc_vec_norm_3.14159"));

  // 6. Unload all plugins and verify status items, editor annotations, and report dock panels are purged.
  QObject::disconnect(evalConn);
  mw.setCoordinator(nullptr);
  mw.pluginManager()->unloadAll();
  QApplication::processEvents();
  QVERIFY(srcView->pluginAnnotations().isEmpty());
  QCOMPARE(reportDock->panelCount(), 0);
  QVERIFY(mw.findChild<QToolButton*>(QStringLiteral("pluginStatusItem_dummy.mesh_status")) == nullptr);
}
