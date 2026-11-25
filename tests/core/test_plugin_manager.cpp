/**
 * @file test_plugin_manager.cpp
 * @brief Security, re-entrancy, dual-scope, host-bridge, and `PluginInstaller` regression suite.
 */

#include <cstdint>
#include <cstring>

#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QPair>
#include <QProcess>
#include <QSettings>
#include <QSignalSpy>
#include <QString>
#include <QTemporaryDir>
#include <QTimer>
#include <QVector>
#include <QtTest>

#include "../../src/core/hpc/gdb/GdbRankCoordinator.hpp"
#include "../../src/core/managers/ConfigManager.hpp"
#include "../../src/core/plugins/PluginHostBridge.hpp"
#include "../../src/core/plugins/PluginInstaller.hpp"
#include "../../src/core/plugins/PluginManager.hpp"
#include "../../src/core/plugins/PluginManifest.hpp"
#include "gridlock/plugin_api.h"

namespace {

class ScopedPluginEnv {
 public:
  ScopedPluginEnv()
      : m_hadPrevEnv(qEnvironmentVariableIsSet("GRIDLOCK_TEST_CONFIG_DIR")),
        m_prevEnv(qgetenv("GRIDLOCK_TEST_CONFIG_DIR")),
        m_prevWorkspace(gridlock::core::ConfigManager::instance().workspacePath()) {
    qputenv("GRIDLOCK_TEST_CONFIG_DIR", m_cfgDir.path().toUtf8());
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, m_cfgDir.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_cfgDir.path());
    auto& cfg = gridlock::core::ConfigManager::instance();
    cfg.loadConfig();
    cfg.setWorkspace(m_wsDir.path());
    cfg.setDisabledPluginIds({});
    cfg.setWorkspaceDisabledPluginIds({});
    QDir().mkpath(m_cfgDir.path() + QStringLiteral("/plugins"));
    QDir().mkpath(m_wsDir.path() + QStringLiteral("/.gridlock/plugins"));
  }

  ~ScopedPluginEnv() {
    if (m_hadPrevEnv) qputenv("GRIDLOCK_TEST_CONFIG_DIR", m_prevEnv);
    else qunsetenv("GRIDLOCK_TEST_CONFIG_DIR");
    auto& cfg = gridlock::core::ConfigManager::instance();
    cfg.loadConfig();
    cfg.setWorkspace(m_prevWorkspace);
  }

  QString tempRoot() const { return m_cfgDir.path(); }
  QString workspaceRoot() const { return m_wsDir.path(); }
  QString userPluginDir() const { return m_cfgDir.path() + QStringLiteral("/plugins"); }
  QString workspacePluginDir() const { return m_wsDir.path() + QStringLiteral("/.gridlock/plugins"); }

 private:
  QTemporaryDir m_cfgDir;
  QTemporaryDir m_wsDir;
  bool m_hadPrevEnv = false;
  QByteArray m_prevEnv;
  QString m_prevWorkspace;
};

bool copyPluginArtifact(const QString& srcSoPath, const QString& destDir, const QString& destName) {
  const QString dst = QDir(destDir).filePath(destName);
  QFile::remove(dst);
  return QFile::copy(srcSoPath, dst);
}

QString sha256OfFile(const QString& path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) return QString();
  QCryptographicHash h(QCryptographicHash::Sha256);
  if (!h.addData(&f)) return QString();
  return QString::fromLatin1(h.result().toHex()).toLower();
}

bool runGit(const QString& workDir, const QStringList& args) {
  QProcess p;
  p.setWorkingDirectory(workDir);
  p.start(QStringLiteral("git"), args);
  return p.waitForFinished(10000) && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

bool initLocalBarePluginRepo(const QString& rootPath, QString* bareOut, QString* workOut, QString* srcOut) {
  *bareOut = QDir(rootPath).filePath(QStringLiteral("local-plugin.git"));
  *workOut = QDir(rootPath).filePath(QStringLiteral("work"));
  *srcOut = QDir(*workOut).filePath(QStringLiteral("plugin_src.c"));
  return QDir().mkpath(*bareOut) && QDir().mkpath(*workOut) &&
         runGit(*bareOut, {QStringLiteral("init"), QStringLiteral("--bare"), QStringLiteral("-b"), QStringLiteral("main")}) &&
         runGit(*workOut, {QStringLiteral("init"), QStringLiteral("-b"), QStringLiteral("main")}) &&
         runGit(*workOut, {QStringLiteral("config"), QStringLiteral("user.email"), QStringLiteral("test@gridlock.local")}) &&
         runGit(*workOut, {QStringLiteral("config"), QStringLiteral("user.name"), QStringLiteral("GridLock Test")}) &&
         QFile::copy(QStringLiteral(GRIDLOCK_TEST_DUMMY_PLUGIN_SRC), *srcOut) &&
         runGit(*workOut, {QStringLiteral("add"), QStringLiteral("plugin_src.c")}) &&
         runGit(*workOut, {QStringLiteral("commit"), QStringLiteral("-m"), QStringLiteral("v1 commit")}) &&
         runGit(*workOut, {QStringLiteral("remote"), QStringLiteral("add"), QStringLiteral("origin"), *bareOut}) &&
         runGit(*workOut, {QStringLiteral("push"), QStringLiteral("origin"), QStringLiteral("main")});
}

struct ScopedGitRouteEnv {
  explicit ScopedGitRouteEnv(const QString& bareRepo, const QString& githubUrl) {
    const QString fileUrl = QStringLiteral("file://") + bareRepo;
    qputenv("GRIDLOCK_INCLUDE_DIR", QByteArrayLiteral(GRIDLOCK_TEST_INCLUDE_DIR));
    qputenv("GIT_CONFIG_COUNT", "2");
    qputenv("GIT_CONFIG_KEY_0", QStringLiteral("url.%1.insteadOf").arg(fileUrl).toUtf8());
    qputenv("GIT_CONFIG_VALUE_0", githubUrl.toUtf8());
    qputenv("GIT_CONFIG_KEY_1", "protocol.file.allow");
    qputenv("GIT_CONFIG_VALUE_1", "always");
  }
  ~ScopedGitRouteEnv() {
    for (const char* k : {"GIT_CONFIG_COUNT", "GIT_CONFIG_KEY_0", "GIT_CONFIG_VALUE_0",
                          "GIT_CONFIG_KEY_1", "GIT_CONFIG_VALUE_1", "GRIDLOCK_INCLUDE_DIR"}) {
      qunsetenv(k);
    }
  }
};

}  // namespace

class TestPluginManager : public QObject {
  Q_OBJECT

