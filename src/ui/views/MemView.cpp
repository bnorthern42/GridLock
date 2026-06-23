#include "MemView.hpp"
#include "../../core/managers/ConfigManager.hpp"
#include "../../core/hpc/MemoryExporter.hpp"
#include <QFileDialog>
#include <QtConcurrent/QtConcurrent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QPlainTextEdit>
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

    m_readBtn = new QPushButton("Read Memory");
    topLayout->addWidget(m_readBtn);

    m_exportBtn = new QPushButton("Export Matrix");
    topLayout->addWidget(m_exportBtn);

    topLayout->addStretch();

    mainLayout->addLayout(topLayout);

    m_dumpEdit = new QPlainTextEdit();
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
}

void MemView::onReadClicked() {
    QString addr = m_addressEdit->text().trimmed();
    if (!addr.isEmpty()) {
        emit requestMemory(addr, m_lengthBox->value());
    }
}

void MemView::setMemoryData(qint64 beginAddress, const QString& hexContents) {
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
        return;
    }

    QString fileName = QFileDialog::getSaveFileName(this, "Export Memory Matrix", "", "CSV Files (*.csv);;Binary Files (*.bin);;All Files (*)");
    if (fileName.isEmpty()) return;

    std::vector<uint8_t> buffer(m_lastRawData.begin(), m_lastRawData.end());

    QtConcurrent::run([buffer, fileName]() {
        if (fileName.endsWith(".csv", Qt::CaseInsensitive)) {
            int cols = buffer.size() / sizeof(float);
            if (cols == 0) cols = 1;
            gridlock::core::hpc::MemoryExporter::exportToCsv(fileName.toStdString(), buffer, 1, cols, gridlock::core::hpc::DataType::Float);
        } else {
            gridlock::core::hpc::MemoryExporter::exportToBinary(fileName.toStdString(), buffer);
        }
    });
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
