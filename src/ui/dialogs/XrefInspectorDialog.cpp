/**
 * @file XrefInspectorDialog.cpp
 * @brief Implementation of the interactive cross-references and call-graph dialog.
 *
 * Implements XrefInspectorDialog, providing a dual-table interface for incoming
 * and outgoing code references, real-time substring filtering, context previews,
 * and synchronized double-click address navigation.
 */

#include "XrefInspectorDialog.hpp"

#include <QColor>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <algorithm>

namespace gridlock::ui {

/**
 * @brief Constructs a new XrefInspectorDialog widget.
 *
 * Initializes the dialog geometry, sets up the dark-slate theme styles,
 * constructs the filter bar, split table layouts, and preview editor.
 *
 * @param parent Optional parent widget.
 */
XrefInspectorDialog::XrefInspectorDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(QStringLiteral("Cross References & Call Graph"));
    resize(850, 520);
    setupUi();
}

/**
 * @brief Builds the visual hierarchy and configures signal-slot connections.
 */
void XrefInspectorDialog::setupUi() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(6);

    // Header bar with target title and search filter input
    auto* headerWidget = new QWidget(this);
    auto* headerLayout = new QHBoxLayout(headerWidget);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(8);

    m_titleLabel = new QLabel(QStringLiteral("XREFS & CALL GRAPH: 0x00000000"), headerWidget);
    QFont titleFont(QStringLiteral("JetBrains Mono"), 10, QFont::Bold);
    m_titleLabel->setFont(titleFont);
    m_titleLabel->setStyleSheet(QStringLiteral("color: #61afef;"));
    headerLayout->addWidget(m_titleLabel);

    m_filterEdit = new QLineEdit(headerWidget);
    m_filterEdit->setPlaceholderText(
        QStringLiteral("Filter xrefs by address, symbol, or mnemonic..."));
    m_filterEdit->setClearButtonEnabled(true);
    m_filterEdit->setStyleSheet(
        QStringLiteral("QLineEdit { background: #21252b; color: #abb2bf; "
                       "border: 1px solid #3b4048; border-radius: 3px; padding: 3px 6px; "
                       "font-family: 'JetBrains Mono'; font-size: 11px; }"));
    headerLayout->addWidget(m_filterEdit, 1);
    mainLayout->addWidget(headerWidget);

    // Primary vertical splitter dividing tables from context preview pane
    auto* mainSplitter = new QSplitter(Qt::Vertical, this);
    mainSplitter->setHandleWidth(4);

    // Horizontal splitter dividing incoming callers from outgoing calls
    auto* tableSplitter = new QSplitter(Qt::Horizontal, mainSplitter);
    tableSplitter->setHandleWidth(4);

    // Helper lambda to create standardized reference tables
    auto createTable = [this](const QString& title) -> QTableWidget* {
        auto* container = new QWidget(this);
        auto* lay = new QVBoxLayout(container);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(2);

        auto* lbl = new QLabel(title, container);
        lbl->setStyleSheet(QStringLiteral("color: #98c379; font-weight: bold; font-size: 10px;"));
        lay->addWidget(lbl);

        auto* tbl = new QTableWidget(container);
        tbl->setColumnCount(4);
        tbl->setHorizontalHeaderLabels({QStringLiteral("TYPE"), QStringLiteral("FROM"),
                                        QStringLiteral("TO"), QStringLiteral("INSTRUCTION")});
        tbl->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        tbl->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        tbl->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        tbl->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
        tbl->verticalHeader()->setVisible(false);
        tbl->setSelectionBehavior(QAbstractItemView::SelectRows);
        tbl->setSelectionMode(QAbstractItemView::SingleSelection);
        tbl->setEditTriggers(QAbstractItemView::NoEditTriggers);
        tbl->setStyleSheet(
            QStringLiteral("QTableWidget { background: #1e2227; color: #abb2bf; "
                           "gridline-color: #2c313c; font-family: 'JetBrains Mono'; font-size: 11px; }"
                           "QHeaderView::section { background: #282c34; color: #5c6370; "
                           "padding: 2px; border: 1px solid #1e2227; font-size: 10px; }"
                           "QTableWidget::item:selected { background: #2c313a; color: #61afef; }"));
        lay->addWidget(tbl);
        return tbl;
    };

    m_incomingTable = createTable(QStringLiteral("XREFS TO (INCOMING CALLERS / BRANCHES)"));
    m_outgoingTable = createTable(QStringLiteral("XREFS FROM (OUTGOING CALLS / REFERENCES)"));

    auto* leftContainer = m_incomingTable->parentWidget();
    auto* rightContainer = m_outgoingTable->parentWidget();
    tableSplitter->addWidget(leftContainer);
    tableSplitter->addWidget(rightContainer);
    tableSplitter->setSizes({400, 400});
    mainSplitter->addWidget(tableSplitter);

    // Bottom pane: Context preview editor
    auto* previewContainer = new QWidget(mainSplitter);
    auto* previewLayout = new QVBoxLayout(previewContainer);
    previewLayout->setContentsMargins(0, 0, 0, 0);
    previewLayout->setSpacing(2);

    auto* previewLabel = new QLabel(QStringLiteral("ASSEMBLY & CODE CONTEXT PREVIEW"), previewContainer);
    previewLabel->setStyleSheet(QStringLiteral("color: #e5c07b; font-weight: bold; font-size: 10px;"));
    previewLayout->addWidget(previewLabel);

    m_previewEdit = new QPlainTextEdit(previewContainer);
    m_previewEdit->setReadOnly(true);
    m_previewEdit->setFont(QFont(QStringLiteral("JetBrains Mono"), 10));
    m_previewEdit->setStyleSheet(
        QStringLiteral("QPlainTextEdit { background: #14161a; color: #abb2bf; "
                       "border: 1px solid #282c34; font-family: 'JetBrains Mono'; }"));
    previewLayout->addWidget(m_previewEdit);

    mainSplitter->addWidget(previewContainer);
    mainSplitter->setSizes({340, 140});
    mainLayout->addWidget(mainSplitter);

    // Connect signals for interactive user navigation and real-time filtering
    connect(m_filterEdit, &QLineEdit::textChanged, this, &XrefInspectorDialog::onFilterChanged);
    connect(m_incomingTable, &QTableWidget::cellDoubleClicked,
            this, &XrefInspectorDialog::onIncomingCellDoubleClicked);
    connect(m_outgoingTable, &QTableWidget::cellDoubleClicked,
            this, &XrefInspectorDialog::onOutgoingCellDoubleClicked);
    connect(m_incomingTable, &QTableWidget::itemSelectionChanged,
            this, &XrefInspectorDialog::onIncomingSelectionChanged);
    connect(m_outgoingTable, &QTableWidget::itemSelectionChanged,
            this, &XrefInspectorDialog::onOutgoingSelectionChanged);
}

