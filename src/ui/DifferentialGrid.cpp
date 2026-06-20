#include "DifferentialGrid.hpp"
#include <QHeaderView>
#include <algorithm>

namespace gridlock::ui {

DifferentialGrid::DifferentialGrid(QWidget *parent) : QTableWidget(parent) {
    horizontalHeader()->setStretchLastSection(true);
    
    m_headers = {"rank", "calc", "i"};
    setColumnCount(m_headers.size());
    QStringList headerLabels;
    for (const auto& h : m_headers) headerLabels << h;
    setHorizontalHeaderLabels(headerLabels);
    
    int numRanks = 4;
    setRowCount(numRanks);
    for (int r = 0; r < numRanks; ++r) {
        setVerticalHeaderItem(r, new QTableWidgetItem(QString("Rank %1").arg(r)));
        for (size_t c = 0; c < m_headers.size(); ++c) {
            setItem(r, c, new QTableWidgetItem("-"));
        }
    }
}

void DifferentialGrid::setVariableData(int rank, const QString& varName, const QString& value) {
    m_varData[rank][varName] = value;
    
    if (std::find(m_headers.begin(), m_headers.end(), varName) == m_headers.end()) {
        m_headers.push_back(varName);
        setColumnCount(m_headers.size());
        QStringList headerLabels;
        for (const auto& h : m_headers) headerLabels << h;
        setHorizontalHeaderLabels(headerLabels);
    }
    
    if (rank >= rowCount()) {
        int oldRow = rowCount();
        setRowCount(rank + 1);
        for(int r = oldRow; r <= rank; ++r) {
            setVerticalHeaderItem(r, new QTableWidgetItem(QString("Rank %1").arg(r)));
            for (int c = 0; c < columnCount(); ++c) {
                if (!item(r, c)) setItem(r, c, new QTableWidgetItem("-"));
            }
        }
    }
    
    int col = std::distance(m_headers.begin(), std::find(m_headers.begin(), m_headers.end(), varName));
    QTableWidgetItem* cellItem = item(rank, col);
    if (!cellItem) {
        cellItem = new QTableWidgetItem(value);
        setItem(rank, col, cellItem);
    } else {
        cellItem->setText(value);
    }
    
    updateHighlights();
}

void DifferentialGrid::setVariableData(int rank, const std::unordered_map<QString, QString>& variables) {
    for (const auto& kv : variables) {
        updateVariable(rank, kv.first, kv.second);
    }
}

void DifferentialGrid::updateVariable(int rankId, const QString& name, const QString& value) {
    m_varData[rankId][name] = value;
    auto it = std::find(m_headers.begin(), m_headers.end(), name);
    if (it == m_headers.end() || rankId >= rowCount()) {
        setVariableData(rankId, name, value);
        return;
    }
    int col = std::distance(m_headers.begin(), it);
    QTableWidgetItem* cellItem = item(rankId, col);
    if (!cellItem) {
        cellItem = new QTableWidgetItem(value);
        setItem(rankId, col, cellItem);
    } else {
        cellItem->setText(value);
    }
}

void DifferentialGrid::updateHighlights() {
    for (int col = 0; col < columnCount(); ++col) {
        std::unordered_map<QString, int> counts;
        for (int row = 0; row < rowCount(); ++row) {
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
        
        for (int row = 0; row < rowCount(); ++row) {
            QTableWidgetItem* it = item(row, col);
            if (it) {
                if (it->text() != "-" && it->text() != majorityStr && counts.size() > 1) {
                    it->setBackground(QColor(255, 200, 200));
                } else {
                    it->setBackground(Qt::white);
                }
            }
        }
    }
}

} // namespace gridlock::ui
