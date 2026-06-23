#pragma once
#include <QWidget>
#include <QPushButton>

namespace gridlock::ui::widgets {

class CustomTitleBar : public QWidget {
    Q_OBJECT
public:
    explicit CustomTitleBar(QWidget* parent = nullptr);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private slots:
    void onMaxButtonClicked();

private:
    QPushButton* m_minButton;
    QPushButton* m_maxButton;
    QPushButton* m_closeButton;
    QPoint m_dragPosition;
};

} // namespace gridlock::ui::widgets
