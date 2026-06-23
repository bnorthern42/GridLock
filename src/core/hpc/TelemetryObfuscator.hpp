#pragma once
#include <QByteArray>
#include <QObject>
#include <QTimer>
#include <random>
#include <mutex>
#include <queue>

namespace gridlock::core {

class TelemetryObfuscator : public QObject {
    Q_OBJECT
public:
    static constexpr size_t MTU_THRESHOLD = 1400;
    static constexpr int FLUSH_INTERVAL_MS = 50;

    explicit TelemetryObfuscator(QObject* parent = nullptr);
    ~TelemetryObfuscator() override;

    void pushData(const QByteArray& data);

signals:
    void dataFlushed(const QByteArray& paddedData);

private slots:
    void onFlush();

private:
    QByteArray m_buffer;
    QTimer* m_timer;
    std::mt19937_64 m_prng;
    std::mutex m_mutex;
};

} // namespace gridlock::core
