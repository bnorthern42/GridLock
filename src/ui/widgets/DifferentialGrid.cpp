#include "DifferentialGrid.hpp"
#include <QHeaderView>
#include <algorithm>

namespace gridlock::ui {

DifferentialGrid::DifferentialGrid(QWidget *parent) : QTableWidget(parent) {
    horizontalHeader()->setStretchLastSection(true);
    
    setRowCount(1);
    setColumnCount(1);
    setVerticalHeaderItem(0, new QTableWidgetItem("Add Watch ->"));
    setHorizontalHeaderItem(0, new QTableWidgetItem("New Variable"));
    setItem(0, 0, new QTableWidgetItem(""));
    
    connect(this, &QTableWidget::cellChanged, this, [this](int row, int col) {
        if (row == 0 && col == 0) {
            QTableWidgetItem* cell = item(row, col);
            if (!cell) return;
            QString newVarName = cell->text().trimmed();
            if (!newVarName.isEmpty() && newVarName != "...") {
                emit watchVariableAdded(newVarName);
                
                this->blockSignals(true);
                cell->setText("...");
                this->blockSignals(false);
            }
        }
    });
}

void DifferentialGrid::setVariableData(int rankId, const QHash<QString, QString>& variables) {
    for (auto it = variables.constBegin(); it != variables.constEnd(); ++it) {
        updateVariableDisplay(rankId, it.key(), it.value());
    }
}

void DifferentialGrid::addVariableColumn(const QString& name) {
    int col = -1;
    for (int c = 0; c < columnCount(); ++c) {
        if (horizontalHeaderItem(c) && horizontalHeaderItem(c)->text() == name) {
            col = c;
            break;
        }
    }
    
    if (col == -1) {
        col = columnCount();
        setColumnCount(col + 1);
        setHorizontalHeaderItem(col, new QTableWidgetItem(name));
        for (int r = 0; r < rowCount(); ++r) {
            setItem(r, col, new QTableWidgetItem("-"));
        }
    }
}

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

void DifferentialGrid::updateVariableDisplay(int rankId, const QString& varName, const QString& value) {
    int targetRow = rankId + 1;
    
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
        setHorizontalHeaderItem(col, new QTableWidgetItem(varName));
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
    
    if (targetRow >= rowCount()) {
        int oldRow = rowCount();
        setRowCount(targetRow + 1);
        for(int r = oldRow; r <= targetRow; ++r) {
            setVerticalHeaderItem(r, new QTableWidgetItem(QString("Rank %1").arg(r - 1)));
            for (int c = 0; c < columnCount(); ++c) {
                setItem(r, c, new QTableWidgetItem("-"));
            }
        }
    }
    
    QTableWidgetItem* cellItem = item(targetRow, col);
    if (!cellItem) {
        cellItem = new QTableWidgetItem(value);
        setItem(targetRow, col, cellItem);
    } else {
        cellItem->setText(value);
    }
    
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
    
    updateHighlights();
}

void DifferentialGrid::updateHighlights() {
    for (int col = 1; col < columnCount(); ++col) {
        std::unordered_map<QString, int> counts;
        for (int row = 1; row < rowCount(); ++row) {
            QTableWidgetItem* it = item(row, col);
            if (it && it->text() != "-") counts[it->text()]++;
        }
        
        QString majorityStr;
        int maxCount = 0;
        for (const auto& kv : counts) {
            if (kv.second > maxCount) {
                maxCount = kv.second;
                majorityStr = kv.first;
            }
        }
        
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

} // namespace gridlock::ui
