/**
 * @file ShortcutItemDelegate.cpp
 * @brief Implementations of KeySequenceRecorderWidget and ShortcutItemDelegate for KeybindingsSettingsPage.
 */

#include "KeybindingsSettingsPage.hpp"
#include "../../../core/managers/ShortcutManager.hpp"
#include "../../../core/managers/ThemeManager.hpp"

#include <QApplication>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QPainter>
#include <QPointer>
#include <algorithm>

namespace gridlock::ui {

ShortcutItemDelegate::ShortcutItemDelegate(QObject* parent)
    : QStyledItemDelegate(parent) {}

void ShortcutItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const {
    if (!painter) return;
    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, index);
    opt.text.clear();
    const QStyle* st = opt.widget ? opt.widget->style() : QApplication::style();
    painter->save();
    st->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);
    painter->restore();

    const QKeySequence seq = index.data(Qt::UserRole + 1).value<QKeySequence>();
    const bool assigned = !seq.isEmpty();
    const QString text = assigned ? seq.toString(QKeySequence::NativeText) : QObject::tr("Unassigned");

    const bool selected = (opt.state & QStyle::State_Selected) != 0;
    const QColor baseCol = opt.palette.color(QPalette::Base);
    const bool isDark = baseCol.lightness() < 128;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    QFont badgeFont = opt.font;
    badgeFont.setPixelSize(12);
    badgeFont.setWeight(assigned ? QFont::DemiBold : QFont::Normal);
    painter->setFont(badgeFont);

    const QRect avail = opt.rect.adjusted(8, 4, -8, -4);
    if (!assigned) {
        QColor muted = selected ? opt.palette.color(QPalette::HighlightedText)
                                : opt.palette.color(QPalette::Text);
        muted.setAlpha(selected ? 210 : 185);
        painter->setPen(muted);
        painter->drawText(avail.adjusted(2, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, text);
        painter->restore();
        return;
    }

    const QFontMetrics fm(badgeFont);
    const int pillW = std::clamp(fm.horizontalAdvance(text) + 18, 34, std::max(34, avail.width()));
    const int pillH = std::min(24, std::max(18, avail.height()));
    const QRectF pillRect(avail.left(), avail.center().y() - pillH / 2.0 + 0.5, pillW, pillH);

    const QColor btnCol = opt.palette.color(QPalette::Button);
    const QColor altCol = opt.palette.color(QPalette::AlternateBase);
    const QColor fillCol = isDark ? btnCol.lighter(128) : altCol.darker(105);
    const QColor borderCol = selected ? opt.palette.color(QPalette::Highlight)
                                      : opt.palette.color(QPalette::Mid);
    QColor textCol = opt.palette.color(QPalette::Text);
    if (std::abs(textCol.lightness() - fillCol.lightness()) < 100) {
        textCol = (fillCol.lightness() < 128) ? QColor(255, 255, 255) : QColor(22, 24, 29);
    }

    painter->setBrush(fillCol);
    painter->setPen(QPen(borderCol, 1.0));
    painter->drawRoundedRect(pillRect, 4.0, 4.0);
    painter->setPen(textCol);
    painter->drawText(pillRect.adjusted(6, 0, -6, 0), Qt::AlignCenter,
                      fm.elidedText(text, Qt::ElideRight, static_cast<int>(pillRect.width()) - 12));
    painter->restore();
}

KeySequenceRecorderWidget::KeySequenceRecorderWidget(QWidget* parent) : QFrame(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setFrameShape(QFrame::NoFrame);
    const auto pal = gridlock::core::managers::ThemeManager::paletteForTheme(
        gridlock::core::managers::ThemeManager::instance().activeThemeId());
    QPalette wp = palette();
    wp.setColor(QPalette::Window, QColor(pal.canvasHex));
    wp.setColor(QPalette::Base, QColor(pal.canvasHex));
    wp.setColor(QPalette::WindowText, QColor(pal.textHex));
    setPalette(wp);
    setStyleSheet(QStringLiteral(
        "QFrame { background-color: %1; border: 1.5px solid %3; border-radius: 4px; padding: 0px; margin: 0px; min-height: 0px; }"
        "QLabel { background: transparent; border: none; color: %2; font-size: 12px; font-weight: 600; padding: 0px; margin: 0px; min-height: 0px; }"
        "QToolButton { background: transparent; border: none; color: %2; font-size: 14px; font-weight: 600; padding: 0px; margin: 0px; min-width: 18px; max-width: 20px; min-height: 18px; max-height: 20px; }"
        "QToolButton:hover { color: %3; }").arg(pal.canvasHex, pal.textHex, pal.accentHex));
    ensurePolished();
    setAttribute(Qt::WA_StyledBackground, true);
    setAutoFillBackground(true);
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(8, 0, 4, 0);
    lay->setSpacing(4);
    m_label = new QLabel(this);
    m_label->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    lay->addWidget(m_label, 1);
    m_clearBtn = new QToolButton(this);
    m_clearBtn->setText(QStringLiteral("×"));
    m_clearBtn->setToolTip(tr("Clear shortcut"));
    m_clearBtn->setFixedSize(20, 20);
    m_clearBtn->setFocusPolicy(Qt::NoFocus);
    m_clearBtn->setCursor(Qt::PointingHandCursor);
    connect(m_clearBtn, &QToolButton::clicked, this, [this]() { setKeySequence(QKeySequence()); });
    lay->addWidget(m_clearBtn);
    refreshDisplay();
}