 private slots:
  void testDiscoveryAbiRejectionAndDuplicateGuard() {
    ScopedPluginEnv env;
    const QString dummySo = QStringLiteral(GRIDLOCK_TEST_DUMMY_PLUGIN_SO);
    const QString badAbiSo = QStringLiteral(GRIDLOCK_TEST_BAD_ABI_PLUGIN_SO);
    QVERIFY(QFileInfo::exists(dummySo) && QFileInfo::exists(badAbiSo));

    QVERIFY(copyPluginArtifact(dummySo, env.userPluginDir(), QStringLiteral("01_dummy.so")));
    QVERIFY(copyPluginArtifact(badAbiSo, env.userPluginDir(), QStringLiteral("02_bad_abi.so")));
    QVERIFY(copyPluginArtifact(dummySo, env.workspacePluginDir(), QStringLiteral("03_dup_dummy.so")));

    gridlock::GdbRankCoordinator coord;
    gridlock::core::plugins::PluginManager mgr(&coord);
    QCOMPARE(mgr.discoverAndLoad(), 1);
    QCOMPARE(mgr.plugins().size(), 3);

    bool sawLoadedDummy = false, sawRejectedBadAbi = false, sawRejectedDuplicate = false;
    for (const auto& info : mgr.plugins()) {
      if (info.libraryPath.endsWith(QStringLiteral("01_dummy.so"))) {
        QVERIFY(info.enabled && info.errorMessage.isEmpty());
        QCOMPARE(info.id, QStringLiteral("org.gridlock.test_dummy"));
        sawLoadedDummy = true;
      } else if (info.libraryPath.endsWith(QStringLiteral("02_bad_abi.so"))) {
        QVERIFY(!info.enabled && info.errorMessage.contains(QStringLiteral("ABI major mismatch")));
        sawRejectedBadAbi = true;
      } else if (info.libraryPath.endsWith(QStringLiteral("03_dup_dummy.so"))) {
        QVERIFY(!info.enabled && info.errorMessage.contains(QStringLiteral("Duplicate plugin ID")));
        sawRejectedDuplicate = true;
      }
    }
    QVERIFY(sawLoadedDummy && sawRejectedBadAbi && sawRejectedDuplicate);
    QCOMPARE(mgr.hostBridge()->variableActions().size(), 1);
    QCOMPARE(mgr.hostBridge()->matchingVariableActions(QStringLiteral("Kokkos::View<double**>")).size(), 1);
    QCOMPARE(mgr.hostBridge()->matchingVariableActions(QStringLiteral("int")).size(), 0);
  }

  void testHostBridgeEvalMemorySanitizationAndStopLifecycle() {
    ScopedPluginEnv env;
    QVERIFY(copyPluginArtifact(QStringLiteral(GRIDLOCK_TEST_DUMMY_PLUGIN_SO),
                               env.userPluginDir(), QStringLiteral("dummy.so")));

    gridlock::GdbRankCoordinator coord;
    coord.initializeMockSession(2);
    gridlock::core::plugins::PluginManager mgr(&coord);
    mgr.hostBridge()->setRequestTimeoutMs(80);
    mgr.hostBridge()->setActiveRank(1);

    QVector<QPair<QString, QString>> events;
    QObject::connect(mgr.hostBridge(), &gridlock::core::plugins::PluginHostBridge::structuredEventEmitted,
                     &coord, [&](const QString& ch, const QString& payload) { events.append(qMakePair(ch, payload)); });

    QCOMPARE(mgr.discoverAndLoad(), 1);
    const GridLockHostApi* api = mgr.hostBridge()->hostApi();
    size_t outLen = 0u;
    char smallBuf[8]{};

    QCOMPARE(api->eval_expr(api->host_ctx, 0, "x", smallBuf, sizeof(smallBuf), &outLen), GRIDLOCK_ERR_NOT_STOPPED);
    QCOMPARE(api->eval_expr(api->host_ctx, 99, "x", smallBuf, sizeof(smallBuf), &outLen), GRIDLOCK_ERR_INVALID_RANK);

    coord.processGdbOutput(
        0, QStringLiteral("*stopped,reason=\"breakpoint-hit\",frame={addr=\"0x401234\",func=\"solve\",file=\"sim.c\",fullname=\"/tmp/sim.c\",line=\"77\"}\n"));
    QVERIFY(!events.isEmpty() && events.last().first == QStringLiteral("dummy.stop"));
    QVERIFY(events.last().second.contains(QStringLiteral("\"line\":77")));
    QCOMPARE(api->eval_expr(api->host_ctx, 0, "unanswered_expr", smallBuf, sizeof(smallBuf), &outLen), GRIDLOCK_ERR_TIMEOUT);

    const auto miResponder = QObject::connect(
        &coord, &gridlock::GdbRankCoordinator::commandSentToGdb, &coord, [&](int rankId, const QString& cmd) {
          const QString tok = cmd.section(QLatin1Char('-'), 0, 0);
          if (cmd.contains(QStringLiteral("-data-evaluate-expression"))) {
            if (cmd.contains(QStringLiteral("mesh_field"))) {
              QTimer::singleShot(5, &coord, [&, rankId, tok]() {
                coord.processGdbOutput(rankId, QStringLiteral("%1^done,value=\"extent_256x128x64_double_grid\"\n").arg(tok));
              });
            } else if (cmd.contains(QStringLiteral("broken_sym"))) {
              coord.processGdbOutput(rankId, QStringLiteral("%1^error,msg=\"No symbol broken_sym\"\n").arg(tok));
            }
          } else if (cmd.contains(QStringLiteral("-data-read-memory-bytes 0x5000"))) {
            coord.processGdbOutput(
                rankId, QStringLiteral("%1^done,memory=[{begin=\"0x00005000\",offset=\"0x00000000\",end=\"0x00005004\",contents=\"deadbeef\"}]\n").arg(tok));
          } else if (cmd.contains(QStringLiteral("-data-read-memory-bytes 0xbad0"))) {
            coord.processGdbOutput(rankId, QStringLiteral("%1^error,msg=\"Cannot access memory\"\n").arg(tok));
          } else if (cmd.contains(QStringLiteral("-data-write-memory-bytes 0x5000"))) {
            coord.processGdbOutput(rankId, QStringLiteral("%1^done\n").arg(tok));
          } else if (cmd.contains(QStringLiteral("-data-write-memory-bytes 0xbad0"))) {
            coord.processGdbOutput(rankId, QStringLiteral("%1^error,msg=\"Cannot write memory\"\n").arg(tok));
          }
        });

    GridLockVariableContext varCtx{};
    varCtx.struct_size = static_cast<uint32_t>(sizeof(GridLockVariableContext));
    varCtx.abi_major = GRIDLOCK_PLUGIN_ABI_MAJOR;
    varCtx.abi_minor = GRIDLOCK_PLUGIN_ABI_MINOR;
    varCtx.rank = 0;
    varCtx.action_id_utf8 = "dummy.inspect_field";
    varCtx.name_utf8 = "mesh_field";
    varCtx.expression_utf8 = "mesh_field";
    varCtx.type_utf8 = "PetscVec";
    varCtx.value_utf8 = "0x5000";
    QCOMPARE(mgr.hostBridge()->invokeVariableAction(QStringLiteral("dummy.inspect_field"), varCtx), GRIDLOCK_OK);
    QCOMPARE(events.last().second, QStringLiteral("extent_256x128x64_double_grid"));
    QCOMPARE(api->eval_expr(api->host_ctx, 0, "broken_sym", smallBuf, sizeof(smallBuf), &outLen), GRIDLOCK_ERR_EVAL_FAILED);

    // HIGH-02 / HIGH-03 / HIGH-04 bounds and MI injection checks.
    uint8_t memBytes[4]{};
    size_t bytesRead = 0u, written = 0u;
    QCOMPARE(api->read_memory(api->host_ctx, 0, 0x5000ULL, memBytes, 64ULL * 1024ULL * 1024ULL + 1ULL, &bytesRead),
             GRIDLOCK_ERR_INVALID_ARG);
    QCOMPARE(api->read_memory(api->host_ctx, 0, 0x5000ULL, memBytes, sizeof(memBytes), &bytesRead), GRIDLOCK_OK);
    QCOMPARE(bytesRead, size_t{4u});
    QCOMPARE(memBytes[0], uint8_t{0xde});
    QCOMPARE(api->read_memory(api->host_ctx, 0, 0xbad0ULL, memBytes, sizeof(memBytes), &bytesRead), GRIDLOCK_ERR_MEMORY_READ_FAILED);

    QCOMPARE(api->write_memory(api->host_ctx, 0, 0x5000ULL, memBytes, sizeof(memBytes), &written), GRIDLOCK_OK);
    QCOMPARE(written, size_t{4u});
    QCOMPARE(api->write_memory(api->host_ctx, 0, 0xbad0ULL, memBytes, sizeof(memBytes), &written), GRIDLOCK_ERR_MEMORY_READ_FAILED);

    int32_t bpId = -1;
    QCOMPARE(api->set_breakpoint(api->host_ctx, "/tmp/sim.c\n-interpreter-exec console \"quit\"", 10, nullptr, &bpId),
             GRIDLOCK_ERR_INVALID_ARG);
    QCOMPARE(api->set_breakpoint(api->host_ctx, "/tmp/sim.c", 10, "i == 1\nquit", &bpId), GRIDLOCK_ERR_INVALID_ARG);
    QCOMPARE(api->set_breakpoint(api->host_ctx, "/tmp/sim.c", 80, "i == 3", &bpId), GRIDLOCK_OK);
    QCOMPARE(api->remove_breakpoint(api->host_ctx, bpId), GRIDLOCK_OK);

    QObject::disconnect(miResponder);
    mgr.unloadAll();
    QCOMPARE(mgr.hostBridge()->liveAllocationCount(), 0);
  }

