/**
 * @file JsonRpcFramer.hpp
 * @brief Shared bounded `Content-Length` JSON-RPC stream framer for LSP and DAP transports.
 *
 * Enforces strict upper bounds on individual frame payloads (`16 MiB` default) and
 * rolling receive buffers (`32 MiB` default), safely advances past `Content-Length: 0`
 * headers without deadlocking, and recovers cleanly from malformed or oversized headers.
 */

#pragma once

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace gridlock::core {

/**
 * @class JsonRpcFramer
 * @brief Incremental `Content-Length` wire framer shared by `LspCoordinator`, `ClangdLspClient`, and `DapCoordinator`.
 */
class JsonRpcFramer {
 public:
  static constexpr qsizetype kDefaultMaxFrameBytes = 16 * 1024 * 1024;   ///< 16 MiB maximum single JSON payload.
  static constexpr qsizetype kDefaultMaxBufferBytes = 32 * 1024 * 1024;  ///< 32 MiB maximum rolling receive buffer.
  static constexpr qsizetype kMaxHeaderBlockBytes = 16 * 1024;           ///< 16 KiB maximum header block before `\r\n\r\n`.

  /**
   * @struct FeedResult
   * @brief Complete JSON payload byte slices extracted from a chunk alongside any framing errors.
   */
  struct FeedResult {
    QVector<QByteArray> payloads;
    QStringList errors;
  };

  explicit JsonRpcFramer(qsizetype maxFrameBytes = kDefaultMaxFrameBytes,
                         qsizetype maxBufferBytes = kDefaultMaxBufferBytes)
      : m_maxFrameBytes(maxFrameBytes > 0 ? maxFrameBytes : kDefaultMaxFrameBytes),
        m_maxBufferBytes(maxBufferBytes > 0 ? maxBufferBytes : kDefaultMaxBufferBytes) {}

  /** @brief Encodes a JSON object into a compact `Content-Length: <N>\r\n\r\n<json>` wire frame. */
  static QByteArray encode(const QJsonObject& payload) {
    const QByteArray jsonBytes = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    QByteArray frame;
    frame.reserve(32 + jsonBytes.size());
    frame.append("Content-Length: ");
    frame.append(QByteArray::number(jsonBytes.size()));
    frame.append("\r\n\r\n");
    frame.append(jsonBytes);
    return frame;
  }

  /** @brief Configures custom frame and rolling-buffer byte limits (values `<= 0` keep current limits). */
  void setLimits(qsizetype maxFrameBytes, qsizetype maxBufferBytes) {
    if (maxFrameBytes > 0) m_maxFrameBytes = maxFrameBytes;
    if (maxBufferBytes > 0) m_maxBufferBytes = maxBufferBytes;
  }

  qsizetype maxFrameBytes() const { return m_maxFrameBytes; }
  qsizetype maxBufferBytes() const { return m_maxBufferBytes; }
  qsizetype bufferedBytes() const { return m_buffer.size(); }

  /** @brief Discards any partial buffered bytes and resets stream framing state. */
  void clear() { m_buffer.clear(); }

  /**
   * @brief Appends raw incoming bytes and extracts every complete valid frame in strict FIFO order.
   *
   * - Advances cleanly past `Content-Length: 0` headers instead of stalling.
   * - Rejects negative, non-numeric, or oversized `Content-Length` headers and resets corrupted streams.
   * - Caps rolling buffer growth at `m_maxBufferBytes` and unterminated headers at `kMaxHeaderBlockBytes`.
   */
  FeedResult feed(const QByteArray& chunk) {
    FeedResult result;
    if (chunk.isEmpty()) return result;

    if (chunk.size() > m_maxBufferBytes || m_buffer.size() > m_maxBufferBytes - chunk.size()) {
      result.errors.append(
          QStringLiteral("JSON-RPC receive buffer exceeded %1-byte limit; resetting stream")
              .arg(m_maxBufferBytes));
      m_buffer.clear();
      return result;
    }

    m_buffer.append(chunk);

    while (true) {
      const qsizetype headerEnd = m_buffer.indexOf("\r\n\r\n");
      if (headerEnd < 0) {
        if (m_buffer.size() > kMaxHeaderBlockBytes) {
          result.errors.append(
              QStringLiteral("JSON-RPC header block exceeded %1-byte limit without terminator; resetting stream")
                  .arg(kMaxHeaderBlockBytes));
          m_buffer.clear();
        }
        break;
      }

      const QByteArray headerBlock = m_buffer.left(headerEnd);
      bool hasContentLength = false;
      bool parseOk = false;
      qint64 contentLength = -1;

      for (const QByteArray& rawLine : headerBlock.split('\n')) {
        const QByteArray trimmed = rawLine.trimmed();
        if (trimmed.toLower().startsWith("content-length:")) {
          hasContentLength = true;
          contentLength = trimmed.mid(15).trimmed().toLongLong(&parseOk);
          break;
        }
      }

      if (!hasContentLength || !parseOk || contentLength < 0) {
        result.errors.append(QStringLiteral("Invalid or missing Content-Length header; discarding header block"));
        m_buffer.remove(0, headerEnd + 4);
        continue;
      }

      if (contentLength > static_cast<qint64>(m_maxFrameBytes)) {
        result.errors.append(
            QStringLiteral("JSON-RPC Content-Length %1 exceeds %2-byte frame limit; resetting stream")
                .arg(contentLength)
                .arg(m_maxFrameBytes));
        m_buffer.clear();
        break;
      }

      // Advance cleanly past empty frames so `Content-Length: 0` can never deadlock the parser.
      if (contentLength == 0) {
        m_buffer.remove(0, headerEnd + 4);
        continue;
      }

      const qsizetype bodyLen = static_cast<qsizetype>(contentLength);
      const qsizetype totalFrameBytes = headerEnd + 4 + bodyLen;
      if (m_buffer.size() < totalFrameBytes) {
        break;
      }

      result.payloads.append(m_buffer.mid(headerEnd + 4, bodyLen));
      m_buffer.remove(0, totalFrameBytes);
    }

    return result;
  }

 private:
  QByteArray m_buffer;
  qsizetype m_maxFrameBytes = kDefaultMaxFrameBytes;
  qsizetype m_maxBufferBytes = kDefaultMaxBufferBytes;
};

}  // namespace gridlock::core
