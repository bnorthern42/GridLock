/**
 * @file CommandPaletteDialog.cpp
 * @brief Implementation of the unified fuzzy Command Palette overlay for GridLock.
 *
 * Implements deterministic dynamic-programming subsequence fuzzy scoring, prefix-driven
 * category filtering (>, @, #, theme:, and unified file/command search), custom-painted
 * match-highlighting rows, and MainWindow data collection across menu actions, workspace
 * files, C++/ELF symbols, MPI ranks, and workstation color themes.
 */

#include "CommandPaletteDialog.hpp"

#include <algorithm>
#include <limits>

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QListWidgetItem>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QRegularExpression>
#include <QSet>
#include <QStyledItemDelegate>
#include <QTextBlock>
#include <QTextCursor>
#include <QVBoxLayout>

#include "../../core/managers/ThemeManager.hpp"
#include "../../core/utils/ElfBinaryInspector.hpp"
#include "../EditorTabManager.hpp"
#include "../MainWindow.hpp"
#include "../views/ServerRackView.hpp"
#include "../views/SourceCodeView.hpp"
#include "../widgets/ProjectExplorerWidget.hpp"

namespace gridlock::ui {

namespace {

/**
 * @brief Checks whether position @p j in @p text sits on a word, path, or camelCase boundary.
 * @param text Candidate string being evaluated.
 * @param j 0-based index within @p text.
 * @return True if @p j is at index 0 or immediately follows a delimiter or lower-to-upper transition.
 */
bool isWordBoundaryIndex(const QString& text, int j) {
    if (j <= 0) return true;
    const QChar prev = text.at(j - 1);
    const QChar curr = text.at(j);
    if (prev == QLatin1Char('/') || prev == QLatin1Char('\\') || prev == QLatin1Char('_') ||
        prev == QLatin1Char('-') || prev == QLatin1Char(' ') || prev == QLatin1Char(':') ||
        prev == QLatin1Char('.')) {
        return true;
    }
    return (prev.isLower() && curr.isUpper()) || (!prev.isLetterOrNumber() && curr.isLetterOrNumber());
}

/**
 * @brief Computes single-character alignment bonus for matching pattern[i] at candidate[j].
 * @param pattern Search query string.
 * @param i Index in @p pattern.
 * @param candidate Target string.
 * @param j Index in @p candidate.
 * @return Positional and boundary bonus score for this character match.
 */
int computeCharBonus(const QString& pattern, int i, const QString& candidate, int j) {
    int bonus = 15 + (pattern.at(i) == candidate.at(j) ? 2 : 0);
    if (j == 0) return bonus + 45;
    if (candidate.at(j - 1) == QLatin1Char('/') || candidate.at(j - 1) == QLatin1Char('\\')) return bonus + 38;
    if (isWordBoundaryIndex(candidate, j)) return bonus + 32;
    return bonus;
}

/**
 * @class CommandPaletteItemDelegate
 * @brief Custom row renderer painting category pills, bold matched characters, subtitles, and shortcut badges.
 */
class CommandPaletteItemDelegate : public QStyledItemDelegate {
public:
    explicit CommandPaletteItemDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent) {}

    QSize sizeHint(const QStyleOptionViewItem& /*option*/,
                   const QModelIndex& /*index*/) const override {
        return QSize(560, 38);
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        if (!painter) return;
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);

        const auto& tm = gridlock::core::ThemeManager::instance();
        const auto pal = gridlock::core::ThemeManager::paletteForTheme(tm.activeThemeId());
        const bool isDark = pal.isDark;
        const bool isSelected = (option.state & QStyle::State_Selected) != 0;
        const bool isHovered = (option.state & QStyle::State_MouseOver) != 0;

        const QRect rowRect = option.rect.adjusted(4, 2, -4, -2);
        const QColor rowBg = isSelected
                                 ? (isDark ? QColor(QStringLiteral("#1e293b")) : QColor(QStringLiteral("#dbeafe")))
                                 : (isHovered ? (isDark ? QColor(QStringLiteral("#172030")) : QColor(QStringLiteral("#eff6ff")))
                                              : QColor(Qt::transparent));
        if (rowBg.alpha() > 0) {
            painter->setPen(isSelected ? QColor(pal.accentHex) : QColor(Qt::NoPen));
            painter->setBrush(rowBg);
            painter->drawRoundedRect(rowRect, 5.0, 5.0);
        }

        const QString title = index.data(Qt::UserRole).toString();
        const QString subtitle = index.data(Qt::UserRole + 1).toString();
        const QString badge = index.data(Qt::UserRole + 2).toString();
        const int catInt = index.data(Qt::UserRole + 3).toInt();
        const QVariantList matchList = index.data(Qt::UserRole + 4).toList();
        QSet<int> matchedSet;
        for (const QVariant& v : matchList) matchedSet.insert(v.toInt());

        // Resolve category tag label and accent color.
        QString catTag = QStringLiteral("CMD");
        QColor catColor(QStringLiteral("#38bdf8"));
        switch (static_cast<PaletteItem::Category>(catInt)) {
            case PaletteItem::Category::Action: catTag = QStringLiteral("CMD");   catColor = QColor(QStringLiteral("#38bdf8")); break;
            case PaletteItem::Category::File:   catTag = QStringLiteral("FILE");  catColor = QColor(QStringLiteral("#34d399")); break;
            case PaletteItem::Category::Symbol: catTag = QStringLiteral("SYM");   catColor = QColor(QStringLiteral("#f59e0b")); break;
            case PaletteItem::Category::Rank:   catTag = QStringLiteral("RANK");  catColor = QColor(QStringLiteral("#a78bfa")); break;
            case PaletteItem::Category::Theme:  catTag = QStringLiteral("THEME"); catColor = QColor(QStringLiteral("#f472b6")); break;
        }

