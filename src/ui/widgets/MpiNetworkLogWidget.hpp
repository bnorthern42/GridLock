#pragma once

/**
 * @file MpiNetworkLogWidget.hpp
 * @brief Structured MPI network packet trace and event logging workstation view.
 *
 * Implements MpiNetworkLogWidget, combining a 26px #21252b filter/direction header
 * strip with a flush 6-column QTableWidget (TIMESTAMP, SRC RANK, DST RANK,
 * PRIMITIVE, TAG / COMM, PAYLOAD BYTES) for inspecting point-to-point and
 * collective MPI communications.
 */

#include <QComboBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QString>
#include <QTableWidget>
#include <QWidget>

namespace gridlock::ui {

/**
 * @class MpiNetworkLogWidget
 * @brief Workstation table view displaying categorized MPI network traces and diagnostic events.
 */
class MpiNetworkLogWidget : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief Constructs a new MpiNetworkLogWidget with a 26px control strip and 6-column table.
     * @param title Title banner string.
     * @param parent Optional parent widget.
     */
    explicit MpiNetworkLogWidget(const QString& title, QWidget* parent = nullptr);

    /**
     * @brief Destructor.
     */
    ~MpiNetworkLogWidget() override = default;

    /**
     * @brief Parses and appends a structured MPI traffic or system log line into the 6-column table.
     * @param message Formatted or unformatted log entry string.
     */
    void appendLog(const QString& message);

    /**
     * @brief Returns the number of structured rows currently stored in the MPI network log table.
     * @return Row count of the 6-column traffic table.
     */
    int logRowCount() const;

    /**
     * @brief Appends raw unformatted text to the log table and backing buffer.
     * @param text Message string.
     */
    void appendText(const QString& text);

    /**
     * @brief Appends categorized text (e.g., "[SEND]", "[RECV]", "console") into the log table.
     * @param category Category identifier tag.
     * @param text Log message string.
     */
    void appendText(const QString& category, const QString& text);

    /**
     * @brief Appends an error message highlighted in red into the log table.
     * @param text Error message string.
     */
    void appendError(const QString& text);

    /**
     * @brief Clears all recorded MPI network traffic rows from the table.
     */
    void clearLogs();

private:
    /**
     * @brief Inserts a structured 6-column row into the MPI traffic table.
     * @param timestamp Event timestamp string (e.g., "12:00:01.042").
     * @param srcRank Source MPI rank label (e.g., "Rank 0").
     * @param dstRank Destination MPI rank or collective group label (e.g., "Rank 1" or "ALL").
     * @param primitive MPI primitive or system category (e.g., "MPI_Send").
     * @param tagComm Message tag and communicator (e.g., "Tag=0 MPI_COMM_WORLD").
     * @param payloadBytes Payload byte count or message summary.
     * @param isError True to style the row with alert red foreground accents.
     */
    void insertStructuredRow(const QString& timestamp, const QString& srcRank,
                             const QString& dstRank, const QString& primitive,
                             const QString& tagComm, const QString& payloadBytes,
                             bool isError = false);

    /**
     * @brief Filters visible table rows by the query text and traffic direction combo box.
     */
    void applyTrafficFilter();

    /// Substring filter input for rank, primitive, tag, or communicator.
    QLineEdit* m_filterEdit = nullptr;
    /// Traffic direction filter selector (All Traffic, Point-to-Point, Collectives).
    QComboBox* m_directionCombo = nullptr;
    /// Action button clearing all rows in the network log table.
    QPushButton* m_clearBtn = nullptr;
    /// 6-column structured MPI network traffic table.
    QTableWidget* m_logTable = nullptr;
    /// Hidden backing plain-text buffer preserved for legacy stream compatibility.
    QPlainTextEdit* m_textEdit = nullptr;
};

}  // namespace gridlock::ui


