#include "CustomTitleBar.hpp"
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QStyle>

namespace gridlock::ui::widgets {

CustomTitleBar::CustomTitleBar(QWidget* parent) : QWidget(parent) {
    setFixedHeight(30);
    
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    
    QLabel* titleLabel = new QLabel(" GridLock IDE", this);
    titleLabel->setStyleSheet("color: #cdd6f4; font-weight: bold; padding-left: 8px;");
    layout->addWidget(titleLabel);
    
    layout->addStretch();
    
    m_minButton = new QPushButton("🗕", this);
    m_maxButton = new QPushButton("🗖", this);
    m_closeButton = new QPushButton("🗙", this);
    
    m_minButton->setToolTip("Minimize");
    m_maxButton->setToolTip("Maximize");
    m_closeButton->setToolTip("Close");
    
    QString btnStyle = "QPushButton { border: none; background: transparent; color: #cdd6f4; font-family: sans-serif; font-size: 14px; padding: 0 10px; }"
                       "QPushButton:hover { background: #45475a; }"
                       "QPushButton#closeBtn:hover { background: #f38ba8; color: #1e1e2e; }";
                       
    m_minButton->setStyleSheet(btnStyle);
    m_maxButton->setStyleSheet(btnStyle);
    m_closeButton->setObjectName("closeBtn");
    m_closeButton->setStyleSheet(btnStyle);
    
    m_minButton->setFixedSize(40, 30);
    m_maxButton->setFixedSize(40, 30);
    m_closeButton->setFixedSize(40, 30);
    
    layout->addWidget(m_minButton);
    layout->addWidget(m_maxButton);
    layout->addWidget(m_closeButton);
    
    if (parent) {
        connect(m_minButton, &QPushButton::clicked, parent, &QWidget::showMinimized);
        connect(m_closeButton, &QPushButton::clicked, parent, &QWidget::close);
    }
    
    connect(m_maxButton, &QPushButton::clicked, this, &CustomTitleBar::onMaxButtonClicked);
}

void CustomTitleBar::onMaxButtonClicked() {
    QWidget* p = window();
    if (!p) return;
    
    if (p->isMaximized()) {
        p->showNormal();
        m_maxButton->setText("🗖");
        m_maxButton->setToolTip("Maximize");
    } else {
        p->showMaximized();
        m_maxButton->setText("🗗");
        m_maxButton->setToolTip("Restore Down");
    }
}

void CustomTitleBar::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_dragPosition = event->globalPosition().toPoint() - window()->frameGeometry().topLeft();
        event->accept();
    }
}

void CustomTitleBar::mouseMoveEvent(QMouseEvent* event) {
    if (event->buttons() & Qt::LeftButton) {
        if (window()->isMaximized()) {
            return; // Usually, we don't drag if maximized without restoring first
        }
        window()->move(event->globalPosition().toPoint() - m_dragPosition);
        event->accept();
    }
}

void CustomTitleBar::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        onMaxButtonClicked();
        event->accept();
    }
}

} // namespace gridlock::ui::widgets
