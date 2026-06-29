#include "MpiNetworkLogWidget.hpp"
#include <QPalette>
#include <QFont>
#include <QScrollBar>
#include <QVBoxLayout>

namespace gridlock::ui {

MpiNetworkLogWidget::MpiNetworkLogWidget(const QString& title, QWidget *parent) 
    : QWidget(parent) 
{
    Q_UNUSED(title);
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_textEdit = new QPlainTextEdit(this);
    m_textEdit->setReadOnly(true);
    
    QFont font("monospace");
    font.setStyleHint(QFont::Monospace);
    font.setPointSize(10);
    m_textEdit->setFont(font);

    layout->addWidget(m_textEdit);
}

void MpiNetworkLogWidget::appendText(const QString& text) {
    appendText("stdout", text);
}

void MpiNetworkLogWidget::appendText(const QString& category, const QString& text) {
    m_textEdit->moveCursor(QTextCursor::End);
    
    QTextCharFormat fmt;
    if (category == "stderr" || category == "error") {
        fmt.setForeground(QColor(255, 100, 100)); // Red
    } else if (category == "console") {
        fmt.setForeground(QColor(100, 200, 255)); // Blue-ish
    } else {
        fmt.setForeground(m_textEdit->palette().color(QPalette::Text)); // Default
    }
    m_textEdit->textCursor().mergeCharFormat(fmt);
    
    m_textEdit->insertPlainText(text);
    
    m_textEdit->textCursor().setCharFormat(QTextCharFormat());
    m_textEdit->verticalScrollBar()->setValue(m_textEdit->verticalScrollBar()->maximum());
}

void MpiNetworkLogWidget::appendError(const QString& text) {
    m_textEdit->moveCursor(QTextCursor::End);
    
    QTextCharFormat fmt;
    fmt.setForeground(QColor(255, 100, 100));
    m_textEdit->textCursor().mergeCharFormat(fmt);
    
    m_textEdit->insertPlainText(text);
    
    m_textEdit->textCursor().setCharFormat(QTextCharFormat());
    
    m_textEdit->verticalScrollBar()->setValue(m_textEdit->verticalScrollBar()->maximum());
}

} // namespace gridlock::ui
