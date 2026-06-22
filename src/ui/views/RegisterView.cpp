#include "RegisterView.hpp"
#include <QVBoxLayout>
#include <QHeaderView>
#include <QJsonObject>
#include <QJsonValue>
#include "../../core/managers/ConfigManager.hpp"

namespace gridlock::ui {

RegisterView::RegisterView(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(2);
    m_table->setHorizontalHeaderLabels({"Register", "Value"});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    
    QPalette p = m_table->palette();
    p.setColor(QPalette::Base, QColor(gridlock::core::ConfigManager::instance().getSourceBackground()));
    p.setColor(QPalette::Text, QColor(gridlock::core::ConfigManager::instance().getSourceText()));
    m_table->setPalette(p);

    layout->addWidget(m_table);
}

void RegisterView::updateRegisters(const gridlock::RankState& state) {
    if (state.registers.isEmpty()) {
        return;
    }

    if (m_table->rowCount() != state.registers.size()) {
        m_table->setRowCount(state.registers.size());
    }

    int row = 0;
    for (auto it = state.registers.constBegin(); it != state.registers.constEnd(); ++it) {
        QString regName = QString("R%1").arg(it.key());
        QString regVal = it.value();

        QTableWidgetItem* nameItem = m_table->item(row, 0);
        if (!nameItem) {
            nameItem = new QTableWidgetItem(regName);
            m_table->setItem(row, 0, nameItem);
        } else {
            nameItem->setText(regName);
        }

        QTableWidgetItem* valItem = m_table->item(row, 1);
        if (!valItem) {
            valItem = new QTableWidgetItem(regVal);
            m_table->setItem(row, 1, valItem);
        } else {
            valItem->setText(regVal);
        }

        row++;
    }
}

void RegisterView::updateRegisters(const QJsonArray& registers) {
    if (registers.isEmpty()) {
        return;
    }

    if (m_table->rowCount() != registers.size()) {
        m_table->setRowCount(registers.size());
    }

    for (int row = 0; row < registers.size(); ++row) {
        QJsonObject reg = registers[row].toObject();
        QString regName = reg["name"].toString();
        QString regVal = reg["value"].toString();

        QTableWidgetItem* nameItem = m_table->item(row, 0);
        if (!nameItem) {
            nameItem = new QTableWidgetItem(regName);
            m_table->setItem(row, 0, nameItem);
        } else {
            nameItem->setText(regName);
        }

        QTableWidgetItem* valItem = m_table->item(row, 1);
        if (!valItem) {
            valItem = new QTableWidgetItem(regVal);
            m_table->setItem(row, 1, valItem);
        } else {
            valItem->setText(regVal);
        }
    }
}

} // namespace gridlock::ui
