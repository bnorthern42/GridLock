/**
 * @file TelemetryObfuscator.cpp
 * @brief Implementation of constant-rate MTU packet padding for `TelemetryObfuscator`.
 */

#include "TelemetryObfuscator.hpp"

#include <algorithm>

#include <QDataStream>
#include <QIODevice>

namespace gridlock::core {

/**
 * @brief Constructs the obfuscator, seeds the PRNG from `std::random_device`, and starts the timer.
 */
TelemetryObfuscator::TelemetryObfuscator(QObject* parent)
    : QObject(parent),
      m_timer(new QTimer(this)),
      m_prng(std::random_device{}()) {
  connect(m_timer, &QTimer::timeout, this, &TelemetryObfuscator::onFlush);
  m_timer->start(FLUSH_INTERVAL_MS);
}

/**
 * @brief Halts the periodic timer before destroying the QObject.
 */
TelemetryObfuscator::~TelemetryObfuscator() {
  m_timer->stop();
}

/**
 * @brief Thread-safely appends @p data to the internal staging buffer.
 */
void TelemetryObfuscator::pushData(const QByteArray& data) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_buffer.append(data);
}

/**
 * @brief Emits one or more `MTU_THRESHOLD`-byte frames containing a 4-byte length prefix, payload, and random padding.
 *
 * Even when `m_buffer` is empty, a single chaff packet (`actualSize == 0`) is emitted
 * to preserve constant inter-packet timing on the wire.
 */
void TelemetryObfuscator::onFlush() {
  std::lock_guard<std::mutex> lock(m_mutex);

  // Emit at least one packet per tick to maintain constant traffic cadence.
  while (true) {
    QByteArray chunk;
    if (!m_buffer.isEmpty()) {
      // Reserve 4 bytes at the front of the frame for the big-endian `quint32` payload size.
      const int chunkSize = std::min<int>(
          m_buffer.size(), static_cast<int>(MTU_THRESHOLD - sizeof(quint32)));
      chunk = m_buffer.left(chunkSize);
      m_buffer.remove(0, chunkSize);
    }

    // Serialize the 4-byte payload length followed by the raw payload slice.
    QByteArray payload;
    QDataStream out(&payload, QIODevice::WriteOnly);

    const quint32 actualSize = static_cast<quint32>(chunk.size());
    out << actualSize;
    out.writeRawData(chunk.constData(), chunk.size());

    // Fill the remainder of the 1400-byte MTU frame with pseudorandom noise bytes.
    const int paddingSize = static_cast<int>(MTU_THRESHOLD) - payload.size();
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

}  // namespace gridlock::core
