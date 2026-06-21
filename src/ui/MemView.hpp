#pragma once
#include <QWidget>
#include <QString>

class QLineEdit;
class QSpinBox;
class QPushButton;
class QPlainTextEdit;

namespace gridlock::ui {

class MemView : public QWidget {
    Q_OBJECT
public:
    explicit MemView(QWidget* parent = nullptr);
    ~MemView() override = default;

    void setMemoryData(qint64 beginAddress, const QString& hexContents);

signals:
    void requestMemory(const QString& address, int length);

private slots:
    void onReadClicked();

private:
    QString formatHexDump(qint64 startAddress, const QString& hexData);

    QLineEdit* m_addressEdit;
    QSpinBox* m_lengthBox;
    QPushButton* m_readBtn;
    QPlainTextEdit* m_dumpEdit;
};

} // namespace gridlock::ui
