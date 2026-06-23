#include "test_dap_native_bridge.hpp"
#include "../src/core/hpc/DapCoordinator.hpp"
#include <QTest>
#include <QSignalSpy>
#include <QJsonDocument>
#include <QJsonObject>
#include <vector>

#ifdef __linux__
#include <unistd.h>
#endif

class TestableDapCoordinator : public DapCoordinator {
public:
    using DapCoordinator::handleMessage;
    using DapCoordinator::m_rankToPid;
    
    QJsonObject lastSentMessage;
    
protected:
    void sendRawMessage(const QJsonObject& message) override {
        lastSentMessage = message;
    }
};

void TestDapNativeBridge::testPidExtraction() {
    TestableDapCoordinator coordinator;
    
    QJsonObject body;
    body["name"] = "mpi_mm";
    body["systemProcessId"] = 1053418;
    
    QJsonObject event;
    event["type"] = "event";
    event["event"] = "process";
    event["body"] = body;
    
    coordinator.handleMessage(event);
    
    QVERIFY(coordinator.m_rankToPid.contains(0));
    QCOMPARE(coordinator.m_rankToPid[0], 1053418);
}

void TestDapNativeBridge::testRoutingLogic() {
#ifdef __linux__
    TestableDapCoordinator coordinator;
    
    // Setup PID
    QJsonObject body;
    body["name"] = "mpi_mm";
    body["systemProcessId"] = getpid(); // Use current PID so NativeMemoryReader doesn't fail
    
    QJsonObject event;
    event["type"] = "event";
    event["event"] = "process";
    event["body"] = body;
    coordinator.handleMessage(event);
    
    // Allocate some memory to read
    std::vector<double> dummyData(2000, 3.14);
    uintptr_t address = reinterpret_cast<uintptr_t>(dummyData.data());
    QString addressStr = QString("0x%1").arg(address, 0, 16);
    
    QSignalSpy spy(&coordinator, &DapCoordinator::memoryRead);
    
    // Read massive array
    coordinator.readMemory(0, addressStr, 2000);
    
    // Verify it bypassed DAP
    QVERIFY(coordinator.lastSentMessage["command"].toString() != "readMemory");
    
    // Verify memoryRead signal was emitted directly
    QCOMPARE(spy.count(), 1);
    QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.at(0).toInt(), 0);
    QCOMPARE(args.at(1).toString(), addressStr);
    
    QByteArray data = args.at(2).toByteArray();
    QCOMPARE(static_cast<size_t>(data.size()), 2000 * sizeof(double));
    
    const double* returnedDoubles = reinterpret_cast<const double*>(data.constData());
    QCOMPARE(returnedDoubles[0], 3.14);
    QCOMPARE(returnedDoubles[1999], 3.14);
    
    // Test small array (<= 1024) - should route to DAP
    coordinator.readMemory(0, addressStr, 1024);
    QCOMPARE(coordinator.lastSentMessage["command"].toString(), "readMemory");
#else
    QSKIP("NativeMemoryReader is only supported on Linux");
#endif
}

QTEST_GUILESS_MAIN(TestDapNativeBridge)
