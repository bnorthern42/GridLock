/**
 * @file KeybindingsSettingsPage.cpp
 * @brief Implementation of KeybindingsSettingsPage searchable shortcut table and conflict resolution.
 */

#include "KeybindingsSettingsPage.hpp"
#include "../../../core/managers/ShortcutManager.hpp"

#include <QCoreApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QVBoxLayout>

namespace gridlock::ui {

KeybindingsSettingsPage::KeybindingsSettingsPage(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 16, 20, 24);
    layout->setSpacing(10);

    auto* heading = new QLabel(tr("<b>Keyboard Shortcuts</b>"), this);
    heading->setObjectName(QStringLiteral("heading"));
    layout->addWidget(heading);

    auto* separator = new QLabel(this);
    separator->setFixedHeight(1);
    separator->setObjectName(QStringLiteral("separator"));
    layout->addWidget(separator);

    auto* topRow = new QHBoxLayout();
    topRow->setSpacing(8);
    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setObjectName(QStringLiteral("keybindingsFilterEdit"));
    m_filterEdit->setPlaceholderText(tr("Filter actions, categories, or shortcuts..."));
    m_filterEdit->setClearButtonEnabled(true);
    m_filterEdit->setToolTip(tr("Filter keyboard shortcuts by category, action title, or key sequence"));
    topRow->addWidget(m_filterEdit, 1);

    m_clearBtn = new QPushButton(tr("Clear Shortcut"), this);
    m_clearBtn->setObjectName(QStringLiteral("keybindingsClearShortcutBtn"));
    m_clearBtn->setToolTip(tr("Unassign the keyboard shortcut for the selected action"));
    m_clearBtn->setEnabled(false);
    topRow->addWidget(m_clearBtn);

    m_resetBtn = new QPushButton(tr("Reset Defaults"), this);
    m_resetBtn->setObjectName(QStringLiteral("keybindingsResetDefaultsBtn"));
    m_resetBtn->setToolTip(tr("Restore all keyboard shortcuts to their built-in defaults"));
    topRow->addWidget(m_resetBtn);
    layout->addLayout(topRow);

    m_conflictLabel = new QLabel(this);
    m_conflictLabel->setObjectName(QStringLiteral("keybindingsConflictLabel"));
    m_conflictLabel->setWordWrap(true);
    m_conflictLabel->setStyleSheet(QStringLiteral("QLabel { color: #d97706; font-weight: 600; padding: 2px 4px; }"));
    m_conflictLabel->setVisible(false);
    layout->addWidget(m_conflictLabel);

