#include "test_dap_variables.hpp"
#include "../src/core/hpc/DapCoordinator.hpp"
#include "../src/core/models/VariableTreeModel.hpp"
#include "../src/core/models/VariableNode.hpp"
#include <QSignalSpy>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>

class MockDapCoordinatorVars : public DapCoordinator {
public:
    MockDapCoordinatorVars() { m_state = SessionState::Running; }
    
    QByteArray lastWrittenData;

protected:
    bool isAdapterRunning() const override { return true; }
    void writeToAdapter(const QByteArray& data) override {
        lastWrittenData = data;
    }
};

void TestDapVariables::testScopesFetch() {
    MockDapCoordinatorVars coordinator;
    
    coordinator.requestStackTrace(0); // rank=0, seq=1
    QByteArray json = "{\"type\":\"response\",\"command\":\"stackTrace\",\"success\":true,\"request_seq\":1,\"body\":{\"stackFrames\":[{\"id\":1000,\"name\":\"main\",\"source\":{\"path\":\"/test.cpp\"},\"line\":42}]}}";
    QByteArray data = "Content-Length: " + QByteArray::number(json.size()) + "\r\n\r\n" + json;
    
    coordinator.processRawData(data);
    
    QVERIFY(coordinator.lastWrittenData.contains("\"command\":\"scopes\""));
    QVERIFY(coordinator.lastWrittenData.contains("\"frameId\":1000"));
}

void TestDapVariables::testVariablesFetch() {
    MockDapCoordinatorVars coordinator;
    
    coordinator.requestScopes(1000, 0); // rank=0, seq=1
    QByteArray json = "{\"type\":\"response\",\"command\":\"scopes\",\"success\":true,\"request_seq\":1,\"body\":{\"scopes\":[{\"name\":\"Locals\",\"variablesReference\":1001,\"expensive\":false}]}}";
    QByteArray data = "Content-Length: " + QByteArray::number(json.size()) + "\r\n\r\n" + json;
    
    // We must ensure the sequence number is mapped. Actually, we might need to send a mock request first if the coordinator uses request_seq to track rank.
    // Assuming DapCoordinator maps sequence numbers properly, let's just push the raw response.
    coordinator.processRawData(data);
    
    QVERIFY(coordinator.lastWrittenData.contains("\"command\":\"variables\""));
    QVERIFY(coordinator.lastWrittenData.contains("\"variablesReference\":1001"));
}

void TestDapVariables::testVariableTreePopulation() {
    gridlock::VariableTreeModel model(nullptr);
    MockDapCoordinatorVars coordinator;
    
    QObject::connect(&coordinator, &DapCoordinator::localsUpdated, &model, [&model](int rankId, int parentVarRef, const QJsonArray& variables) {
        model.onLocalsUpdated(rankId, parentVarRef, variables);
    });
    
    coordinator.requestVariables(0, 1001); // rank=0, varRef=1001, seq=1
    QByteArray json = "{\"type\":\"response\",\"command\":\"variables\",\"success\":true,\"request_seq\":1,\"body\":{\"variables\":[{\"name\":\"argc\",\"value\":\"1\",\"type\":\"int\",\"variablesReference\":0}]}}";
    QByteArray data = "Content-Length: " + QByteArray::number(json.size()) + "\r\n\r\n" + json;
    
    // If the coordinator uses request_seq mapping for rank, we simulate sending a variables request first.
    // coordinator.requestVariables(0, 1001); // Assumes we add this method, rank=0
    
    coordinator.processRawData(data);
    
    // Check if the model has populated the variable
    QModelIndex rootIndex = model.index(0, 0); // argc node
    QVERIFY(rootIndex.isValid());
    QCOMPARE(model.data(rootIndex, Qt::DisplayRole).toString(), QString("argc"));
    QModelIndex valueIndex = model.index(0, 1);
    QCOMPARE(model.data(valueIndex, Qt::DisplayRole).toString(), QString("1"));
    QModelIndex typeIndex = model.index(0, 2);
    QCOMPARE(model.data(typeIndex, Qt::DisplayRole).toString(), QString("int"));
}

void TestDapVariables::testVariableLazyLoading() {
    MockDapCoordinatorVars coordinator;
    gridlock::VariableTreeModel model(&coordinator);
    
    QObject::connect(&coordinator, &DapCoordinator::localsUpdated, &model, [&model](int rankId, int parentVarRef, const QJsonArray& variables) {
        model.onLocalsUpdated(rankId, parentVarRef, variables);
    });
    
    coordinator.requestVariables(0, 1001); // seq 1
    coordinator.lastWrittenData.clear();
    
    QByteArray json = "{\"type\":\"response\",\"command\":\"variables\",\"success\":true,\"request_seq\":1,\"body\":{\"variables\":[{\"name\":\"argv\",\"value\":\"0x1234\",\"type\":\"char**\",\"variablesReference\":5}]}}";
    QByteArray data = "Content-Length: " + QByteArray::number(json.size()) + "\r\n\r\n" + json;
    
    coordinator.processRawData(data);
    
    // Verify no nested DAP request was sent eagerly
    QVERIFY(coordinator.lastWrittenData.isEmpty());
    
    QModelIndex rootIndex = model.index(0, 0); // argv node
    QVERIFY(rootIndex.isValid());
    
    // Trigger lazy load
    QVERIFY(model.canFetchMore(rootIndex));
    model.fetchMore(rootIndex);
    
    // Verify DAP request was sent for variablesReference = 5
    QVERIFY(coordinator.lastWrittenData.contains("\"command\":\"variables\""));
    QVERIFY(coordinator.lastWrittenData.contains("\"variablesReference\":5"));
}

QTEST_GUILESS_MAIN(TestDapVariables)