        // Draw left category tag pill.
        QFont tagFont = option.font;
        tagFont.setPointSize(qMax(8, option.font.pointSize() - 2));
        tagFont.setBold(true);
        painter->setFont(tagFont);
        const QRect tagRect(rowRect.left() + 8, rowRect.top() + 8, 44, 18);
        QColor tagFill = catColor;
        tagFill.setAlpha(isDark ? 38 : 32);
        painter->setPen(catColor);
        painter->setBrush(tagFill);
        painter->drawRoundedRect(tagRect, 3.0, 3.0);
        painter->drawText(tagRect, Qt::AlignCenter, catTag);

        // Draw right-aligned shortcut or status badge if present.
        int rightLimit = rowRect.right() - 10;
        if (!badge.isEmpty()) {
            QFont badgeFont = option.font;
            badgeFont.setPointSize(qMax(8, option.font.pointSize() - 2));
            badgeFont.setBold(true);
            painter->setFont(badgeFont);
            const QFontMetrics bfm(badgeFont);
            const int bw = qMin(160, bfm.horizontalAdvance(badge) + 14);
            const QRect badgeRect(rightLimit - bw, rowRect.top() + 8, bw, 18);
            const QColor badgeBg = isDark ? QColor(QStringLiteral("#0f172a")) : QColor(QStringLiteral("#e2e8f0"));
            const QColor badgeBorder = isDark ? QColor(QStringLiteral("#334155")) : QColor(QStringLiteral("#cbd5e1"));
            const QColor badgeFg = isDark ? QColor(QStringLiteral("#cbd5e1")) : QColor(QStringLiteral("#334155"));
            painter->setPen(badgeBorder);
            painter->setBrush(badgeBg);
            painter->drawRoundedRect(badgeRect, 3.0, 3.0);
            painter->setPen(badgeFg);
            painter->drawText(badgeRect, Qt::AlignCenter, bfm.elidedText(badge, Qt::ElideRight, bw - 8));
            rightLimit = badgeRect.left() - 10;
        }

        // Draw title with bold highlighted matched characters.
        int cursorX = tagRect.right() + 10;
        const int textTop = rowRect.top() + 3;
        const int textHeight = rowRect.height() - 6;
        QFont normalFont = option.font;
        QFont boldFont = option.font;
        boldFont.setBold(true);
        const QFontMetrics nfm(normalFont);
        const QFontMetrics bfm(boldFont);
        const QColor normalFg = isDark ? QColor(QStringLiteral("#e2e8f0")) : QColor(QStringLiteral("#0f172a"));
        const QColor matchFg = isDark ? QColor(QStringLiteral("#38bdf8")) : QColor(QStringLiteral("#0284c7"));

        for (int i = 0; i < title.size(); ++i) {
            const bool matched = matchedSet.contains(i);
            painter->setFont(matched ? boldFont : normalFont);
            painter->setPen(matched ? matchFg : normalFg);
            const QString chStr(title.at(i));
            const int cw = matched ? bfm.horizontalAdvance(chStr) : nfm.horizontalAdvance(chStr);
            if (cursorX + cw > rightLimit) break;
            painter->drawText(QRect(cursorX, textTop, cw + 2, textHeight), Qt::AlignLeft | Qt::AlignVCenter, chStr);
            cursorX += cw;
        }

        // Draw muted subtitle to the right of the title if horizontal space remains.
        if (!subtitle.isEmpty() && cursorX + 24 < rightLimit) {
            QFont subFont = option.font;
            subFont.setPointSize(qMax(8, option.font.pointSize() - 1));
            painter->setFont(subFont);
            const QFontMetrics sfm(subFont);
            painter->setPen(QColor(QStringLiteral("#64748b")));
            const int avail = rightLimit - (cursorX + 12);
            painter->drawText(QRect(cursorX + 12, textTop, avail, textHeight), Qt::AlignLeft | Qt::AlignVCenter,
                              sfm.elidedText(subtitle, Qt::ElideMiddle, avail));
        }

        painter->restore();
    }
};

}  // namespace

int fuzzyScore(const QString& pattern, const QString& candidate, QVector<int>* matchedIndices) {
    return CommandPaletteDialog::fuzzyScore(pattern, candidate, matchedIndices);
}

int CommandPaletteDialog::fuzzyScore(const QString& pattern, const QString& candidate,
                                     QVector<int>* matchedIndices) {
    if (matchedIndices) matchedIndices->clear();
    if (pattern.isEmpty()) return 0;
    const int m = pattern.size(), n = candidate.size();
    if (n == 0 || m > n) return -1;

    // Fast linear subsequence pre-check (case-insensitive).
    int checkIdx = 0;
    for (int j = 0; j < n && checkIdx < m; ++j) {
        if (pattern.at(checkIdx).toLower() == candidate.at(j).toLower()) ++checkIdx;
    }
    if (checkIdx < m) return -1;

    // Dynamic programming table to find highest-scoring subsequence alignment.
    constexpr int kNegInf = std::numeric_limits<int>::min() / 4;
    QVector<QVector<int>> dp(m, QVector<int>(n, kNegInf));
    QVector<QVector<int>> parent(m, QVector<int>(n, -1));

    const QChar firstPatLower = pattern.at(0).toLower();
    for (int j = 0; j < n; ++j) {
        if (candidate.at(j).toLower() == firstPatLower) {
            dp[0][j] = computeCharBonus(pattern, 0, candidate, j) - qMin(j, 20);
        }
    }

    for (int i = 1; i < m; ++i) {
        const QChar patLower = pattern.at(i).toLower();
        for (int j = i; j < n; ++j) {
            if (candidate.at(j).toLower() != patLower) continue;
            const int cBonus = computeCharBonus(pattern, i, candidate, j);
            int bestPrevScore = kNegInf, bestPrevIdx = -1;
            for (int prevJ = i - 1; prevJ < j; ++prevJ) {
                if (dp[i - 1][prevJ] <= kNegInf) continue;
                int transition = cBonus + ((j == prevJ + 1) ? 30 : -qMin(j - prevJ - 1, 15));
                const int candidateScore = dp[i - 1][prevJ] + transition;
                if (candidateScore > bestPrevScore) {
                    bestPrevScore = candidateScore;
                    bestPrevIdx = prevJ;
                }
            }
            dp[i][j] = bestPrevScore;
            parent[i][j] = bestPrevIdx;
        }
    }

    int bestEndJ = -1, bestTotal = kNegInf;
    for (int j = m - 1; j < n; ++j) {
        if (dp[m - 1][j] > bestTotal) {
            bestTotal = dp[m - 1][j];
            bestEndJ = j;
        }
    }
    if (bestEndJ < 0 || bestTotal <= kNegInf) return -1;

    if (matchedIndices) {
        matchedIndices->resize(m);
        int currJ = bestEndJ;
        for (int i = m - 1; i >= 0; --i) {
            (*matchedIndices)[i] = currJ;
            currJ = (i > 0) ? parent[i][currJ] : -1;
        }
    }

    // Apply compact candidate length preference and exact/prefix bonuses.
    int finalScore = bestTotal + qMax(0, 40 - n / 2);
    if (pattern.compare(candidate, Qt::CaseInsensitive) == 0) finalScore += 100;
    else if (candidate.startsWith(pattern, Qt::CaseInsensitive)) finalScore += 50;
    return qMax(1, finalScore);
}