  void testWorkspaceTrustGateAndSingleByteTamperInvalidation() {
    ScopedPluginEnv env;
    const QString markerPath = QDir(env.tempRoot()).filePath(QStringLiteral("elf_ctor_marker.txt"));
    QFile::remove(markerPath);
    qputenv("GRIDLOCK_TEST_CTOR_MARKER_FILE", markerPath.toUtf8());

    QVERIFY(copyPluginArtifact(QStringLiteral(GRIDLOCK_TEST_DUMMY_PLUGIN_SO),
                               env.workspacePluginDir(), QStringLiteral("ws_untrusted.so")));
    const QFileInfo wsFi(QDir(env.workspacePluginDir()).filePath(QStringLiteral("ws_untrusted.so")));
    const QString canonWsPath = !wsFi.canonicalFilePath().isEmpty() ? wsFi.canonicalFilePath() : wsFi.absoluteFilePath();

    gridlock::GdbRankCoordinator coord;
    gridlock::core::plugins::PluginManager mgr(&coord);

    // 1. Untrusted workspace plugin MUST NOT call dlopen() or execute ELF .init_array constructors.
    QCOMPARE(mgr.discoverAndLoad(), 0);
    QCOMPARE(mgr.plugins().size(), 1);
    QVERIFY(!mgr.plugins().first().trusted && !mgr.plugins().first().enabled);
    QVERIFY(!QFileInfo::exists(markerPath));

    // 2. Approve exact SHA-256 in user-global ConfigManager and verify reload succeeds and constructor runs.
    const QString origSha = mgr.plugins().first().sha256Hex;
    QVERIFY(!origSha.isEmpty());
    gridlock::core::ConfigManager::instance().trustWorkspacePlugin(canonWsPath, origSha);
    QCOMPARE(mgr.discoverAndLoad(), 1);
    QVERIFY(mgr.plugins().first().trusted && mgr.plugins().first().enabled);
    QVERIFY(QFileInfo::exists(markerPath));

    // 3. Unload, remove canary, append 1 byte to tamper binary, and verify trust is invalidated before dlopen().
    mgr.unloadAll();
    QFile::remove(markerPath);
    {
      QFile tamper(canonWsPath);
      QVERIFY(tamper.open(QIODevice::Append));
      QCOMPARE(tamper.write("\x00", 1), qint64{1});
    }
    QVERIFY(sha256OfFile(canonWsPath) != origSha);
    QCOMPARE(mgr.discoverAndLoad(), 0);
    QCOMPARE(mgr.plugins().size(), 1);
    QVERIFY(!mgr.plugins().first().trusted && !mgr.plugins().first().enabled);
    QVERIFY(!QFileInfo::exists(markerPath));

    qunsetenv("GRIDLOCK_TEST_CTOR_MARKER_FILE");
  }

