/**
 * @file test_dap_variables.cpp
 * @brief Implementation of DAP variable inspection and tree model unit tests.
 *
 * Exercises recursive variable trees, lazy fetching via variablesReference IDs,
 * high-scale performance characteristics (10k items), and deduplicated fetches.
 */

#include "test_dap_variables.hpp"

#include <QByteArray>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>

#include "../src/core/hpc/DapCoordinator.hpp"
#include "../src/core/models/VariableNode.hpp"
#include "../src/core/models/VariableTreeModel.hpp"

/**
 * @class MockDapCoordinatorVars
 * @brief Test double capturing outbound adapter messages for variable testing.
 */
class MockDapCoordinatorVars : public DapCoordinator {
public:
    MockDapCoordinatorVars() { m_state = SessionState::Running; }

    QByteArray lastWrittenData;
    int writeCount = 0;

protected:
    /**
     * @brief Reports adapter as running for variable mock queries.
     * @return Always true in mock environment.
     */
    bool isAdapterRunning() const override { return true; }

    /**
     * @brief Records outbound adapter byte payload and increments write count.
     * @param data Raw outbound bytes.
     */
    void writeToAdapter(const QByteArray& data) override {
        lastWrittenData = data;
        writeCount++;
    }
};

/**
 * @brief Verifies that receiving a stack trace response automatically queries scopes.
 */
void TestDapVariables::testScopesFetch() {
    MockDapCoordinatorVars coordinator;
    gridlock::VariableTreeModel model(&coordinator);

    // Seed stack trace response for frame 1000.
    coordinator.requestStackTrace(0); // rank=0, seq=1
    QByteArray json = "{\"type\":\"response\",\"command\":\"stackTrace\",\"success\":true,\"request_seq\":1,\"body\":{\"stackFrames\":[{\"id\":1000,\"name\":\"main\",\"source\":{\"path\":\"/test.cpp\"},\"line\":42}]}}";
    QByteArray data = "Content-Length: " + QByteArray::number(json.size()) + "\r\n\r\n" + json;

    coordinator.processRawData(data);
    QTest::qWait(50);
    QTest::qWait(50);
    QTest::qWait(50);

    // Verify coordinator issued a scopes request for frame 1000.
    QVERIFY(coordinator.lastWrittenData.contains("\"command\":\"scopes\""));
    QVERIFY(coordinator.lastWrittenData.contains("\"frameId\":1000"));
}

/**
 * @brief Verifies that scopes response triggers variables request for Locals scope.
 */
void TestDapVariables::testVariablesFetch() {
    MockDapCoordinatorVars coordinator;
    gridlock::VariableTreeModel model(&coordinator);

    // Seed scopes response containing Locals with variablesReference 1001.
    coordinator.requestScopes(1000, 0); // rank=0, seq=1
    QByteArray json = "{\"type\":\"response\",\"command\":\"scopes\",\"success\":true,\"request_seq\":1,\"body\":{\"scopes\":[{\"name\":\"Locals\",\"variablesReference\":1001,\"expensive\":false}]}}";
    QByteArray data = "Content-Length: " + QByteArray::number(json.size()) + "\r\n\r\n" + json;

    coordinator.processRawData(data);
    QTest::qWait(50);
    QTest::qWait(50);
    QTest::qWait(50);

    // Verify coordinator requested variables with variablesReference 1001.
    QVERIFY(coordinator.lastWrittenData.contains("\"command\":\"variables\""));
    QVERIFY(coordinator.lastWrittenData.contains("\"variablesReference\":1001"));
}

/**
 * @brief Verifies VariableTreeModel populates root items from variables response.
 */
void TestDapVariables::testVariableTreePopulation() {
    gridlock::VariableTreeModel model(nullptr);
    MockDapCoordinatorVars coordinator;

    // Connect localsUpdated signal to model slot.
    QObject::connect(&coordinator, &DapCoordinator::localsUpdated, &model, [&model](int rankId, int parentVarRef, const QJsonArray& variables) {
        model.onLocalsUpdated(rankId, parentVarRef, variables);
    });

    // Request variables for rank 0 and varRef 1001.
    coordinator.requestVariables(0, 1001); // rank=0, varRef=1001, seq=1
    QByteArray json = "{\"type\":\"response\",\"command\":\"variables\",\"success\":true,\"request_seq\":1,\"body\":{\"variables\":[{\"name\":\"argc\",\"value\":\"1\",\"type\":\"int\",\"variablesReference\":0}]}}";
    QByteArray data = "Content-Length: " + QByteArray::number(json.size()) + "\r\n\r\n" + json;

    coordinator.processRawData(data);
    QTest::qWait(50);
    QTest::qWait(50);

    // Check if the model has populated the variable 'argc'.
    QModelIndex rootIndex = model.index(0, 0); // argc node
    QVERIFY(rootIndex.isValid());
    QCOMPARE(model.data(rootIndex, Qt::DisplayRole).toString(), QString("argc"));
    QModelIndex valueIndex = model.index(0, 1);
    QCOMPARE(model.data(valueIndex, Qt::DisplayRole).toString(), QString("1"));
    QModelIndex typeIndex = model.index(0, 2);
    QCOMPARE(model.data(typeIndex, Qt::DisplayRole).toString(), QString("int"));
}

