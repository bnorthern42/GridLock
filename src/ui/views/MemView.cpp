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

uint64_t MemView::parseHexAddress(const QString& address, bool* okOut) {
    QString trimmed = address.trimmed();
    if (trimmed.startsWith("0x", Qt::CaseInsensitive)) {
        trimmed = trimmed.mid(2);
    }
    int spaceIdx = trimmed.indexOf(' ');
    if (spaceIdx > 0) {
        trimmed = trimmed.left(spaceIdx);
    }
    bool ok = false;
    uint64_t val = trimmed.toULongLong(&ok, 16);
    if (okOut) {
        *okOut = ok;
    }
    return ok ? val : 0ULL;
}

MemView::MemView(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    QWidget* headerBar = new QWidget(this);
    headerBar->setObjectName("cutterMemHeader");
    headerBar->setStyleSheet(
        "#cutterMemHeader QLineEdit, #cutterMemHeader QSpinBox, "
        "#cutterMemHeader QComboBox { min-height: 22px; max-height: 24px; "
        "padding: 0px 4px; margin: 0px; }"
        "#cutterMemHeader QPushButton { padding: 2px 8px; min-height: 20px; }");
    QHBoxLayout* topLayout = new QHBoxLayout(headerBar);
    topLayout->setContentsMargins(6, 3, 6, 3);
    topLayout->setSpacing(4);

    auto configureToolbarButton = [](QPushButton* btn) {
        QFont boldFont = btn->font();
        boldFont.setBold(true);
        QFontMetrics fm(boldFont);
        const int textW = std::max(
            fm.horizontalAdvance(btn->text()),
            fm.horizontalAdvance(btn->text().toUpper()));
        const int minW = textW + 24;
        btn->setMinimumWidth(minW);
        btn->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    };

    topLayout->addWidget(new QLabel("Addr:"));
    m_addressEdit = new QLineEdit();
    m_addressEdit->setObjectName("cutterMemAddressEdit");
    m_addressEdit->setMinimumWidth(105);
    m_addressEdit->setText(QStringLiteral("0x00401120"));
    topLayout->addWidget(m_addressEdit);

    topLayout->addWidget(new QLabel("Bytes:"));
    m_lengthBox = new QSpinBox();
    m_lengthBox->setRange(1, 4096);
    m_lengthBox->setValue(256);
    m_lengthBox->setMinimumWidth(68);
    topLayout->addWidget(m_lengthBox);

    topLayout->addWidget(new QLabel("Type:"));
    m_typeBox = new QComboBox();
    m_typeBox->addItem("Int32");
    m_typeBox->addItem("Float");
    m_typeBox->addItem("Double");
    m_typeBox->setMinimumWidth(74);
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
    topLayout->addWidget(m_baselineRankBox);

    topLayout->addWidget(new QLabel("Target:"));
    m_targetRankBox = new QSpinBox();
    m_targetRankBox->setRange(0, 1000);
    m_targetRankBox->setMinimumWidth(56);
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

bool MemView::seekToAddress(uint64_t address) {
    if (address == 0ULL) {
        return false;
    }
    m_currentBaseAddress = address;
    const QString formatted = QStringLiteral("0x%1").arg(address, 8, 16, QChar('0'));
    if (m_addressEdit) {
        m_addressEdit->setText(formatted);
    }
    emit requestMemory(formatted, m_lengthBox ? m_lengthBox->value() : 256);
    return true;
}

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

void MemView::setMemoryData(qint64 beginAddress, const QString& hexContents) {
    const uint64_t startAddr = static_cast<uint64_t>(beginAddress);
    const QByteArray currentData = QByteArray::fromHex(hexContents.toLatin1());

    m_changedByteCount = 0;
    QByteArray prevForDiff;
    if (m_hasDiffBaseline && m_previousBaseAddress == startAddr) {
        prevForDiff = m_previousRawData;
        const int compareLen = qMin(currentData.size(), m_previousRawData.size());
        for (int i = 0; i < compareLen; ++i) {
            if (currentData[i] != m_previousRawData[i]) {
                m_changedByteCount++;
            }
        }
        m_changedByteCount += qAbs(currentData.size() - m_previousRawData.size());
    }

    m_currentBaseAddress = startAddr;
    m_previousBaseAddress = startAddr;
    m_previousRawData = currentData;
    m_lastRawData = currentData;
    m_hasDiffBaseline = true;

    if (m_addressEdit) {
        m_addressEdit->setText(QStringLiteral("0x%1").arg(startAddr, 8, 16, QChar('0')));
    }

    if (m_dumpEdit) {
        m_dumpEdit->setHtml(formatHexDumpHtml(startAddr, currentData, prevForDiff));
    }
}

void MemView::setMemoryData(const QString& address, const QByteArray& data) {
    bool ok = false;
    uint64_t startAddr = parseHexAddress(address, &ok);
    if (!ok) {
        startAddr = m_currentBaseAddress;
    }
    setMemoryData(static_cast<qint64>(startAddr), QString::fromLatin1(data.toHex()));
}

void MemView::onExportMatrixClicked() {
    if (m_lastRawData.isEmpty()) {
        QMessageBox::warning(this, "No Data", "No memory data available to export. Please read memory first.");
        return;
    }

    QString fileName = QFileDialog::getSaveFileName(this, "Export Memory Matrix", "", "CSV Files (*.csv);;Binary Files (*.bin);;All Files (*)");
    if (fileName.isEmpty()) return;

    std::vector<uint8_t> buffer(m_lastRawData.begin(), m_lastRawData.end());
    int typeIndex = m_typeBox->currentIndex();

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

void MemView::displayMemoryDiff(const gridlock::core::hpc::CompareResult& result, void* remoteAddr) {
    if (!result.success) {
        QMessageBox::warning(this, "Diff Failed", "Failed to read memory from one or both PIDs.");
        return;
    }
    
    QString html = "<pre style=\"font-family: 'JetBrains Mono', monospace; font-size: 10pt; margin: 0;\">";
    qint64 startAddress = reinterpret_cast<qint64>(remoteAddr);
    
    int byteCount = result.baselineBuffer.size();
    m_changedByteCount = 0;
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

QString MemView::formatHexDumpHtml(uint64_t startAddress, const QByteArray& currentData, const QByteArray& prevData) {
    QString html = QStringLiteral(
        "<pre style=\"font-family: 'JetBrains Mono', monospace; font-size: 10pt; margin: 4px;\">"
        "<span style=\"color: #858585;\">- offset -         00 01 02 03 04 05 06 07  08 09 0A 0B 0C 0D 0E 0F  |0123456789ABCDEF|</span>\n");

    const int byteCount = currentData.size();
    for (int i = 0; i < byteCount; i += 16) {
        html += QStringLiteral("<span style=\"color: #6a9955;\">0x%1</span> ")
                    .arg(startAddress + static_cast<uint64_t>(i), 16, 16, QChar('0'));

        QString asciiPart = QStringLiteral("|");
        for (int j = 0; j < 16; ++j) {
            if (j == 8) {
                html += QStringLiteral(" ");
            }
            const int idx = i + j;
            if (idx < byteCount) {
                const uint8_t b = static_cast<uint8_t>(currentData[idx]);
                const bool mutated = !prevData.isEmpty() && idx < prevData.size() &&
                                     static_cast<uint8_t>(prevData[idx]) != b;
                const QString byteStr = QStringLiteral("%1").arg(b, 2, 16, QChar('0')).toUpper();

                if (mutated) {
                    html += QStringLiteral("<span style=\"color: #f44747; font-weight: bold;\">%1</span> ").arg(byteStr);
                } else if (b == 0x00) {
                    html += QStringLiteral("<span style=\"color: #555c66;\">%1</span> ").arg(byteStr);
                } else {
                    html += QStringLiteral("<span style=\"color: #d4d7dc;\">%1</span> ").arg(byteStr);
                }

                const char c = static_cast<char>(b);
                if (c >= 32 && c <= 126) {
                    const QString escaped = QString(QChar(c)).toHtmlEscaped();
                    if (mutated) {
                        asciiPart += QStringLiteral("<span style=\"color: #f44747; font-weight: bold;\">%1</span>").arg(escaped);
                    } else {
                        asciiPart += QStringLiteral("<span style=\"color: #4ec9b0;\">%1</span>").arg(escaped);
                    }
                } else {
                    asciiPart += QStringLiteral("<span style=\"color: #555c66;\">.</span>");
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

QString MemView::formatHexDump(qint64 startAddress, const QString& hexData) {
    QString result;
    int dataLen = hexData.length();
    int byteCount = dataLen / 2;
    
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

} // namespace gridlock::ui
