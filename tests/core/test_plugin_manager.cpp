/**
 * @file test_plugin_manager.cpp
 * @brief Integration tests for `PluginManager`, `PluginHostBridge`, and compiled C99 `.so` plugins.
 *
 * Verifies shared-library discovery across `~/.config/gridlock/plugins` and
 * `<workspace>/.gridlock/plugins`, strict ABI v1.0 major/struct_size/duplicate-ID
 * enforcement, synchronous `eval_expr` and `read_memory` bridging with bounded
 * `QEventLoop` + `QTimer` timeouts, and `[plugins] disabled_ids` persistence.
 */

#include <cstdint>
#include <cstring>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QPair>
#include <QSettings>
#include <QString>
#include <QTemporaryDir>
#include <QTimer>
#include <QVector>
#include <QtTest>

#include "../../src/core/hpc/gdb/GdbRankCoordinator.hpp"
#include "../../src/core/managers/ConfigManager.hpp"
#include "../../src/core/plugins/PluginHostBridge.hpp"
#include "../../src/core/plugins/PluginManager.hpp"
#include "gridlock/plugin_api.h"

namespace {

/** @brief RAII guard isolating `GRIDLOCK_TEST_CONFIG_DIR` and workspace path per test. */
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
    QDir().mkpath(m_cfgDir.path() + QStringLiteral("/plugins"));
    QDir().mkpath(m_wsDir.path() + QStringLiteral("/.gridlock/plugins"));
  }

  ~ScopedPluginEnv() {
    if (m_hadPrevEnv) {
      qputenv("GRIDLOCK_TEST_CONFIG_DIR", m_prevEnv);
    } else {
      qunsetenv("GRIDLOCK_TEST_CONFIG_DIR");
    }
    auto& cfg = gridlock::core::ConfigManager::instance();
    cfg.loadConfig();
    cfg.setWorkspace(m_prevWorkspace);
  }

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

}  // namespace

/**
 * @class TestPluginManager
 * @brief QtTest suite exercising native plugin loading, ABI gates, and host bridge calls.
 */
class TestPluginManager : public QObject {
  Q_OBJECT

 private slots:
  /**
   * @brief Verifies `.so` discovery, `abi_major = 999` rejection, staged-action rollback, and duplicate-ID rejection.
   */
  void testDiscoveryAbiRejectionAndDuplicateGuard() {
    ScopedPluginEnv env;
    const QString dummySo = QStringLiteral(GRIDLOCK_TEST_DUMMY_PLUGIN_SO);
    const QString badAbiSo = QStringLiteral(GRIDLOCK_TEST_BAD_ABI_PLUGIN_SO);
    QVERIFY2(QFileInfo::exists(dummySo), qPrintable(dummySo));
    QVERIFY2(QFileInfo::exists(badAbiSo), qPrintable(badAbiSo));

    QVERIFY(copyPluginArtifact(dummySo, env.userPluginDir(), QStringLiteral("01_dummy.so")));
    QVERIFY(copyPluginArtifact(badAbiSo, env.userPluginDir(), QStringLiteral("02_bad_abi.so")));
    // Second copy of the same valid plugin in workspace plugins tests duplicate-ID rejection.
    QVERIFY(copyPluginArtifact(dummySo, env.workspacePluginDir(), QStringLiteral("03_dup_dummy.so")));

    gridlock::GdbRankCoordinator coord;
    gridlock::core::plugins::PluginManager mgr(&coord);

    const int activeCount = mgr.discoverAndLoad();
    QCOMPARE(activeCount, 1);
    QCOMPARE(mgr.plugins().size(), 3);

    bool sawLoadedDummy = false;
    bool sawRejectedBadAbi = false;
    bool sawRejectedDuplicate = false;
    for (const auto& info : mgr.plugins()) {
      if (info.libraryPath.endsWith(QStringLiteral("01_dummy.so"))) {
        QVERIFY(info.enabled);
        QVERIFY(info.errorMessage.isEmpty());
        QCOMPARE(info.id, QStringLiteral("org.gridlock.test_dummy"));
        QCOMPARE(info.abiMajor, GRIDLOCK_PLUGIN_ABI_MAJOR);
        sawLoadedDummy = true;
      } else if (info.libraryPath.endsWith(QStringLiteral("02_bad_abi.so"))) {
        QVERIFY(!info.enabled);
        QCOMPARE(info.abiMajor, 999u);
        QVERIFY(info.errorMessage.contains(QStringLiteral("ABI major mismatch")));
        sawRejectedBadAbi = true;
      } else if (info.libraryPath.endsWith(QStringLiteral("03_dup_dummy.so"))) {
        QVERIFY(!info.enabled);
        QVERIFY(info.errorMessage.contains(QStringLiteral("Duplicate plugin ID")));
        sawRejectedDuplicate = true;
      }
    }
    QVERIFY(sawLoadedDummy);
    QVERIFY(sawRejectedBadAbi);
    QVERIFY(sawRejectedDuplicate);

    // Ensure bad_abi's staged action was discarded and only dummy's action exists.
    const auto varActions = mgr.hostBridge()->variableActions();
    QCOMPARE(varActions.size(), 1);
    QCOMPARE(varActions.first().actionId, QStringLiteral("dummy.inspect_field"));
    QCOMPARE(mgr.hostBridge()->matchingVariableActions(QStringLiteral("Kokkos::View<double**>")).size(), 1);
    QCOMPARE(mgr.hostBridge()->matchingVariableActions(QStringLiteral("int")).size(), 0);
  }