CommandPaletteDialog::CommandPaletteDialog(QWidget* parent)
    : QDialog(parent, Qt::Dialog | Qt::FramelessWindowHint) {
    setObjectName(QStringLiteral("CommandPaletteDialog"));
    setWindowTitle(QStringLiteral("Command Palette"));
    setModal(false);
    resize(680, 420);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(10, 10, 10, 10);
    rootLayout->setSpacing(8);

    // Top search bar row housing mode badge, fuzzy input field, and match counter.
    auto* topBarLayout = new QHBoxLayout();
    topBarLayout->setContentsMargins(0, 0, 0, 0);
    topBarLayout->setSpacing(6);

    m_modePillLabel = new QLabel(QStringLiteral("ALL / FILES"), this);
    m_modePillLabel->setObjectName(QStringLiteral("CommandPaletteModePill"));
    m_modePillLabel->setAlignment(Qt::AlignCenter);
    m_modePillLabel->setFixedHeight(26);

    m_inputEdit = new QLineEdit(this);
    m_inputEdit->setObjectName(QStringLiteral("CommandPaletteInput"));
    m_inputEdit->setPlaceholderText(
        QStringLiteral("Type a file name, > for commands, @ for symbols, # for MPI ranks..."));
    m_inputEdit->setClearButtonEnabled(true);
    m_inputEdit->setMinimumHeight(30);

    m_countLabel = new QLabel(QStringLiteral("0 matches"), this);
    m_countLabel->setObjectName(QStringLiteral("CommandPaletteCountLabel"));
    m_countLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    topBarLayout->addWidget(m_modePillLabel);
    topBarLayout->addWidget(m_inputEdit, 1);
    topBarLayout->addWidget(m_countLabel);
    rootLayout->addLayout(topBarLayout);

    // Filtered results list widget with custom match-highlighting item delegate.
    m_resultsList = new QListWidget(this);
    m_resultsList->setObjectName(QStringLiteral("CommandPaletteResults"));
    m_resultsList->setItemDelegate(new CommandPaletteItemDelegate(m_resultsList));
    m_resultsList->setMouseTracking(true);
    m_resultsList->setUniformItemSizes(true);
    m_resultsList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_resultsList->setSelectionMode(QAbstractItemView::SingleSelection);
    rootLayout->addWidget(m_resultsList, 1);

    // Install event filters on input and results list for seamless Up/Down/Enter/Escape routing.
    m_inputEdit->installEventFilter(this);
    m_resultsList->installEventFilter(this);

    connect(m_inputEdit, &QLineEdit::textChanged, this, [this]() { refreshFilteredResults(); });
    connect(m_resultsList, &QListWidget::itemActivated, this, [this](QListWidgetItem*) { triggerSelected(); });
    connect(m_resultsList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) { triggerSelected(); });

    reloadStyle();
}

void CommandPaletteDialog::reloadStyle() {
    const auto& tm = gridlock::core::ThemeManager::instance();
    const auto pal = gridlock::core::ThemeManager::paletteForTheme(tm.activeThemeId());
    const QString bg = pal.isDark ? QStringLiteral("#131722") : QStringLiteral("#f8fafc");
    const QString inputBg = pal.isDark ? QStringLiteral("#0d1117") : QStringLiteral("#ffffff");
    const QString muted = pal.isDark ? QStringLiteral("#94a3b8") : QStringLiteral("#475569");

    setStyleSheet(QStringLiteral(
        "QDialog#CommandPaletteDialog { background-color: %1; border: 1px solid %2; border-radius: 8px; }"
        "QLineEdit#CommandPaletteInput { background-color: %3; color: %4; border: 1px solid %5; border-radius: 5px; padding: 4px 8px; font-size: 11pt; }"
        "QLabel#CommandPaletteModePill { background-color: %3; color: %5; border: 1px solid %5; border-radius: 4px; padding: 2px 8px; font-size: 8pt; font-weight: bold; }"
        "QLabel#CommandPaletteCountLabel { color: %6; font-size: 9pt; padding: 0 4px; }"
        "QListWidget#CommandPaletteResults { background-color: %1; color: %4; border: none; outline: none; }")
                      .arg(bg, pal.borderHex, inputBg, pal.textHex, pal.accentHex, muted));
    if (m_resultsList && m_resultsList->viewport()) m_resultsList->viewport()->update();
}

void CommandPaletteDialog::setItems(const QVector<PaletteItem>& items) {
    m_allItems = items;
    refreshFilteredResults();
}

void CommandPaletteDialog::addItem(const PaletteItem& item) {
    m_allItems.append(item);
    refreshFilteredResults();
}

void CommandPaletteDialog::clearItems() {
    m_allItems.clear();
    m_filteredItems.clear();
    m_filteredMatchIndices.clear();
    if (m_resultsList) m_resultsList->clear();
    if (m_countLabel) m_countLabel->setText(QStringLiteral("0 matches"));
}

void CommandPaletteDialog::setFilterText(const QString& text) {
    if (!m_inputEdit) return;
    if (m_inputEdit->text() == text) refreshFilteredResults();
    else m_inputEdit->setText(text);
}