  void testCallbackReentrancyDeferralAndShutdownRegistrationLockout() {
    ScopedPluginEnv env;
    QVERIFY(copyPluginArtifact(QStringLiteral(GRIDLOCK_TEST_DUMMY_PLUGIN_SO),
                               env.userPluginDir(), QStringLiteral("dummy.so")));

    gridlock::GdbRankCoordinator coord;
    gridlock::core::plugins::PluginManager mgr(&coord);
    QCOMPARE(mgr.discoverAndLoad(), 1);

    QStringList stopPayloads;
    QString shutdownRegPayload;
    bool wasStillLoadedInsideCallback = false;

    QObject::connect(mgr.hostBridge(), &gridlock::core::plugins::PluginHostBridge::structuredEventEmitted,
                     &coord, [&](const QString& ch, const QString& payload) {
                       if (ch == QStringLiteral("dummy.stop")) {
                         stopPayloads.append(payload);
                         if (stopPayloads.size() == 1) {
                           // Re-entrantly enqueue a second stop; FIFO must finish stop #1 first, then run #2 unless unloaded.
                           mgr.notifyRankStopped(0, QStringLiteral("/tmp/sim.c"), 22, QStringLiteral("solve"), 0x4010u);
                         } else if (stopPayloads.size() == 2) {
                           // Trigger unloadAll() while m_callbackDepth > 0: must defer dlclose until unwind!
                           mgr.unloadAll();
                           wasStillLoadedInsideCallback = !mgr.plugins().isEmpty();
                         }
                       } else if (ch == QStringLiteral("dummy.shutdown_reg")) {
                         shutdownRegPayload = payload;
                       }
                     });

    mgr.notifyRankStopped(0, QStringLiteral("/tmp/sim.c"), 11, QStringLiteral("solve"), 0x4000u);

    QCOMPARE(stopPayloads.size(), 2);
    QVERIFY(stopPayloads.at(0).contains(QStringLiteral("\"line\":11")));
    QVERIFY(stopPayloads.at(1).contains(QStringLiteral("\"line\":22")));
    QVERIFY(wasStillLoadedInsideCallback);
    // After unwind to depth 0, deferred unloadAll() must have completed and rejected shutdown registration (-8).
    QVERIFY(mgr.plugins().isEmpty());
    QVERIFY(shutdownRegPayload.contains(QString::number(static_cast<int>(GRIDLOCK_ERR_UNSUPPORTED))));
    QVERIFY(mgr.hostBridge()->variableActions().isEmpty());
    QCOMPARE(mgr.hostBridge()->liveAllocationCount(), 0);
  }

  void testDualScopeEnablementAndLocalBareGitPluginInstaller() {
    ScopedPluginEnv env;
    QVERIFY(copyPluginArtifact(QStringLiteral(GRIDLOCK_TEST_DUMMY_PLUGIN_SO),
                               env.userPluginDir(), QStringLiteral("dummy.so")));

    gridlock::GdbRankCoordinator coord;
    gridlock::core::plugins::PluginManager mgr(&coord);
    QCOMPARE(mgr.discoverAndLoad(), 1);
    const QString plugId = QStringLiteral("org.gridlock.test_dummy");

    // 1. Effective enablement requires BOTH Global and Workspace flags.
    QVERIFY(mgr.effectivePluginEnabled(plugId));
    QVERIFY(mgr.setPluginWorkspaceEnabled(plugId, false));
    QVERIFY(!mgr.effectivePluginEnabled(plugId) && mgr.isPluginGloballyEnabled(plugId));

    QTemporaryDir secondWs;
    QVERIFY(secondWs.isValid());
    gridlock::core::ConfigManager::instance().setWorkspace(secondWs.path());
    QVERIFY(mgr.effectivePluginEnabled(plugId));

    QVERIFY(mgr.setPluginGloballyEnabled(plugId, false));
    QVERIFY(!mgr.effectivePluginEnabled(plugId));
    gridlock::core::ConfigManager::instance().setWorkspace(env.workspaceRoot());
    QVERIFY(mgr.setPluginGloballyEnabled(plugId, true));
    QVERIFY(mgr.setPluginWorkspaceEnabled(plugId, true));
    mgr.unloadAll();

    // 2. Validate URL security gates against leading '-' flag injection and insecure HTTP schemes.
    using gridlock::core::plugins::PluginInstaller;
    using gridlock::core::plugins::PluginScope;
    QVERIFY(!PluginInstaller::validateGitHubUrl(QStringLiteral("-oProxyCommand=id")));
    QVERIFY(!PluginInstaller::validateGitHubUrl(QStringLiteral("http://insecure.example.com/a/b.git")));
    QVERIFY(!PluginInstaller::validateGitHubUrl(QStringLiteral("https://github.com/owner/repo.git --upload-pack=x")));
    QVERIFY(!PluginInstaller::validateGitRef(QStringLiteral("--upload-pack=id")));
    QVERIFY(PluginInstaller::validateGitHubUrl(QStringLiteral("https://github.com/gridlock-test/local-plugin.git")));

    // 3. Create a local bare git repository with a valid warning-emitting C99 branch and a broken branch.
    QTemporaryDir gitSandbox;
    QVERIFY(gitSandbox.isValid());
    QString bareRepo, workRepo, dstSrc;
    QVERIFY(initLocalBarePluginRepo(gitSandbox.path(), &bareRepo, &workRepo, &dstSrc));

    // Create 'broken' branch by truncating physical C source mid-function to trigger a real compiler syntax error.
    QVERIFY(runGit(workRepo, {QStringLiteral("checkout"), QStringLiteral("-b"), QStringLiteral("broken")}));
    {
      QFile truncFile(dstSrc);
      QVERIFY(truncFile.open(QIODevice::ReadWrite | QIODevice::Append));
      QVERIFY(truncFile.write("\nint broken_unclosed_syntax_error( {\n") > 0);
    }
    QVERIFY(runGit(workRepo, {QStringLiteral("commit"), QStringLiteral("-am"), QStringLiteral("truncate source")}));
    QVERIFY(runGit(workRepo, {QStringLiteral("push"), QStringLiteral("origin"), QStringLiteral("broken")}));

    ScopedGitRouteEnv gitRoute(bareRepo, QStringLiteral("https://github.com/gridlock-test/local-plugin.git"));
    PluginInstaller installer;
    QSignalSpy doneSpy(&installer, &PluginInstaller::installFinished);

    // Build 'main' into Workspace scope: verify .so installed, compiler warning captured, and SHA-256 auto-trusted.
    QVERIFY(installer.startInstallFromGitHub(
        QStringLiteral("https://github.com/gridlock-test/local-plugin.git"),
        QStringLiteral("main"), PluginScope::Workspace, env.workspacePluginDir()));
    QTRY_COMPARE_WITH_TIMEOUT(doneSpy.count(), 1, 15000);
    const auto okRes = doneSpy.takeFirst().at(0).value<gridlock::core::plugins::PluginBuildResult>();
    QVERIFY2(okRes.success, qPrintable(okRes.fullCompilerLog));
    QVERIFY(QFileInfo::exists(okRes.installedPath));
    QVERIFY(!okRes.warnings.isEmpty());
    QVERIFY(gridlock::core::ConfigManager::instance().isWorkspacePluginTrusted(
        okRes.installedPath, sha256OfFile(okRes.installedPath)));

    // Build 'broken' branch: verify clean failure and captured compiler error diagnostics.
    QVERIFY(installer.startInstallFromGitHub(
        QStringLiteral("https://github.com/gridlock-test/local-plugin.git"),
        QStringLiteral("broken"), PluginScope::UserGlobal, env.userPluginDir()));
    QTRY_COMPARE_WITH_TIMEOUT(doneSpy.count(), 1, 15000);
    const auto errRes = doneSpy.takeFirst().at(0).value<gridlock::core::plugins::PluginBuildResult>();
    QVERIFY(!errRes.success);
    QVERIFY(!errRes.errors.isEmpty());
  }

