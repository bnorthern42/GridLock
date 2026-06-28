#include "HoverWidget.hpp"
#include <QVBoxLayout>
#include <QGraphicsDropShadowEffect>
#include <QGuiApplication>
#include <QScreen>
#include <QEvent>
#include <QMouseEvent>
#include <QApplication>

namespace gridlock::ui {

HoverWidget::HoverWidget(QWidget* parent)
    : QWidget(parent) {
    setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    
    // UI Styling
    m_frame = new QFrame(this);
    m_frame->setObjectName("hoverFrame");
    m_frame->setStyleSheet(R"(
        #hoverFrame {
            background-color: #1e1e2e;
            border: 1px solid #45475a;
            border-radius: 8px;
        }
    )");

    auto* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(15);
    shadow->setOffset(0, 4);
    shadow->setColor(QColor(0, 0, 0, 100)); // semi-transparent black
    m_frame->setGraphicsEffect(shadow);

    m_textBrowser = new QTextBrowser(m_frame);
    m_textBrowser->setFrameShape(QFrame::NoFrame);
    m_textBrowser->viewport()->setAutoFillBackground(false);
    m_textBrowser->setStyleSheet("background: transparent; color: #cdd6f4;");
    m_textBrowser->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    
    auto* layout = new QVBoxLayout(m_frame);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->addWidget(m_textBrowser);
    
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(15, 15, 15, 15); // Leave space for shadow
    mainLayout->addWidget(m_frame);

    m_checkTimer = new QTimer(this);
    connect(m_checkTimer, &QTimer::timeout, this, [this]() {
        if (isVisible()) {
            QRect expandedRect = geometry().adjusted(-20, -20, 20, 20); // geometry() is in global coordinates for Qt::ToolTip
            if (!expandedRect.contains(QCursor::pos())) {
                hide();
                m_checkTimer->stop();
            }
        }
    });

    // Install event filter on application to close tooltip when clicks occur
    if (qApp) {
        qApp->installEventFilter(this);
    }
}

HoverWidget::~HoverWidget() {
    if (qApp) {
        qApp->removeEventFilter(this);
    }
}

void HoverWidget::showHoverData(const QPoint& globalPos, const QString& markdownText) {
    m_textBrowser->setMarkdown(markdownText);
    
    // Adjust size to contents (with limits)
    m_textBrowser->document()->setTextWidth(400); // Max width 400
    QSizeF docSize = m_textBrowser->document()->size();
    
    int newWidth = qMin(static_cast<int>(docSize.width()) + 30, 430);
    int newHeight = qMin(static_cast<int>(docSize.height()) + 30, 300);
    resize(newWidth, newHeight);

    // Smart Positioning
    QPoint pos = globalPos + QPoint(10, 10);
    
    if (QScreen* screen = QGuiApplication::screenAt(globalPos)) {
        QRect avail = screen->availableGeometry();
        
        if (pos.x() + width() > avail.right()) {
            pos.setX(globalPos.x() - width() - 10);
        }
        if (pos.y() + height() > avail.bottom()) {
            pos.setY(globalPos.y() - height() - 10);
        }
        
        // Ensure it doesn't go off the left/top edges
        pos.setX(qMax(pos.x(), avail.left()));
        pos.setY(qMax(pos.y(), avail.top()));
    }
    
    move(pos);
    show();
    m_checkTimer->start(100);
}

bool HoverWidget::eventFilter(QObject* obj, QEvent* event) {
    if (isVisible()) {
        int t = event->type();
        if (t == QEvent::MouseButtonPress || t == QEvent::KeyPress || t == QEvent::MouseButtonDblClick) {
            hide();
            m_checkTimer->stop();
        }
    }
    return QWidget::eventFilter(obj, event);
}

} // namespace gridlock::ui