void KeySequenceRecorderWidget::setKeySequence(const QKeySequence& seq) {
    if (m_sequence == seq && !m_label->text().isEmpty()) return;
    m_sequence = seq;
    refreshDisplay();
    emit keySequenceChanged(m_sequence);
}

void KeySequenceRecorderWidget::refreshDisplay() {
    if (!m_label) return;
    const bool empty = m_sequence.isEmpty();
    m_label->setText(empty ? tr("Press shortcut...") : m_sequence.toString(QKeySequence::NativeText));
    if (m_clearBtn) m_clearBtn->setVisible(!empty);
}

bool KeySequenceRecorderWidget::event(QEvent* ev) {
    if (ev && ev->type() == QEvent::ShortcutOverride) { ev->accept(); return true; }
    return QFrame::event(ev);
}

void KeySequenceRecorderWidget::keyPressEvent(QKeyEvent* event) {
    if (!event) return;
    const int k = event->key();
    if (k == Qt::Key_Shift || k == Qt::Key_Control || k == Qt::Key_Alt ||
        k == Qt::Key_Meta || k == Qt::Key_AltGr || k == Qt::Key_unknown) {
        event->accept();
        return;
    }
    if (k == Qt::Key_Escape && event->modifiers() == Qt::NoModifier) {
        emit editingCancelled();
        event->accept();
        return;
    }
    setKeySequence(QKeySequence(event->keyCombination()));
    emit editingFinished();
    event->accept();
}

QWidget* ShortcutItemDelegate::createEditor(QWidget* parent, const QStyleOptionViewItem&, const QModelIndex& index) const {
    auto* editor = new KeySequenceRecorderWidget(parent);
    const QString actionId = index.data(Qt::UserRole).toString();
    editor->setObjectName(actionId.isEmpty() ? QStringLiteral("keySeqInlineEditor") : QStringLiteral("keySeqEdit_%1").arg(actionId));
    const QPointer<KeySequenceRecorderWidget> edGuard(editor);
    connect(editor, &KeySequenceRecorderWidget::keySequenceChanged, this, [this, edGuard](const QKeySequence&) {
        if (!edGuard || !edGuard->isVisible() || edGuard->property("gl_populating").toBool()) return;
        emit const_cast<ShortcutItemDelegate*>(this)->commitData(edGuard.data());
    });
    connect(editor, &KeySequenceRecorderWidget::editingFinished, this, [this, edGuard]() {
        if (edGuard) emit const_cast<ShortcutItemDelegate*>(this)->closeEditor(edGuard.data(), QAbstractItemDelegate::NoHint);
    });
    connect(editor, &KeySequenceRecorderWidget::editingCancelled, this, [this, edGuard]() {
        if (edGuard) emit const_cast<ShortcutItemDelegate*>(this)->closeEditor(edGuard.data(), QAbstractItemDelegate::RevertModelCache);
    });
    return editor;
}

void ShortcutItemDelegate::setEditorData(QWidget* editor, const QModelIndex& index) const {
    if (auto* rec = qobject_cast<KeySequenceRecorderWidget*>(editor)) {
        rec->setProperty("gl_populating", true);
        rec->setKeySequence(index.data(Qt::UserRole + 1).value<QKeySequence>());
        rec->setProperty("gl_populating", false);
    }
}

void ShortcutItemDelegate::setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const {
    auto* rec = qobject_cast<KeySequenceRecorderWidget*>(editor);
    if (!rec || !model) return;
    const QKeySequence seq = rec->keySequence();
    if (index.data(Qt::UserRole + 1).value<QKeySequence>() == seq) return;
    model->setData(index, QVariant::fromValue(seq), Qt::UserRole + 1);
    model->setData(index, seq.isEmpty() ? QObject::tr("Unassigned") : seq.toString(QKeySequence::NativeText), Qt::DisplayRole);
    const QString actionId = index.data(Qt::UserRole).toString();
    if (!actionId.isEmpty()) {
        gridlock::core::ShortcutManager::instance().setShortcutForAction(actionId, seq, true);
        emit const_cast<ShortcutItemDelegate*>(this)->shortcutAssigned(actionId, seq);
    }
}

void ShortcutItemDelegate::updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex&) const {
    if (editor) { editor->setAutoFillBackground(true); editor->setGeometry(option.rect.adjusted(3, 3, -3, -3)); }
}

}  // namespace gridlock::ui
