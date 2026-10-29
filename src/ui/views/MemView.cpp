/**
 * @file MemView.cpp
 * @brief Memory inspection, hex dump display, and multi-rank diff viewer.
 *
 * Implements the MemView widget providing synchronized hex dumping,
 * memory editing, multi-rank differential comparisons, and data matrix
 * export capabilities for HPC and binary analysis workloads.
 */

#include "MemView.hpp"
#include "../../core/managers/ConfigManager.hpp"
#include "../../core/hpc/MemoryExporter.hpp"
#include "../../core/hpc/MemoryDiffer.hpp"
#include <QComboBox>
#include <QFileDialog>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPalette>
#include <QPushButton>
#include <QSpinBox>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

namespace gridlock::ui {

/**
 * @brief Parses a string representation of a hex address into a 64-bit integer.
 *
 * Strips optional "0x" prefixes, trims whitespace, and stops at the first
 * space delimiter before parsing hexadecimal digits.
 *
 * @param address The input address string (e.g., "0x00401120").
 * @param okOut Optional pointer receiving true on successful conversion.
 * @return Parsed 64-bit address, or 0 on conversion failure.
 */
uint64_t MemView::parseHexAddress(const QString& address, bool* okOut) {
    // Trim leading/trailing whitespace
    QString trimmed = address.trimmed();

    bool isDecimal = false;
    if (trimmed.startsWith("0d", Qt::CaseInsensitive)) {
        trimmed = trimmed.mid(2);
        isDecimal = true;
    } else if (trimmed.startsWith("0x", Qt::CaseInsensitive)) {
        trimmed = trimmed.mid(2);
    }

    // Stop at any trailing comment or label offset separated by spaces
    int spaceIdx = trimmed.indexOf(' ');
    if (spaceIdx > 0) {
        trimmed = trimmed.left(spaceIdx);
    }

    // Convert hex string (or decimal) to 64-bit unsigned integer
    bool ok = false;
    uint64_t val = isDecimal ? trimmed.toULongLong(&ok, 10)
                             : trimmed.toULongLong(&ok, 16);
    if (!ok && !isDecimal) {
        val = trimmed.toULongLong(&ok, 10);
    }
    if (okOut) {
        *okOut = ok;
    }
    return ok ? val : 0ULL;
}

/**
 * @brief Constructs a new MemView widget.
 *
 * Initializes the compact toolbar controls (address, byte count, data type,
 * read/export/diff buttons) and the read-only monospace hex dump text editor.
 *
 * @param parent Optional parent widget.
 */
MemView::MemView(QWidget* parent) : QWidget(parent) {
    // Configure vertical root layout with zero margins for dock docking
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Build the compact slate toolbar header bar
    QWidget* headerBar = new QWidget(this);
    headerBar->setObjectName("cutterMemHeader");
    headerBar->setFixedHeight(26);
    headerBar->setStyleSheet(
        "#cutterMemHeader { background: #21252b; border-bottom: 1px solid #2c313a; }"
        "#cutterMemHeader QLabel { color: #abb2bf; font-size: 11px; font-weight: 600; }"
        "#cutterMemHeader QLineEdit, #cutterMemHeader QSpinBox, "
        "#cutterMemHeader QComboBox { min-height: 20px; max-height: 22px; "
        "padding: 0px 4px; margin: 0px; background: #16181d; color: #d7dae0; border: 1px solid #323844; border-radius: 2px; }"
        "#cutterMemHeader QPushButton { padding: 1px 8px; min-height: 20px; max-height: 22px; "
        "background: #252932; color: #d7dae0; border: 1px solid #323844; border-radius: 2px; font-size: 10px; font-weight: 600; }"
        "#cutterMemHeader QPushButton:hover { background: #2c323d; border: 1px solid #3e4452; color: #ffffff; }");
    QHBoxLayout* topLayout = new QHBoxLayout(headerBar);
    topLayout->setContentsMargins(6, 2, 6, 2);
    topLayout->setSpacing(4);

    // Helper lambda to measure button text and assign minimum width
    auto configureToolbarButton = [](QPushButton* btn) {
        QFont boldFont = btn->font();
        boldFont.setBold(true);
        QFontMetrics fm(boldFont);
        const int textW = std::max(
            fm.horizontalAdvance(btn->text()),
            fm.horizontalAdvance(btn->text().toUpper()));
        const int minW = textW + 20;
        btn->setMinimumWidth(minW);
        btn->setFixedHeight(22);
        btn->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    };

    topLayout->addWidget(new QLabel("Addr:"));
    m_addressEdit = new QLineEdit();
    m_addressEdit->setObjectName("cutterMemAddressEdit");
    m_addressEdit->setMinimumWidth(105);
    m_addressEdit->setFixedHeight(22);
    m_addressEdit->setText(QStringLiteral("0x00401120"));
    topLayout->addWidget(m_addressEdit);

    topLayout->addWidget(new QLabel("Bytes:"));
    m_lengthBox = new QSpinBox();
    m_lengthBox->setRange(1, 4096);
    m_lengthBox->setValue(256);
    m_lengthBox->setMinimumWidth(68);
    m_lengthBox->setFixedHeight(22);
    topLayout->addWidget(m_lengthBox);

    topLayout->addWidget(new QLabel("Type:"));
    m_typeBox = new QComboBox();
    m_typeBox->addItem("Int32");
    m_typeBox->addItem("Float");
    m_typeBox->addItem("Double");
    m_typeBox->setMinimumWidth(74);
    m_typeBox->setFixedHeight(22);
    topLayout->addWidget(m_typeBox);

    m_readBtn = new QPushButton("Read");
    configureToolbarButton(m_readBtn);
    topLayout->addWidget(m_readBtn);

    m_exportBtn = new QPushButton("Export");
    configureToolbarButton(m_exportBtn);
    topLayout->addWidget(m_exportBtn);

    topLayout->addWidget(new QLabel("Base:"));
    m_baselineRankBox = new QSpinBox();
    m_baselineRankBox->setRange(0, 1000);
    m_baselineRankBox->setMinimumWidth(56);
    m_baselineRankBox->setFixedHeight(22);
    topLayout->addWidget(m_baselineRankBox);

    topLayout->addWidget(new QLabel("Target:"));
    m_targetRankBox = new QSpinBox();
    m_targetRankBox->setRange(0, 1000);
    m_targetRankBox->setMinimumWidth(56);
    m_targetRankBox->setFixedHeight(22);
    topLayout->addWidget(m_targetRankBox);

    m_diffBtn = new QPushButton("Diff Ranks");
    configureToolbarButton(m_diffBtn);
    topLayout->addWidget(m_diffBtn);

    topLayout->addStretch();

    mainLayout->addWidget(headerBar);

    m_dumpEdit = new QTextEdit();
    m_dumpEdit->setObjectName("cutterHexDumpEdit");
    m_dumpEdit->setReadOnly(true);
    m_dumpEdit->setFrameShape(QFrame::NoFrame);
    QFont font("JetBrains Mono");
    font.setStyleHint(QFont::Monospace);
    font.setPointSize(10);
    m_dumpEdit->setFont(font);

    QPalette p = m_dumpEdit->palette();
    p.setColor(QPalette::Base, QColor(gridlock::core::ConfigManager::instance().getSourceBackground()));
    p.setColor(QPalette::Text, QColor(gridlock::core::ConfigManager::instance().getSourceText()));
    m_dumpEdit->setPalette(p);

    mainLayout->addWidget(m_dumpEdit, 1);

    connect(m_readBtn, &QPushButton::clicked, this, &MemView::onReadClicked);
    connect(m_addressEdit, &QLineEdit::returnPressed, this, &MemView::onReadClicked);
    connect(m_exportBtn, &QPushButton::clicked, this, &MemView::onExportMatrixClicked);
    connect(m_diffBtn, &QPushButton::clicked, this, &MemView::onDiffClicked);

    // Seed with a realistic 64-byte x86_64 prologue & MPI frame preview at 0x00401120
    const QByteArray seedBytes = QByteArray::fromHex(
        "f30f1efa554889e54881ec9000000048"
        "8dbd78ffffff488db570ffffffe80efe"
        "ffffc745fc00000000837dfc027f298b"
        "45fc83f8007411f20f1005a90e000000");
    setMemoryData(QStringLiteral("0x00401120"), seedBytes);
    // Reset diff baseline after seeding so the first test or live update starts cleanly
    m_hasDiffBaseline = false;
    m_previousRawData.clear();
    m_changedByteCount = 0;
}

/**
 * @brief Seeks the memory view to a specific 64-bit memory address.
 *
 * Updates the address text box and emits a requestMemory signal to query the
 * underlying process memory subsystem.
 *
 * @param address The target 64-bit address.
 * @return True if the address was non-zero and seek was dispatched; false otherwise.
 */
bool MemView::seekToAddress(uint64_t address) {
    if (address == 0ULL) {
        return false;
    }
    m_currentBaseAddress = address;
    const QString formatted = QStringLiteral("0x%1").arg(address, 8, 16, QChar('0'));
    if (m_addressEdit) {
        m_addressEdit->setText(formatted);
    }
    // Dispatched memory read request for the specified byte length
    emit requestMemory(formatted, m_lengthBox ? m_lengthBox->value() : 256);
    return true;
}

/**
 * @brief Handles user clicks on the Read button or pressing Enter in the address input.
 *
 * Parses the user-entered address string and fires the requestMemory signal.
 */
void MemView::onReadClicked() {
    QString addr = m_addressEdit->text().trimmed();
    if (!addr.isEmpty()) {
        bool ok = false;
        uint64_t parsed = parseHexAddress(addr, &ok);
        if (ok && parsed != 0ULL) {
            m_currentBaseAddress = parsed;
        }
        emit requestMemory(addr, m_lengthBox->value());
    }
}

/**
 * @brief Populates the hex dump display with raw hexadecimal memory contents.
 *
 * Compares incoming bytes against the previous snapshot to detect mutated
 * bytes, highlights changed values in red, and updates the HTML render.
 *
 * @param beginAddress 64-bit base address of the memory slice.
 * @param hexContents Hex-encoded string of byte values.
 */
void MemView::setMemoryData(qint64 beginAddress, const QString& hexContents) {
    const uint64_t startAddr = static_cast<uint64_t>(beginAddress);
    const QByteArray currentData = QByteArray::fromHex(hexContents.toLatin1());

    m_changedByteCount = 0;
    QByteArray prevForDiff;

    // Detect changed bytes if a previous snapshot at the same address exists
    if (m_hasDiffBaseline && m_previousBaseAddress == startAddr) {
        prevForDiff = m_previousRawData;
        const int compareLen = qMin(currentData.size(), m_previousRawData.size());
        for (int i = 0; i < compareLen; ++i) {
            if (currentData[i] != m_previousRawData[i]) {
                m_changedByteCount++;
            }
        }
        // Account for any difference in slice length
        m_changedByteCount += qAbs(currentData.size() - m_previousRawData.size());
    }

    // Cache current state as new diff baseline
    m_currentBaseAddress = startAddr;
    m_previousBaseAddress = startAddr;
    m_previousRawData = currentData;
    m_lastRawData = currentData;
    m_unpatchedRawData = currentData;
    m_hasDiffBaseline = true;

    if (m_addressEdit) {
        m_addressEdit->setText(QStringLiteral("0x%1").arg(startAddr, 8, 16, QChar('0')));
    }

    // Render formatted monospace HTML with differential color tags
    if (m_dumpEdit) {
        m_dumpEdit->setHtml(formatHexDumpHtml(startAddr, currentData, prevForDiff));
    }
}

/**
 * @brief Applies live byte patches at specified virtual memory address.
 * @param address 64-bit target memory address.
 * @param newBytes Byte array to write.
 * @return True if byte payload was non-empty and recorded; false otherwise.
 */
bool MemView::patchBytes(uint64_t address, const QByteArray& newBytes) {
    if (newBytes.isEmpty()) {
        return false;
    }
    // Record each byte into active patch map
    for (int i = 0; i < newBytes.size(); ++i) {
        m_patchedBytes.insert(address + static_cast<uint64_t>(i), newBytes.at(i));
    }
    // If target address falls within current viewport slice, update local buffer
    if (!m_lastRawData.isEmpty() && address >= m_currentBaseAddress &&
        address < m_currentBaseAddress + static_cast<uint64_t>(m_lastRawData.size())) {
        const uint64_t offset = address - m_currentBaseAddress;
        const int copyLen = qMin(newBytes.size(), static_cast<int>(m_lastRawData.size() - offset));
        for (int i = 0; i < copyLen; ++i) {
            m_lastRawData[static_cast<qsizetype>(offset + i)] = newBytes.at(i);
        }
        if (m_dumpEdit) {
            m_dumpEdit->setHtml(formatHexDumpHtml(m_currentBaseAddress, m_lastRawData, m_previousRawData));
        }
    }
    emit bytesPatched(address, newBytes);
    return true;
}

/**
 * @brief Reverts all actively applied live byte patches and restores baseline memory buffer.
 */
void MemView::revertAllPatches() {
    m_patchedBytes.clear();
    if (!m_unpatchedRawData.isEmpty()) {
        m_lastRawData = m_unpatchedRawData;
        if (m_dumpEdit) {
            m_dumpEdit->setHtml(formatHexDumpHtml(m_currentBaseAddress, m_lastRawData, m_previousRawData));
        }
    }
}

/**
 * @brief Overloaded helper to set memory data from raw byte arrays.
 *
 * @param address Address string (e.g. "0x00401120").
 * @param data Raw byte array.
 */
void MemView::setMemoryData(const QString& address, const QByteArray& data) {
    bool ok = false;
    uint64_t startAddr = parseHexAddress(address, &ok);
    if (!ok) {
        startAddr = m_currentBaseAddress;
    }
    setMemoryData(static_cast<qint64>(startAddr), QString::fromLatin1(data.toHex()));
}

/**
 * @brief Exports currently cached memory buffer to disk as CSV or raw binary.
 *
 * Prompts user for a save file path and invokes MemoryExporter asynchronously.
 */
void MemView::onExportMatrixClicked() {
    if (m_lastRawData.isEmpty()) {
        QMessageBox::warning(this, "No Data", "No memory data available to export. Please read memory first.");
        return;
    }

    QString fileName = QFileDialog::getSaveFileName(this, "Export Memory Matrix", "", "CSV Files (*.csv);;Binary Files (*.bin);;All Files (*)");
    if (fileName.isEmpty()) return;

    std::vector<uint8_t> buffer(m_lastRawData.begin(), m_lastRawData.end());
    int typeIndex = m_typeBox->currentIndex();

    // Run file export in background thread pool to avoid blocking the GUI
    (void)QtConcurrent::run([buffer, fileName, typeIndex]() {
        if (fileName.endsWith(".csv", Qt::CaseInsensitive)) {
            gridlock::core::hpc::DataType type = gridlock::core::hpc::DataType::Int32;
            int typeSize = sizeof(int32_t);
            
            if (typeIndex == 1) {
                type = gridlock::core::hpc::DataType::Float;
                typeSize = sizeof(float);
            } else if (typeIndex == 2) {
                type = gridlock::core::hpc::DataType::Double;
                typeSize = sizeof(double);
            }
            
            int cols = buffer.size() / typeSize;
            if (cols == 0) cols = 1;
            gridlock::core::hpc::MemoryExporter::exportToCsv(fileName.toStdString(), buffer, 1, cols, type);
        } else {
            gridlock::core::hpc::MemoryExporter::exportToBinary(fileName.toStdString(), buffer);
        }
    });
}

/**
 * @brief Triggers a cross-rank memory differential analysis.
 *
 * Validates the entered address and emits the requestMemoryDiff signal.
 */
void MemView::onDiffClicked() {
    int baselineRank = m_baselineRankBox->value();
    int targetRank = m_targetRankBox->value();
    QString addrStr = m_addressEdit->text().trimmed();
    size_t length = m_lengthBox->value();
    
    bool ok = false;
    (void)parseHexAddress(addrStr, &ok);
    if (!ok) {
        QMessageBox::warning(this, "Invalid Address", "Please enter a valid hex address.");
        return;
    }
    
    emit requestMemoryDiff(baselineRank, targetRank, addrStr, length);
}

/**
 * @brief Renders the visual diff output between two MPI ranks into the hex editor.
 *
 * Highlights divergence between baseline and target memory buffers with red spans
 * and tooltips indicating baseline values.
 *
 * @param result The differential comparison result structure.
 * @param remoteAddr Base pointer of the compared memory slice.
 */
void MemView::displayMemoryDiff(const gridlock::core::hpc::CompareResult& result, void* remoteAddr) {
    if (!result.success) {
        QMessageBox::warning(this, "Diff Failed", "Failed to read memory from one or both PIDs.");
        return;
    }
    
    QString html = "<pre style=\"font-family: 'JetBrains Mono', monospace; font-size: 10pt; margin: 0;\">";
    qint64 startAddress = reinterpret_cast<qint64>(remoteAddr);
    
    int byteCount = result.baselineBuffer.size();
    m_changedByteCount = 0;
    // Iterate in 16-byte chunks representing traditional hex dump rows
    for (int i = 0; i < byteCount; i += 16) {
        html += QString("<span style=\"color: #6a9955;\">0x%1</span>  ")
                    .arg(QString::number((quint64)(startAddress + i), 16).rightJustified(16, '0').toUpper());
        
        QString asciiPart = "| ";
        for (int j = 0; j < 16; ++j) {
            if (i + j < byteCount) {
                uint8_t baseByte = result.baselineBuffer[i+j];
                uint8_t targetByte = result.targetBuffer[i+j];
                bool diff = result.diffMask[i+j];
                if (diff) {
                    m_changedByteCount++;
                }
                
                QString byteStr = QString::number(targetByte, 16).rightJustified(2, '0').toUpper();
                
                // Color mutated bytes in red with tooltip, muted zeros in gray
                if (diff) {
                    QString tooltip = QString("Baseline: 0x%1 | Target: 0x%2")
                        .arg(QString::number(baseByte, 16).rightJustified(2, '0').toUpper())
                        .arg(byteStr);
                    html += QString("<span style=\"color: #f44747; font-weight: bold;\" title=\"%1\">%2</span> ")
                        .arg(tooltip).arg(byteStr);
                } else if (targetByte == 0) {
                    html += QString("<span style=\"color: #555c66;\">%1</span> ").arg(byteStr);
                } else {
                    html += byteStr + " ";
                }
                
                // Printable ASCII representation
                char c = static_cast<char>(targetByte);
                if (c >= 32 && c <= 126) {
                    if (diff) {
                        asciiPart += QString("<span style=\"color: #f44747; font-weight: bold;\">%1</span>").arg(QString(c).toHtmlEscaped());
                    } else {
                        asciiPart += QString(c).toHtmlEscaped();
                    }
                } else {
                    asciiPart += "<span style=\"color: #555c66;\">.</span>";
                }
            } else {
                html += "   ";
            }
        }
        html += " " + asciiPart + " |<br>";
    }
    html += "</pre>";
    
    m_dumpEdit->setHtml(html);
    m_lastRawData = QByteArray(reinterpret_cast<const char*>(result.targetBuffer.data()), result.targetBuffer.size());
}

/**
 * @brief Generates HTML-formatted hex dump with offset headers, syntax colors, and mutation diffs.
 *
 * @param startAddress 64-bit base offset for the first row.
 * @param currentData Current byte array contents.
 * @param prevData Previous snapshot byte array used for mutation highlighting.
 * @return HTML string ready for display in QTextEdit.
 */
QString MemView::formatHexDumpHtml(uint64_t startAddress, const QByteArray& currentData, const QByteArray& prevData) {
    // Monospace header with offset markers 00 through 0F
    QString html = QStringLiteral(
        "<pre style=\"font-family: 'JetBrains Mono', monospace; font-size: 10pt; margin: 4px;\">"
        "<span style=\"color: #858585;\">- offset -         00 01 02 03 04 05 06 07  08 09 0A 0B 0C 0D 0E 0F  |0123456789ABCDEF|</span>\n");

    const int byteCount = currentData.size();
    // Format 16 bytes per line
    for (int i = 0; i < byteCount; i += 16) {
        // Line address prefix in muted green
        html += QStringLiteral("<span style=\"color: #6a9955;\">0x%1</span> ")
                    .arg(startAddress + static_cast<uint64_t>(i), 16, 16, QChar('0'));

        QString asciiPart = QStringLiteral("|");
        for (int j = 0; j < 16; ++j) {
            // Extra spacing at 8-byte boundary for readability
            if (j == 8) {
                html += QStringLiteral(" ");
            }
            const int idx = i + j;
            if (idx < byteCount) {
                const uint64_t byteAddr = startAddress + static_cast<uint64_t>(idx);
                const bool isPatched = m_patchedBytes.contains(byteAddr);
                const uint8_t b = static_cast<uint8_t>(currentData[idx]);
                // Check if byte value mutated compared to previous snapshot
                const bool mutated = !prevData.isEmpty() && idx < prevData.size() &&
                                     static_cast<uint8_t>(prevData[idx]) != b;
                const QString byteStr = QStringLiteral("%1").arg(b, 2, 16, QChar('0')).toUpper();

                if (isPatched) {
                    html += QStringLiteral("<span style=\"color: #e5c07b; font-weight: bold;\">%1</span> ").arg(byteStr);
                } else if (mutated) {
                    html += QStringLiteral("<span style=\"color: #f44747; font-weight: bold;\">%1</span> ").arg(byteStr);
                } else if (b == 0x00) {
                    html += QStringLiteral("<span style=\"color: #555c66;\">%1</span> ").arg(byteStr);
                } else {
                    html += QStringLiteral("<span style=\"color: #d4d7dc;\">%1</span> ").arg(byteStr);
                }

                // Append printable character or dot for non-printables
                const char c = static_cast<char>(b);
                if (c >= 32 && c <= 126) {
                    const QString escaped = QString(QChar(c)).toHtmlEscaped();
                    if (isPatched) {
                        asciiPart += QStringLiteral("<span style=\"color: #e5c07b; font-weight: bold;\">%1</span>").arg(escaped);
                    } else if (mutated) {
                        asciiPart += QStringLiteral("<span style=\"color: #f44747; font-weight: bold;\">%1</span>").arg(escaped);
                    } else {
                        asciiPart += QStringLiteral("<span style=\"color: #4ec9b0;\">%1</span>").arg(escaped);
                    }
                } else {
                    if (isPatched) {
                        asciiPart += QStringLiteral("<span style=\"color: #e5c07b;\">.</span>");
                    } else {
                        asciiPart += QStringLiteral("<span style=\"color: #555c66;\">.</span>");
                    }
                }
            } else {
                html += QStringLiteral("   ");
                asciiPart += QStringLiteral(" ");
            }
        }
        asciiPart += QStringLiteral("|");
        html += QStringLiteral(" ") + asciiPart + QStringLiteral("\n");
    }

    html += QStringLiteral("</pre>");
    return html;
}

/**
 * @brief Generates a plain-text hex dump representation of hexadecimal contents.
 *
 * @param startAddress 64-bit base offset.
 * @param hexData String containing raw hex characters.
 * @return Plain text hex dump formatted with offsets and ASCII column.
 */
QString MemView::formatHexDump(qint64 startAddress, const QString& hexData) {
    QString result;
    int dataLen = hexData.length();
    int byteCount = dataLen / 2;
    
    // Group hex digits into 16-byte lines
    for (int i = 0; i < byteCount; i += 16) {
        result += QString("0x%1: ").arg(QString::number((quint64)(startAddress + i), 16).rightJustified(16, '0').toUpper());
        
        QString asciiPart = "| ";
        for (int j = 0; j < 16; ++j) {
            if (i + j < byteCount) {
                QString byteStr = hexData.mid((i + j) * 2, 2);
                result += byteStr.toUpper() + " ";
                
                bool ok;
                char c = byteStr.toInt(&ok, 16);
                if (c >= 32 && c <= 126) {
                    asciiPart += c;
                } else {
                    asciiPart += '.';
                }
            } else {
                result += "   ";
            }
        }
        
        asciiPart += " |";
        result += asciiPart + "\n";
    }
    
    return result;
}

}  // namespace gridlock::ui