QString CommandPaletteDialog::filterText() const {
    return m_inputEdit ? m_inputEdit->text() : QString();
}

void CommandPaletteDialog::openWithPrefix(const QString& initialPrefix) {
    reloadStyle();
    if (m_inputEdit) {
        m_inputEdit->blockSignals(true);
        m_inputEdit->setText(initialPrefix);
        m_inputEdit->blockSignals(false);
    }
    refreshFilteredResults();
    positionOverlayNearTopCenter();
    show();
    raise();
    activateWindow();
    if (m_inputEdit) {
        m_inputEdit->setFocus(Qt::ShortcutFocusReason);
        m_inputEdit->setCursorPosition(m_inputEdit->text().size());
    }
}

int CommandPaletteDialog::resultCount() const {
    return m_resultsList ? m_resultsList->count() : 0;
}

int CommandPaletteDialog::selectedIndex() const {
    return m_resultsList ? m_resultsList->currentRow() : -1;
}

void CommandPaletteDialog::setSelectedIndex(int row) {
    if (!m_resultsList || row < 0 || row >= m_resultsList->count()) return;
    m_resultsList->setCurrentRow(row);
    m_resultsList->scrollToItem(m_resultsList->item(row));
}

PaletteItem CommandPaletteDialog::resultItemAt(int row) const {
    return (row >= 0 && row < m_filteredItems.size()) ? m_filteredItems.at(row) : PaletteItem{};
}

void CommandPaletteDialog::moveSelection(int delta) {
    const int total = resultCount();
    if (total <= 0) return;
    int current = selectedIndex();
    if (current < 0) current = 0;
    int next = (current + delta) % total;
    if (next < 0) next += total;
    setSelectedIndex(next);
}

bool CommandPaletteDialog::triggerSelected() {
    const int row = selectedIndex();
    if (row < 0 || row >= m_filteredItems.size()) return false;
    const auto cb = m_filteredItems.at(row).callback;
    accept();
    if (cb) cb();
    return true;
}

bool CommandPaletteDialog::eventFilter(QObject* watched, QEvent* event) {
    if ((watched == m_inputEdit || watched == m_resultsList) && event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        const int key = ke->key();
        const auto mods = ke->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier);
        if (key == Qt::Key_Down || (key == Qt::Key_N && mods == Qt::ControlModifier)) { moveSelection(+1); return true; }
        if (key == Qt::Key_Up || (key == Qt::Key_P && mods == Qt::ControlModifier)) { moveSelection(-1); return true; }
        if (key == Qt::Key_Return || key == Qt::Key_Enter) { triggerSelected(); return true; }
        if (key == Qt::Key_Escape) { reject(); return true; }
    }
    return QDialog::eventFilter(watched, event);
}

void CommandPaletteDialog::keyPressEvent(QKeyEvent* event) {
    if (event) {
        const int key = event->key();
        const auto mods = event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier);
        if (key == Qt::Key_Down || (key == Qt::Key_N && mods == Qt::ControlModifier)) { moveSelection(+1); event->accept(); return; }
        if (key == Qt::Key_Up || (key == Qt::Key_P && mods == Qt::ControlModifier)) { moveSelection(-1); event->accept(); return; }
        if (key == Qt::Key_Return || key == Qt::Key_Enter) { triggerSelected(); event->accept(); return; }
        if (key == Qt::Key_Escape) { reject(); event->accept(); return; }
    }
    QDialog::keyPressEvent(event);
}

void CommandPaletteDialog::positionOverlayNearTopCenter() {
    if (QWidget* p = parentWidget()) {
        const QRect parentGeom = p->geometry();
        const int w = qBound(520, (parentGeom.width() * 52) / 100, 760);
        const int h = qBound(340, (parentGeom.height() * 48) / 100, 480);
        resize(w, h);
        move(p->mapToGlobal(QPoint((parentGeom.width() - w) / 2, 44)));
    }
}

