#include "MemView.hpp"
#include "../../core/managers/ConfigManager.hpp"
#include "../../core/hpc/MemoryExporter.hpp"
#include "../../core/hpc/MemoryDiffer.hpp"
#include <QFileDialog>
#include <QMessageBox>
#include <QtConcurrent/QtConcurrent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QTextEdit>
#include <QComboBox>
#include <QFont>
#include <QPalette>

namespace gridlock::ui {

MemView::MemView(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    QHBoxLayout* topLayout = new QHBoxLayout();
    
    topLayout->addWidget(new QLabel("Address/Variable:"));
    m_addressEdit = new QLineEdit();
    topLayout->addWidget(m_addressEdit);

    topLayout->addWidget(new QLabel("Length (bytes):"));
    m_lengthBox = new QSpinBox();
    m_lengthBox->setRange(1, 4096);
    m_lengthBox->setValue(256);
    topLayout->addWidget(m_lengthBox);

    topLayout->addWidget(new QLabel("Type:"));
    m_typeBox = new QComboBox();
    m_typeBox->addItem("Int32");
    m_typeBox->addItem("Float");
    m_typeBox->addItem("Double");
    topLayout->addWidget(m_typeBox);

    m_readBtn = new QPushButton("Read Memory");
    topLayout->addWidget(m_readBtn);

    m_exportBtn = new QPushButton("Export Matrix");
    topLayout->addWidget(m_exportBtn);

    topLayout->addWidget(new QLabel("Baseline Rank:"));
    m_baselineRankBox = new QSpinBox();
    m_baselineRankBox->setRange(0, 1000);
    topLayout->addWidget(m_baselineRankBox);

    topLayout->addWidget(new QLabel("Target Rank:"));
    m_targetRankBox = new QSpinBox();
    m_targetRankBox->setRange(0, 1000);
    topLayout->addWidget(m_targetRankBox);

    m_diffBtn = new QPushButton("Diff Ranks");
    topLayout->addWidget(m_diffBtn);

    topLayout->addStretch();

    mainLayout->addLayout(topLayout);

    m_dumpEdit = new QTextEdit();
    m_dumpEdit->setReadOnly(true);
    QFont font("monospace");
    font.setStyleHint(QFont::Monospace);
    font.setPointSize(11);
    m_dumpEdit->setFont(font);

    QPalette p = m_dumpEdit->palette();
    p.setColor(QPalette::Base, QColor(gridlock::core::ConfigManager::instance().getSourceBackground()));
    p.setColor(QPalette::Text, QColor(gridlock::core::ConfigManager::instance().getSourceText()));
    m_dumpEdit->setPalette(p);

    mainLayout->addWidget(m_dumpEdit);

    connect(m_readBtn, &QPushButton::clicked, this, &MemView::onReadClicked);
    connect(m_exportBtn, &QPushButton::clicked, this, &MemView::onExportMatrixClicked);
    connect(m_diffBtn, &QPushButton::clicked, this, &MemView::onDiffClicked);
}

void MemView::onReadClicked() {
    QString addr = m_addressEdit->text().trimmed();
    if (!addr.isEmpty()) {
        emit requestMemory(addr, m_lengthBox->value());
    }
}

void MemView::setMemoryData(qint64 beginAddress, const QString& hexContents) {
    m_lastRawData = QByteArray::fromHex(hexContents.toLatin1());
    m_dumpEdit->setPlainText(formatHexDump(beginAddress, hexContents));
}

void MemView::setMemoryData(const QString& address, const QByteArray& data) {
    bool ok;
    qint64 startAddr = address.toULongLong(&ok, 16);
    if (!ok) startAddr = 0;
    m_lastRawData = data;
    setMemoryData(startAddr, data.toHex());
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
    
    bool ok;
    (void)addrStr.toULongLong(&ok, 16);
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
    
    QString html = "<pre style=\"font-family: monospace; font-size: 11pt;\">";
    qint64 startAddress = reinterpret_cast<qint64>(remoteAddr);
    
    int byteCount = result.baselineBuffer.size();
    for (int i = 0; i < byteCount; i += 16) {
        html += QString("0x%1: ").arg(QString::number((quint64)(startAddress + i), 16).rightJustified(16, '0').toUpper());
        
        QString asciiPart = "| ";
        for (int j = 0; j < 16; ++j) {
            if (i + j < byteCount) {
                uint8_t baseByte = result.baselineBuffer[i+j];
                uint8_t targetByte = result.targetBuffer[i+j];
                bool diff = result.diffMask[i+j];
                
                QString byteStr = QString::number(targetByte, 16).rightJustified(2, '0').toUpper();
                
                if (diff) {
                    QString tooltip = QString("Baseline: 0x%1 | Target: 0x%2")
                        .arg(QString::number(baseByte, 16).rightJustified(2, '0').toUpper())
                        .arg(byteStr);
                    html += QString("<span style=\"color: #e74c3c; font-weight: bold;\" title=\"%1\">%2</span> ")
                        .arg(tooltip).arg(byteStr);
                } else {
                    html += byteStr + " ";
                }
                
                char c = targetByte;
                if (c >= 32 && c <= 126) {
                    if (diff) {
                        asciiPart += QString("<span style=\"color: #e74c3c; font-weight: bold;\">%1</span>").arg(QString(c).toHtmlEscaped());
                    } else {
                        asciiPart += QString(c).toHtmlEscaped();
                    }
                } else {
                    asciiPart += '.';
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