    m_table = new QTableWidget(0, 3, this);
    m_table->setObjectName(QStringLiteral("keybindingsTable"));
    m_table->setHorizontalHeaderLabels({tr("Category"), tr("Action"), tr("Shortcut")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    m_table->setAlternatingRowColors(true);
    m_table->setWordWrap(false);
    m_table->setTextElideMode(Qt::ElideRight);
    m_table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(38);
    m_table->verticalHeader()->setMinimumSectionSize(38);
    m_table->horizontalHeader()->setMinimumSectionSize(115);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_table->setColumnWidth(2, 200);
    auto* delegate = new ShortcutItemDelegate(m_table);
    m_table->setItemDelegateForColumn(2, delegate);
    layout->addWidget(m_table, 1);

    connect(delegate, &ShortcutItemDelegate::shortcutAssigned, this,
            [this](const QString& actionId, const QKeySequence& seq) {
                if (!m_table || !m_conflictLabel) return;
                if (seq.isEmpty()) { m_conflictLabel->setVisible(false); return; }
                QStringList reassignedFrom;
                for (int r = 0; r < m_table->rowCount(); ++r) {
                    QTableWidgetItem* actItem = m_table->item(r, 1);
                    QTableWidgetItem* seqItem = m_table->item(r, 2);
                    if (!actItem || !seqItem) continue;
                    const QString otherId = seqItem->data(Qt::UserRole).toString();
                    if (otherId == actionId) continue;
                    if (seqItem->data(Qt::UserRole + 1).value<QKeySequence>() == seq) {
                        seqItem->setData(Qt::UserRole + 1, QVariant::fromValue(QKeySequence()));
                        seqItem->setText(tr("Unassigned"));
                        gridlock::core::ShortcutManager::instance().setShortcutForAction(otherId, QKeySequence(), true);
                        reassignedFrom.append(actItem->text());
                    }
                }
                if (!reassignedFrom.isEmpty()) {
                    m_conflictLabel->setText(tr("Reassigned '%1' from '%2'.").arg(
                        seq.toString(QKeySequence::NativeText), reassignedFrom.join(QStringLiteral("', '"))));
                    m_conflictLabel->setVisible(true);
                } else {
                    m_conflictLabel->setVisible(false);
                }
            });
    connect(m_filterEdit, &QLineEdit::textChanged, this, &KeybindingsSettingsPage::filterRows);
    connect(m_table, &QTableWidget::cellClicked, this, [this](int row, int col) {
        if (col != 2) return;
        for (auto* ed : m_table->viewport()->findChildren<KeySequenceRecorderWidget*>()) {
            if (ed && ed->isVisible()) return;
        }
        if (QTableWidgetItem* it = m_table->item(row, 2); it && (it->flags() & Qt::ItemIsEditable)) {
            m_table->editItem(it);
        }
    });
    connect(m_table->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this](const QModelIndex& cur, const QModelIndex&) {
                if (m_clearBtn) m_clearBtn->setEnabled(cur.isValid());
            });
    connect(m_clearBtn, &QPushButton::clicked, this, &KeybindingsSettingsPage::clearSelectedShortcut);
    connect(m_resetBtn, &QPushButton::clicked, this, [this]() {
        gridlock::core::ShortcutManager::instance().resetAllToDefaults(true);
        refreshBindings();
    });
    refreshBindings();
}

void KeybindingsSettingsPage::refreshBindings() {
    auto& sm = gridlock::core::ShortcutManager::instance();
    sm.syncMenuActionsFromMainWindow();
    const auto& bindings = sm.registeredBindings();
    m_table->setCurrentCell(-1, -1);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    m_table->setRowCount(bindings.size());
    for (int row = 0; row < bindings.size(); ++row) {
        const auto& b = bindings[row];
        auto* catItem = new QTableWidgetItem(b.category);
        catItem->setFlags(catItem->flags() & ~Qt::ItemIsEditable);
        catItem->setToolTip(tr("Category: %1").arg(b.category));
        auto* actItem = new QTableWidgetItem(b.label);
        actItem->setFlags(actItem->flags() & ~Qt::ItemIsEditable);
        actItem->setData(Qt::UserRole, b.id);
        const QString defStr = b.defaultShortcut.isEmpty() ? tr("Unassigned") : b.defaultShortcut.toString(QKeySequence::NativeText);
        actItem->setToolTip(tr("%1 -> %2 (Default: %3)").arg(b.category, b.label, defStr));
        const QString curStr = b.currentShortcut.isEmpty() ? tr("Unassigned") : b.currentShortcut.toString(QKeySequence::NativeText);
        auto* seqItem = new QTableWidgetItem(curStr);
        seqItem->setFlags(seqItem->flags() | Qt::ItemIsEditable);
        seqItem->setData(Qt::UserRole, b.id);
        seqItem->setData(Qt::UserRole + 1, QVariant::fromValue(b.currentShortcut));
        seqItem->setToolTip(tr("Click to edit shortcut for '%1' (Default: %2)").arg(b.label, defStr));
        m_table->setItem(row, 0, catItem);
        m_table->setItem(row, 1, actItem);
        m_table->setItem(row, 2, seqItem);
        m_table->setRowHeight(row, 38);
    }
    m_table->resizeColumnToContents(0);
    if (m_conflictLabel) m_conflictLabel->setVisible(false);
    if (m_clearBtn) m_clearBtn->setEnabled(false);
    if (m_filterEdit && !m_filterEdit->text().isEmpty()) filterRows(m_filterEdit->text());
}

void KeybindingsSettingsPage::clearSelectedShortcut() {
    if (!m_table || m_table->currentRow() < 0) return;
    QTableWidgetItem* seqItem = m_table->item(m_table->currentRow(), 2);
    if (!seqItem) return;
    seqItem->setData(Qt::UserRole + 1, QVariant::fromValue(QKeySequence()));
    seqItem->setText(tr("Unassigned"));
    if (const QString actionId = seqItem->data(Qt::UserRole).toString(); !actionId.isEmpty()) {
        gridlock::core::ShortcutManager::instance().setShortcutForAction(actionId, QKeySequence(), true);
    }
}

void KeybindingsSettingsPage::applyPendingChanges() {
    if (!m_table) return;
    auto& sm = gridlock::core::ShortcutManager::instance();
    for (int row = 0; row < m_table->rowCount(); ++row) {
        QTableWidgetItem* actItem = m_table->item(row, 1);
        QTableWidgetItem* seqItem = m_table->item(row, 2);
        if (!actItem || !seqItem) continue;
        const QString id = actItem->data(Qt::UserRole).toString();
        const QKeySequence seq = seqItem->data(Qt::UserRole + 1).value<QKeySequence>();
        if (sm.shortcutForAction(id) != seq) sm.setShortcutForAction(id, seq, true);
    }
}

void KeybindingsSettingsPage::filterRows(const QString& query) {
    if (!m_table) return;
    const QString q = query.trimmed();
    for (int row = 0; row < m_table->rowCount(); ++row) {
        if (q.isEmpty()) { m_table->setRowHidden(row, false); continue; }
        const QString cat = m_table->item(row, 0) ? m_table->item(row, 0)->text() : QString();
        const QString act = m_table->item(row, 1) ? m_table->item(row, 1)->text() : QString();
        const QString seq = m_table->item(row, 2) ? m_table->item(row, 2)->text() : QString();
        const bool match = cat.contains(q, Qt::CaseInsensitive) || act.contains(q, Qt::CaseInsensitive) ||
                           seq.contains(q, Qt::CaseInsensitive);
        m_table->setRowHidden(row, !match);
    }
}

}  // namespace gridlock::ui