void CommandPaletteDialog::refreshFilteredResults() {
    if (!m_resultsList) return;
    QString trimmedLeft = m_inputEdit ? m_inputEdit->text() : QString();
    while (!trimmedLeft.isEmpty() && trimmedLeft.at(0).isSpace()) trimmedLeft.remove(0, 1);

    // Parse prefix mode: '>', '@', '#' / 'rank:', 'theme:', or unified mode.
    bool filterCategory = false;
    Category requiredCategory = Category::File;
    QString query = trimmedLeft;
    QString pillText = QStringLiteral("ALL / FILES");

    if (trimmedLeft.startsWith(QLatin1Char('>'))) {
        filterCategory = true; requiredCategory = Category::Action;
        query = trimmedLeft.mid(1).trimmed(); pillText = QStringLiteral("COMMANDS (>)");
    } else if (trimmedLeft.startsWith(QLatin1Char('@'))) {
        filterCategory = true; requiredCategory = Category::Symbol;
        query = trimmedLeft.mid(1).trimmed(); pillText = QStringLiteral("SYMBOLS (@)");
    } else if (trimmedLeft.startsWith(QLatin1Char('#'))) {
        filterCategory = true; requiredCategory = Category::Rank;
        query = trimmedLeft.mid(1).trimmed(); pillText = QStringLiteral("MPI RANKS (#)");
    } else if (trimmedLeft.startsWith(QStringLiteral("rank:"), Qt::CaseInsensitive)) {
        filterCategory = true; requiredCategory = Category::Rank;
        query = trimmedLeft.mid(5).trimmed(); pillText = QStringLiteral("MPI RANKS (#)");
    } else if (trimmedLeft.startsWith(QStringLiteral("theme:"), Qt::CaseInsensitive)) {
        filterCategory = true; requiredCategory = Category::Theme;
        query = trimmedLeft.mid(6).trimmed(); pillText = QStringLiteral("THEMES");
    } else {
        query = trimmedLeft.trimmed();
    }
    if (m_modePillLabel) m_modePillLabel->setText(pillText);

    QVector<ScoredEntry> scored;
    scored.reserve(m_allItems.size());

    for (int idx = 0; idx < m_allItems.size(); ++idx) {
        const PaletteItem& item = m_allItems.at(idx);
        if (filterCategory && item.category != requiredCategory) continue;

        if (query.isEmpty()) {
            // In default unified empty-query mode, prioritize Files first, then Ranks, Symbols, Actions, Themes.
            int baseOrderScore = 100;
            if (!filterCategory) {
                switch (item.category) {
                    case Category::File:   baseOrderScore = 500; break;
                    case Category::Rank:   baseOrderScore = 400; break;
                    case Category::Symbol: baseOrderScore = 300; break;
                    case Category::Action: baseOrderScore = 200; break;
                    case Category::Theme:  baseOrderScore = 100; break;
                }
            }
            scored.append(ScoredEntry{idx, baseOrderScore, {}});
            continue;
        }

        QVector<int> matchedIndices;
        int sc = fuzzyScore(query, item.title, &matchedIndices);
        if (sc < 0 && !item.subtitle.isEmpty()) {
            const int subSc = fuzzyScore(query, item.subtitle, nullptr);
            if (subSc >= 0) sc = qMax(1, subSc - 15);
        }
        if (sc >= 0) scored.append(ScoredEntry{idx, sc, matchedIndices});
    }

    std::stable_sort(scored.begin(), scored.end(), [this, &query](const ScoredEntry& a, const ScoredEntry& b) {
        if (a.score != b.score) return a.score > b.score;
        if (!query.isEmpty()) {
            const int lenA = m_allItems.at(a.itemIndex).title.size();
            const int lenB = m_allItems.at(b.itemIndex).title.size();
            if (lenA != lenB) return lenA < lenB;
        }
        return a.itemIndex < b.itemIndex;
    });

    m_filteredItems.clear();
    m_filteredMatchIndices.clear();
    m_resultsList->clear();

    const int maxVisible = qMin(static_cast<int>(scored.size()), 250);
    m_filteredItems.reserve(maxVisible);
    m_filteredMatchIndices.reserve(maxVisible);

    for (int i = 0; i < maxVisible; ++i) {
        const ScoredEntry& se = scored.at(i);
        const PaletteItem& item = m_allItems.at(se.itemIndex);
        m_filteredItems.append(item);
        m_filteredMatchIndices.append(se.matchedIndices);

        auto* listItem = new QListWidgetItem(item.title, m_resultsList);
        listItem->setToolTip(item.subtitle.isEmpty() ? item.title : item.subtitle);
        listItem->setData(Qt::UserRole, item.title);
        listItem->setData(Qt::UserRole + 1, item.subtitle);
        listItem->setData(Qt::UserRole + 2, item.badge);
        listItem->setData(Qt::UserRole + 3, static_cast<int>(item.category));
        QVariantList matchVars;
        matchVars.reserve(se.matchedIndices.size());
        for (int pos : se.matchedIndices) matchVars.append(pos);
        listItem->setData(Qt::UserRole + 4, matchVars);
    }

    if (m_countLabel) {
        m_countLabel->setText(QStringLiteral("%1 %2")
                                  .arg(m_filteredItems.size())
                                  .arg(m_filteredItems.size() == 1 ? QStringLiteral("match") : QStringLiteral("matches")));
    }
    if (m_resultsList->count() > 0) m_resultsList->setCurrentRow(0);
}

