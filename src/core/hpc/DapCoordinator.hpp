#pragma once

#include <QObject>
#include <QProcess>
#include <QByteArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QString>
#include <atomic>

class DapCoordinator : public QObject {
    Q_OBJECT

public:
    explicit DapCoordinator(QObject* parent = nullptr);
    ~DapCoordinator() override;

    void startAdapter(const QString& program);
    void stopAdapter();
    void initializeAdapter();
    void sendRequest(const QString& command, const QJsonObject& arguments = QJsonObject());
    void sendRawMessage(const QJsonObject& message);

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

private:
    QProcess* m_process;
    QByteArray m_buffer;
    std::atomic<int> m_sequenceNumber{1};
};
