#pragma once
#include <QWidget>
#include <QPlainTextEdit>
#include <QString>

namespace gridlock::ui {

class TerminalDock : public QWidget {
    Q_OBJECT
public:
    explicit TerminalDock(const QString& title, QWidget *parent = nullptr);
    void appendText(const QString& text);
    void appendError(const QString& text);
private:
    QPlainTextEdit* m_textEdit;
};

} // namespace gridlock::ui
