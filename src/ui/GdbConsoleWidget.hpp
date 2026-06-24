#pragma once

#include <QWidget>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace gridlock::ui {

class GdbConsoleWidget : public QWidget {
    Q_OBJECT
public:
    explicit GdbConsoleWidget(QWidget* parent = nullptr);
    void resetRanks(int count);

public slots:
    void appendGdbOutput(int rank, const QString& output);
    void appendGdbInput(int rank, const QString& input);

signals:
    void commandEntered(int rank, const QString& command);

private slots:
    void onClearClicked();
    void onCommandReturnPressed();
    void onFilterTextChanged(const QString& text);
    void onRankFilterChanged(int index);
    void onRawMiToggled(bool checked);

private:
    void applyFilter();

    QLineEdit* m_filterEdit;
    QPushButton* m_clearButton;
    QCheckBox* m_rawMiCheckbox;
    QComboBox* m_rankCombo;
    QPlainTextEdit* m_consoleEdit;
    QLineEdit* m_commandEdit;

    struct LogEntry {
        int rank;
        QString text;
        bool isInput = false;
    };
    QList<LogEntry> m_logs;
};

} // namespace gridlock::ui
