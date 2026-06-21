#pragma once
#include <QObject>
#include <QProcess>
#include <QJsonObject>
#include <QJsonDocument>
#include <QString>
#include <QMap>
#include <QPoint>

namespace gridlock::core {

class LspCoordinator : public QObject {
    Q_OBJECT
public:
    explicit LspCoordinator(QObject *parent = nullptr);
    ~LspCoordinator() override;

    void start();
    void stop();

    void didOpen(const QString& file, const QString& text);
    void requestHover(const QString& file, int line, int character, QPoint globalPos = QPoint());

signals:
    void hoverResultReceived(const QString& resultMarkdown, const QPoint& globalPos);

private slots:
    void readyReadStandardOutput();
    void readyReadStandardError();

private:
    void sendPayload(const QJsonObject& payload);
    void processMessage(const QJsonObject& message);

    QProcess* m_process;
    QByteArray m_buffer;
    int m_nextRequestId = 1;
    QMap<int, QPoint> m_hoverRequests; // requestId -> globalPos
};

} // namespace gridlock::core