  void testUiCallbacksMenusSurfacesAnnotationsReportsAndUnloadPurge() {
    ScopedPluginEnv env;
    QVERIFY(copyPluginArtifact(QStringLiteral(GRIDLOCK_TEST_DUMMY_PLUGIN_SO),
                               env.userPluginDir(), QStringLiteral("01_dummy.so")));

    gridlock::GdbRankCoordinator coord;
    gridlock::core::plugins::PluginManager mgr(&coord);
    auto* bridge = mgr.hostBridge();

    QVector<QPair<QString, QString>> events;
    QObject::connect(bridge, &gridlock::core::plugins::PluginHostBridge::structuredEventEmitted,
                     &coord, [&](const QString& ch, const QString& payload) { events.append(qMakePair(ch, payload)); });

    int msgBoxCalls = 0, promptCalls = 0, toastCalls = 0, navLine = 0;
    QString navFile;
    QObject::connect(bridge, &gridlock::core::plugins::PluginHostBridge::messageBoxRequested,
                     &coord, [&](const QString&, int, int, const QString&, const QString&,
                                 int32_t* outRes, bool* outHandled) {
                       ++msgBoxCalls;
                       *outRes = GRIDLOCK_RESULT_YES;
                       *outHandled = true;
                     });
    QObject::connect(bridge, &gridlock::core::plugins::PluginHostBridge::promptInputRequested,
                     &coord, [&](const QString&, const QString&, const QString&, const QString&,
                                 QString* outVal, bool* outAccepted, bool* outHandled) {
                       ++promptCalls;
                       *outVal = QStringLiteral("long_custom_mesh_tag_exceeding_stack_probe");
                       *outAccepted = true;
                       *outHandled = true;
                     });
    QObject::connect(bridge, &gridlock::core::plugins::PluginHostBridge::toastRequested,
                     &coord, [&](const QString&, int, const QString&, const QString&, int) { ++toastCalls; });
    QObject::connect(bridge, &gridlock::core::plugins::PluginHostBridge::navigateRequested,
                     &coord, [&](const QString&, const QString& file, int line, int) {
                       navFile = file;
                       navLine = line;
                     });

    QCOMPARE(mgr.discoverAndLoad(), 1);
    QCOMPARE(bridge->menuActions().size(), 1);
    QCOMPARE(bridge->menuActions().first().pathSegments,
             (QStringList{QStringLiteral("Mesh Tools"), QStringLiteral("Refinement"), QStringLiteral("Export VTK")}));
    QCOMPARE(bridge->statusItems().size(), 1);
    QCOMPARE(bridge->statusItems().first().text, QStringLiteral("Mesh: Ready"));
    QCOMPARE(bridge->contextActions().size(), 5);
    QCOMPARE(bridge->matchingContextActions(GRIDLOCK_SURFACE_VARIABLES, QStringLiteral("PetscVec")).size(), 1);
    QCOMPARE(bridge->matchingContextActions(GRIDLOCK_SURFACE_VARIABLES, QStringLiteral("int")).size(), 0);
    QCOMPARE(bridge->matchingContextActions(GRIDLOCK_SURFACE_CODE_EDITOR, QStringLiteral("main.c")).size(), 1);
    QCOMPARE(bridge->matchingContextActions(GRIDLOCK_SURFACE_CALL_STACK, QStringLiteral("solve")).size(), 1);
    QCOMPARE(bridge->matchingContextActions(GRIDLOCK_SURFACE_BREAKPOINTS, QStringLiteral("main.c:12")).size(), 1);
    QCOMPARE(bridge->matchingContextActions(GRIDLOCK_SURFACE_RANK_LIST, QStringLiteral("stopped")).size(), 1);

    // 1. Click status-bar item: exercises message box + two-pass cached promptInput without opening prompt twice.
    QCOMPARE(bridge->invokeStatusItem(QStringLiteral("dummy.mesh_status")), GRIDLOCK_OK);
    QCOMPARE(msgBoxCalls, 1);
    QCOMPARE(promptCalls, 1);
    QCOMPARE(bridge->statusItems().first().text, QStringLiteral("Mesh: Active"));
    QVERIFY(!events.isEmpty() && events.last().first == QStringLiteral("dummy.status_click"));
    QVERIFY(events.last().second.contains(QStringLiteral("long_custom_mesh_tag_exceeding_stack_probe")));

    // 2. Invoke nested menu action: publishes Markdown, Key-Value, and JSON Tree report panels + toast.
    GridLockCommandSpec menuSpec{static_cast<uint32_t>(sizeof(GridLockCommandSpec)),
                                 GRIDLOCK_PLUGIN_ABI_MAJOR, GRIDLOCK_PLUGIN_ABI_MINOR, 0u,
                                 0, 2, "dummy.export_vtk", "Mesh Tools/Refinement/Export VTK", "Ctrl+Alt+M", nullptr};
    QCOMPARE(bridge->invokeMenuAction(QStringLiteral("dummy.export_vtk"), menuSpec), GRIDLOCK_OK);
    QCOMPARE(bridge->reportPanels().size(), 3);
    QCOMPARE(toastCalls, 1);

    // 3. Invoke context actions on CodeEditor (sets annotation) and CallStack (triggers navigate_to_location).
    GridLockSurfaceContext edCtx{static_cast<uint32_t>(sizeof(GridLockSurfaceContext)),
                                 GRIDLOCK_PLUGIN_ABI_MAJOR, GRIDLOCK_PLUGIN_ABI_MINOR, 0u,
                                 GRIDLOCK_SURFACE_CODE_EDITOR, 0, 0, 42, 8, -1, 0x401000ULL,
                                 "halo_buf", nullptr, "/tmp/sim.c", "halo_buf"};
    QCOMPARE(bridge->invokeContextAction(QStringLiteral("dummy.ctx.editor"), edCtx), GRIDLOCK_OK);
    QCOMPARE(bridge->editorAnnotations(QStringLiteral("/tmp/sim.c")).size(), 1);
    QCOMPARE(bridge->editorAnnotations(QStringLiteral("/tmp/sim.c")).first().badgeText, QStringLiteral("HOTSPOT"));

    GridLockSurfaceContext csCtx{static_cast<uint32_t>(sizeof(GridLockSurfaceContext)),
                                 GRIDLOCK_PLUGIN_ABI_MAJOR, GRIDLOCK_PLUGIN_ABI_MINOR, 0u,
                                 GRIDLOCK_SURFACE_CALL_STACK, 0, 1, 88, -1, -1, 0x402000ULL,
                                 "caller_fn", nullptr, "/tmp/sim.c", nullptr};
    QCOMPARE(bridge->invokeContextAction(QStringLiteral("dummy.ctx.stack"), csCtx), GRIDLOCK_OK);
    QCOMPARE(navFile, QStringLiteral("/tmp/sim.c"));
    QCOMPARE(navLine, 88);

    // 4. Validate bounds: menu depth > 4, control characters, and theme change broadcast.
    const GridLockHostApi* rawApi = bridge->hostApi();
    QCOMPARE(rawApi->register_menu_action(
                 rawApi->host_ctx, "bad.depth", "A/B/C/D/E", nullptr,
                 [](void*, const GridLockHostApi*, const GridLockCommandSpec*) noexcept -> GridLockStatus {
                   return GRIDLOCK_OK;
                 }, nullptr),
             GRIDLOCK_ERR_INVALID_ARG);
    QCOMPARE(rawApi->set_status_item(rawApi->host_ctx, "bad\nid", "Txt", nullptr, nullptr, nullptr, nullptr),
             GRIDLOCK_ERR_INVALID_ARG);

    GridLockThemeInfo lightTheme{};
    lightTheme.struct_size = static_cast<uint32_t>(sizeof(GridLockThemeInfo));
    lightTheme.abi_major = GRIDLOCK_PLUGIN_ABI_MAJOR;
    lightTheme.abi_minor = GRIDLOCK_PLUGIN_ABI_MINOR;
    lightTheme.is_dark = 0;
    std::snprintf(lightTheme.theme_id_utf8, sizeof(lightTheme.theme_id_utf8), "%s", "daylight_light");
    mgr.notifyThemeChanged(lightTheme);
    QVERIFY(!events.isEmpty() && events.last().first == QStringLiteral("dummy.theme_changed"));
    QVERIFY(events.last().second.contains(QStringLiteral("daylight_light")));

    // 5. Unload must purge all menus, status items, context actions, annotations, and report panels.
    mgr.unloadAll();
    QVERIFY(bridge->menuActions().isEmpty());
    QVERIFY(bridge->statusItems().isEmpty());
    QVERIFY(bridge->contextActions().isEmpty());
    QVERIFY(bridge->editorAnnotations().isEmpty());
    QVERIFY(bridge->reportPanels().isEmpty());
    QCOMPARE(bridge->liveAllocationCount(), 0);
  }