/**
 * @brief Verifies lazy loading of child variables upon tree node expansion.
 */
void TestDapVariables::testVariableLazyLoading() {
    MockDapCoordinatorVars coordinator;
    gridlock::VariableTreeModel model(&coordinator);

    QObject::connect(&coordinator, &DapCoordinator::localsUpdated, &model, [&model](int rankId, int parentVarRef, const QJsonArray& variables) {
        model.onLocalsUpdated(rankId, parentVarRef, variables);
    });

    coordinator.requestVariables(0, 1001); // seq 1
    coordinator.lastWrittenData.clear();

    // Variable 'argv' with variablesReference 5.
    QByteArray json = "{\"type\":\"response\",\"command\":\"variables\",\"success\":true,\"request_seq\":1,\"body\":{\"variables\":[{\"name\":\"argv\",\"value\":\"0x1234\",\"type\":\"char**\",\"variablesReference\":5}]}}";
    QByteArray data = "Content-Length: " + QByteArray::number(json.size()) + "\r\n\r\n" + json;

    coordinator.processRawData(data);
    QTest::qWait(50);
    QTest::qWait(50);

    // Verify no nested DAP request was sent eagerly.
    QVERIFY(coordinator.lastWrittenData.isEmpty());

    QModelIndex rootIndex = model.index(0, 0); // argv node
    QVERIFY(rootIndex.isValid());

    // Trigger lazy load via canFetchMore and fetchMore.
    QVERIFY(model.canFetchMore(rootIndex));
    model.fetchMore(rootIndex);

    // Verify DAP request was sent for variablesReference = 5.
    QVERIFY(coordinator.lastWrittenData.contains("\"command\":\"variables\""));
    QVERIFY(coordinator.lastWrittenData.contains("\"variablesReference\":5"));
}

/**
 * @brief Verifies VariableTreeModel parent() lookup stays O(1) across 10,000 nodes.
 */
void TestDapVariables::testVariableTreePerformance() {
    MockDapCoordinatorVars coordinator;
    gridlock::VariableTreeModel model(&coordinator);

    QObject::connect(&coordinator, &DapCoordinator::localsUpdated, &model, [&model](int rankId, int parentVarRef, const QJsonArray& variables) {
        model.onLocalsUpdated(rankId, parentVarRef, variables);
    });

    // Create 10,000 dummy variable objects.
    QJsonArray varsArray;
    for (int i = 0; i < 10000; ++i) {
        QJsonObject varObj;
        varObj["name"] = QString("item_%1").arg(i);
        varObj["value"] = "0.0";
        varObj["type"] = "double";
        varObj["variablesReference"] = 0;
        varsArray.append(varObj);
    }

    QJsonObject topObj;
    topObj["name"] = "hugeArray";
    topObj["value"] = "[10000]";
    topObj["type"] = "double*";
    topObj["variablesReference"] = 999;

    QJsonArray rootArray;
    rootArray.append(topObj);

    // 1. Insert root element.
    model.onLocalsUpdated(0, 0, rootArray);

    // 2. Insert 10,000 children under root element.
    model.onLocalsUpdated(0, 999, varsArray);

    QModelIndex rootIndex = model.index(0, 0);
    QVERIFY(rootIndex.isValid());

    // 3. Time calling parent() on all 10,000 children.
    QElapsedTimer timer;
    timer.start();

    for (int i = 0; i < 10000; ++i) {
        QModelIndex childIndex = model.index(i, 0, rootIndex);
        QModelIndex pIndex = model.parent(childIndex);
        QVERIFY(pIndex == rootIndex);
    }

    qint64 elapsed = timer.elapsed();
    qDebug() << "Performance test elapsed time:" << elapsed << "ms";

    // Lookup should be O(1) and complete well under 50ms.
    QVERIFY2(elapsed < 50, "VariableTreeModel::parent() is too slow, likely O(N) instead of O(1)!");
}

/**
 * @brief Verifies deduplication of rapid successive fetchMore calls on the same node.
 */
void TestDapVariables::testStepVariableRefresh() {
    MockDapCoordinatorVars coordinator;
    gridlock::VariableTreeModel model(&coordinator);

    QJsonArray vars;
    QJsonObject v1; v1["name"] = "a"; v1["value"] = "1"; v1["type"] = "int"; v1["variablesReference"] = 0;
    QJsonObject v2; v2["name"] = "b"; v2["value"] = "2"; v2["type"] = "int"; v2["variablesReference"] = 10;
    vars.append(v1);
    vars.append(v2);

    QElapsedTimer timer;
    timer.start();

    // Populate the root with variables a and b.
    model.onLocalsUpdated(0, 0, vars);

    // Attempt fetchMore on node with variablesReference = 10.
    QModelIndex parentIdx = model.index(1, 0, QModelIndex());
    QVERIFY(parentIdx.isValid());

    model.fetchMore(parentIdx);

    // Immediately call fetchMore again (simulating QTreeView aggressive fetching).
    model.fetchMore(parentIdx);
    model.fetchMore(parentIdx);

    // It should have only sent ONE request for varRef 10!
    QCOMPARE(coordinator.writeCount, 1);

    qint64 elapsed = timer.elapsed();
    QVERIFY(elapsed < 50); // MUST be under 50ms
}

QTEST_GUILESS_MAIN(TestDapVariables)
