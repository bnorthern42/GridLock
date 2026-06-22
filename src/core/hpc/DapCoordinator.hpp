#pragma once

#include <QObject>
#include <QProcess>
#include <QByteArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QString>
#include <atomic>
#include "IBackendCoordinator.hpp"

class DapCoordinator : public IBackendCoordinator {
    Q_OBJECT

public:
    explicit DapCoordinator(QObject* parent = nullptr);
    ~DapCoordinator() override;

    void startAdapter(const QString& program);
    void stopAdapter();
    void initializeAdapter();
    void sendRequest(const QString& command, const QJsonObject& arguments = QJsonObject());
    void processRawData(const QByteArray& data);

    void stepOver(int threadId) override;
    void stepInto(int threadId) override;
    void continueExecution(int threadId) override;
    void pauseExecution(int threadId) override;

signals:
    void messageReceived(const QJsonObject& message);
    void adapterStarted();
    void adapterExited(int exitCode, QProcess::ExitStatus exitStatus);
    void errorOccurred(const QString& errorString);

private slots:
    void readyReadStandardOutput();
    void handleMessage(const QJsonObject& message);
    void handleProcessError(QProcess::ProcessError error);
    void handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

protected:
    virtual void sendRawMessage(const QJsonObject& message);
    virtual bool isAdapterRunning() const;
    virtual void writeToAdapter(const QByteArray& data);

private:
    QProcess* m_process;
    QByteArray m_buffer;
    std::atomic<int> m_sequenceNumber{1};
};