  void testOwnerRepoShorthandAndDeclarativeManifestSync() {
    ScopedPluginEnv env;
    using gridlock::core::plugins::GitForgeHost;
    using gridlock::core::plugins::PluginBuildResult;
    using gridlock::core::plugins::PluginInstaller;
    using gridlock::core::plugins::PluginLockEntry;
    using gridlock::core::plugins::PluginManifest;
    using gridlock::core::plugins::PluginManifestEntry;
    using gridlock::core::plugins::PluginScope;

    // 1. Validate multi-host forge shorthands (GitHub, GitLab subgroups, Codeberg), Gitea/custom URLs, and rejections.
    QString normUrl, slug;
    QVERIFY(PluginInstaller::normalizeAndValidateUrl(
        QStringLiteral("bnorthern42/gridlock-stl-inspector"), GitForgeHost::GitHub, &normUrl, &slug));
    QCOMPARE(normUrl, QStringLiteral("https://github.com/bnorthern42/gridlock-stl-inspector.git"));
    QCOMPARE(slug, QStringLiteral("gridlock-stl-inspector"));

    QVERIFY(PluginInstaller::normalizeAndValidateUrl(
        QStringLiteral("github:bnorthern42/gridlock-stl-inspector"), GitForgeHost::CustomUrl, &normUrl, &slug));
    QCOMPARE(normUrl, QStringLiteral("https://github.com/bnorthern42/gridlock-stl-inspector.git"));

    QVERIFY(PluginInstaller::normalizeAndValidateUrl(
        QStringLiteral("gitlab:hpc-tools/mpi/mesh-inspector"), GitForgeHost::GitHub, &normUrl, &slug));
    QCOMPARE(normUrl, QStringLiteral("https://gitlab.com/hpc-tools/mpi/mesh-inspector.git"));
    QCOMPARE(slug, QStringLiteral("mesh-inspector"));

    QVERIFY(PluginInstaller::normalizeAndValidateUrl(
        QStringLiteral("hpc-tools/sub/probe"), GitForgeHost::GitLab, &normUrl, &slug));
    QCOMPARE(normUrl, QStringLiteral("https://gitlab.com/hpc-tools/sub/probe.git"));

    QVERIFY(PluginInstaller::normalizeAndValidateUrl(
        QStringLiteral("codeberg:scicomp/gridlock-probe"), GitForgeHost::GitHub, &normUrl, &slug));
    QCOMPARE(normUrl, QStringLiteral("https://codeberg.org/scicomp/gridlock-probe.git"));
    QVERIFY(PluginInstaller::normalizeAndValidateUrl(
        QStringLiteral("scicomp/gridlock-probe"), GitForgeHost::Codeberg, &normUrl, &slug));
    QCOMPARE(normUrl, QStringLiteral("https://codeberg.org/scicomp/gridlock-probe.git"));

    QVERIFY(PluginInstaller::normalizeAndValidateUrl(
        QStringLiteral("https://gitea.example.org:3000/lab/gridlock-plugin.git"), GitForgeHost::GitHub, &normUrl, &slug));
    QCOMPARE(normUrl, QStringLiteral("https://gitea.example.org:3000/lab/gridlock-plugin.git"));
    QCOMPARE(slug, QStringLiteral("gridlock-plugin"));

    QVERIFY(PluginInstaller::normalizeAndValidateUrl(
        QStringLiteral("git@gitea.example.org:lab/gridlock-plugin.git"), GitForgeHost::Codeberg, &normUrl, &slug));
    QCOMPARE(normUrl, QStringLiteral("git@gitea.example.org:lab/gridlock-plugin.git"));

    QVERIFY(!PluginInstaller::normalizeAndValidateUrl(QStringLiteral("owner/repo"), GitForgeHost::CustomUrl));
    QVERIFY(!PluginInstaller::normalizeAndValidateUrl(QStringLiteral("--upload-pack=id/repo")));
    QVERIFY(!PluginInstaller::normalizeAndValidateUrl(QStringLiteral("-upload-pack=evil")));
    QVERIFY(!PluginInstaller::normalizeAndValidateUrl(QStringLiteral("-evil/repo")));
    QVERIFY(!PluginInstaller::normalizeAndValidateUrl(QStringLiteral("owner/-evil")));
    QVERIFY(!PluginInstaller::normalizeAndValidateUrl(QStringLiteral("owner/..")));
    QVERIFY(!PluginInstaller::normalizeAndValidateUrl(QStringLiteral("../repo")));
    QVERIFY(!PluginInstaller::normalizeAndValidateUrl(QStringLiteral("gitlab:group/../repo")));
    QVERIFY(!PluginInstaller::normalizeAndValidateUrl(QStringLiteral("https://gitea.example.org/lab/../evil.git")));
    QVERIFY(!PluginInstaller::normalizeAndValidateUrl(QStringLiteral("owner/.git")));
    QVERIFY(!PluginInstaller::normalizeAndValidateUrl(QStringLiteral("owner/repo/extra"), GitForgeHost::GitHub));

    // 2. Parse declarative [[plugins]] manifest entries across GitHub, GitLab, Codeberg, and Gitea URLs.
    QVector<PluginManifestEntry> parsed;
    QString parseErr;
    const QString sampleToml = QStringLiteral(
        "[[plugins]]\n"
        "repo = \"codeberg:bnorthern42/gridlock-stl-inspector\"\n"
        "ref = \"main\"\n"
        "build = \"meson\"\n"
        "\n"
        "[[plugins]]\n"
        "repo = \"gitlab:hpc/sub/disabled-probe\"\n"
        "build = \"single\"\n"
        "enabled = false\n"
        "\n"
        "[[plugins]]\n"
        "repo = \"https://gitea.example.org/user/custom-probe.git\"\n");
    QVERIFY2(PluginManifest::parseManifestToml(sampleToml, &parsed, &parseErr), qPrintable(parseErr));
    QCOMPARE(parsed.size(), 3);
    QCOMPARE(parsed.at(0).normalizedUrl, QStringLiteral("https://codeberg.org/bnorthern42/gridlock-stl-inspector.git"));
    QCOMPARE(parsed.at(0).ref, QStringLiteral("main"));
    QCOMPARE(parsed.at(0).build, QStringLiteral("meson"));
    QVERIFY(parsed.at(0).enabled);
    QCOMPARE(parsed.at(1).normalizedUrl, QStringLiteral("https://gitlab.com/hpc/sub/disabled-probe.git"));
    QCOMPARE(parsed.at(1).build, QStringLiteral("single"));
    QVERIFY(!parsed.at(1).enabled);
    QCOMPARE(parsed.at(2).normalizedUrl, QStringLiteral("https://gitea.example.org/user/custom-probe.git"));
    QVERIFY(parsed.at(2).enabled);

    QVERIFY(!PluginManifest::parseManifestToml(
        QStringLiteral("[[plugins]]\nrepo = \"owner/repo\"\nbuild = \"bazel\"\n"), &parsed, &parseErr));

    // 3. Set up local bare git repository and route GitHub shorthand URLs to file:// via Git config env.
    QTemporaryDir gitSandbox;
    QVERIFY(gitSandbox.isValid());
    QString bareRepo, workRepo, dstSrc;
    QVERIFY(initLocalBarePluginRepo(gitSandbox.path(), &bareRepo, &workRepo, &dstSrc));
    const QString firstCommitSha = PluginManifest::queryLocalHeadSha(workRepo);
    QCOMPARE(firstCommitSha.size(), 40);
    ScopedGitRouteEnv gitRoute(bareRepo, QStringLiteral("https://github.com/gridlock-test/local-plugin.git"));

    // 4. Shorthand install appends to user plugins.toml and writes SHA-256 + commit_sha to user-global plugins-lock.toml.
    PluginInstaller installer;
    QSignalSpy doneSpy(&installer, &PluginInstaller::installFinished);
    QVERIFY(installer.startInstallFromGitHub(
        QStringLiteral("gridlock-test/local-plugin"), QStringLiteral("main"), PluginScope::UserGlobal));
    QTRY_COMPARE_WITH_TIMEOUT(doneSpy.count(), 1, 15000);
    const auto installRes = doneSpy.takeFirst().at(0).value<PluginBuildResult>();
    QVERIFY2(installRes.success, qPrintable(installRes.fullCompilerLog));
    QCOMPARE(installRes.commitSha, firstCommitSha);

    QVector<PluginManifestEntry> userManifestEntries;
    QVERIFY(PluginManifest::loadManifestFile(PluginManifest::userManifestPath(), &userManifestEntries));
    QCOMPARE(userManifestEntries.size(), 1);
    QCOMPARE(userManifestEntries.first().repo, QStringLiteral("gridlock-test/local-plugin"));

    QVector<PluginLockEntry> lockEntries;
    QVERIFY(PluginManifest::loadLockFile(PluginManifest::userLockFilePath(), &lockEntries));
    QCOMPARE(lockEntries.size(), 1);
    QCOMPARE(lockEntries.first().repo, QStringLiteral("https://github.com/gridlock-test/local-plugin.git"));
    QCOMPARE(lockEntries.first().commitSha, firstCommitSha);
    QCOMPARE(lockEntries.first().binarySha256, sha256OfFile(installRes.installedPath));
    QVERIFY(!QFileInfo::exists(QDir(env.workspaceRoot()).filePath(QStringLiteral(".gridlock/plugins-lock.toml"))));

    // 5. Write workspace plugins.toml: UserGlobal sync must be up-to-date (0 built) and NOT auto-build/trust workspace.
    const QString wsManifest = PluginManifest::workspaceManifestPath();
    QVERIFY(PluginManifest::upsertManifestEntry(
        wsManifest, PluginManifestEntry{QStringLiteral("gridlock-test/local-plugin"), QString(), QString(),
                                        firstCommitSha, QStringLiteral("single"), true}));
    QVERIFY(installer.syncFromManifests(PluginManifest::userManifestPath(), wsManifest, PluginScope::UserGlobal));
    QTRY_COMPARE_WITH_TIMEOUT(doneSpy.count(), 1, 15000);
    const auto noopRes = doneSpy.takeFirst().at(0).value<PluginBuildResult>();
    QVERIFY(noopRes.success);
    QCOMPARE(noopRes.installedCount, 0);
    QVERIFY(QDir(env.workspacePluginDir()).entryList(QDir::Files).isEmpty());

    // 6. Push a new upstream commit on main, then explicitly sync Workspace scope: both outdated global & missing workspace build!
    {
      QFile appendComment(dstSrc);
      QVERIFY(appendComment.open(QIODevice::Append));
      QVERIFY(appendComment.write("\n/* upstream v2 update */\n") > 0);
    }
    QVERIFY(runGit(workRepo, {QStringLiteral("commit"), QStringLiteral("-am"), QStringLiteral("v2 commit")}));
    QVERIFY(runGit(workRepo, {QStringLiteral("push"), QStringLiteral("origin"), QStringLiteral("main")}));
    const QString secondCommitSha = PluginManifest::queryLocalHeadSha(workRepo);
    QVERIFY(secondCommitSha != firstCommitSha);

    QVERIFY(installer.syncFromManifests(PluginManifest::userManifestPath(), wsManifest, PluginScope::Workspace));
    QTRY_COMPARE_WITH_TIMEOUT(doneSpy.count(), 1, 20000);
    const auto syncRes = doneSpy.takeFirst().at(0).value<PluginBuildResult>();
    QVERIFY2(syncRes.success, qPrintable(syncRes.fullCompilerLog));
    QCOMPARE(syncRes.installedCount, 2);
    QVERIFY(!syncRes.warnings.isEmpty());
    QVERIFY(gridlock::core::ConfigManager::instance().isWorkspacePluginTrusted(
        syncRes.installedPath, sha256OfFile(syncRes.installedPath)));

    QVERIFY(PluginManifest::loadLockFile(PluginManifest::userLockFilePath(), &lockEntries));
    QCOMPARE(lockEntries.size(), 2);
  }

