#pragma once
#include <QWidget>
#include <QString>

class QLineEdit;
class QSpinBox;
class QPushButton;
class QPlainTextEdit;
class QComboBox;

namespace gridlock::ui {

class MemView : public QWidget {
    Q_OBJECT
public:
    explicit MemView(QWidget* parent = nullptr);
    ~MemView() override = default;

    void setMemoryData(qint64 beginAddress, const QString& hexContents);
    void setMemoryData(const QString& address, const QByteArray& data);

signals:
    void requestMemory(const QString& address, int length);

private slots:
    void onReadClicked();
    void onExportMatrixClicked();

private:
    QString formatHexDump(qint64 startAddress, const QString& hexData);

    QLineEdit* m_addressEdit;
    QSpinBox* m_lengthBox;
    QPushButton* m_readBtn;
    QPushButton* m_exportBtn;
    QComboBox* m_typeBox;
    QPlainTextEdit* m_dumpEdit;
    QByteArray m_lastRawData;
};

} // namespace gridlock::ui
