#include "DifferentialGrid.hpp"
#include <QHeaderView>
#include <algorithm>

namespace gridlock::ui {

DifferentialGrid::DifferentialGrid(QWidget *parent) : QTableWidget(parent) {
    horizontalHeader()->setStretchLastSection(true);
    
    m_watchVariables = {"rank", "calc", "i"};
    
    setColumnCount(m_numRanks + 1);
    QStringList headerLabels;
    headerLabels << "Name";
    for (int r = 0; r < m_numRanks; ++r) {
        headerLabels << QString("Rank %1").arg(r);
    }
    setHorizontalHeaderLabels(headerLabels);
    
    setRowCount(0);
    
    for (const QString& var : m_watchVariables) {
        int row = rowCount();
        insertRow(row);
        auto* nameItem = new QTableWidgetItem(var);
        setItem(row, 0, nameItem);
        for (int c = 1; c <= m_numRanks; ++c) {
            auto* valItem = new QTableWidgetItem("-");
            valItem->setFlags(valItem->flags() & ~Qt::ItemIsEditable);
            setItem(row, c, valItem);
        }
    }
    
    addEmptyWatchRow();
    
    connect(this, &QTableWidget::cellChanged, this, [this](int row, int col) {
        if (col == 0) {
            QTableWidgetItem* item = this->item(row, col);
            if (!item) return;
            QString newVar = item->text().trimmed();
            if (!newVar.isEmpty() && row == rowCount() - 1) {
                m_watchVariables.push_back(newVar);
                for (int c = 1; c <= m_numRanks; ++c) {
                    this->item(row, c)->setText("...");
                }
                emit watchVariableAdded(newVar);
                
                this->blockSignals(true);
                addEmptyWatchRow();
                this->blockSignals(false);
            }
        }
    });
}

void DifferentialGrid::addEmptyWatchRow() {
    int row = rowCount();
    insertRow(row);
    auto* nameItem = new QTableWidgetItem("");
    setItem(row, 0, nameItem);
    for (int c = 1; c <= m_numRanks; ++c) {
        auto* valItem = new QTableWidgetItem("");
        valItem->setFlags(valItem->flags() & ~Qt::ItemIsEditable);
        setItem(row, c, valItem);
    }
}

void DifferentialGrid::setVariableData(int rank, const QString& varName, const QString& value) {
    updateVariable(rank, varName, value);
}

void DifferentialGrid::setVariableData(int rank, const std::unordered_map<QString, QString>& variables) {
    for (const auto& kv : variables) {
        updateVariable(rank, kv.first, kv.second);
    }
}

void DifferentialGrid::updateVariable(int rankId, const QString& name, const QString& value) {
    if (rankId >= m_numRanks) {
        m_numRanks = rankId + 1;
        setColumnCount(m_numRanks + 1);
        horizontalHeaderItem(m_numRanks)->setText(QString("Rank %1").arg(rankId));
        for (int r = 0; r < rowCount(); ++r) {
            auto* valItem = new QTableWidgetItem("-");
            valItem->setFlags(valItem->flags() & ~Qt::ItemIsEditable);
            setItem(r, m_numRanks, valItem);
        }
    }

    auto it = std::find(m_watchVariables.begin(), m_watchVariables.end(), name);
    int row = -1;
    if (it == m_watchVariables.end()) {
        return;
    } else {
        row = std::distance(m_watchVariables.begin(), it);
    }
    
    QTableWidgetItem* cellItem = item(row, rankId + 1);
    if (cellItem) {
        cellItem->setText(value);
    }
    updateHighlights();
}

void DifferentialGrid::updateHighlights() {
    for (int row = 0; row < m_watchVariables.size(); ++row) {
        std::unordered_map<QString, int> counts;
        for (int col = 1; col <= m_numRanks; ++col) {
            QTableWidgetItem* it = item(row, col);
            if (it && it->text() != "-" && it->text() != "..." && !it->text().isEmpty()) counts[it->text()]++;
        }
        
        QString majorityStr;
        int maxCount = 0;
        for (const auto& kv : counts) {
            if (kv.second > maxCount) {
                maxCount = kv.second;
                majorityStr = kv.first;
            }
        }
        
        for (int col = 1; col <= m_numRanks; ++col) {
            QTableWidgetItem* it = item(row, col);
            if (it) {
                if (it->text() != "-" && it->text() != "..." && !it->text().isEmpty() && it->text() != majorityStr && counts.size() > 1) {
                    it->setBackground(QColor(255, 200, 200));
                } else {
                    it->setBackground(Qt::white);
                }
            }
        }
    }
}

} // namespace gridlock::ui
