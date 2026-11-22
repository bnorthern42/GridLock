/**
 * @file PreferencesDialogChrome.cpp
 * @brief Theme-driven QSS/QPalette surface styling, sidebar icon rendering, and window events for PreferencesDialog.
 */

#include "PreferencesDialog.hpp"
#include "../../../core/managers/ThemeManager.hpp"

#include <QAbstractButton>
#include <QFont>
#include <QHeaderView>
#include <QIcon>
#include <QListWidgetItem>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPushButton>
#include <QScrollArea>
#include <QTableWidget>
#include <QWindow>

namespace gridlock::ui {

void PreferencesDialog::applyDialogThemeChrome() {
    auto& tm = gridlock::core::managers::ThemeManager::instance();
    const auto pal = gridlock::core::managers::ThemeManager::paletteForTheme(tm.activeThemeId());
    const QString canvas = pal.canvasHex;
    const QString header = pal.headerHex;
    const QString border = pal.borderHex;
    const QString accent = pal.accentHex;
    const QString text = pal.textHex;

    const QColor accentCol(accent);
    const QString selFg = (accentCol.lightness() < 165) ? QStringLiteral("#ffffff") : QStringLiteral("#16181d");
    const QString hoverBg = pal.isDark ? QStringLiteral("#252932") : canvas;
    const QString hoverFg = pal.isDark ? QStringLiteral("#ffffff") : text;

    setStyleSheet(QStringLiteral(
        "QDialog, QDialog#PreferencesDialog, QWidget#PreferencesDialog, "
        "QStackedWidget, QStackedWidget > QWidget, QScrollArea { background-color: %1; color: %5; font-size: 13px; }"
        "QLabel, QCheckBox { color: %5; font-size: 13px; }"
        "QGroupBox { background-color: %2; border: 1px solid %3; border-radius: 4px; "
        "margin-top: 8px; padding-top: 10px; color: %5; font-size: 13px; font-weight: 600; }"
        "QLineEdit, QComboBox, QSpinBox { background-color: %1; color: %5; "
        "border: 1px solid %3; border-radius: 3px; padding: 1px 6px; min-height: 22px; max-height: 26px; font-size: 13px; }"
        "QTableWidget { background-color: %1; alternate-background-color: %2; color: %5; selection-background-color: %4; selection-color: %8; gridline-color: %3; border: 1px solid %3; border-radius: 4px; }"
        "QTableWidget::item { color: %5; padding: 2px 8px; }"
        "QTableWidget::item:selected { background-color: %4; color: %8; }"
        "QHeaderView:vertical { min-height: 0px; max-height: 16777215px; }"
        "QHeaderView::section { background-color: %2; color: %5; border: none; border-bottom: 1px solid %3; border-right: 1px solid %3; padding: 4px 8px; font-weight: 600; }"
        "QStackedWidget QPushButton { background-color: %2; color: %5; border: 1px solid %3; "
        "border-radius: 3px; padding: 2px 10px; min-height: 22px; max-height: 26px; font-size: 13px; }"
        "QStackedWidget QPushButton:hover { background-color: %6; color: %7; border-color: %4; }"
        "QLabel#separator { background-color: %3; }")
                      .arg(canvas, header, border, accent, text, hoverBg, hoverFg, selFg));
    ensurePolished();

    const QColor canvasCol(canvas);
    const QColor headerCol(header);
    const QColor borderCol(border);
    const QColor textCol(text);
    const QColor selFgCol(selFg);
    QColor placeholderCol(textCol);
    placeholderCol.setAlpha(185);

    QPalette dlgPal = palette();
    for (QPalette::ColorGroup cg : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        dlgPal.setColor(cg, QPalette::Window, canvasCol);
        dlgPal.setColor(cg, QPalette::Base, canvasCol);
        dlgPal.setColor(cg, QPalette::AlternateBase, headerCol);
        dlgPal.setColor(cg, QPalette::WindowText, textCol);
        dlgPal.setColor(cg, QPalette::Text, textCol);
        dlgPal.setColor(cg, QPalette::PlaceholderText, placeholderCol);
        dlgPal.setColor(cg, QPalette::Button, headerCol);
        dlgPal.setColor(cg, QPalette::ButtonText, textCol);
        dlgPal.setColor(cg, QPalette::Mid, borderCol);
        dlgPal.setColor(cg, QPalette::Highlight, accentCol);
        dlgPal.setColor(cg, QPalette::HighlightedText, selFgCol);
    }
    setPalette(dlgPal);
    setAttribute(Qt::WA_StyledBackground, true);
    setAutoFillBackground(true);

    if (m_stack) {
        m_stack->setAttribute(Qt::WA_StyledBackground, true);
        m_stack->setPalette(dlgPal);
        m_stack->setAutoFillBackground(true);
        for (int i = 0; i < m_stack->count(); ++i) {
            if (QWidget* page = m_stack->widget(i)) {
                page->setAttribute(Qt::WA_StyledBackground, true);
                page->setPalette(dlgPal);
                page->setAutoFillBackground(true);
                if (auto* sa = qobject_cast<QScrollArea*>(page)) {
                    if (sa->viewport()) {
                        sa->viewport()->setPalette(dlgPal);
                        sa->viewport()->setAutoFillBackground(true);
                    }
                    if (sa->widget()) {
                        sa->widget()->setAttribute(Qt::WA_StyledBackground, true);
                        sa->widget()->setPalette(dlgPal);
                        sa->widget()->setAutoFillBackground(true);
                    }
                }
            }
        }
        for (QTableWidget* tbl : m_stack->findChildren<QTableWidget*>()) {
            tbl->setPalette(dlgPal);
            if (QHeaderView* vh = tbl->verticalHeader()) {
                vh->setStyleSheet(QStringLiteral("QHeaderView { min-height: 0px; max-height: 16777215px; height: auto; }"));
                vh->setMaximumHeight(QWIDGETSIZE_MAX);
            }
            if (tbl->viewport()) {
                tbl->viewport()->setPalette(dlgPal);
                tbl->viewport()->update();
            }
        }
    }

    if (m_sidebar) {
        m_sidebar->setStyleSheet(QStringLiteral(
            "QListWidget, QListView, QListWidget#preferencesCategoryList { "
            "background-color: %1; color: %4; selection-background-color: %3; selection-color: %7; "
            "padding: 0px; margin: 0px; border: none; border-right: 1px solid %2; border-radius: 0px; "
            "outline: none; font-size: 13px; }"
            "QListWidget::item, QListView::item { "
            "padding: 8px 12px; min-height: 20px; background-color: %1; color: %4; "
            "selection-background-color: %3; selection-color: %7; }"
            "QListWidget::item:hover, QListView::item:hover { background-color: %5; color: %6; }"
            "QListWidget::item:selected, QListWidget::item:selected:focus, "
            "QListWidget::item:selected:active, QListWidget::item:selected:!active, "
            "QListWidget::item:selected:hover, QListView::item:selected, "
            "QListView::item:selected:focus, QListView::item:selected:active, "
            "QListView::item:selected:!active, QListView::item:selected:hover { "
            "background-color: %3; selection-background-color: %3; "
            "color: %7; selection-color: %7; font-weight: 600; }")
                                     .arg(header, border, accent, text, hoverBg, hoverFg, selFg));
        m_sidebar->ensurePolished();

        QPalette sidePal = m_sidebar->palette();
        for (QPalette::ColorGroup cg : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
            sidePal.setColor(cg, QPalette::Window, headerCol);
            sidePal.setColor(cg, QPalette::Base, headerCol);
            sidePal.setColor(cg, QPalette::AlternateBase, headerCol);
            sidePal.setColor(cg, QPalette::WindowText, textCol);
            sidePal.setColor(cg, QPalette::Text, textCol);
            sidePal.setColor(cg, QPalette::Highlight, accentCol);
            sidePal.setColor(cg, QPalette::HighlightedText, selFgCol);
        }
        m_sidebar->setPalette(sidePal);
        m_sidebar->setAttribute(Qt::WA_StyledBackground, true);
        m_sidebar->setAutoFillBackground(true);
        if (m_sidebar->viewport()) {
            m_sidebar->viewport()->setPalette(sidePal);
            m_sidebar->viewport()->setAutoFillBackground(true);
        }
        refreshSidebarIcons();
    }

    if (m_buttonBox) {
        const QString okCss = QStringLiteral(
            "QPushButton, QPushButton#prefsOkBtn { background-color: %1; color: #ffffff; border: 1px solid %1; "
            "font-size: 13px; font-weight: 600; text-transform: none; padding: 6px 18px; margin: 0px; border-radius: 4px; "
            "min-height: 16px; max-height: 16px; }"
            "QPushButton:hover, QPushButton#prefsOkBtn:hover { background-color: %1; color: #ffffff; border: 1px solid %2; }")
                                  .arg(accent, text);
        const QString secCss = QStringLiteral(
            "QPushButton, QPushButton#prefsSecBtn { background-color: %1; color: %2; border: 1px solid %3; "
            "font-size: 13px; text-transform: none; padding: 6px 16px; margin: 0px; border-radius: 4px; "
            "min-height: 16px; max-height: 16px; }"
            "QPushButton:hover, QPushButton#prefsSecBtn:hover { background-color: %4; color: %5; border: 1px solid %6; }")
                                   .arg(header, text, border, hoverBg, hoverFg, accent);
        if (auto* okBtn = m_buttonBox->button(QDialogButtonBox::Ok)) {
            okBtn->setObjectName(QStringLiteral("prefsOkBtn"));
            okBtn->setStyleSheet(okCss);
            okBtn->ensurePolished();
        }
        for (auto role : {QDialogButtonBox::Cancel, QDialogButtonBox::Apply}) {
            if (auto* secBtn = m_buttonBox->button(role)) {
                secBtn->setObjectName(QStringLiteral("prefsSecBtn"));
                secBtn->setStyleSheet(secCss);
                secBtn->ensurePolished();
            }
        }
        for (QAbstractButton* btn : m_buttonBox->buttons()) {
            if (btn) { btn->setIcon(QIcon()); btn->setIconSize(QSize(0, 0)); }
        }
    }
}

void PreferencesDialog::refreshSidebarIcons() {
    if (!m_sidebar) return;
    const auto pal = gridlock::core::managers::ThemeManager::paletteForTheme(
        gridlock::core::managers::ThemeManager::instance().activeThemeId());
    const QColor selFgCol = (QColor(pal.accentHex).lightness() < 165)
                                ? QColor(QStringLiteral("#ffffff"))
                                : QColor(QStringLiteral("#16181d"));
    const QColor textCol(pal.textHex);
    const int activeRow = m_sidebar->currentRow();
    for (int i = 0; i < m_sidebar->count(); ++i) {
        QListWidgetItem* item = m_sidebar->item(i);
        if (!item) continue;
        QPixmap pixmap(16, 16);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setFont(QFont(QStringLiteral("Symbols Nerd Font"), 11));
        painter.setPen((i == activeRow) ? selFgCol : textCol);
        painter.drawText(0, 0, 16, 16, Qt::AlignCenter, item->data(Qt::UserRole).toString());
        item->setIcon(QIcon(pixmap));
    }
}

void PreferencesDialog::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && window()->windowHandle()) window()->windowHandle()->startSystemMove();
    else QDialog::mousePressEvent(event);
}

void PreferencesDialog::mouseMoveEvent(QMouseEvent* event) { QDialog::mouseMoveEvent(event); }

void PreferencesDialog::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    auto& tm = gridlock::core::managers::ThemeManager::instance();
    const auto pal = gridlock::core::managers::ThemeManager::paletteForTheme(tm.activeThemeId());
    painter.fillRect(rect(), QColor(pal.canvasHex));
    QDialog::paintEvent(event);
}

}  // namespace gridlock::ui
