#include "TelemetryObfuscator.hpp"
#include <QDataStream>
#include <QIODevice>

namespace gridlock::core {

TelemetryObfuscator::TelemetryObfuscator(QObject* parent) 
    : QObject(parent), m_timer(new QTimer(this)), m_prng(std::random_device{}()) 
{
    connect(m_timer, &QTimer::timeout, this, &TelemetryObfuscator::onFlush);
    m_timer->start(FLUSH_INTERVAL_MS);
}

TelemetryObfuscator::~TelemetryObfuscator() {
    m_timer->stop();
}

void TelemetryObfuscator::pushData(const QByteArray& data) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_buffer.append(data);
}

void TelemetryObfuscator::onFlush() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    while (!m_buffer.isEmpty() || true) { // We flush even if empty to maintain constant traffic timing
        QByteArray chunk;
        if (!m_buffer.isEmpty()) {
            int chunkSize = std::min<int>(m_buffer.size(), static_cast<int>(MTU_THRESHOLD - sizeof(quint32))); // Leave room for length
            chunk = m_buffer.left(chunkSize);
            m_buffer.remove(0, chunkSize);
        }

        QByteArray payload;
        QDataStream out(&payload, QIODevice::WriteOnly);
        
        quint32 actualSize = chunk.size();
        out << actualSize;
        out.writeRawData(chunk.constData(), chunk.size());

        // Pad to MTU_THRESHOLD
        int paddingSize = MTU_THRESHOLD - payload.size();
        if (paddingSize > 0) {
            QByteArray padding;
            padding.resize(paddingSize);
            for (int i = 0; i < paddingSize; ++i) {
                padding[i] = static_cast<char>(m_prng() & 0xFF);
            }
            out.writeRawData(padding.constData(), padding.size());
        }

        emit dataFlushed(payload);

        if (m_buffer.isEmpty()) {
            break;
        }
    }
}

} // namespace gridlock::core
