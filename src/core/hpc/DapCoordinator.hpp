#pragma once

#include <QObject>
#include <QProcess>
#include <QByteArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QString>

class DapCoordinator : public QObject {
    Q_OBJECT

public:
    explicit DapCoordinator(QObject* parent = nullptr);
    ~DapCoordinator() override;

    void startAdapter(const QString& program);
    void stopAdapter();
    void sendRequest(const QJsonObject& request);

signals:
    void messageReceived(const QJsonObject& message);
    void adapterStarted();
    void adapterExited(int exitCode, QProcess::ExitStatus exitStatus);
    void errorOccurred(const QString& errorString);

private slots:
    void readyReadStandardOutput();
    void handleProcessError(QProcess::ProcessError error);
    void handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    QProcess* m_process;
    QByteArray m_buffer;
};
