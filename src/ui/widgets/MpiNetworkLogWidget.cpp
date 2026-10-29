/**
 * @file MpiNetworkLogWidget.cpp
 * @brief Structured MPI network packet trace and diagnostic logging workstation view.
 *
 * Implements MpiNetworkLogWidget with a 26px #21252b control strip (substring filter,
 * traffic direction combo box, and Clear button) above a flush 6-column QTableWidget
 * displaying TIMESTAMP, SRC RANK, DST RANK, PRIMITIVE, TAG / COMM, and PAYLOAD BYTES.
 */

#include "MpiNetworkLogWidget.hpp"

#include <QAbstractItemView>
#include <QColor>
#include <QDateTime>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPalette>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace gridlock::ui {

/**
 * @brief Constructs a new MpiNetworkLogWidget with a 26px filter strip and 6-column table.
 * @param title Optional title banner string.
 * @param parent Optional parent widget.
 */
MpiNetworkLogWidget::MpiNetworkLogWidget(const QString& title, QWidget* parent)
    : QWidget(parent) {
    Q_UNUSED(title);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Top 26px #21252b control strip hosting traffic filter, direction combo, and Clear button.
    auto* headerBar = new QWidget(this);
    headerBar->setObjectName(QStringLiteral("mpiNetworkLogHeaderBar"));
    headerBar->setFixedHeight(26);
    headerBar->setStyleSheet(QStringLiteral(
        "#mpiNetworkLogHeaderBar { background: #21252b; border-bottom: 1px solid #2c313a; }"));

    auto* headerLayout = new QHBoxLayout(headerBar);
    headerLayout->setContentsMargins(6, 2, 6, 2);
    headerLayout->setSpacing(6);

    // Substring filter input for rank, primitive, tag, or communicator.
    m_filterEdit = new QLineEdit(headerBar);
    m_filterEdit->setObjectName(QStringLiteral("mpiNetworkLogFilterEdit"));
    m_filterEdit->setPlaceholderText(QStringLiteral("Filter traffic / rank / tag..."));
    m_filterEdit->setFixedHeight(22);
    headerLayout->addWidget(m_filterEdit, 1);

    // Direction filter dropdown (All Traffic, Point-to-Point, Collectives).
    m_directionCombo = new QComboBox(headerBar);
    m_directionCombo->setObjectName(QStringLiteral("mpiNetworkDirectionCombo"));
    m_directionCombo->addItems({
        QStringLiteral("All Traffic"),
        QStringLiteral("Point-to-Point"),
        QStringLiteral("Collectives")
    });
    m_directionCombo->setFixedHeight(22);
    m_directionCombo->setMinimumWidth(116);
    headerLayout->addWidget(m_directionCombo);

    // Action button to clear all rows in the network log table.
    m_clearBtn = new QPushButton(QStringLiteral("Clear"), headerBar);
    m_clearBtn->setObjectName(QStringLiteral("mpiNetworkClearBtn"));
    m_clearBtn->setFixedHeight(20);
    headerLayout->addWidget(m_clearBtn);

    layout->addWidget(headerBar);

    // Flush 6-column structured MPI traffic table.
    m_logTable = new QTableWidget(0, 6, this);
    m_logTable->setObjectName(QStringLiteral("mpiNetworkLogTable"));
    m_logTable->setFrameShape(QFrame::NoFrame);
    m_logTable->setAlternatingRowColors(true);
    m_logTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_logTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_logTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_logTable->verticalHeader()->setVisible(false);
    m_logTable->verticalHeader()->setDefaultSectionSize(20);
    m_logTable->setHorizontalHeaderLabels({
        QStringLiteral("TIMESTAMP"),
        QStringLiteral("SRC RANK"),
        QStringLiteral("DST RANK"),
        QStringLiteral("PRIMITIVE"),
        QStringLiteral("TAG / COMM"),
        QStringLiteral("PAYLOAD BYTES")
    });

    QHeaderView* hdr = m_logTable->horizontalHeader();
    hdr->setVisible(true);
    hdr->setMinimumSectionSize(48);
    hdr->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    hdr->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    hdr->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    hdr->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    hdr->setSectionResizeMode(4, QHeaderView::Stretch);
    hdr->setSectionResizeMode(5, QHeaderView::ResizeToContents);

    layout->addWidget(m_logTable, 1);

    // Hidden backing plain-text buffer preserved for legacy stream callers.
    m_textEdit = new QPlainTextEdit(this);
    m_textEdit->setFrameShape(QFrame::NoFrame);
    m_textEdit->setReadOnly(true);
    m_textEdit->setVisible(false);

    // Wire live filter input, direction dropdown, and clear button.
    connect(m_filterEdit, &QLineEdit::textChanged, this, [this](const QString&) {
        applyTrafficFilter();
    });
    connect(m_directionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { applyTrafficFilter(); });
    connect(m_clearBtn, &QPushButton::clicked, this, &MpiNetworkLogWidget::clearLogs);

    // Seed initial baseline MPI session trace entries so the view is immediately informative.
    insertStructuredRow(QStringLiteral("00:00.001"), QStringLiteral("Rank 0..3"),
                        QStringLiteral("ALL"), QStringLiteral("MPI_Init"),
                        QStringLiteral("MPI_COMM_WORLD"), QStringLiteral("0 B"));
    insertStructuredRow(QStringLiteral("00:00.014"), QStringLiteral("Rank 0"),
                        QStringLiteral("ALL"), QStringLiteral("MPI_Bcast"),
                        QStringLiteral("Root=0 MPI_COMM_WORLD"), QStringLiteral("256 B"));
    insertStructuredRow(QStringLiteral("00:00.029"), QStringLiteral("Rank 0"),
                        QStringLiteral("Rank 1"), QStringLiteral("MPI_Isend"),
                        QStringLiteral("Tag=100 MPI_COMM_WORLD"), QStringLiteral("4096 B"));
    insertStructuredRow(QStringLiteral("00:00.031"), QStringLiteral("Rank 1"),
                        QStringLiteral("Rank 0"), QStringLiteral("MPI_Recv"),
                        QStringLiteral("Tag=100 MPI_COMM_WORLD"), QStringLiteral("4096 B"));
}