/**
 * @brief Sets the focal memory address and updates the dialog header label.
 * @param address 64-bit virtual memory address.
 */
void XrefInspectorDialog::setTargetAddress(uint64_t address) {
    m_targetAddress = address;
    if (m_titleLabel) {
        m_titleLabel->setText(
            QStringLiteral("XREFS & CALL GRAPH: 0x%1")
                .arg(address, 8, 16, QChar('0')));
    }
}

/**
 * @brief Populates the incoming and outgoing cross-reference collections.
 *
 * Updates internal collections, refreshes visible table rows according
 * to active filter text, and initializes the assembly context preview pane.
 *
 * @param incoming Vector of incoming branch and call references.
 * @param outgoing Vector of outgoing calls, jumps, and memory references.
 * @param contextPreview Formatted assembly preview string.
 */
void XrefInspectorDialog::setXrefs(const QVector<XrefRecord>& incoming,
                                   const QVector<XrefRecord>& outgoing,
                                   const QString& contextPreview) {
    m_allIncoming = incoming;
    m_allOutgoing = outgoing;
    populateTables();
    if (!contextPreview.isEmpty() && m_previewEdit) {
        m_previewEdit->setPlainText(contextPreview);
    }
}

/**
 * @brief Returns number of visible rows in the incoming reference table.
 */
