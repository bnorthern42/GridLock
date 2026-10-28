#pragma once

/**
 * @file MpiNetworkLogWidget.hpp
 * @brief MPI network packet trace and event logging console.
 *
 * Implements MpiNetworkLogWidget, a high-throughput console widget for logging
 * MPI point-to-point and collective communications, timestamps, tags, and protocol errors.
 */

#include <QWidget>
#include <QPlainTextEdit>
#include <QString>

namespace gridlock::ui {

/**
 * @class MpiNetworkLogWidget
 * @brief Console widget displaying categorized MPI network traces and error logs.
 */
class MpiNetworkLogWidget : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief Constructs a new MpiNetworkLogWidget.
     * @param title Title banner string.
     * @param parent Optional parent widget.
     */
    explicit MpiNetworkLogWidget(const QString& title, QWidget *parent = nullptr);

    /**
     * @brief Destructor.
     */
    ~MpiNetworkLogWidget() override = default;

    /**
     * @brief Appends raw unformatted text to the console.
     * @param text Message string.
     */
    void appendText(const QString& text);

    /**
     * @brief Appends categorized text (e.g., "[SEND]", "[RECV]") with color formatting.
     * @param category Category identifier tag.
     * @param text Log message string.
     */
    void appendText(const QString& category, const QString& text);

    /**
     * @brief Appends an error message highlighted in red.
     * @param text Error message string.
     */
    void appendError(const QString& text);

private:
    /// Monospace read-only text buffer.
    QPlainTextEdit* m_textEdit = nullptr;
};

}  // namespace gridlock::ui

