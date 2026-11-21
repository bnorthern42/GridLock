/**
 * @file InlineFindBar.cpp
 * @brief Implementation of the compact floating inline text search bar.
 *
 * Anchors inside the top-right corner of a QPlainTextEdit without overlapping
 * its vertical scrollbar, evaluates case-insensitive or case-sensitive matches,
 * renders highlight selections, and handles Enter / Shift+Enter / Escape navigation.
 */

#include "InlineFindBar.hpp"

#include <algorithm>

#include <QColor>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QPalette>
#include <QScrollBar>
#include <QTextCursor>
#include <QTextDocument>

namespace gridlock::ui {

InlineFindBar::InlineFindBar(QPlainTextEdit* editor, bool externalSelectionSync)
    : QWidget(editor), m_editor(editor), m_externalSelectionSync(externalSelectionSync) {
    setObjectName(QStringLiteral("InlineFindBar"));
    setAttribute(Qt::WA_StyledBackground, true);
    setAutoFillBackground(true);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->setSpacing(4);

    m_findEdit = new QLineEdit(this);
    m_findEdit->setObjectName(QStringLiteral("inlineFindEdit"));
    m_findEdit->setPlaceholderText(tr("Find..."));
    m_findEdit->setClearButtonEnabled(true);
    m_findEdit->setMinimumWidth(145);
    m_findEdit->setFixedHeight(24);
    m_findEdit->installEventFilter(this);
    layout->addWidget(m_findEdit, 1);

    m_matchLabel = new QLabel(QStringLiteral("0/0"), this);
    m_matchLabel->setObjectName(QStringLiteral("inlineFindMatchLabel"));
    m_matchLabel->setAlignment(Qt::AlignCenter);
    m_matchLabel->setMinimumWidth(38);
    layout->addWidget(m_matchLabel);

    m_prevBtn = new QPushButton(tr("Prev"), this);
    m_prevBtn->setObjectName(QStringLiteral("inlineFindPrevBtn"));
    m_prevBtn->setToolTip(tr("Previous Match (Shift+Enter)"));
    m_prevBtn->setFixedHeight(24);
    layout->addWidget(m_prevBtn);

    m_nextBtn = new QPushButton(tr("Next"), this);
    m_nextBtn->setObjectName(QStringLiteral("inlineFindNextBtn"));
    m_nextBtn->setToolTip(tr("Next Match (Enter)"));
    m_nextBtn->setFixedHeight(24);
    layout->addWidget(m_nextBtn);

    m_caseCheck = new QCheckBox(tr("Aa"), this);
    m_caseCheck->setObjectName(QStringLiteral("inlineFindCaseCheck"));
    m_caseCheck->setToolTip(tr("Case Sensitive"));
    layout->addWidget(m_caseCheck);

    m_closeBtn = new QPushButton(QStringLiteral("×"), this);
    m_closeBtn->setObjectName(QStringLiteral("inlineFindCloseBtn"));
    m_closeBtn->setToolTip(tr("Close Find Bar (Esc)"));
    m_closeBtn->setFixedSize(24, 24);
    layout->addWidget(m_closeBtn);

    connect(m_findEdit, &QLineEdit::textChanged, this, [this]() { recomputeMatches(true); });
    connect(m_caseCheck, &QCheckBox::toggled, this, [this]() { recomputeMatches(true); });
    connect(m_nextBtn, &QPushButton::clicked, this, &InlineFindBar::findNext);
    connect(m_prevBtn, &QPushButton::clicked, this, &InlineFindBar::findPrevious);
    connect(m_closeBtn, &QPushButton::clicked, this, &InlineFindBar::closeBar);

    if (m_editor) {
        m_editor->installEventFilter(this);
        if (m_editor->document()) {
            connect(m_editor->document(), &QTextDocument::contentsChanged, this, [this]() {
                if (isVisible()) recomputeMatches(false);
            });
        }
    }

    hide();
}

void InlineFindBar::openAndFocus() {
    if (!m_editor) return;
    // Seed the search input from a single-line editor selection when present.
    const QString selected = m_editor->textCursor().selectedText();
    if (!selected.isEmpty() && !selected.contains(QChar(0x2029)) && !selected.contains(QLatin1Char('\n'))) {
        if (m_findEdit->text() != selected) {
            m_findEdit->setText(selected);
        }
    }
    updatePositionAndStyle();
    show();
    raise();
    recomputeMatches(true);
    m_findEdit->setFocus(Qt::ShortcutFocusReason);
    m_findEdit->selectAll();
}

void InlineFindBar::closeBar() {
    if (!isVisible() && m_matches.isEmpty()) return;
    hide();
    m_matches.clear();
    m_activeMatchIndex = -1;
    if (m_editor) {
        if (!m_externalSelectionSync) {
            m_editor->setExtraSelections({});
        }
        m_editor->setFocus(Qt::OtherFocusReason);
    }
    emit matchesUpdated();
}

bool InlineFindBar::findNext() {
    if (m_matches.isEmpty()) {
        recomputeMatches(true);
    }
    if (m_matches.isEmpty()) return false;
    m_activeMatchIndex = (m_activeMatchIndex + 1) % static_cast<int>(m_matches.size());
    focusActiveMatch(true);
    return true;
}

bool InlineFindBar::findPrevious() {
    if (m_matches.isEmpty()) {
        recomputeMatches(true);
    }
    if (m_matches.isEmpty()) return false;
    const int n = static_cast<int>(m_matches.size());
    m_activeMatchIndex = (m_activeMatchIndex - 1 + n) % n;
    focusActiveMatch(true);
    return true;
}

void InlineFindBar::setSearchText(const QString& text) {
    m_findEdit->setText(text);
}

QString InlineFindBar::searchText() const {
    return m_findEdit ? m_findEdit->text() : QString();
}

bool InlineFindBar::isCaseSensitive() const {
    return m_caseCheck && m_caseCheck->isChecked();
}

void InlineFindBar::setCaseSensitive(bool enabled) {
    if (m_caseCheck) m_caseCheck->setChecked(enabled);
}

QList<QTextEdit::ExtraSelection> InlineFindBar::matchSelections() const {
    QList<QTextEdit::ExtraSelection> out;
    if (!isVisible() || !m_editor || !m_editor->document() || m_matches.isEmpty()) {
        return out;
    }
    const bool isDark = m_editor->palette().color(QPalette::Base).lightness() < 128;
    const QColor passiveBg = isDark ? QColor(224, 175, 104, 70) : QColor(245, 158, 11, 75);
    const QColor activeBg = isDark ? QColor(224, 175, 104, 175) : QColor(245, 158, 11, 170);

    out.reserve(m_matches.size());
    for (int i = 0; i < m_matches.size(); ++i) {
        QTextEdit::ExtraSelection sel;
        sel.format.setBackground(i == m_activeMatchIndex ? activeBg : passiveBg);
        QTextCursor cur(m_editor->document());
        cur.setPosition(m_matches[i].first);
        cur.setPosition(m_matches[i].second, QTextCursor::KeepAnchor);
        sel.cursor = cur;
        out.append(sel);
    }
    return out;
}

void InlineFindBar::updatePositionAndStyle() {
    if (!m_editor) return;
    const bool isDark = m_editor->palette().color(QPalette::Base).lightness() < 128;
    const QString bg = isDark ? QStringLiteral("#21252b") : QStringLiteral("#f3f4f6");
    const QString fieldBg = isDark ? QStringLiteral("#181a1f") : QStringLiteral("#ffffff");
    const QString fg = isDark ? QStringLiteral("#dcdfe4") : QStringLiteral("#1f2328");
    const QString border = isDark ? QStringLiteral("#3b4048") : QStringLiteral("#d0d7de");

    setStyleSheet(QStringLiteral(
        "QWidget#InlineFindBar { background-color: %1; border: 1px solid %4; border-radius: 4px; }"
        "QWidget#InlineFindBar QLineEdit { background-color: %2; color: %3; border: 1px solid %4; border-radius: 3px; padding: 1px 6px; font-size: 12px; }"
        "QWidget#InlineFindBar QPushButton { background-color: %2; color: %3; border: 1px solid %4; border-radius: 3px; padding: 1px 6px; font-size: 11px; }"
        "QWidget#InlineFindBar QLabel, QWidget#InlineFindBar QCheckBox { color: %3; font-size: 11px; background: transparent; border: none; }")
                      .arg(bg, fieldBg, fg, border));

    // Anchor inside the top-right corner of the editor while keeping the vertical scrollbar uncovered.
    const int sbW = (m_editor->verticalScrollBar() && m_editor->verticalScrollBar()->isVisible())
                        ? m_editor->verticalScrollBar()->width()
                        : 14;
    const int leftInset = m_editor->viewport() ? m_editor->viewport()->geometry().left() : 0;
    const int availW = std::max(180, m_editor->width() - leftInset - sbW - 12);
    const int barW = std::clamp(sizeHint().width(), 220, std::min(430, availW));
    const int barH = std::max(32, sizeHint().height());
    const int x = std::max(leftInset + 4, m_editor->width() - sbW - barW - 8);
    const int y = m_editor->contentsRect().top() + 6;
    setGeometry(x, y, barW, barH);
}

bool InlineFindBar::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_editor && event) {
        if (event->type() == QEvent::Resize || event->type() == QEvent::Show) {
            if (isVisible()) updatePositionAndStyle();
        } else if (event->type() == QEvent::KeyPress) {
            auto* ke = static_cast<QKeyEvent*>(event);
            const auto mods = ke->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier);
            if (mods == Qt::ControlModifier && ke->key() == Qt::Key_F) {
                openAndFocus();
                return true;
            }
            if (isVisible() && mods == Qt::NoModifier && ke->key() == Qt::Key_Escape) {
                closeBar();
                return true;
            }
        }
    } else if (watched == m_findEdit && event && event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Escape) {
            closeBar();
            return true;
        }
        if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
            if (ke->modifiers() & Qt::ShiftModifier) {
                findPrevious();
            } else {
                findNext();
            }
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void InlineFindBar::recomputeMatches(bool preserveNearestToCursor) {
    m_matches.clear();
    m_activeMatchIndex = -1;
    if (!m_editor || !m_editor->document()) {
        updateMatchLabel();
        return;
    }

    const QString query = searchText();
    if (query.isEmpty()) {
        updateMatchLabel();
        if (!m_externalSelectionSync && isVisible()) m_editor->setExtraSelections({});
        emit matchesUpdated();
        return;
    }

    QTextDocument::FindFlags flags;
    if (isCaseSensitive()) flags |= QTextDocument::FindCaseSensitively;

    // Collect every occurrence of `query` in document order.
    QTextCursor scan(m_editor->document());
    while (true) {
        scan = m_editor->document()->find(query, scan, flags);
        if (scan.isNull()) break;
        m_matches.append({scan.selectionStart(), scan.selectionEnd()});
        if (scan.selectionEnd() == scan.selectionStart()) break;
    }

    if (!m_matches.isEmpty()) {
        const int anchorPos = m_editor->textCursor().selectionStart();
        m_activeMatchIndex = 0;
        if (preserveNearestToCursor) {
            for (int i = 0; i < m_matches.size(); ++i) {
                if (m_matches[i].first >= anchorPos) {
                    m_activeMatchIndex = i;
                    break;
                }
            }
        }
    }

    focusActiveMatch(preserveNearestToCursor && isVisible());
}

