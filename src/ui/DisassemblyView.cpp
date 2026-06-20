#include "DisassemblyView.hpp"
#include <QPainter>
#include <QStringList>

namespace gridlock::ui {

DisassemblyView::DisassemblyView(QWidget *parent) : QWidget(parent) {}

void DisassemblyView::paintEvent(QPaintEvent */*event*/) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor(30, 30, 30));
    
    QFont font("monospace");
    font.setStyleHint(QFont::Monospace);
    font.setPointSize(11);
    painter.setFont(font);

    if (m_asmCode.isEmpty()) {
        painter.setPen(QColor(120, 120, 120));
        painter.drawText(rect(), Qt::AlignCenter, "; Select a rank to view disassembly...");
        return;
    }

    painter.setPen(Qt::green);
    QStringList lines = m_asmCode.split('\n');
    int y = 20;
    for (const QString& line : lines) {
        painter.drawText(10, y, line);
        y += 20;
    }
}

} // namespace gridlock::ui
