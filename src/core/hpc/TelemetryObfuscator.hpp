/**
 * @file TelemetryObfuscator.hpp
 * @brief Constant-rate, MTU-padded telemetry packet shaper against SSH traffic analysis.
 *
 * Buffers outgoing debugger telemetry and flushes fixed `1400`-byte frames every
 * `50 ms` padded with CSPRNG/PRNG noise to mask bursty GDB/MI inspection patterns.
 */

#pragma once

#include <mutex>
#include <queue>
#include <random>

#include <QByteArray>
#include <QObject>
#include <QTimer>

namespace gridlock::core {

/**
 * @class TelemetryObfuscator
 * @brief Shapes variable-length telemetry chunks into fixed-size 1400-byte packets at 20 Hz.
 */
class TelemetryObfuscator : public QObject {
  Q_OBJECT

 public:
  /// Fixed Ethernet/SSH-friendly packet size in bytes.
  static constexpr size_t MTU_THRESHOLD = 1400;
  /// Constant periodic flush interval in milliseconds (20 Hz).
  static constexpr int FLUSH_INTERVAL_MS = 50;

  /**
   * @brief Starts the periodic 50 ms packet shaper timer and seeds the 64-bit PRNG.
   */
  explicit TelemetryObfuscator(QObject* parent = nullptr);

  /**
   * @brief Stops the periodic flush timer on destruction.
   */
  ~TelemetryObfuscator() override;

  /**
   * @brief Appends raw telemetry bytes to the thread-safe outbound queue.
   */
  void pushData(const QByteArray& data);

 signals:
  /**
   * @brief Emitted every `FLUSH_INTERVAL_MS` with a length-prefixed, `MTU_THRESHOLD`-padded frame.
   */
  void dataFlushed(const QByteArray& paddedData);

 private slots:
  /** @brief Drains queued chunks (or emits a dummy frame) padded to `MTU_THRESHOLD`. */
  void onFlush();

 private:
  QByteArray m_buffer;      ///< Queued unpadded telemetry payload bytes.
  QTimer* m_timer;          ///< Periodic 50 ms flush timer.
  std::mt19937_64 m_prng;   ///< 64-bit Mersenne Twister used to generate random tail padding.
  std::mutex m_mutex;       ///< Protects `m_buffer` across producer/consumer threads.
};

}  // namespace gridlock::core
