/**
 * @file test_dap_native_bridge.cpp
 * @brief Implementation of DAP native bridge unit tests.
 *
 * Exercises the process ID binding from DAP 'process' events and ensures that
 * memory reads exceeding threshold limits route through process_vm_readv rather
 * than saturating DAP stdio streams.
 */

#include "test_dap_native_bridge.hpp"

#include <vector>

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

#include "../src/core/hpc/DapCoordinator.hpp"

#ifdef __linux__
#include <unistd.h>
#endif

/**
 * @class TestableDapCoordinator
 * @brief Test double exposing protected message handlers and rank-to-PID tables.
 */
class TestableDapCoordinator : public DapCoordinator {
public:
    using DapCoordinator::handleMessage;
    using DapCoordinator::m_rankToPid;

    TestableDapCoordinator() { m_state = SessionState::Running; }

    QJsonObject lastSentMessage;

protected:
    /**
     * @brief Captures outbound JSON-RPC messages before serialization.
     * @param message Structured DAP message object.
     */
    void sendRawMessage(const QJsonObject& message) override {
        lastSentMessage = message;
    }
};

/**
 * @brief Tests extraction of OS systemProcessId from DAP process lifecycle events.
 */
void TestDapNativeBridge::testPidExtraction() {
    TestableDapCoordinator coordinator;

    // Simulate DAP process creation event carrying target PID.
    QJsonObject body;
    body["name"] = "mpi_mm";
    body["systemProcessId"] = 1053418;

    QJsonObject event;
    event["type"] = "event";
    event["event"] = "process";
    event["body"] = body;

    // Dispatch event into coordinator message handler.
    coordinator.handleMessage(event);

    // Verify internal mapping associates rank 0 with extracted PID.
    QVERIFY(coordinator.m_rankToPid.contains(0));
    QCOMPARE(coordinator.m_rankToPid[0], 1053418);
}

/**
 * @brief Tests threshold-based routing between native Linux memory read and DAP JSON.
 */
void TestDapNativeBridge::testRoutingLogic() {
#ifdef __linux__
    TestableDapCoordinator coordinator;

    // Associate rank 0 with the current process PID so process_vm_readv succeeds.
    QJsonObject body;
    body["name"] = "mpi_mm";
    body["systemProcessId"] = getpid();

    QJsonObject event;
    event["type"] = "event";
    event["event"] = "process";
    event["body"] = body;
    coordinator.handleMessage(event);

    // Allocate local memory buffer representing remote target data.
    std::vector<double> dummyData(2000, 3.14);
    uintptr_t address = reinterpret_cast<uintptr_t>(dummyData.data());
    QString addressStr = QString("0x%1").arg(address, 0, 16);

    QSignalSpy spy(&coordinator, &DapCoordinator::memoryRead);

    // Request large memory block (> 1024 elements).
    coordinator.readMemory(0, addressStr, 2000);

    // Verify it bypassed DAP JSON request emission.
    QVERIFY(coordinator.lastSentMessage["command"].toString() != "readMemory");

    // Verify memoryRead signal was emitted synchronously via native reader.
    QCOMPARE(spy.count(), 1);
    QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.at(0).toInt(), 0);
    QCOMPARE(args.at(1).toString(), addressStr);

    QByteArray data = args.at(2).toByteArray();
    QCOMPARE(static_cast<size_t>(data.size()), 2000 * sizeof(double));

    const double* returnedDoubles = reinterpret_cast<const double*>(data.constData());
    QCOMPARE(returnedDoubles[0], 3.14);
    QCOMPARE(returnedDoubles[1999], 3.14);

    // Request small block (<= 1024) - should route to standard DAP protocol.
    coordinator.readMemory(0, addressStr, 1024);
    QCOMPARE(coordinator.lastSentMessage["command"].toString(), "readMemory");
#else
    QSKIP("NativeMemoryReader is only supported on Linux");
#endif
}

QTEST_GUILESS_MAIN(TestDapNativeBridge)
