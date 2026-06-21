#include "ServerRackView.hpp"
#include <QPainter>
#include <QMouseEvent>
#include <QLinearGradient>
#include <QPainterPath>

namespace gridlock::ui {

ServerRackView::ServerRackView(QWidget *parent) : QWidget(parent) {
    setMouseTracking(true);
}

void ServerRackView::paintEvent(QPaintEvent */*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor(20, 20, 20));
    
    int bladeHeight = 35;
    int spacing = 10;

    QFont monoFont("monospace", 10, QFont::Bold);
    monoFont.setStyleHint(QFont::Monospace);
    painter.setFont(monoFont);

    for (int i = 0; i < m_rankCount; ++i) {
        QRect rect(10, 10 + i * (bladeHeight + spacing), width() - 20, bladeHeight);
        
        QPainterPath path;
        path.addRoundedRect(rect, 5, 5);

        painter.setBrush(QColor(35, 35, 35));
        
        QPen borderPen(i == m_selectedRank ? QColor(100, 150, 255) : QColor(80, 80, 80));
        borderPen.setWidth(i == m_selectedRank ? 2 : 1);
        painter.setPen(borderPen);
        painter.drawPath(path);

        QString status = "offline";
        qint64 runTime = 0;
        if (m_states.count(i)) {
            status = m_states[i].currentState;
            runTime = m_states[i].totalRuntimeMs;
            if (status == "running" && m_states[i].executionTimer.isValid()) {
                runTime += m_states[i].executionTimer.elapsed();
            }
        }
        
        QColor ledColor;
        if (status == "running") {
            ledColor = QColor(0, 230, 115);
        } else if (status == "stopped") {
            ledColor = QColor(255, 191, 0);
        } else {
            ledColor = QColor(100, 100, 100);
        }

        painter.setPen(QColor(220, 220, 220));
        QString titleStr = QString("R[%1] %2ms").arg(i).arg(runTime);
        painter.drawText(rect.left() + 15, rect.center().y() + 4, titleStr);

        painter.setBrush(ledColor);
        painter.setPen(Qt::NoPen);
        
        painter.drawEllipse(rect.right() - 20, rect.center().y() - 5, 10, 10);
    }
}

void ServerRackView::mousePressEvent(QMouseEvent *event) {
    int bladeHeight = 35;
    int spacing = 10;

    for (int i = 0; i < m_rankCount; ++i) {
        QRect rect(10, 10 + i * (bladeHeight + spacing), width() - 20, bladeHeight);
        if (rect.contains(event->pos())) {
            m_selectedRank = i;
            emit rankSelected(i);
            update();
            break;
        }
    }
}

void ServerRackView::mouseMoveEvent(QMouseEvent *event) {
    int bladeHeight = 35;
    int spacing = 10;
    
    int newHover = -1;
    for (int i = 0; i < m_rankCount; ++i) {
        QRect rect(10, 10 + i * (bladeHeight + spacing), width() - 20, bladeHeight);
        if (rect.contains(event->pos())) {
            newHover = i;
            break;
        }
    }
    
    if (newHover != m_hoveredRank) {
        m_hoveredRank = newHover;
        update();
    }
}

void ServerRackView::leaveEvent(QEvent */*event*/) {
    if (m_hoveredRank != -1) {
        m_hoveredRank = -1;
        update();
    }
}

} // namespace gridlock::ui
