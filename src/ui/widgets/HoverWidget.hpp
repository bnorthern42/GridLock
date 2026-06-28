#pragma once
#include <QWidget>
#include <QTextBrowser>
#include <QFrame>
#include <QEvent>
#include <QTimer>

namespace gridlock::ui {

class HoverWidget : public QWidget {
    Q_OBJECT
public:
    explicit HoverWidget(QWidget* parent = nullptr);
    ~HoverWidget() override;

    void showHoverData(const QPoint& globalPos, const QString& markdownText);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    QFrame* m_frame;
    QTextBrowser* m_textBrowser;
    QTimer* m_checkTimer = nullptr;
};

} // namespace gridlock::ui
