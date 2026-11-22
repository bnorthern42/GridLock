/**
 * @file KeybindingsSettingsPage.hpp
 * @brief Declarations of KeySequenceRecorderWidget, ShortcutItemDelegate, and KeybindingsSettingsPage.
 */

#pragma once

#include <QFrame>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QToolButton>
#include <QWidget>

namespace gridlock::ui {

/**
 * @class KeySequenceRecorderWidget
 * @brief Opaque, vertically centered inline shortcut recorder replacing QKeySequenceEdit in KeybindingsSettingsPage.
 */
class KeySequenceRecorderWidget : public QFrame {
    Q_OBJECT

public:
    explicit KeySequenceRecorderWidget(QWidget* parent = nullptr);

    QKeySequence keySequence() const { return m_sequence; }
    void setKeySequence(const QKeySequence& seq);
    QLabel* displayLabel() const { return m_label; }
    QToolButton* clearButton() const { return m_clearBtn; }

signals:
    void keySequenceChanged(const QKeySequence& seq);
    void editingFinished();
    void editingCancelled();

protected:
    bool event(QEvent* ev) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void refreshDisplay();

    QKeySequence m_sequence;
    QLabel* m_label = nullptr;
    QToolButton* m_clearBtn = nullptr;
};

/**
 * @class ShortcutItemDelegate
 * @brief Paints rounded keycap shortcut badges and opens KeySequenceRecorderWidget strictly on demand inside column 2.
 */
class ShortcutItemDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit ShortcutItemDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option,
                          const QModelIndex& index) const override;
    void setEditorData(QWidget* editor, const QModelIndex& index) const override;
    void setModelData(QWidget* editor, QAbstractItemModel* model,
                      const QModelIndex& index) const override;
    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option,
                              const QModelIndex& index) const override;

signals:
    void shortcutAssigned(const QString& actionId, const QKeySequence& sequence);
};

/**
 * @class KeybindingsSettingsPage
 * @brief Searchable shortcut customization page backed by ShortcutManager and ConfigManager.
 */
class KeybindingsSettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit KeybindingsSettingsPage(QWidget* parent = nullptr);

    void refreshBindings();
    void applyPendingChanges();

    QLineEdit* filterEdit() const { return m_filterEdit; }
    QTableWidget* table() const { return m_table; }
    QPushButton* clearShortcutButton() const { return m_clearBtn; }
    QPushButton* resetDefaultsButton() const { return m_resetBtn; }
    QLabel* conflictLabel() const { return m_conflictLabel; }

private slots:
    void filterRows(const QString& query);
    void clearSelectedShortcut();

private:
    QLineEdit* m_filterEdit = nullptr;
    QLabel* m_conflictLabel = nullptr;
    QTableWidget* m_table = nullptr;
    QPushButton* m_clearBtn = nullptr;
    QPushButton* m_resetBtn = nullptr;
};

}  // namespace gridlock::ui
