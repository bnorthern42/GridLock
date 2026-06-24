#pragma once
#include <QWidget>
#include <QString>
#include "../../core/hpc/MemoryDiffer.hpp"

class QLineEdit;
class QSpinBox;
class QPushButton;
class QTextEdit;
class QComboBox;

namespace gridlock::ui {

class MemView : public QWidget {
    Q_OBJECT
public:
    explicit MemView(QWidget* parent = nullptr);
    ~MemView() override = default;

    void setMemoryData(qint64 beginAddress, const QString& hexContents);
    void setMemoryData(const QString& address, const QByteArray& data);
    void displayMemoryDiff(const gridlock::core::hpc::CompareResult& result, void* remoteAddr);

signals:
    void requestMemory(const QString& address, int length);
    void requestMemoryDiff(int baseRank, int targetRank, const QString& address, int length);

private slots:
    void onReadClicked();
    void onExportMatrixClicked();
    void onDiffClicked();

private:
    QString formatHexDump(qint64 startAddress, const QString& hexData);

    QLineEdit* m_addressEdit;
    QSpinBox* m_lengthBox;
    QPushButton* m_readBtn;
    QPushButton* m_exportBtn;
    QComboBox* m_typeBox;
    QSpinBox* m_baselineRankBox;
    QSpinBox* m_targetRankBox;
    QPushButton* m_diffBtn;
    QTextEdit* m_dumpEdit;
    QByteArray m_lastRawData;
};

} // namespace gridlock::ui
