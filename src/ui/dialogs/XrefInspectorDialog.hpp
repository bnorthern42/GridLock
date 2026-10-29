/**
 * @file XrefInspectorDialog.hpp
 * @brief Interactive cross-references and call-graph inspection dialog.
 *
 * Implements XrefInspectorDialog, displaying incoming and outgoing code/data
 * references with interactive filtering, structured tables, and live assembly
 * context preview panes.
 */

#pragma once

#include <QDialog>
#include <QString>
#include <QVector>
#include <cstdint>

#include "../views/DisassemblyView.hpp"

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QTableWidget;

namespace gridlock::ui {

/**
 * @class XrefInspectorDialog
 * @brief Dark-slate dialog for exploring incoming and outgoing code/data references.
 *
 * Provides a split-pane inspector showing incoming callers/branches, outgoing
 * function calls, and live assembly context preview around the inspected address.
 */
class XrefInspectorDialog : public QDialog {
    Q_OBJECT

public:
    /**
     * @brief Constructs an XrefInspectorDialog instance.
     * @param parent Optional parent widget.
     */
    explicit XrefInspectorDialog(QWidget* parent = nullptr);

    /** @brief Destructor. */
    ~XrefInspectorDialog() override = default;

    /**
     * @brief Sets the focal memory address under examination.
     * @param address 64-bit virtual memory address.
     */
    void setTargetAddress(uint64_t address);

    /**
     * @brief Populates incoming and outgoing cross-reference lists.
     * @param incoming List of incoming branch/call references.
     * @param outgoing List of outgoing call/jump/data references.
     * @param contextPreview Formatted assembly context string.
     */
    void setXrefs(const QVector<XrefRecord>& incoming,
                  const QVector<XrefRecord>& outgoing,
                  const QString& contextPreview = QString());

    /** @brief Returns count of currently visible filtered incoming references. */
    int incomingRowCount() const;

    /** @brief Returns count of currently visible filtered outgoing references. */
    int outgoingRowCount() const;

    /**
     * @brief Applies substring filter across reference tables.
     * @param text Filter query string.
     */
    void setFilterText(const QString& text);

    /** @brief Returns text currently displayed in the preview pane. */
    QString previewText() const;

    /**
     * @brief Sets text displayed in the context preview pane.
     * @param text Context preview string.
     */
    void setPreviewText(const QString& text);

    /**
     * @brief Activates the incoming reference row at index and emits addressActivated.
     * @param index 0-based row index in filtered incoming table.
     * @return True if valid index and activated; false otherwise.
     */
    bool activateIncomingXref(int index);

    /**
     * @brief Activates the outgoing reference row at index and emits addressActivated.
     * @param index 0-based row index in filtered outgoing table.
     * @return True if valid index and activated; false otherwise.
     */
    bool activateOutgoingXref(int index);

signals:
    /**
     * @brief Emitted when a reference row is activated to navigate the workbench.
     * @param address Target virtual memory address.
     */
    void addressActivated(uint64_t address);

private slots:
    void onFilterChanged(const QString& text);
    void onIncomingCellDoubleClicked(int row, int column);
    void onOutgoingCellDoubleClicked(int row, int column);
    void onIncomingSelectionChanged();
    void onOutgoingSelectionChanged();

private:
    void setupUi();
    void populateTables();
    void updateTableRows(QTableWidget* table, const QVector<XrefRecord>& records);

    uint64_t m_targetAddress = 0;
    QVector<XrefRecord> m_allIncoming;
    QVector<XrefRecord> m_allOutgoing;
    QVector<XrefRecord> m_filteredIncoming;
    QVector<XrefRecord> m_filteredOutgoing;
    QString m_filterText;

    QLabel* m_titleLabel = nullptr;
    QLineEdit* m_filterEdit = nullptr;
    QTableWidget* m_incomingTable = nullptr;
    QTableWidget* m_outgoingTable = nullptr;
    QPlainTextEdit* m_previewEdit = nullptr;
};

}  // namespace gridlock::ui