int XrefInspectorDialog::incomingRowCount() const {
    return static_cast<int>(m_filteredIncoming.size());
}

/**
 * @brief Returns number of visible rows in the outgoing reference table.
 */
int XrefInspectorDialog::outgoingRowCount() const {
    return static_cast<int>(m_filteredOutgoing.size());
}

/**
 * @brief Applies a new filter query text and updates table views.
 * @param text Query substring to match against instructions and addresses.
 */
void XrefInspectorDialog::setFilterText(const QString& text) {
    m_filterText = text.trimmed();
    if (m_filterEdit && m_filterEdit->text() != m_filterText) {
        m_filterEdit->setText(m_filterText);
    }
    populateTables();
}

/**
 * @brief Returns the text contents of the context preview editor.
 */
QString XrefInspectorDialog::previewText() const {
    return m_previewEdit ? m_previewEdit->toPlainText() : QString();
}

/**
 * @brief Sets the text contents of the context preview editor.
 * @param text Text snippet to display.
 */
void XrefInspectorDialog::setPreviewText(const QString& text) {
    if (m_previewEdit) {
        m_previewEdit->setPlainText(text);
    }
}

/**
 * @brief Activates an incoming reference row and emits the addressActivated signal.
 * @param index 0-based row index in filtered incoming table.
 * @return True if index is valid and signal was emitted.
 */
bool XrefInspectorDialog::activateIncomingXref(int index) {
    if (index < 0 || index >= m_filteredIncoming.size()) {
        return false;
    }
    const uint64_t callerAddr = m_filteredIncoming.at(index).fromAddress;
    emit addressActivated(callerAddr);
    return true;
}

/**
 * @brief Activates an outgoing reference row and emits the addressActivated signal.
 * @param index 0-based row index in filtered outgoing table.
 * @return True if index is valid and signal was emitted.
 */
bool XrefInspectorDialog::activateOutgoingXref(int index) {
    if (index < 0 || index >= m_filteredOutgoing.size()) {
        return false;
    }
    const uint64_t targetAddr = m_filteredOutgoing.at(index).toAddress;
    emit addressActivated(targetAddr);
    return true;
}

/**
 * @brief Handles text changes in the search filter line edit.
 * @param text Updated filter query.
 */
void XrefInspectorDialog::onFilterChanged(const QString& text) {
    m_filterText = text.trimmed();
    populateTables();
}

/**
 * @brief Handles double-click events on incoming table cells.
 */
void XrefInspectorDialog::onIncomingCellDoubleClicked(int row, int /*column*/) {
    activateIncomingXref(row);
}

/**
 * @brief Handles double-click events on outgoing table cells.
 */
void XrefInspectorDialog::onOutgoingCellDoubleClicked(int row, int /*column*/) {
    activateOutgoingXref(row);
}

/**
 * @brief Updates the preview pane when user selects an incoming reference row.
 */
void XrefInspectorDialog::onIncomingSelectionChanged() {
    if (!m_incomingTable || !m_previewEdit) return;
    const int row = m_incomingTable->currentRow();
    if (row >= 0 && row < m_filteredIncoming.size()) {
        const auto& xr = m_filteredIncoming.at(row);
        const QString info = QStringLiteral(
            "[INCOMING XREF CONTEXT]\n"
            "Origin:      0x%1\n"
            "Target:      0x%2\n"
            "Type:        %3\n"
            "Instruction: %4\n")
            .arg(xr.fromAddress, 8, 16, QChar('0'))
            .arg(xr.toAddress, 8, 16, QChar('0'))
            .arg(xr.type)
            .arg(xr.instructionText);
        m_previewEdit->setPlainText(info);
    }
}

