#pragma once

/**
 * @file HoverWidget.hpp
 * @brief Floating hover tooltip widget for debugger symbol inspection.
 *
 * Implements HoverWidget, a floating popup tooltip rendering markdown-formatted
 * variable inspect data, types, memory locations, and docstrings on mouse hover.
 */

#include <QWidget>
#include <QTextBrowser>
#include <QFrame>
#include <QEvent>
#include <QTimer>

namespace gridlock::ui {

/**
 * @class HoverWidget
 * @brief Frameless popup displaying evaluation details for hovered source identifiers.
 */
class HoverWidget : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief Constructs a new HoverWidget.
     * @param parent Optional parent widget.
     */
    explicit HoverWidget(QWidget* parent = nullptr);

    /**
     * @brief Destructor.
     */
    ~HoverWidget() override;

    /**
     * @brief Positions and displays the hover popup at a given global position.
     * @param globalPos Screen coordinate to anchor the tooltip.
     * @param markdownText Markdown or HTML content detailing the hovered variable.
     */
    void showHoverData(const QPoint& globalPos, const QString& markdownText);

protected:
    /**
     * @brief Filters global application events to auto-dismiss on external clicks.
     * @param obj Watched QObject.
     * @param event The event being filtered.
     * @return True if event filtered, false otherwise.
     */
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    /// Visual frame container with subtle border and shadow.
    QFrame* m_frame = nullptr;

    /// Monospace markdown/rich text browser.
    QTextBrowser* m_textBrowser = nullptr;

    /// Periodic timer checking mouse distance to dismiss when cursor leaves.
    QTimer* m_checkTimer = nullptr;
};

}  // namespace gridlock::ui

