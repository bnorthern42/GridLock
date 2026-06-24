#pragma once

#include <QObject>
#include <QProcess>
#include <QByteArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QString>
#include <atomic>
#include <sys/types.h>
#include "IBackendCoordinator.hpp"

enum class SessionState {
    Disconnected,
    Launching,
    Queued,
    Running,
    Paused
};

class DapCoordinator : public IBackendCoordinator {
    Q_OBJECT

public:
    explicit DapCoordinator(QObject* parent = nullptr);
    ~DapCoordinator() override;

    void startAdapter(const QString& program);
    void stopAdapter();
    void initializeAdapter();
    int sendRequest(const QString& command, const QJsonObject& arguments = QJsonObject());
    void processRawData(const QByteArray& data);
    void terminateSession();
    
    void toggleBreakpoint(const QString& file, int line);
    void requestStackTrace(int rankId);
    void requestScopes(int frameId, int rankId);
    void requestVariables(int rankId, int variablesReference);
    void evaluateExpression(int rankId, const QString& expression) override;
    void readMemory(int rankId, const QString& memoryReference, int count) override;
    void requestHeatmapRender(int rankId, const QString& expression, int rows, int cols);

    void stepOver(int threadId) override;
    void stepInto(int threadId) override;
    void continueExecution(int threadId) override;
    void pauseExecution(int threadId) override;
    void launchParallelSession(const QString& binaryPath, int ranks) override;
    pid_t getPidForRank(int rankId) const override { return m_rankToPid.value(rankId, 0); }

signals:
    void executionStopped(int rankId, const QString& reason);
    void messageReceived(const QJsonObject& message);
    void localsUpdated(int rankId, const QJsonArray& variables);
    void expressionEvaluated(int rankId, QString expr, QString result);
    void targetOutputReceived(QString category, QString output);
    void memoryRead(int rankId, const QString& address, const QByteArray& data);
    void heatmapDataReady(const std::vector<double>& data, int rows, int cols);
    void registersUpdated(int rankId, const QJsonArray& registers);
    void adapterStarted();
    void adapterExited(int exitCode, QProcess::ExitStatus exitStatus);
    void errorOccurred(const QString& errorString);
    void stateChanged(SessionState newState);

private slots:
    void readyReadStandardOutput();
    void handleProcessError(QProcess::ProcessError error);
    void handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

protected slots:
    void handleMessage(const QJsonObject& message);

protected:
    virtual void sendRawMessage(const QJsonObject& message);
    virtual bool isAdapterRunning() const;
    virtual void writeToAdapter(const QByteArray& data);

    QMap<int, pid_t> m_rankToPid;

private:
    QProcess* m_process;
    QByteArray m_buffer;
    std::atomic<int> m_sequenceNumber{1};
    QMap<QString, QList<int>> m_breakpoints;
    QMap<int, int> m_stackTraceRequests; // Map sequenceNumber to rankId
    QMap<int, int> m_scopesRequests;
    QMap<int, int> m_variablesRequests;
    QMap<int, int> m_registersRequests;
    QMap<int, int> m_memoryRequests;
    QMap<int, int> m_activeFrameIds;
    QMap<int, QPair<int, QString>> m_evaluateRequests;
    
    struct HeatmapRequest {
        int rankId;
        int rows;
        int cols;
    };
    QMap<int, HeatmapRequest> m_heatmapRequests;
    
    SessionState m_state = SessionState::Disconnected;
    int m_slurmJobId = -1;
};