/**
 * @brief Parses and appends a structured MPI traffic or system log line into the 6-column table.
 * @param message Formatted or unformatted log entry string.
 */
void MpiNetworkLogWidget::appendLog(const QString& message) {
    const QString trimmed = message.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    if (m_textEdit) {
        m_textEdit->appendPlainText(trimmed);
    }

    // Attempt pipe-delimited parsing: "[timestamp] Rank X -> Rank Y | MPI_Primitive | Tag=... | N B"
    if (trimmed.contains(QLatin1Char('|'))) {
        const QStringList parts = trimmed.split(QLatin1Char('|'));
        QString timestamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("HH:mm:ss.zzz"));
        QString routePart = parts.value(0).trimmed();
        static const QRegularExpression tsRx(QStringLiteral("^\\[([^\\]]+)\\]\\s*(.*)$"));
        const QRegularExpressionMatch tsMatch = tsRx.match(routePart);
        if (tsMatch.hasMatch()) {
            timestamp = tsMatch.captured(1).trimmed();
            routePart = tsMatch.captured(2).trimmed();
        }
        QString srcRank = QStringLiteral("Rank 0");
        QString dstRank = QStringLiteral("ALL");
        if (routePart.contains(QStringLiteral("->"))) {
            const QStringList endpoints = routePart.split(QStringLiteral("->"));
            srcRank = endpoints.value(0).trimmed();
            dstRank = endpoints.value(1).trimmed();
        } else if (!routePart.isEmpty()) {
            srcRank = routePart;
        }
        const QString primitive = parts.value(1, QStringLiteral("MPI_Send")).trimmed();
        const QString tagComm = parts.value(2, QStringLiteral("MPI_COMM_WORLD")).trimmed();
        const QString payload = parts.value(3, QStringLiteral("0 B")).trimmed();
        insertStructuredRow(timestamp, srcRank, dstRank, primitive, tagComm, payload, false);
        return;
    }

    // Fallback for system/session diagnostic messages (e.g. target load or build status).
    const QString ts = QDateTime::currentDateTimeUtc().toString(QStringLiteral("HH:mm:ss.zzz"));
    const bool isErr = trimmed.startsWith(QStringLiteral("CRITICAL"), Qt::CaseInsensitive) ||
                       trimmed.contains(QStringLiteral("error"), Qt::CaseInsensitive) ||
                       trimmed.contains(QStringLiteral("failed"), Qt::CaseInsensitive);
    insertStructuredRow(ts, QStringLiteral("HOST"), QStringLiteral("SESSION"),
                        isErr ? QStringLiteral("DIAG_ERR") : QStringLiteral("SYS_EVENT"),
                        trimmed, QStringLiteral("-"), isErr);
}

/**
 * @brief Returns the number of structured rows currently stored in the MPI network log table.
 * @return Row count of the 6-column traffic table.
 */
int MpiNetworkLogWidget::logRowCount() const {
    return m_logTable ? m_logTable->rowCount() : 0;
}

/**
 * @brief Convenience overload delegating to appendText("stdout", text).
 * @param text Log message string.
 */
void MpiNetworkLogWidget::appendText(const QString& text) {
    appendText(QStringLiteral("stdout"), text);
}

/**
 * @brief Appends categorized text into both the structured table and backing buffer.
 * @param category Category identifier string.
 * @param text Content to append.
 */
void MpiNetworkLogWidget::appendText(const QString& category, const QString& text) {
    const bool isErr = (category == QStringLiteral("stderr") || category == QStringLiteral("error"));
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        if (isErr) {
            appendError(line);
        } else {
            appendLog(line);
        }
    }
}

/**
 * @brief Appends an error message in alert red formatting into the structured table.
 * @param text Error message text.
 */
