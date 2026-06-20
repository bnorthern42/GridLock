#include "TerminalDock.hpp"
#include <QPalette>
#include <QFont>
#include <QScrollBar>
#include <QVBoxLayout>

namespace gridlock::ui {

TerminalDock::TerminalDock(const QString& title, QWidget *parent) 
    : QWidget(parent) 
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_textEdit = new QPlainTextEdit(this);
    m_textEdit->setReadOnly(true);
    
    QFont font("monospace");
    font.setStyleHint(QFont::Monospace);
    font.setPointSize(10);
    m_textEdit->setFont(font);

    QPalette p = m_textEdit->palette();
    p.setColor(QPalette::Base, QColor(10, 10, 10)); // Black background
    p.setColor(QPalette::Text, QColor(100, 255, 100)); // Lime text
    m_textEdit->setPalette(p);
    
    layout->addWidget(m_textEdit);
}

void TerminalDock::appendText(const QString& text) {
    m_textEdit->moveCursor(QTextCursor::End);
    m_textEdit->insertPlainText(text);
    m_textEdit->verticalScrollBar()->setValue(m_textEdit->verticalScrollBar()->maximum());
}

void TerminalDock::appendError(const QString& text) {
    m_textEdit->moveCursor(QTextCursor::End);
    
    QTextCharFormat fmt;
    fmt.setForeground(QColor(255, 100, 100));
    m_textEdit->textCursor().mergeCharFormat(fmt);
    
    m_textEdit->insertPlainText(text);
    
    fmt.setForeground(QColor(100, 255, 100));
    m_textEdit->textCursor().mergeCharFormat(fmt);
    
    m_textEdit->verticalScrollBar()->setValue(m_textEdit->verticalScrollBar()->maximum());
}

} // namespace gridlock::ui