void InlineFindBar::focusActiveMatch(bool moveEditorCursor) {
    updateMatchLabel();
    if (moveEditorCursor && m_editor && m_activeMatchIndex >= 0 && m_activeMatchIndex < m_matches.size()) {
        QTextCursor cur(m_editor->document());
        cur.setPosition(m_matches[m_activeMatchIndex].first);
        cur.setPosition(m_matches[m_activeMatchIndex].second, QTextCursor::KeepAnchor);
        m_editor->setTextCursor(cur);
        m_editor->ensureCursorVisible();
    }
    if (m_editor && !m_externalSelectionSync) {
        m_editor->setExtraSelections(isVisible() ? matchSelections() : QList<QTextEdit::ExtraSelection>{});
    }
    emit matchesUpdated();
}

void InlineFindBar::updateMatchLabel() {
    if (!m_matchLabel) return;
    if (searchText().isEmpty()) {
        m_matchLabel->setText(QString());
    } else if (m_matches.isEmpty() || m_activeMatchIndex < 0) {
        m_matchLabel->setText(QStringLiteral("0/0"));
    } else {
        m_matchLabel->setText(QStringLiteral("%1/%2").arg(m_activeMatchIndex + 1).arg(m_matches.size()));
    }
}

}  // namespace gridlock::ui
