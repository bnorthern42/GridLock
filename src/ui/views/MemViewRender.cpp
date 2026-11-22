/**
 * @file MemViewRender.cpp
 * @brief HTML/plain-text hex dump formatting, cross-rank diff rendering, styling, and byte-offset lookup for MemView.
 *
 * Implements setMemoryData() snapshot diffing, displayMemoryDiff() cross-rank HTML spans,
 * formatHexDumpHtml() / formatHexDump(), ThemeManager palette synchronization, and
 * cursor/viewport coordinate-to-byte-offset / 64-bit word extraction.
 */

#include "MemView.hpp"

#include <algorithm>
#include <cstring>

#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPalette>
#include <QTextCursor>
#include <QTextEdit>

#include "../../core/managers/ThemeManager.hpp"
#include "MemoryProfilerWidget.hpp"

namespace gridlock::ui {

/** @brief Updates the toolbar source indicator badge (`IDLE (NO BINARY)`, `STATIC ELF`, `LIVE RANK`). */
void MemView::setSourceModeLabel(const QString& label) {
    if (!m_sourceBadge) return;
    m_sourceBadge->setText(label);
    const auto pal = gridlock::core::ThemeManager::instance().paletteForTheme(
        gridlock::core::ThemeManager::instance().activeThemeId());
    const bool isLive = label.startsWith(QStringLiteral("LIVE RANK"), Qt::CaseInsensitive);
    const bool isStatic = label.startsWith(QStringLiteral("STATIC ELF"), Qt::CaseInsensitive);
    const QString fg = isLive ? QStringLiteral("#4ec9b0") : (isStatic ? QStringLiteral("#61afef") : pal.textHex);
    m_sourceBadge->setStyleSheet(QStringLiteral(
        "QLabel#memSourceBadge { color: %1; border: 1px solid %2; border-radius: 2px; "
        "padding: 1px 6px; font-family: 'JetBrains Mono', monospace; font-size: 10px; font-weight: 700; }")
        .arg(fg, pal.borderHex));
}

/** @brief Populates the hex dump display with raw hexadecimal memory contents and highlights mutated bytes. */
void MemView::setMemoryData(qint64 beginAddress, const QString& hexContents) {
    const uint64_t startAddr = static_cast<uint64_t>(beginAddress);
    const QByteArray currentData = QByteArray::fromHex(hexContents.toLatin1());

    m_changedByteCount = 0;
    QByteArray prevForDiff;

    if (m_hasDiffBaseline && m_previousBaseAddress == startAddr) {
        prevForDiff = m_previousRawData;
        const int compareLen = qMin(currentData.size(), m_previousRawData.size());
        for (int i = 0; i < compareLen; ++i) {
            if (currentData[i] != m_previousRawData[i]) m_changedByteCount++;
        }
        m_changedByteCount += qAbs(currentData.size() - m_previousRawData.size());
    }

    m_currentBaseAddress = startAddr;
    m_previousBaseAddress = startAddr;
    m_previousRawData = currentData;
    m_lastRawData = currentData;
    m_unpatchedRawData = currentData;
    m_hasDiffBaseline = true;

    if (m_addressEdit) m_addressEdit->setText(QStringLiteral("0x%1").arg(startAddr, 8, 16, QChar('0')));
    if (m_dumpEdit) m_dumpEdit->setHtml(formatHexDumpHtml(startAddr, currentData, prevForDiff));
}

/** @brief Overloaded helper to set memory data from raw byte arrays. */
void MemView::setMemoryData(const QString& address, const QByteArray& data) {
    bool ok = false;
    uint64_t startAddr = parseHexAddress(address, &ok);
    if (!ok) startAddr = m_currentBaseAddress;
    setMemoryData(static_cast<qint64>(startAddr), QString::fromLatin1(data.toHex()));
}

/** @brief Renders the visual diff output between two MPI ranks into the hex editor. */
void MemView::displayMemoryDiff(const gridlock::core::hpc::CompareResult& result, void* remoteAddr) {
    if (!result.success) {
        QMessageBox::warning(this, "Diff Failed", "Failed to read memory from one or both PIDs.");
        return;
    }

    QString html = "<pre style=\"font-family: 'JetBrains Mono', monospace; font-size: 10pt; margin: 0;\">";
    const qint64 startAddress = reinterpret_cast<qint64>(remoteAddr);

    const bool isDark = gridlock::core::ThemeManager::instance().isDarkMode();
    const QString offsetHex = isDark ? QStringLiteral("#6a9955") : QStringLiteral("#0550ae");
    const QString zeroHex = isDark ? QStringLiteral("#555c66") : QStringLiteral("#6e7781");
    const QString nonZeroHex = isDark ? QStringLiteral("#d4d7dc") : QStringLiteral("#1f2328");
    const QString asciiHex = isDark ? QStringLiteral("#4ec9b0") : QStringLiteral("#1f2328");

    const int byteCount = static_cast<int>(result.baselineBuffer.size());
    m_changedByteCount = 0;
    for (int i = 0; i < byteCount; i += 16) {
        html += QString("<span style=\"color: %1;\">0x%2</span>  ")
                    .arg(offsetHex, QString::number(static_cast<quint64>(startAddress + i), 16).rightJustified(16, '0').toUpper());

        QString asciiPart = "| ";
        for (int j = 0; j < 16; ++j) {
            if (i + j < byteCount) {
                const uint8_t baseByte = result.baselineBuffer[i + j];
                const uint8_t targetByte = result.targetBuffer[i + j];
                const bool diff = result.diffMask[i + j];
                if (diff) m_changedByteCount++;

                const QString byteStr = QString::number(targetByte, 16).rightJustified(2, '0').toUpper();
                if (diff) {
                    const QString tooltip = QString("Baseline: 0x%1 | Target: 0x%2")
                                                .arg(QString::number(baseByte, 16).rightJustified(2, '0').toUpper(), byteStr);
                    html += QString("<span style=\"color: #f44747; font-weight: bold;\" title=\"%1\">%2</span> ").arg(tooltip, byteStr);
                } else if (targetByte == 0) {
                    html += QString("<span style=\"color: %1;\">%2</span> ").arg(zeroHex, byteStr);
                } else {
                    html += QString("<span style=\"color: %1; font-weight: 500;\">%2</span> ").arg(nonZeroHex, byteStr);
                }

                const char c = static_cast<char>(targetByte);
                if (c >= 32 && c <= 126) {
                    asciiPart += diff ? QString("<span style=\"color: #f44747; font-weight: bold;\">%1</span>").arg(QString(c).toHtmlEscaped())
                                      : QString("<span style=\"color: %1;\">%2</span>").arg(asciiHex, QString(c).toHtmlEscaped());
                } else {
                    asciiPart += QString("<span style=\"color: %1;\">.</span>").arg(zeroHex);
                }
            } else {
                html += "   ";
            }
        }
        html += " " + asciiPart + " |<br>";
    }
    html += "</pre>";

    m_dumpEdit->setHtml(html);
    m_lastRawData = QByteArray(reinterpret_cast<const char*>(result.targetBuffer.data()), static_cast<qsizetype>(result.targetBuffer.size()));
}

/** @brief Generates HTML-formatted hex dump with offset headers, syntax colors, patches, and mutation diffs. */
QString MemView::formatHexDumpHtml(uint64_t startAddress, const QByteArray& currentData, const QByteArray& prevData) {
    const auto& tm = gridlock::core::ThemeManager::instance();
    const auto pal = tm.paletteForTheme(tm.activeThemeId());
    const bool isDark = pal.isDark;
    const QString headerColor = isDark ? QStringLiteral("#858585") : QStringLiteral("#57606a");
    const QString offsetColor = isDark ? QStringLiteral("#6a9955") : QStringLiteral("#0550ae");
    const QString zeroColor = isDark ? QStringLiteral("#555c66") : QStringLiteral("#6e7781");
    const QString hexColor = isDark ? QStringLiteral("#d4d7dc") : QStringLiteral("#1f2328");
    const QString asciiColor = isDark ? QStringLiteral("#4ec9b0") : QStringLiteral("#1f2328");
    const QString patchColor = isDark ? QStringLiteral("#e5c07b") : QStringLiteral("#9a6700");

    QString html = QStringLiteral(
        "<pre style=\"font-family: 'JetBrains Mono', monospace; font-size: 10pt; margin: 4px;\">"
        "<span style=\"color: %1;\">- offset -         00 01 02 03 04 05 06 07  08 09 0A 0B 0C 0D 0E 0F  |0123456789ABCDEF|</span>\n")
        .arg(headerColor);

    const int byteCount = currentData.size();
    for (int i = 0; i < byteCount; i += 16) {
        html += QStringLiteral("<span style=\"color: %1;\">0x%2</span> ")
                    .arg(offsetColor)
                    .arg(startAddress + static_cast<uint64_t>(i), 16, 16, QChar('0'));

        QString asciiPart = QStringLiteral("|");
        for (int j = 0; j < 16; ++j) {
            if (j == 8) html += QStringLiteral(" ");
            const int idx = i + j;
            if (idx < byteCount) {
                const uint64_t byteAddr = startAddress + static_cast<uint64_t>(idx);
                const bool isPatched = m_patchedBytes.contains(byteAddr);
                const uint8_t b = static_cast<uint8_t>(currentData[idx]);
                const bool mutated = !prevData.isEmpty() && idx < prevData.size() && static_cast<uint8_t>(prevData[idx]) != b;
                const QString byteStr = QStringLiteral("%1").arg(b, 2, 16, QChar('0')).toUpper();

                if (isPatched) html += QStringLiteral("<span style=\"color: %1; font-weight: bold;\">%2</span> ").arg(patchColor, byteStr);
                else if (mutated) html += QStringLiteral("<span style=\"color: #f44747; font-weight: bold;\">%1</span> ").arg(byteStr);
                else if (b == 0x00) html += QStringLiteral("<span style=\"color: %1;\">%2</span> ").arg(zeroColor, byteStr);
                else html += isDark ? QStringLiteral("<span style=\"color: %1;\">%2</span> ").arg(hexColor, byteStr)
                                    : QStringLiteral("<span style=\"color: %1; font-weight: 500;\">%2</span> ").arg(hexColor, byteStr);

                const char c = static_cast<char>(b);
                if (c >= 32 && c <= 126) {
                    const QString escaped = QString(QChar(c)).toHtmlEscaped();
                    if (isPatched) asciiPart += QStringLiteral("<span style=\"color: %1; font-weight: bold;\">%2</span>").arg(patchColor, escaped);
                    else if (mutated) asciiPart += QStringLiteral("<span style=\"color: #f44747; font-weight: bold;\">%1</span>").arg(escaped);
                    else asciiPart += QStringLiteral("<span style=\"color: %1;\">%2</span>").arg(asciiColor, escaped);
                } else {
                    asciiPart += QStringLiteral("<span style=\"color: %1;\">.</span>").arg(isPatched ? patchColor : zeroColor);
                }
            } else {
                html += QStringLiteral("   "); asciiPart += QStringLiteral(" ");
            }
        }
        asciiPart += QStringLiteral("|");
        html += QStringLiteral(" ") + asciiPart + QStringLiteral("\n");
    }

    html += QStringLiteral("</pre>");
    return html;
}

/** @brief Generates a plain-text hex dump representation of hexadecimal contents. */
QString MemView::formatHexDump(qint64 startAddress, const QString& hexData) {
    QString result;
    const int dataLen = hexData.length(), byteCount = dataLen / 2;
    for (int i = 0; i < byteCount; i += 16) {
        result += QString("0x%1: ").arg(QString::number(static_cast<quint64>(startAddress + i), 16).rightJustified(16, '0').toUpper());
        QString asciiPart = "| ";
        for (int j = 0; j < 16; ++j) {
            if (i + j < byteCount) {
                const QString byteStr = hexData.mid((i + j) * 2, 2);
                result += byteStr.toUpper() + " ";
                bool ok = false; const char c = static_cast<char>(byteStr.toInt(&ok, 16));
                asciiPart += (c >= 32 && c <= 126) ? c : '.';
            } else {
                result += "   ";
            }
        }
        result += asciiPart + " |\n";
    }
    return result;
}

QColor MemView::activeCanvasBackgroundColor() const {
    if (m_dumpEdit) return m_dumpEdit->palette().color(QPalette::Base);
    const auto pal = gridlock::core::ThemeManager::instance().paletteForTheme(gridlock::core::ThemeManager::instance().activeThemeId());
    return QColor(pal.canvasHex);
}

QColor MemView::activeHexTextColor() const {
    return gridlock::core::ThemeManager::instance().isDarkMode() ? QColor(QStringLiteral("#d4d7dc")) : QColor(QStringLiteral("#1f2328"));
}

void MemView::paintEvent(QPaintEvent* event) {
    const QString activeTheme = gridlock::core::ThemeManager::instance().activeThemeId();
    if (m_lastStyledThemeId != activeTheme) reloadStyle();
    QWidget::paintEvent(event);
}

/** @brief Reloads hex dump viewport and control header styles from ThemeManager. */
void MemView::reloadStyle() {
    auto& tm = gridlock::core::ThemeManager::instance();
    m_lastStyledThemeId = tm.activeThemeId();
    const auto pal = tm.paletteForTheme(m_lastStyledThemeId);
    if (m_dumpEdit) {
        QPalette p = m_dumpEdit->palette();
        p.setColor(QPalette::Base, QColor(pal.canvasHex));
        p.setColor(QPalette::Window, QColor(pal.canvasHex));
        p.setColor(QPalette::Text, activeHexTextColor());
        m_dumpEdit->setPalette(p);
        m_dumpEdit->setStyleSheet(QStringLiteral(
            "QTextEdit#cutterHexDumpEdit { background-color: %1; color: %2; border: none; }")
            .arg(pal.canvasHex, activeHexTextColor().name()));
        if (m_dumpEdit->viewport()) {
            QPalette vp = m_dumpEdit->viewport()->palette();
            vp.setColor(QPalette::Base, QColor(pal.canvasHex));
            vp.setColor(QPalette::Window, QColor(pal.canvasHex));
            vp.setColor(QPalette::Text, activeHexTextColor());
            m_dumpEdit->viewport()->setPalette(vp);
            m_dumpEdit->viewport()->setAutoFillBackground(true);
        }
        if (!m_lastRawData.isEmpty()) {
            m_dumpEdit->setHtml(formatHexDumpHtml(m_currentBaseAddress, m_lastRawData, m_previousRawData));
        }
    }
    if (QWidget* headerBar = findChild<QWidget*>(QStringLiteral("cutterMemHeader"))) {
        headerBar->setStyleSheet(QStringLiteral(
            "#cutterMemHeader { background-color: %1; border-bottom: 1px solid %2; } "
            "#cutterMemHeader QLabel { color: %3; font-size: 11px; font-weight: 600; } "
            "#cutterMemHeader QLineEdit, #cutterMemHeader QSpinBox, #cutterMemHeader QComboBox { "
            "min-height: 20px; max-height: 22px; padding: 0px 4px; margin: 0px; background-color: %4; color: %3; "
            "border: 1px solid %2; border-radius: 2px; } "
            "#cutterMemHeader QPushButton { padding: 1px 8px; min-height: 20px; max-height: 22px; "
            "background-color: %1; color: %3; border: 1px solid %2; border-radius: 2px; font-size: 10px; font-weight: 600; } "
            "#cutterMemHeader QPushButton:hover { background-color: %2; color: %3; } "
            "#cutterMemHeader QPushButton:checked { background-color: %5; color: #ffffff; border: 1px solid %5; }")
            .arg(pal.headerHex, pal.borderHex, pal.textHex, pal.canvasHex, pal.accentHex));
    }
    if (m_sourceBadge) setSourceModeLabel(m_sourceBadge->text());
    if (m_profilerWidget) m_profilerWidget->reloadStyle();
    update();
}

int MemView::byteOffsetAtCursor(const QTextCursor& cursor) const {
    if (!m_dumpEdit || m_lastRawData.isEmpty()) return 0;
    const QString fullText = m_dumpEdit->toPlainText();
    const int docPos = std::clamp(cursor.position(), 0, static_cast<int>(fullText.size()));
    const int nlPrev = fullText.lastIndexOf(QLatin1Char('\n'), std::max(0, docPos - 1));
    const int lineStart = (nlPrev < 0) ? 0 : (nlPrev + 1);
    const int nlNext = fullText.indexOf(QLatin1Char('\n'), docPos);
    const QString line = fullText.mid(lineStart, ((nlNext < 0) ? fullText.size() : nlNext) - lineStart);
    const int col = docPos - lineStart, pipeIdx = line.indexOf(QLatin1Char('|'));
    const bool isDataRow = line.startsWith(QLatin1String("0x"), Qt::CaseInsensitive);
    const int hexStart = isDataRow ? (line.indexOf(QLatin1Char(' ')) + 1) : 19;
    int rowOffset = 0, byteInRow = 0;
    if (isDataRow && hexStart > 3) { bool ok = false; const uint64_t a = parseHexAddress(line.left(hexStart - 1), &ok); if (ok && a >= m_currentBaseAddress) rowOffset = static_cast<int>(a - m_currentBaseAddress); }
    if (pipeIdx >= 0 && col > pipeIdx && col <= pipeIdx + 16) byteInRow = col - (pipeIdx + 1);
    else if (hexStart > 0 && col >= hexStart) {
        const int rel = col - hexStart;
        const bool midGap = (line.mid(hexStart + 24, 1) == QLatin1String(" "));
        byteInRow = midGap ? ((rel < 24) ? (rel / 3) : ((rel == 24) ? 8 : (8 + (rel - 25) / 3))) : (rel / 3);
    }
    return std::clamp(rowOffset + std::clamp(byteInRow, 0, 15), 0, static_cast<int>(m_lastRawData.size()) - 1);
}

int MemView::byteOffsetAtPosition(const QPoint& pos) const {
    return m_dumpEdit ? byteOffsetAtCursor(m_dumpEdit->cursorForPosition(pos)) : 0;
}

QString MemView::wordHexAtByteOffset(int byteOffset, int* outBitWidth) const {
    if (outBitWidth) *outBitWidth = 0;
    if (byteOffset < 0 || byteOffset >= m_lastRawData.size()) return QString();
    const int rem = static_cast<int>(m_lastRawData.size()) - byteOffset;
    if (rem >= 8) { uint64_t w = 0; std::memcpy(&w, m_lastRawData.constData() + byteOffset, 8); if (outBitWidth) *outBitWidth = 64; return QStringLiteral("0x") + QString::number(w, 16).toUpper(); }
    if (rem >= 4) { uint32_t w = 0; std::memcpy(&w, m_lastRawData.constData() + byteOffset, 4); if (outBitWidth) *outBitWidth = 32; return QStringLiteral("0x") + QString::number(w, 16).toUpper(); }
    return QString();
}

}  // namespace gridlock::ui