  void testLockfileMetadataDiagnosticsAndInPlaceUpdate() {
    ScopedPluginEnv env;
    using gridlock::core::plugins::PluginBuildResult;
    using gridlock::core::plugins::PluginInstaller;
    using gridlock::core::plugins::PluginLockEntry;
    using gridlock::core::plugins::PluginManager;
    using gridlock::core::plugins::PluginManifest;
    using gridlock::core::plugins::PluginScope;
    using gridlock::core::plugins::PluginUpdateTarget;

    QTemporaryDir gitSandbox;
    QVERIFY(gitSandbox.isValid());
    QString bareRepo, workRepo, dstSrc;
    QVERIFY(initLocalBarePluginRepo(gitSandbox.path(), &bareRepo, &workRepo, &dstSrc));
    const QString sha1 = PluginManifest::queryLocalHeadSha(workRepo);
    QCOMPARE(sha1.size(), 40);
    ScopedGitRouteEnv gitRoute(bareRepo, QStringLiteral("https://github.com/gridlock-test/local-plugin.git"));

    PluginInstaller installer;
    QSignalSpy doneSpy(&installer, &PluginInstaller::installFinished);
    QVERIFY(installer.startInstallFromGitHub(
        QStringLiteral("gridlock-test/local-plugin"), QStringLiteral("main"), PluginScope::UserGlobal));
    QTRY_COMPARE_WITH_TIMEOUT(doneSpy.count(), 1, 15000);
    const auto res1 = doneSpy.takeFirst().at(0).value<PluginBuildResult>();
    QVERIFY2(res1.success && res1.anyBinaryChanged, qPrintable(res1.fullCompilerLog));
    const QString binSha1 = sha256OfFile(res1.installedPath);

    gridlock::GdbRankCoordinator coord;
    PluginManager mgr(&coord);
    QCOMPARE(mgr.discoverAndLoad(), 1);
    QCOMPARE(mgr.plugins().size(), 1);
    const auto& info1 = mgr.plugins().first();
    QCOMPARE(info1.version, QStringLiteral("1.0.0"));
    QCOMPARE(info1.gitCommitSha, sha1);
    QCOMPARE(info1.gitCommitShort, sha1.left(7));
    QCOMPARE(info1.gitRepo, QStringLiteral("https://github.com/gridlock-test/local-plugin.git"));
    QCOMPARE(info1.gitRef, QStringLiteral("main"));
    QCOMPARE(mgr.loadDiagnostics(),
             QStringList{QStringLiteral("Loaded GridLock Test Dummy Plugin 1.0.0 (%1) [ABI 1.0]").arg(sha1.left(7))});

    // Push upstream commit updating plugin_version_utf8 -> 1.1.0, then run startUpdatePlugins + refreshInstalledLockMetadata.
    {
      QFile srcFile(dstSrc);
      QVERIFY(srcFile.open(QIODevice::ReadOnly));
      QByteArray content = srcFile.readAll();
      srcFile.close();
      content.replace("\"1.0.0\"", "\"1.1.0\"");
      QVERIFY(srcFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
      QVERIFY(srcFile.write(content) == content.size());
    }
    QVERIFY(runGit(workRepo, {QStringLiteral("commit"), QStringLiteral("-am"), QStringLiteral("bump version 1.1.0")}));
    QVERIFY(runGit(workRepo, {QStringLiteral("push"), QStringLiteral("origin"), QStringLiteral("main")}));
    const QString sha2 = PluginManifest::queryLocalHeadSha(workRepo);
    QVERIFY(sha2 != sha1);

    QVERIFY(installer.startUpdatePlugins({PluginUpdateTarget{
        info1.id, info1.gitRepo, info1.gitRef, info1.buildSystem, info1.scope,
        env.userPluginDir(), QFileInfo(info1.libraryPath).fileName(), true}}));
    QTRY_COMPARE_WITH_TIMEOUT(doneSpy.count(), 1, 15000);
    const auto res2 = doneSpy.takeFirst().at(0).value<PluginBuildResult>();
    QVERIFY2(res2.success && res2.anyBinaryChanged, qPrintable(res2.fullCompilerLog));
    QVERIFY(sha256OfFile(res2.installedPath) != binSha1);
    QVERIFY(mgr.refreshInstalledLockMetadata());
    QCOMPARE(mgr.plugins().first().gitCommitShort, sha2.left(7));
    QVERIFY(mgr.plugins().first().requiresRestart);

    QCOMPARE(mgr.discoverAndLoad(), 1);
    QCOMPARE(mgr.plugins().first().version, QStringLiteral("1.1.0"));
    QCOMPARE(mgr.loadDiagnostics(),
             QStringList{QStringLiteral("Loaded GridLock Test Dummy Plugin 1.1.0 (%1) [ABI 1.0]").arg(sha2.left(7))});
  }
};

QTEST_MAIN(TestPluginManager)
#include "test_plugin_manager.moc"