void MpiNetworkLogWidget::appendError(const QString& text) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    if (m_textEdit) {
        m_textEdit->appendPlainText(trimmed);
    }
    const QString ts = QDateTime::currentDateTimeUtc().toString(QStringLiteral("HH:mm:ss.zzz"));
    insertStructuredRow(ts, QStringLiteral("HOST"), QStringLiteral("ALL"),
                        QStringLiteral("MPI_ERR"), trimmed, QStringLiteral("-"), true);
}

/**
 * @brief Clears all recorded MPI network traffic rows from the table.
 */
void MpiNetworkLogWidget::clearLogs() {
    if (m_logTable) {
        m_logTable->setRowCount(0);
    }
    if (m_textEdit) {
        m_textEdit->clear();
    }
}

/**
 * @brief Inserts a structured 6-column row into the MPI traffic table.
 *
 * Applies semantic foreground accents across the 6 columns:
 * - Error rows highlight all columns in coral red (#e06c75).
 * - Normal rows highlight SRC RANK and DST RANK in teal (#4ec9b0),
 *   PRIMITIVE in sky blue (#61afef), and metadata in slate (#d7dae0).
 *
 * @param timestamp Event timestamp string (e.g., "12:00:01.042").
 * @param srcRank Source MPI rank label (e.g., "Rank 0").
 * @param dstRank Destination MPI rank or collective group label (e.g., "Rank 1" or "ALL").
 * @param primitive MPI primitive or system category (e.g., "MPI_Send").
 * @param tagComm Message tag and communicator (e.g., "Tag=0 MPI_COMM_WORLD").
 * @param payloadBytes Payload byte count or message summary.
 * @param isError True to style the row with alert red foreground accents.
 */
void MpiNetworkLogWidget::insertStructuredRow(const QString& timestamp, const QString& srcRank,
                                              const QString& dstRank, const QString& primitive,
                                              const QString& tagComm, const QString& payloadBytes,
                                              bool isError) {
    if (!m_logTable) {
        return;
    }
    // Append a new row at the end of the 6-column traffic table.
    const int row = m_logTable->rowCount();
    m_logTable->insertRow(row);

    // Populate each cell with semantic foreground colors matching the workstation palette.
    const QStringList cols = {timestamp, srcRank, dstRank, primitive, tagComm, payloadBytes};
    for (int col = 0; col < cols.size(); ++col) {
        auto* item = new QTableWidgetItem(cols[col]);
        if (isError) {
            // Coral red foreground for MPI protocol or compilation errors.
            item->setForeground(QColor("#e06c75"));
        } else if (col == 3) {
            // Sky blue foreground for MPI primitive names (MPI_Send, MPI_Recv, etc.).
            item->setForeground(QColor("#61afef"));
        } else if (col == 1 || col == 2) {
            // Teal foreground for source and destination rank identifiers.
            item->setForeground(QColor("#4ec9b0"));
        } else {
            // Standard light-slate foreground for timestamps, tags, and byte counts.
            item->setForeground(QColor("#d7dae0"));
        }
        m_logTable->setItem(row, col, item);
    }
    // Re-evaluate active filter criteria and keep latest traffic visible at the bottom.
    applyTrafficFilter();
    m_logTable->scrollToBottom();
}

/**
 * @brief Filters visible table rows by the query text and traffic direction combo box.
 *
 * Evaluates both the traffic direction mode (All Traffic, Point-to-Point, Collectives)
 * and the case-insensitive substring query across all 6 columns of every row.
 */
void MpiNetworkLogWidget::applyTrafficFilter() {
    if (!m_logTable) {
        return;
    }
    const QString query = m_filterEdit ? m_filterEdit->text().trimmed() : QString();
    const QString dirMode = m_directionCombo ? m_directionCombo->currentText() : QStringLiteral("All Traffic");

    for (int row = 0; row < m_logTable->rowCount(); ++row) {
        const QString dstText = m_logTable->item(row, 2) ? m_logTable->item(row, 2)->text() : QString();
        const QString primText = m_logTable->item(row, 3) ? m_logTable->item(row, 3)->text() : QString();
        const bool isCollective = (dstText.compare(QStringLiteral("ALL"), Qt::CaseInsensitive) == 0) ||
                                  primText.contains(QStringLiteral("Bcast"), Qt::CaseInsensitive) ||
                                  primText.contains(QStringLiteral("Barrier"), Qt::CaseInsensitive) ||
                                  primText.contains(QStringLiteral("Reduce"), Qt::CaseInsensitive) ||
                                  primText.contains(QStringLiteral("Init"), Qt::CaseInsensitive);

        bool dirMatch = true;
        if (dirMode == QStringLiteral("Point-to-Point")) {
            dirMatch = !isCollective;
        } else if (dirMode == QStringLiteral("Collectives")) {
            dirMatch = isCollective;
        }

        bool textMatch = query.isEmpty();
        if (!textMatch) {
            for (int col = 0; col < m_logTable->columnCount(); ++col) {
                if (const QTableWidgetItem* it = m_logTable->item(row, col)) {
                    if (it->text().contains(query, Qt::CaseInsensitive)) {
                        textMatch = true;
                        break;
                    }
                }
            }
        }
        m_logTable->setRowHidden(row, !(dirMatch && textMatch));
    }
}

}  // namespace gridlock::ui


