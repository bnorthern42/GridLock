#pragma once

#include <QDockWidget>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace gridlock::ui {

class GdbConsoleDock : public QDockWidget {
    Q_OBJECT
public:
    explicit GdbConsoleDock(QWidget* parent = nullptr);

    void appendGdbOutput(int rank, const QString& output);

signals:
    void commandEntered(int rank, const QString& command);

private slots:
    void onClearClicked();
    void onCommandReturnPressed();
    void onFilterTextChanged(const QString& text);
    void onRankFilterChanged(int index);

private:
    void applyFilter();

    QLineEdit* m_filterEdit;
    QPushButton* m_clearButton;
    QComboBox* m_rankCombo;
    QPlainTextEdit* m_consoleEdit;
    QLineEdit* m_commandEdit;

    struct LogEntry {
        int rank;
        QString text;
    };
    QList<LogEntry> m_logs;
};

} // namespace gridlock::ui