void CommandPaletteDialog::populateFromMainWindow(MainWindow* mainWindow) {
    if (!mainWindow) return;
    QVector<PaletteItem> catalog;
    catalog.reserve(256);

    // 1. Collect Menu Bar & Core Debugger Actions (Category::Action).
    QSet<QString> seenActions;
    auto registerActionItem = [&](const QString& title, const QString& subtitle,
                                  const QString& badge, std::function<void()> cb) {
        if (title.isEmpty() || seenActions.contains(title)) return;
        seenActions.insert(title);
        catalog.append(PaletteItem{Category::Action, title, subtitle, badge, std::move(cb)});
    };

    if (QMenuBar* mb = mainWindow->menuBar()) {
        auto cleanLabel = [](QString s) { return s.remove(QLatin1Char('&')).trimmed(); };
        std::function<void(QMenu*, const QString&)> walkMenu = [&](QMenu* menu, const QString& prefixPath) {
            if (!menu) return;
            for (QAction* act : menu->actions()) {
                if (!act || act->isSeparator() || !act->isVisible()) continue;
                const QString actText = cleanLabel(act->text());
                if (actText.isEmpty() || actText.startsWith(QStringLiteral("(No "))) continue;
                if (QMenu* sub = act->menu()) {
                    walkMenu(sub, prefixPath + QStringLiteral(" -> ") + actText);
                    continue;
                }
                const QString topCat = prefixPath.section(QStringLiteral(" -> "), -1);
                const QString fullTitle = QStringLiteral("%1: %2").arg(topCat, actText);
                registerActionItem(fullTitle, QStringLiteral("Menu -> ") + prefixPath,
                                   act->shortcut().toString(QKeySequence::NativeText),
                                   [act]() { if (act) act->trigger(); });
            }
        };
        for (QAction* topAct : mb->actions()) {
            if (topAct && topAct->menu()) walkMenu(topAct->menu(), cleanLabel(topAct->menu()->title()));
        }
    }

    // Add explicit Debugger and Workspace Perspective commands so key debugger workflows are always searchable.
    registerActionItem(QStringLiteral("Debug: Attach to Process..."),
                       QStringLiteral("Attach GDB or LLDB-DAP to running /proc PIDs, MPI jobs, or remote gdbserver"),
                       QStringLiteral("Ctrl+Alt+P"), [mainWindow]() { mainWindow->attachToProcessWorkflow(); });
    registerActionItem(QStringLiteral("Debug: Run / Launch Target"),
                       QStringLiteral("Compile and start the configured MPI debugging session"),
                       QStringLiteral("F5"), [mainWindow]() { mainWindow->startTargetWorkflow(); });
    registerActionItem(QStringLiteral("Debug: Continue Execution"),
                       QStringLiteral("Resume execution of the active rank until the next breakpoint"),
                       QStringLiteral("F5"), [mainWindow]() { mainWindow->continueExecutionWorkflow(); });
    registerActionItem(QStringLiteral("Debug: Step Over"),
                       QStringLiteral("Step over the current source line on the focused MPI rank"),
                       QStringLiteral("F10"), [mainWindow]() { mainWindow->stepOverWorkflow(); });
    registerActionItem(QStringLiteral("Debug: Step Into"),
                       QStringLiteral("Step into the called function on the focused MPI rank"),
                       QStringLiteral("F11"), [mainWindow]() { mainWindow->stepIntoWorkflow(); });
    registerActionItem(QStringLiteral("Debug: Step Out"),
                       QStringLiteral("Finish the current stack frame and pause in the caller (-exec-finish)"),
                       QStringLiteral("Shift+F11"), [mainWindow]() { mainWindow->stepOutWorkflow(); });
    registerActionItem(QStringLiteral("Debug: Step Instruction"),
                       QStringLiteral("Single-step one machine instruction on the focused MPI rank"),
                       QStringLiteral("Alt+S"), [mainWindow]() { mainWindow->stepInstructionWorkflow(); });
    registerActionItem(QStringLiteral("Debug: Step Over Instruction (nexti)"),
                       QStringLiteral("Step over one machine instruction (-exec-next-instruction) on the focused MPI rank"),
                       QStringLiteral("Ctrl+Shift+F10"), [mainWindow]() { mainWindow->nextInstructionWorkflow(); });
    registerActionItem(QStringLiteral("Debug: Run to Cursor"),
                       QStringLiteral("Execute until the active source line or disassembly instruction (-exec-until)"),
                       QStringLiteral("Ctrl+F10"), [mainWindow]() { mainWindow->runToCursorWorkflow(); });
    registerActionItem(QStringLiteral("Debug: Set Next Statement / Jump..."),
                       QStringLiteral("Relocate execution PC to a target source line with lexical scope safety check (-exec-jump)"),
                       QString(), [mainWindow]() {
                           if (auto* act = mainWindow->findChild<QAction*>(QStringLiteral("actionJumpToLine"))) act->trigger();
                       });
    registerActionItem(QStringLiteral("Debug: Force Early Return..."),
                       QStringLiteral("Return immediately from the current stack frame with an optional expression (-exec-return)"),
                       QString(), [mainWindow]() { mainWindow->forceEarlyReturnWorkflow(QString(), true); });
    registerActionItem(QStringLiteral("Debug: Break on Function Regex (rbreak)..."),
                       QStringLiteral("Install breakpoints on all functions matching a regular expression (rbreak <pattern>)"),
                       QString(), [mainWindow]() { mainWindow->insertRegexBreakpointWorkflow(QString(), true); });
    registerActionItem(QStringLiteral("Debug: Pause Active Rank"),
                       QStringLiteral("Interrupt execution on the currently focused MPI rank"),
                       QStringLiteral("F6"), [mainWindow]() { mainWindow->pauseRankWorkflow(); });
    registerActionItem(QStringLiteral("Debug: Stop Debugging Session"),
                       QStringLiteral("Terminate all active MPI ranks and return to IDLE state"),
                       QStringLiteral("Shift+F5"), [mainWindow]() { mainWindow->stopTargetWorkflow(); });
    registerActionItem(QStringLiteral("View: Switch to Code Perspective"),
                       QStringLiteral("Focus source editor, variables, and call stack layout"),
                       QStringLiteral("Perspective"), [mainWindow]() { mainWindow->setWorkspacePerspective(QStringLiteral("Code")); });
    registerActionItem(QStringLiteral("View: Switch to Reverse Engineering Perspective"),
                       QStringLiteral("Focus CFG disassembly, pseudo-C decompiler, registers, and hex memory"),
                       QStringLiteral("Perspective"), [mainWindow]() { mainWindow->setWorkspacePerspective(QStringLiteral("Reverse")); });
    registerActionItem(QStringLiteral("View: Switch to MPI Cluster Perspective"),
                       QStringLiteral("Focus MPI deadlock diagnostics, differential watch grid, and server rack"),
                       QStringLiteral("Perspective"), [mainWindow]() { mainWindow->setWorkspacePerspective(QStringLiteral("MPI")); });
    registerActionItem(QStringLiteral("Programmer's Calculator"),
                       QStringLiteral("Floating Hex / Dec / Oct / Bin binary calculator, 64-bit toggle grid, and BSWAP"),
                       QStringLiteral("Ctrl+Alt+C"), [mainWindow]() { mainWindow->openProgrammerCalculator(); });
    registerActionItem(QStringLiteral("Signals & Catchpoints..."),
                       QStringLiteral("Configure POSIX/HPC signal Stop/Print/Pass matrix and C++/syscall/dlopen catchpoints"),
                       QStringLiteral("Ctrl+Alt+S"), [mainWindow]() { mainWindow->openSignalsAndCatchpoints(); });
    registerActionItem(QStringLiteral("Process Checkpoints..."),
                       QStringLiteral("Create, inspect, and restore copy-on-write fork checkpoints with /proc IPC/RDMA safety checks"),
                       QStringLiteral("Ctrl+Alt+K"), [mainWindow]() { mainWindow->openProcessCheckpoints(); });

    // 2. Collect Workspace & Tutorial Files (Category::File) with cached directory indexing.
    QString wsRoot = mainWindow->activeWorkspaceDirectory();
    if (wsRoot.isEmpty() && mainWindow->projectExplorerWidget()) wsRoot = mainWindow->projectExplorerWidget()->rootPath();
    if (wsRoot.isEmpty() || !QFileInfo::exists(wsRoot)) {
        wsRoot = QFileInfo::exists(QStringLiteral("tutorial"))
                     ? QDir::currentPath()
                     : QDir(QDir::currentPath()).filePath(QStringLiteral(".."));
    }
    wsRoot = QFileInfo(wsRoot).canonicalFilePath().isEmpty()
                 ? QFileInfo(wsRoot).absoluteFilePath()
                 : QFileInfo(wsRoot).canonicalFilePath();

    if (m_cachedWorkspaceRoot != wsRoot || m_cachedWorkspaceFiles.isEmpty()) {
        m_cachedWorkspaceRoot = wsRoot;
        m_cachedWorkspaceFiles.clear();

        const QSet<QString> allowedExts = {
            QStringLiteral("cpp"), QStringLiteral("c"),   QStringLiteral("cc"),
            QStringLiteral("cxx"), QStringLiteral("hpp"), QStringLiteral("h"),
            QStringLiteral("hxx"), QStringLiteral("cu"),  QStringLiteral("cuh"),
            QStringLiteral("toml"), QStringLiteral("md"), QStringLiteral("sh")};
        const QSet<QString> allowedNames = {
            QStringLiteral("meson.build"), QStringLiteral("CMakeLists.txt"), QStringLiteral("Makefile")};

        QDir rootDir(wsRoot);
        QDirIterator it(wsRoot, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext() && m_cachedWorkspaceFiles.size() < 1500) {
            const QString filePath = it.next();
            const QString relPath = rootDir.relativeFilePath(filePath);
            if (relPath.startsWith(QStringLiteral(".git/")) || relPath.startsWith(QStringLiteral("builddir/")) ||
                relPath.startsWith(QStringLiteral("build/")) || relPath.startsWith(QStringLiteral("build_meson/")) ||
                relPath.startsWith(QStringLiteral("build_cmake/")) || relPath.startsWith(QStringLiteral(".gridlock/")) ||
                relPath.startsWith(QStringLiteral("external/")) || relPath.startsWith(QStringLiteral(".cache/")) ||
                relPath.contains(QStringLiteral("/."))) {
                continue;
            }
            const QFileInfo fi(filePath);
            if (allowedExts.contains(fi.suffix().toLower()) || allowedNames.contains(fi.fileName())) {
                m_cachedWorkspaceFiles.append(relPath);
            }
        }
        m_cachedWorkspaceFiles.sort(Qt::CaseInsensitive);
    }

    QSet<QString> seenFiles;
    QDir rootDir(wsRoot);
    auto addFileEntry = [&](const QString& relPath, const QString& absPath) {
        if (relPath.isEmpty() || seenFiles.contains(relPath)) return;
        seenFiles.insert(relPath);
        const QFileInfo fi(relPath);
        const QString ext = fi.suffix().toLower();
        QString badge = QStringLiteral("File");
        if (ext == QStringLiteral("cpp") || ext == QStringLiteral("cc") || ext == QStringLiteral("c")) badge = QStringLiteral("C/C++");
        else if (ext == QStringLiteral("hpp") || ext == QStringLiteral("h")) badge = QStringLiteral("Header");
        else if (fi.fileName() == QStringLiteral("meson.build") || fi.fileName() == QStringLiteral("CMakeLists.txt")) badge = QStringLiteral("Build");
        catalog.append(PaletteItem{Category::File, relPath, absPath, badge, [mainWindow, absPath]() {
            mainWindow->loadSourceFile(absPath);
            if (auto* sv = mainWindow->sourceCodeView()) sv->setFocus();
        }});
    };

    // Ensure the 7 bundled MPI showcase tutorial files appear first for immediate quick-open discovery.
    const QStringList showcaseFiles = {
        QStringLiteral("tutorial/inspection_demo.cpp"), QStringLiteral("tutorial/deadlock_ring.cpp"),
        QStringLiteral("tutorial/collective_mismatch.cpp"), QStringLiteral("tutorial/ghost_cell_drift.cpp"),
        QStringLiteral("tutorial/heap_imbalance.cpp"), QStringLiteral("tutorial/tag_mismatch_stall.cpp"),
        QStringLiteral("tutorial/simd_struct_watch.cpp")};
    for (const QString& tutRel : showcaseFiles) {
        QString absTut = rootDir.filePath(tutRel);
        if (!QFileInfo::exists(absTut) && QFileInfo::exists(tutRel)) absTut = QFileInfo(tutRel).absoluteFilePath();
        else if (!QFileInfo::exists(absTut) && QFileInfo::exists(QStringLiteral("../") + tutRel))
            absTut = QFileInfo(QStringLiteral("../") + tutRel).absoluteFilePath();
        addFileEntry(tutRel, absTut);
    }
    for (const QString& relPath : m_cachedWorkspaceFiles) addFileEntry(relPath, rootDir.filePath(relPath));

    // 3. Collect Active Document & ELF Binary Symbols (Category::Symbol).
    QSet<QString> seenSymbols;
    if (SourceCodeView* sv = mainWindow->sourceCodeView()) {
        const QString filePath = !sv->currentFile().isEmpty() ? sv->currentFile() : mainWindow->activeTargetSourcePath();
        const QString baseName = QFileInfo(filePath).fileName().isEmpty()
                                     ? QStringLiteral("active_buffer.cpp")
                                     : QFileInfo(filePath).fileName();
        const QStringList lines = sv->toPlainText().split(QLatin1Char('\n'));
        static const QRegularExpression typeRegex(QStringLiteral("^\\s*(struct|class|enum\\s+class|enum)\\s+([A-Za-z_][A-Za-z0-9_]*)"));
        static const QRegularExpression funcRegex(QStringLiteral("^\\s*(?:[A-Za-z_][A-Za-z0-9_:<>,\\s\\*&]*\\s+)?([A-Za-z_][A-Za-z0-9_:]*)\\s*\\([^;]*\\)\\s*(?:const\\s*)?\\{?\\s*$"));
        const QSet<QString> controlKeywords = {
            QStringLiteral("if"), QStringLiteral("for"), QStringLiteral("while"),
            QStringLiteral("switch"), QStringLiteral("catch"), QStringLiteral("return")};

        auto jumpToLineCb = [mainWindow, filePath](int lineOneBased) {
            return [mainWindow, filePath, lineOneBased]() {
                if (!filePath.isEmpty() && QFileInfo::exists(filePath)) mainWindow->loadSourceFile(filePath);
                if (SourceCodeView* view = mainWindow->sourceCodeView()) {
                    QTextCursor tc(view->document()->findBlockByNumber(lineOneBased - 1));
                    view->setTextCursor(tc);
                    view->centerCursor();
                    view->setFocus();
                }
            };
        };

        for (int i = 0; i < lines.size(); ++i) {
            const int lineOneBased = i + 1;
            const QString lineText = lines.at(i);
            if (const auto tm = typeRegex.match(lineText); tm.hasMatch()) {
                const QString kind = tm.captured(1).startsWith(QStringLiteral("class")) ? QStringLiteral("Class") : QStringLiteral("Struct");
                const QString symName = tm.captured(2);
                const QString key = symName + QLatin1Char(':') + QString::number(lineOneBased);
                if (!seenSymbols.contains(key)) {
                    seenSymbols.insert(key);
                    catalog.append(PaletteItem{Category::Symbol, symName,
                                               QStringLiteral("%1:%2").arg(baseName).arg(lineOneBased),
                                               kind, jumpToLineCb(lineOneBased)});
                }
                continue;
            }
            if (const auto fm = funcRegex.match(lineText); fm.hasMatch()) {
                const QString fnName = fm.captured(1);
                if (!controlKeywords.contains(fnName)) {
                    const QString key = fnName + QLatin1Char(':') + QString::number(lineOneBased);
                    if (!seenSymbols.contains(key)) {
                        seenSymbols.insert(key);
                        catalog.append(PaletteItem{Category::Symbol, fnName + QStringLiteral("()"),
                                                   QStringLiteral("%1:%2").arg(baseName).arg(lineOneBased),
                                                   QStringLiteral("Function"), jumpToLineCb(lineOneBased)});
                    }
                }
            }
        }
    }

    if (auto* elf = mainWindow->elfInspector(); elf && elf->isValid()) {
        for (const auto& sym : elf->symbols()) {
            if (sym.name.isEmpty() || sym.address == 0ULL || seenSymbols.contains(sym.name)) continue;
            seenSymbols.insert(sym.name);
            const QString addrStr = QStringLiteral("0x%1").arg(sym.address, 8, 16, QLatin1Char('0'));
            catalog.append(PaletteItem{Category::Symbol, sym.name, QStringLiteral("ELF Symbol @ %1").arg(addrStr),
                                       sym.isFunction ? QStringLiteral("FUNC") : QStringLiteral("OBJ"),
                                       [mainWindow, addrStr]() { mainWindow->seekToAddressOrExpression(addrStr); }});
        }
    }

    if (seenSymbols.isEmpty()) {
        catalog.append(PaletteItem{Category::Symbol, QStringLiteral("main()"),
                                   QStringLiteral("Entry point function"), QStringLiteral("Function"),
                                   [mainWindow]() {
                                       if (SourceCodeView* view = mainWindow->sourceCodeView()) {
                                           const int ln = mainWindow->findSourceLineForStatement(QStringLiteral("main("));
                                           if (ln > 0) {
                                               view->setTextCursor(QTextCursor(view->document()->findBlockByNumber(ln - 1)));
                                               view->centerCursor();
                                           }
                                       }
                                   }});
    }

    // 4. Collect Active MPI Ranks (Category::Rank).
    int rankCount = mainWindow->configuredRankCount();
    if (auto* rack = mainWindow->serverRackView()) rankCount = qMax(rankCount, rack->rankNodeCount());
    rankCount = qBound(1, rankCount, 64);

    const auto& states = mainWindow->latestRankStates();
    for (int r = 0; r < rankCount; ++r) {
        QString stateLabel = QStringLiteral("IDLE"), fnLabel;
        if (auto it = states.find(r); it != states.end()) {
            const QString rawSt = it->second.currentState.toLower();
            if (rawSt == QStringLiteral("stopped") || rawSt == QStringLiteral("paused")) stateLabel = QStringLiteral("PAUSED");
            else if (rawSt == QStringLiteral("running")) stateLabel = QStringLiteral("RUNNING");
            else if (rawSt == QStringLiteral("deadlock")) stateLabel = QStringLiteral("DEADLOCK");
            else if (rawSt == QStringLiteral("waiting")) stateLabel = QStringLiteral("WAITING");
            else if (!rawSt.isEmpty()) stateLabel = rawSt.toUpper();
            fnLabel = !it->second.currentFunction.isEmpty() ? it->second.currentFunction : it->second.registers.value(16);
        }
        const QString subtitle = fnLabel.isEmpty()
                                     ? QStringLiteral("Focus MPI Rank %1 (%2)").arg(r).arg(stateLabel)
                                     : QStringLiteral("Focus MPI Rank %1 (%2 @ %3)").arg(r).arg(stateLabel, fnLabel);
        const QString badge = (r == mainWindow->focusedRank())
                                  ? QStringLiteral("%1 [FOCUSED]").arg(stateLabel)
                                  : stateLabel;
        catalog.append(PaletteItem{Category::Rank, QStringLiteral("Rank %1").arg(r), subtitle, badge,
                                   [mainWindow, r]() {
                                       if (auto* rack = mainWindow->serverRackView()) rack->selectRank(r);
                                       mainWindow->onRankSelected(r);
                                   }});
    }

    // 5. Collect Built-In Workstation Themes (Category::Theme).
    auto& tm = gridlock::core::ThemeManager::instance();
    const QString activeTheme = tm.activeThemeId();
    for (const QString& themeId : tm.availableThemes()) {
        const QString displayName = gridlock::core::ThemeManager::themeDisplayName(themeId);
        const auto pal = gridlock::core::ThemeManager::paletteForTheme(themeId);
        const QString badge = (themeId == activeTheme)
                                  ? QStringLiteral("Active")
                                  : (pal.isDark ? QStringLiteral("Dark") : QStringLiteral("Light"));
        catalog.append(PaletteItem{Category::Theme, QStringLiteral("Theme: %1").arg(displayName),
                                   QStringLiteral("Switch workstation palette to %1").arg(displayName), badge,
                                   [mainWindow, themeId]() {
                                       gridlock::core::ThemeManager::instance().applyTheme(themeId);
                                       mainWindow->propagateThemeToAllWidgets();
                                   }});
    }

    setItems(catalog);
}

}  // namespace gridlock::ui