  /**
   * @brief Verifies synchronous `eval_expr` buffer sizing, `read_memory`, timeouts, `on_rank_stopped`, and `shutdown`.
   */
  void testHostBridgeEvalMemoryTimeoutsAndStopLifecycle() {
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
                     &coord, [&](const QString& ch, const QString& payload) {
                       events.append(qMakePair(ch, payload));
                     });

    QCOMPARE(mgr.discoverAndLoad(), 1);
    QCOMPARE(mgr.hostBridge()->liveAllocationCount(), 1);

    const GridLockHostApi* api = mgr.hostBridge()->hostApi();
    size_t outLen = 0u;
    char smallBuf[8]{};

    // Rank 0 starts in "running" state after initializeMockSession(2, true).
    QCOMPARE(api->eval_expr(api->host_ctx, 0, "x", smallBuf, sizeof(smallBuf), &outLen),
             GRIDLOCK_ERR_NOT_STOPPED);
    QCOMPARE(api->eval_expr(api->host_ctx, 99, "x", smallBuf, sizeof(smallBuf), &outLen),
             GRIDLOCK_ERR_INVALID_RANK);

    // Transition rank 0 to stopped via GDB/MI *stopped record and verify on_rank_stopped fires.
    coord.processGdbOutput(
        0, QStringLiteral("*stopped,reason=\"breakpoint-hit\",frame={addr=\"0x401234\",func=\"solve\",file=\"sim.c\",fullname=\"/tmp/sim.c\",line=\"77\"}\n"));
    QCOMPARE(coord.getRankState(0).currentState, QStringLiteral("stopped"));
    QVERIFY(!events.isEmpty());
    QCOMPARE(events.last().first, QStringLiteral("dummy.stop"));
    QVERIFY(events.last().second.contains(QStringLiteral("\"line\":77")));

    // Verify timeout when GDB produces no response for an evaluation query.
    QCOMPARE(api->eval_expr(api->host_ctx, 0, "unanswered_expr", smallBuf, sizeof(smallBuf), &outLen),
             GRIDLOCK_ERR_TIMEOUT);

