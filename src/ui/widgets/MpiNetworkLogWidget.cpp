/**
 * @file MpiNetworkLogWidget.cpp
 * @brief MPI network trace and diagnostic message logging console implementation.
 *
 * Implements MpiNetworkLogWidget, handling categorized message styling (stderr/error in red,
 * console in blue, stdout in default theme text color) and autoscrolling.
 */

#include "MpiNetworkLogWidget.hpp"
#include <QFont>
#include <QFrame>
#include <QPalette>
#include <QScrollBar>
#include <QVBoxLayout>

namespace gridlock::ui {

/**
 * @brief Constructs a new MpiNetworkLogWidget.
 *
 * Configures the monospace text area with zero margins and borderless frame for dock docking.
 *
 * @param title Optional title banner string.
 * @param parent Optional parent widget.
 */
MpiNetworkLogWidget::MpiNetworkLogWidget(const QString& title, QWidget *parent) 
    : QWidget(parent) 
{
    Q_UNUSED(title);
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Initialize borderless monospace text editor on #16181d dark-slate canvas.
    m_textEdit = new QPlainTextEdit(this);
    m_textEdit->setFrameShape(QFrame::NoFrame);
    m_textEdit->setReadOnly(true);
    m_textEdit->setStyleSheet(QStringLiteral(
        "QPlainTextEdit { background: #16181d; color: #d7dae0; border: none; padding: 4px 6px; }"));
    
    QFont font("monospace");
    font.setStyleHint(QFont::Monospace);
    font.setPointSize(10);
    m_textEdit->setFont(font);

    layout->addWidget(m_textEdit, 1);
}

/**
 * @brief Convenience overload delegating to appendText("stdout", text).
 * @param text Log message string.
 */
void MpiNetworkLogWidget::appendText(const QString& text) {
    appendText("stdout", text);
}

/**
 * @brief Appends text formatted according to category tag and scrolls to end.
 *
 * Category "stderr" or "error" is formatted in red (#ff6464),
 * "console" in cyan (#64c8ff), and others in default palette text color.
 *
 * @param category Category identifier string.
 * @param text Content to append.
 */
void MpiNetworkLogWidget::appendText(const QString& category, const QString& text) {
    m_textEdit->moveCursor(QTextCursor::End);
    
    // Choose syntax color format based on stream category
    QTextCharFormat fmt;
    if (category == "stderr" || category == "error") {
        fmt.setForeground(QColor(255, 100, 100)); // Red for errors
    } else if (category == "console") {
        fmt.setForeground(QColor(100, 200, 255)); // Soft blue for system info
    } else {
        fmt.setForeground(m_textEdit->palette().color(QPalette::Text)); // Default theme text
    }
    m_textEdit->textCursor().mergeCharFormat(fmt);
    
    m_textEdit->insertPlainText(text);
    
    // Reset format and autoscroll to the latest line
    m_textEdit->textCursor().setCharFormat(QTextCharFormat());
    m_textEdit->verticalScrollBar()->setValue(m_textEdit->verticalScrollBar()->maximum());
}

/**
 * @brief Appends an error message in bold/alert red formatting.
 * @param text Error message text.
 */
void MpiNetworkLogWidget::appendError(const QString& text) {
    m_textEdit->moveCursor(QTextCursor::End);
    
    QTextCharFormat fmt;
    fmt.setForeground(QColor(255, 100, 100));
    m_textEdit->textCursor().mergeCharFormat(fmt);
    
    m_textEdit->insertPlainText(text);
    m_textEdit->textCursor().setCharFormat(QTextCharFormat());
    
    // Autoscroll to bottom
    m_textEdit->verticalScrollBar()->setValue(m_textEdit->verticalScrollBar()->maximum());
}

}  // namespace gridlock::ui