/**
 * @brief Updates the preview pane when user selects an outgoing reference row.
 */
void XrefInspectorDialog::onOutgoingSelectionChanged() {
    if (!m_outgoingTable || !m_previewEdit) return;
    const int row = m_outgoingTable->currentRow();
    if (row >= 0 && row < m_filteredOutgoing.size()) {
        const auto& xr = m_filteredOutgoing.at(row);
        const QString info = QStringLiteral(
            "[OUTGOING REFERENCE / CALL CONTEXT]\n"
            "Origin:      0x%1\n"
            "Target:      0x%2\n"
            "Type:        %3\n"
            "Instruction: %4\n")
            .arg(xr.fromAddress, 8, 16, QChar('0'))
            .arg(xr.toAddress, 8, 16, QChar('0'))
            .arg(xr.type)
            .arg(xr.instructionText);
        m_previewEdit->setPlainText(info);
    }
}

/**
 * @brief Filters and populates both the incoming and outgoing reference tables.
 */
void XrefInspectorDialog::populateTables() {
    auto matches = [this](const XrefRecord& r) -> bool {
        if (m_filterText.isEmpty()) {
            return true;
        }
        const QString q = m_filterText.toLower();
        const QString fromStr = QStringLiteral("0x%1").arg(r.fromAddress, 0, 16);
        const QString toStr = QStringLiteral("0x%1").arg(r.toAddress, 0, 16);
        return r.type.toLower().contains(q) ||
               r.mnemonic.toLower().contains(q) ||
               r.instructionText.toLower().contains(q) ||
               r.targetSymbol.toLower().contains(q) ||
               fromStr.contains(q) ||
               toStr.contains(q);
    };

    // Filter incoming records
    m_filteredIncoming.clear();
    for (const auto& r : m_allIncoming) {
        if (matches(r)) {
            m_filteredIncoming.append(r);
        }
    }

    // Filter outgoing records
    m_filteredOutgoing.clear();
    for (const auto& r : m_allOutgoing) {
        if (matches(r)) {
            m_filteredOutgoing.append(r);
        }
    }

    // Populate both UI tables
    updateTableRows(m_incomingTable, m_filteredIncoming);
    updateTableRows(m_outgoingTable, m_filteredOutgoing);
}

/**
 * @brief Renders reference records into standard 4-column QTableWidget rows.
 * @param table Pointer to destination table widget.
 * @param records Reference records to display.
 */
void XrefInspectorDialog::updateTableRows(QTableWidget* table,
                                          const QVector<XrefRecord>& records) {
    if (!table) return;
    table->setRowCount(0);
    table->setRowCount(static_cast<int>(records.size()));

    for (int i = 0; i < records.size(); ++i) {
        const auto& r = records[i];

        auto* typeItem = new QTableWidgetItem(r.type);
        // Color-code reference types for fast visual differentiation
        if (r.type == QStringLiteral("CALL")) {
            typeItem->setForeground(QColor(QStringLiteral("#e5c07b")));
        } else if (r.type == QStringLiteral("CJMP")) {
            typeItem->setForeground(QColor(QStringLiteral("#c678dd")));
        } else if (r.type == QStringLiteral("JMP")) {
            typeItem->setForeground(QColor(QStringLiteral("#61afef")));
        } else {
            typeItem->setForeground(QColor(QStringLiteral("#98c379")));
        }

        auto* fromItem = new QTableWidgetItem(
            QStringLiteral("0x%1").arg(r.fromAddress, 8, 16, QChar('0')));
        auto* toItem = new QTableWidgetItem(
            QStringLiteral("0x%1").arg(r.toAddress, 8, 16, QChar('0')));
        auto* insnItem = new QTableWidgetItem(r.instructionText);

        table->setItem(i, 0, typeItem);
        table->setItem(i, 1, fromItem);
        table->setItem(i, 2, toItem);
        table->setItem(i, 3, insnItem);
    }
}

}  // namespace gridlock::ui