    // Connect mock MI responder for eval_expr and read_memory.
    const auto miResponder = QObject::connect(
        &coord, &gridlock::GdbRankCoordinator::commandSentToGdb, &coord,
        [&](int rankId, const QString& cmd) {
          if (cmd.contains(QStringLiteral("-data-evaluate-expression"))) {
            const QString tok = cmd.section(QLatin1Char('-'), 0, 0);
            if (cmd.contains(QStringLiteral("mesh_field"))) {
              QTimer::singleShot(5, &coord, [&, rankId, tok]() {
                coord.processGdbOutput(
                    rankId,
                    QStringLiteral("%1^done,value=\"extent_256x128x64_double_grid\"\n").arg(tok));
              });
            } else if (cmd.contains(QStringLiteral("broken_sym"))) {
              coord.processGdbOutput(
                  rankId, QStringLiteral("%1^error,msg=\"No symbol broken_sym\"\n").arg(tok));
            }
          } else if (cmd.startsWith(QStringLiteral("-data-read-memory-bytes 0x5000"))) {
            QTimer::singleShot(5, &coord, [&, rankId]() {
              coord.processGdbOutput(
                  rankId,
                  QStringLiteral("^done,memory=[{begin=\"0x00005000\",offset=\"0x00000000\",end=\"0x00005004\",contents=\"deadbeef\"}]\n"));
            });
          } else if (cmd.startsWith(QStringLiteral("-data-read-memory-bytes 0xbad0"))) {
            coord.processGdbOutput(0, QStringLiteral("^error,msg=\"Cannot access memory\"\n"));
          }
        });

    // Trigger C99 plugin's variable action (4-byte buffer probe -> alloc exact -> retry -> emit event).
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

    QCOMPARE(mgr.hostBridge()->invokeVariableAction(QStringLiteral("dummy.inspect_field"), varCtx),
             GRIDLOCK_OK);
    QCOMPARE(events.last().first, QStringLiteral("dummy.var_eval"));
    QCOMPARE(events.last().second, QStringLiteral("extent_256x128x64_double_grid"));
    QCOMPARE(mgr.hostBridge()->liveAllocationCount(), 1);

    // Verify eval failure status mapping.
    QCOMPARE(api->eval_expr(api->host_ctx, 0, "broken_sym", smallBuf, sizeof(smallBuf), &outLen),
             GRIDLOCK_ERR_EVAL_FAILED);

    // Verify synchronous read_memory success and fault paths.
    uint8_t memBytes[4]{};
    size_t bytesRead = 0u;
    QCOMPARE(api->read_memory(api->host_ctx, 0, 0x5000ULL, memBytes, sizeof(memBytes), &bytesRead),
             GRIDLOCK_OK);
    QCOMPARE(bytesRead, size_t{4u});
    QCOMPARE(memBytes[0], uint8_t{0xde});
    QCOMPARE(memBytes[1], uint8_t{0xad});
    QCOMPARE(memBytes[2], uint8_t{0xbe});
    QCOMPARE(memBytes[3], uint8_t{0xef});

    QCOMPARE(api->read_memory(api->host_ctx, 0, 0xbad0ULL, memBytes, sizeof(memBytes), &bytesRead),
             GRIDLOCK_ERR_MEMORY_READ_FAILED);

    // Exercise write_memory, breakpoints, rank count, active rank, and command invocation.
    size_t written = 0u;
    QCOMPARE(api->write_memory(api->host_ctx, 0, 0x5000ULL, memBytes, sizeof(memBytes), &written),
             GRIDLOCK_OK);
    QCOMPARE(written, size_t{4u});

    int32_t bpId = -1;
    QCOMPARE(api->set_breakpoint(api->host_ctx, "/tmp/sim.c", 80, "i == 3", &bpId), GRIDLOCK_OK);
    QVERIFY(bpId > 0);
    QCOMPARE(api->remove_breakpoint(api->host_ctx, bpId), GRIDLOCK_OK);

