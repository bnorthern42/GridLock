/**
 * @file HoverWidget.cpp
 * @brief Floating hover tooltip widget implementation for variable inspection.
 *
 * Implements HoverWidget, providing a frameless popup window that dynamically sizes
 * itself to markdown-rendered variable details, stays within screen boundaries,
 * and auto-dismisses when the user clicks or moves the cursor away.
 */

#include "HoverWidget.hpp"
#include <QVBoxLayout>
#include <QGraphicsDropShadowEffect>
#include <QGuiApplication>
#include <QScreen>
#include <QEvent>
#include <QMouseEvent>
#include <QApplication>

namespace gridlock::ui {

/**
 * @brief Constructs a new HoverWidget popup window.
 *
 * Sets frameless and popup window flags, builds the inner frame and text browser,
 * and installs an event filter on QApplication to handle external clicks.
 *
 * @param parent Optional parent widget.
 */
HoverWidget::HoverWidget(QWidget* parent)
    : QWidget(parent) {
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_StyledBackground, true);

    // Frame container for styling borders and background
    m_frame = new QFrame(this);
    m_frame->setObjectName("hoverFrame");

    // Monospace markdown text display
    m_textBrowser = new QTextBrowser(m_frame);
    m_textBrowser->setFrameShape(QFrame::NoFrame);
    m_textBrowser->viewport()->setAutoFillBackground(false);
    m_textBrowser->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    
    auto* layout = new QVBoxLayout(m_frame);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->addWidget(m_textBrowser);
    
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->addWidget(m_frame);

    // Timer checking if mouse cursor has strayed far from tooltip
    m_checkTimer = new QTimer(this);
    connect(m_checkTimer, &QTimer::timeout, this, [this]() {
        if (isVisible()) {
            QRect expandedRect = geometry().adjusted(-20, -20, 20, 20);
            if (!expandedRect.contains(QCursor::pos())) {
                hide();
                m_checkTimer->stop();
            }
        }
    });

    // Install application-wide event filter to dismiss on external user interaction
    if (qApp) {
        qApp->installEventFilter(this);
    }
}

/**
 * @brief Destructor removing the application event filter.
 */
HoverWidget::~HoverWidget() {
    if (qApp) {
        qApp->removeEventFilter(this);
    }
}

/**
 * @brief Renders markdown content and displays tooltip near globalPos with screen boundary clamping.
 *
 * Computes optimal dimensions from document layout, detects current monitor boundaries,
 * and repositions tooltip to ensure it remains completely on screen.
 *
 * @param globalPos Screen anchor position.
 * @param markdownText Markdown string detailing the variable.
 */
void HoverWidget::showHoverData(const QPoint& globalPos, const QString& markdownText) {
    m_textBrowser->setMarkdown(markdownText);
    
    // Constrain document width to compute layout bounds
    m_textBrowser->document()->setTextWidth(400);
    QSizeF docSize = m_textBrowser->document()->size();
    
    int newWidth = qMin(static_cast<int>(docSize.width()) + 30, 430);
    int newHeight = qMin(static_cast<int>(docSize.height()) + 30, 300);
    resize(newWidth, newHeight);

    // Initial offset anchor from mouse cursor
    QPoint pos = globalPos + QPoint(10, 10);
    
    // Screen boundary collision avoidance
    if (QScreen* screen = QGuiApplication::screenAt(globalPos)) {
        QRect avail = screen->availableGeometry();
        
        // Flip to left of cursor if extending beyond right edge
        if (pos.x() + width() > avail.right()) {
            pos.setX(globalPos.x() - width() - 10);
        }
        // Flip above cursor if extending beyond bottom edge
        if (pos.y() + height() > avail.bottom()) {
            pos.setY(globalPos.y() - height() - 10);
        }
        
        // Clamp to left and top screen boundaries
        pos.setX(qMax(pos.x(), avail.left()));
        pos.setY(qMax(pos.y(), avail.top()));
    }
    
    move(pos);
    show();
    m_checkTimer->start(100);
}

/**
 * @brief Intercepts mouse and key presses across the application to dismiss the popup.
 * @param obj Watched object.
 * @param event The triggered event.
 * @return Result of parent eventFilter.
 */
bool HoverWidget::eventFilter(QObject* obj, QEvent* event) {
    if (isVisible()) {
        int t = event->type();
        // Hide tooltip on mouse clicks or keyboard navigation
        if (t == QEvent::MouseButtonPress || t == QEvent::KeyPress || t == QEvent::MouseButtonDblClick) {
            hide();
            m_checkTimer->stop();
        }
    }
    return QWidget::eventFilter(obj, event);
}

}  // namespace gridlock::ui

