/**
 * @file DifferentialGrid.cpp
 * @brief Differential multi-rank variable grid implementation.
 *
 * Implements the DifferentialGrid table widget, which tracks watched expressions
 * across all active MPI ranks and detects statistical divergence using majority-vote
 * consensus highlighting.
 */

#include "DifferentialGrid.hpp"
#include "../../core/managers/ThemeManager.hpp"

#include <algorithm>

#include <QHeaderView>
#include <QPalette>

namespace gridlock::ui {

/**
 * @brief Constructs a new DifferentialGrid widget.
 *
 * Configures the initial 1x1 table layout with an inline "New Variable"
 * input cell at (0, 0), enforces QHeaderView::Stretch across watched columns,
 * and synchronizes verticalHeader() and viewport() background palettes with the active theme.
 *
 * @param parent Optional parent widget.
 */
DifferentialGrid::DifferentialGrid(QWidget *parent) : QTableWidget(parent) {
    setFrameShape(QFrame::NoFrame);
    setAlternatingRowColors(true);
    setTextElideMode(Qt::ElideRight);
    horizontalHeader()->setVisible(true);
    horizontalHeader()->setMinimumSectionSize(64);
    horizontalHeader()->setDefaultSectionSize(96);
    horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    horizontalHeader()->setStretchLastSection(true);
    horizontalHeader()->setTextElideMode(Qt::ElideRight);
    verticalHeader()->setDefaultSectionSize(22);

    reloadStyle();
    connect(&gridlock::core::ThemeManager::instance(),
            &gridlock::core::ThemeManager::themeModeChanged,
            this, [this](bool) { reloadStyle(); });

    // Seed initial row/col for inline watch input
    setRowCount(1);
    setColumnCount(1);
    setVerticalHeaderItem(0, new QTableWidgetItem("Add Watch ->"));
    setHorizontalHeaderItem(0, new QTableWidgetItem("New Variable"));
    setItem(0, 0, new QTableWidgetItem(""));
    setColumnWidth(0, 140);
    
    // When user types an expression in cell (0, 0), dispatch watchVariableAdded
    connect(this, &QTableWidget::cellChanged, this, [this](int row, int col) {
        if (row == 0 && col == 0) {
            QTableWidgetItem* cell = item(row, col);
            if (!cell) return;
            QString newVarName = cell->text().trimmed();
            if (!newVarName.isEmpty() && newVarName != "...") {
                emit watchVariableAdded(newVarName);
                
                // Temporarily show placeholder while watch request dispatches
                this->blockSignals(true);
                cell->setText("...");
                this->blockSignals(false);
            }
        }
    });
}

/**
 * @brief Returns whether horizontal header columns are configured to stretch evenly across the dock width.
 * @return True if QHeaderView::Stretch is active on the horizontal header; false otherwise.
 */
bool DifferentialGrid::horizontalHeaderStretchEnabled() const {
    return horizontalHeader() != nullptr &&
           horizontalHeader()->sectionResizeMode(0) == QHeaderView::Stretch;
}

/**
 * @brief Updates all variable entries for a specific rank from a hash map.
 * @param rankId Zero-based rank index.
 * @param variables Mapping of variable identifiers to evaluated string values.
 */
void DifferentialGrid::setVariableData(int rankId, const QHash<QString, QString>& variables) {
    for (auto it = variables.constBegin(); it != variables.constEnd(); ++it) {
        updateVariableDisplay(rankId, it.key(), it.value());
    }
}

/**
 * @brief Adds a new column header for the given variable if it doesn't already exist.
 * @param name Variable identifier or expression string.
 */
void DifferentialGrid::addVariableColumn(const QString& name) {
    int col = -1;
    for (int c = 0; c < columnCount(); ++c) {
        if (horizontalHeaderItem(c) && horizontalHeaderItem(c)->text() == name) {
            col = c;
            break;
        }
    }
    
    // Allocate new column if not present
    if (col == -1) {
        col = columnCount();
        setColumnCount(col + 1);
        auto* hdrItem = new QTableWidgetItem(name);
        hdrItem->setToolTip(name);
        setHorizontalHeaderItem(col, hdrItem);
        for (int r = 0; r < rowCount(); ++r) {
            setItem(r, col, new QTableWidgetItem("-"));
        }
    }
}

/**
 * @brief Retrieves the list of watched expressions currently tracked as columns.
 * @return Vector of expression strings.
 */
std::vector<std::string> DifferentialGrid::getWatchExpressions() const {
    std::vector<std::string> watches;
    for (int c = 1; c < columnCount(); ++c) {
        if (auto* item = horizontalHeaderItem(c)) {
            QString name = item->text();
            watches.push_back(name.toStdString());
        }
    }
    return watches;
}

/**
 * @brief Resets the grid back to its initial empty state with only the watch prompt.
 */
void DifferentialGrid::clearWatches() {
    setColumnCount(1);
    setHorizontalHeaderItem(0, new QTableWidgetItem("New Variable"));
    setRowCount(1);
    setItem(0, 0, new QTableWidgetItem(""));
}

/**
 * @brief Populates or updates an evaluation cell for a specific rank and variable.
 *
 * Dynamically expands table rows and columns as new ranks or variables are encountered.
 * Recomputes differential divergence highlights after insertion.
 *
 * @param rankId Rank index (0-based).
 * @param varName Variable name.
 * @param value String value or error description.
 */
void DifferentialGrid::updateVariableDisplay(int rankId, const QString& varName, const QString& value) {
    int targetRow = rankId + 1;
    
    // Find or create column for varName
    int col = -1;
    for (int c = 0; c < columnCount(); ++c) {
        if (horizontalHeaderItem(c) && horizontalHeaderItem(c)->text() == varName) {
            col = c;
            break;
        }
    }
    
    if (col == -1) {
        col = columnCount();
        setColumnCount(col + 1);
        auto* hdrItem = new QTableWidgetItem(varName);
        hdrItem->setToolTip(varName);
        setHorizontalHeaderItem(col, hdrItem);
        for (int r = 0; r < rowCount(); ++r) {
            setItem(r, col, new QTableWidgetItem("-"));
        }
        
        // Clear the "..." in the new variable cell if it matches
        QTableWidgetItem* inputCell = item(0, 0);
        if (inputCell && inputCell->text() == "...") {
            blockSignals(true);
            inputCell->setText("");
            blockSignals(false);
        }
    }
    
    // Dynamically expand rows to accommodate rank index
    if (targetRow >= rowCount()) {
        int oldRow = rowCount();
        setRowCount(targetRow + 1);
        for (int r = oldRow; r <= targetRow; ++r) {
            setVerticalHeaderItem(r, new QTableWidgetItem(QString("Rank %1").arg(r - 1)));
            for (int c = 0; c < columnCount(); ++c) {
                setItem(r, c, new QTableWidgetItem("-"));
            }
        }
    }
    
    // Set cell contents
    QTableWidgetItem* cellItem = item(targetRow, col);
    if (!cellItem) {
        cellItem = new QTableWidgetItem(value);
        setItem(targetRow, col, cellItem);
    } else {
        cellItem->setText(value);
    }
    
    // Dim out of scope variables in italic gray
    if (value == "<Out of Scope>") {
        cellItem->setForeground(Qt::gray);
        QFont font = cellItem->font();
        font.setItalic(true);
        cellItem->setFont(font);
    } else {
        cellItem->setForeground(QBrush());
        QFont font = cellItem->font();
        font.setItalic(false);
        cellItem->setFont(font);
    }
    
    // Recalculate divergence across all ranks for this variable
    updateHighlights();
}

/**
 * @brief Inspects all watched columns to detect and highlight divergent values.
 *
 * Uses frequency voting to determine the majority/consensus value in each column.
 * Any rank displaying a value different from the majority is highlighted with
 * a distinct light-red alert background.
 */
void DifferentialGrid::updateHighlights() {
    for (int col = 1; col < columnCount(); ++col) {
        // Count value frequency across active rank rows
        std::unordered_map<QString, int> counts;
        for (int row = 1; row < rowCount(); ++row) {
            QTableWidgetItem* it = item(row, col);
            if (it && it->text() != "-") counts[it->text()]++;
        }
        
        // Identify majority value consensus
        QString majorityStr;
        int maxCount = 0;
        for (const auto& kv : counts) {
            if (kv.second > maxCount) {
                maxCount = kv.second;
                majorityStr = kv.first;
            }
        }
        
        // Highlight minority/divergent cells
        for (int row = 1; row < rowCount(); ++row) {
            QTableWidgetItem* it = item(row, col);
            if (it) {
                if (it->text() != "-" && it->text() != majorityStr && counts.size() > 1) {
                    it->setBackground(QColor(255, 200, 200));
                } else {
                    it->setBackground(QBrush());
                }
            }
        }
    }
}

/**
 * @brief Returns active table canvas background color according to the theme.
 */
QColor DifferentialGrid::activeCanvasBackgroundColor() const {
    return palette().color(QPalette::Base);
}

/**
 * @brief Reloads widget styling according to the active theme palette.
 */
void DifferentialGrid::reloadStyle() {
    auto& tm = gridlock::core::ThemeManager::instance();
    const auto pal = tm.paletteForTheme(tm.activeThemeId());
    const QColor canvasColor(pal.canvasHex);
    const QColor headerColor(pal.headerHex);
    const QColor textColor(pal.textHex);
    const QColor altColor = pal.isDark ? QColor(0x18, 0x1b, 0x20) : QColor(0xf5, 0xef, 0xdc);

    QPalette p = palette();
    p.setColor(QPalette::Base, canvasColor);
    p.setColor(QPalette::AlternateBase, altColor);
    p.setColor(QPalette::Window, canvasColor);
    p.setColor(QPalette::Button, headerColor);
    p.setColor(QPalette::Text, textColor);
    p.setColor(QPalette::WindowText, textColor);
    setPalette(p);

    if (viewport()) {
        viewport()->setAutoFillBackground(true);
        viewport()->setPalette(p);
    }
    if (verticalHeader()) {
        verticalHeader()->setAutoFillBackground(true);
        verticalHeader()->setPalette(p);
        if (verticalHeader()->viewport()) {
            verticalHeader()->viewport()->setAutoFillBackground(true);
            verticalHeader()->viewport()->setPalette(p);
        }
    }
    if (horizontalHeader()) {
        horizontalHeader()->setAutoFillBackground(true);
        horizontalHeader()->setPalette(p);
    }

    setStyleSheet(QStringLiteral(
        "QTableWidget, QTableView { background-color: %1; alternate-background-color: %2; "
        "color: %3; border: none; gridline-color: %4; } "
        "QHeaderView, QHeaderView:vertical, QHeaderView:horizontal { background-color: %1; border: none; } "
        "QHeaderView::section { background-color: %5; color: %3; padding: 2px 6px; "
        "height: 20px; min-height: 20px; max-height: 22px; border: 0px; "
        "border-right: 1px solid %4; border-bottom: 1px solid %4; font-size: 10px; font-weight: 600; } "
        "QTableCornerButton::section { background-color: %5; border: 0px; border-right: 1px solid %4; border-bottom: 1px solid %4; }")
        .arg(pal.canvasHex, altColor.name(), pal.textHex, pal.borderHex, pal.headerHex));

    updateHighlights();
    update();
}

}  // namespace gridlock::ui