    GridLockCommandSpec cmdSpec{};
    cmdSpec.struct_size = static_cast<uint32_t>(sizeof(GridLockCommandSpec));
    cmdSpec.abi_major = GRIDLOCK_PLUGIN_ABI_MAJOR;
    cmdSpec.abi_minor = GRIDLOCK_PLUGIN_ABI_MINOR;
    cmdSpec.active_rank = 1;
    cmdSpec.rank_count = 2;
    cmdSpec.command_id_utf8 = "dummy.ping_ranks";
    cmdSpec.menu_label_utf8 = "Ping Active Ranks";
    QCOMPARE(mgr.hostBridge()->invokeCommandAction(QStringLiteral("dummy.ping_ranks"), cmdSpec),
             GRIDLOCK_OK);
    QCOMPARE(events.last().first, QStringLiteral("dummy.cmd"));
    QVERIFY(events.last().second.contains(QStringLiteral("\"ranks\":2")));

    QObject::disconnect(miResponder);

    // Unload verifies shutdown callback frees plugin_ctx cleanly.
    mgr.unloadAll();
    QCOMPARE(mgr.hostBridge()->liveAllocationCount(), 0);
    QCOMPARE(events.last().first, QStringLiteral("dummy.lifecycle"));
    QCOMPARE(events.last().second, QStringLiteral("{\"event\":\"shutdown\"}"));
  }

  /**
   * @brief Verifies `[plugins] disabled_ids` persistence and live `setPluginEnabled` unload/reload.
   */
  void testDisabledPluginConfigAndLiveToggle() {
    ScopedPluginEnv env;
    QVERIFY(copyPluginArtifact(QStringLiteral(GRIDLOCK_TEST_DUMMY_PLUGIN_SO),
                               env.userPluginDir(), QStringLiteral("dummy.so")));

    gridlock::GdbRankCoordinator coord;
    gridlock::core::plugins::PluginManager mgr(&coord);
    QCOMPARE(mgr.discoverAndLoad(), 1);
    QCOMPARE(mgr.hostBridge()->variableActions().size(), 1);
    QCOMPARE(mgr.hostBridge()->commandActions().size(), 1);
    QCOMPARE(mgr.hostBridge()->liveAllocationCount(), 1);

    // Disable live: must unregister actions, call shutdown(), unload .so, and persist to TOML.
    QVERIFY(mgr.setPluginEnabled(QStringLiteral("org.gridlock.test_dummy"), false));
    QCOMPARE(mgr.hostBridge()->variableActions().size(), 0);
    QCOMPARE(mgr.hostBridge()->commandActions().size(), 0);
    QCOMPARE(mgr.hostBridge()->liveAllocationCount(), 0);
    QVERIFY(gridlock::core::ConfigManager::instance().isPluginDisabled(
        QStringLiteral("org.gridlock.test_dummy")));

    // Reload TOML into a second PluginManager and verify disabled plugin is not initialized.
    gridlock::core::ConfigManager::instance().loadConfig();
    QVERIFY(gridlock::core::ConfigManager::instance().isPluginDisabled(
        QStringLiteral("org.gridlock.test_dummy")));

    gridlock::core::plugins::PluginManager mgrReloaded(&coord);
    QCOMPARE(mgrReloaded.discoverAndLoad(), 0);
    QCOMPARE(mgrReloaded.plugins().size(), 1);
    QVERIFY(!mgrReloaded.plugins().first().enabled);
    QCOMPARE(mgrReloaded.hostBridge()->liveAllocationCount(), 0);

    // Re-enable live and verify plugin initializes and restores its actions.
    QVERIFY(mgrReloaded.setPluginEnabled(QStringLiteral("org.gridlock.test_dummy"), true));
    QVERIFY(mgrReloaded.plugins().first().enabled);
    QCOMPARE(mgrReloaded.hostBridge()->variableActions().size(), 1);
    QCOMPARE(mgrReloaded.hostBridge()->liveAllocationCount(), 1);
  }
};

QTEST_MAIN(TestPluginManager)
#include "test_plugin_manager.moc"
