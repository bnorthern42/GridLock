/**
 * @file CommandPaletteDialog.cpp
 * @brief Implementation of the JetBrains-style Search Everywhere overlay for GridLock.
 */

#include "CommandPaletteDialog.hpp"

#include <algorithm>
#include <QAbstractButton>
#include <QAction>
#include <QChildEvent>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QListWidgetItem>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QSet>
#include <QShortcut>
#include <QStyledItemDelegate>
#include <QTextBlock>
#include <QTextCursor>
#include <QVBoxLayout>

#include "../../core/managers/ConfigManager.hpp"
#include "../../core/managers/ThemeManager.hpp"
#include "../../core/utils/ElfBinaryInspector.hpp"
#include "../EditorTabManager.hpp"
#include "../MainWindow.hpp"
#include "../views/EditorSyntaxHighlighter.hpp"
#include "../views/ServerRackView.hpp"
#include "../views/SourceCodeView.hpp"
#include "../widgets/ProjectExplorerWidget.hpp"

namespace gridlock::ui {
namespace {

class CommandPaletteItemDelegate : public QStyledItemDelegate {
public:
    explicit CommandPaletteItemDelegate(QObject* parent = nullptr) : QStyledItemDelegate(parent) {}
    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override { return QSize(440, 36); }
    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override {
        if (!p) return;
        p->save(); p->setRenderHint(QPainter::Antialiasing, true);
        const auto pal = gridlock::core::ThemeManager::paletteForTheme(gridlock::core::ThemeManager::instance().activeThemeId());
        const bool isDark = pal.isDark, isSel = (opt.state & QStyle::State_Selected) != 0, isHov = (opt.state & QStyle::State_MouseOver) != 0;
        const QRect rr = opt.rect.adjusted(4, 2, -4, -2);
        const QColor rowBg = isSel ? QColor(isDark ? QStringLiteral("#1e293b") : QStringLiteral("#dbeafe"))
                                   : (isHov ? QColor(isDark ? QStringLiteral("#172030") : QStringLiteral("#eff6ff")) : QColor(Qt::transparent));
        if (rowBg.alpha() > 0) { p->setPen(isSel ? QColor(pal.accentHex) : QColor(Qt::NoPen)); p->setBrush(rowBg); p->drawRoundedRect(rr, 5.0, 5.0); }
        const QString title = idx.data(Qt::UserRole).toString(), subtitle = idx.data(Qt::UserRole + 1).toString(), badge = idx.data(Qt::UserRole + 2).toString();
        QSet<int> matchedSet; for (const QVariant& v : idx.data(Qt::UserRole + 4).toList()) matchedSet.insert(v.toInt());
        QString catTag = QStringLiteral("ACT"); QColor catColor(QStringLiteral("#38bdf8"));
        switch (static_cast<PaletteItem::Category>(idx.data(Qt::UserRole + 3).toInt())) {
            case PaletteItem::Category::Action: catTag = QStringLiteral("ACT");   catColor = QColor(QStringLiteral("#38bdf8")); break;
            case PaletteItem::Category::File:   catTag = QStringLiteral("FILE");  catColor = QColor(QStringLiteral("#34d399")); break;
            case PaletteItem::Category::Symbol: catTag = QStringLiteral("SYM");   catColor = QColor(QStringLiteral("#f59e0b")); break;
            case PaletteItem::Category::Rank:   catTag = QStringLiteral("RANK");  catColor = QColor(QStringLiteral("#a78bfa")); break;
            case PaletteItem::Category::Theme:  catTag = QStringLiteral("THEME"); catColor = QColor(QStringLiteral("#f472b6")); break;
            case PaletteItem::Category::Text:   catTag = QStringLiteral("TEXT");  catColor = QColor(QStringLiteral("#fb923c")); break;
        }
        QFont tagFont = opt.font; tagFont.setPointSize(qMax(8, opt.font.pointSize() - 2)); tagFont.setBold(true); p->setFont(tagFont);
        const QRect tagRect(rr.left() + 8, rr.top() + 7, 44, 18);
        QColor tagFill = catColor; tagFill.setAlpha(isDark ? 38 : 32);
        p->setPen(catColor); p->setBrush(tagFill); p->drawRoundedRect(tagRect, 3.0, 3.0); p->drawText(tagRect, Qt::AlignCenter, catTag);
        int rightLimit = rr.right() - 8;
        if (!badge.isEmpty()) {
            const QFontMetrics bfm(tagFont); const int bw = qMin(150, bfm.horizontalAdvance(badge) + 12);
            const QRect br(rightLimit - bw, rr.top() + 7, bw, 18);
            p->setPen(QColor(isDark ? QStringLiteral("#334155") : QStringLiteral("#cbd5e1")));
            p->setBrush(QColor(isDark ? QStringLiteral("#0f172a") : QStringLiteral("#e2e8f0")));
            p->drawRoundedRect(br, 3.0, 3.0);
            p->setPen(QColor(isDark ? QStringLiteral("#cbd5e1") : QStringLiteral("#334155")));
            p->drawText(br, Qt::AlignCenter, bfm.elidedText(badge, Qt::ElideRight, bw - 6));
            rightLimit = br.left() - 8;
        }
        int cursorX = tagRect.right() + 8;
        const int textTop = rr.top() + 2, textH = rr.height() - 4;
        QFont normalFont = opt.font, boldFont = opt.font; boldFont.setBold(true);
        const QFontMetrics nfm(normalFont), bfm(boldFont);
        const QColor normalFg(isDark ? QStringLiteral("#e2e8f0") : QStringLiteral("#0f172a")), matchFg(isDark ? QStringLiteral("#38bdf8") : QColor(QStringLiteral("#0284c7")));
        for (int i = 0; i < title.size(); ++i) {
            const bool m = matchedSet.contains(i); p->setFont(m ? boldFont : normalFont); p->setPen(m ? matchFg : normalFg);
            const QString ch(title.at(i)); const int cw = m ? bfm.horizontalAdvance(ch) : nfm.horizontalAdvance(ch);
            if (cursorX + cw > rightLimit) break;
            p->drawText(QRect(cursorX, textTop, cw + 2, textH), Qt::AlignLeft | Qt::AlignVCenter, ch); cursorX += cw;
        }
        if (!subtitle.isEmpty() && cursorX + 20 < rightLimit) {
            QFont subFont = opt.font; subFont.setPointSize(qMax(8, opt.font.pointSize() - 1)); p->setFont(subFont); p->setPen(QColor(QStringLiteral("#64748b")));
            const int avail = rightLimit - (cursorX + 10);
            p->drawText(QRect(cursorX + 10, textTop, avail, textH), Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(subFont).elidedText(subtitle, Qt::ElideMiddle, avail));
        }
        p->restore();
    }
};

void openFileInMainWindow(MainWindow* mw, const QString& absPath, int lineOneBased, bool split) {
    if (!mw || absPath.isEmpty()) return;
    if (split && mw->editorTabManager()) {
        auto* etm = mw->editorTabManager(); etm->splitEditor(Qt::Horizontal);
        if (SourceCodeView* sec = etm->secondaryEditorView()) {
            QFile sf(absPath); if (sf.open(QIODevice::ReadOnly | QIODevice::Text)) sec->setSourceCode(QString::fromUtf8(sf.readAll()), lineOneBased);
            sec->setCurrentFile(absPath); if (etm->secondaryTabWidget()) etm->secondaryTabWidget()->setTabText(0, QFileInfo(absPath).fileName());
            sec->setTextCursor(QTextCursor(sec->document()->findBlockByNumber(qMax(0, lineOneBased - 1)))); sec->centerCursor(); sec->setFocus(); return;
        }
    }
    if (mw->editorTabManager() && lineOneBased > 1)
        if (auto* sv = mw->editorTabManager()->navigateToLocation(absPath, lineOneBased, 0)) { sv->setFocus(); return; }
    mw->loadSourceFile(absPath);
    if (SourceCodeView* view = mw->sourceCodeView()) {
        if (lineOneBased > 1) { view->setTextCursor(QTextCursor(view->document()->findBlockByNumber(lineOneBased - 1))); view->centerCursor(); }
        view->setFocus();
    }
}
}  // namespace

int fuzzyScore(const QString& pattern, const QString& candidate, QVector<int>* matchedIndices) { return CommandPaletteDialog::fuzzyScore(pattern, candidate, matchedIndices); }
int CommandPaletteDialog::fuzzyScore(const QString& pattern, const QString& candidate, QVector<int>* matchedIndices) { return gridlock::core::utils::SearchEverywhereEngine::scoreMatch(pattern, candidate, matchedIndices); }

CommandPaletteDialog::CommandPaletteDialog(QWidget* parent)
    : QDialog(parent, Qt::Dialog | Qt::FramelessWindowHint) {
    setObjectName(QStringLiteral("CommandPaletteDialog")); setWindowTitle(QStringLiteral("Search Everywhere"));
    setModal(false); resize(860, 480);
    auto* rootLayout = new QVBoxLayout(this); rootLayout->setContentsMargins(10, 8, 10, 10); rootLayout->setSpacing(6);

    auto* scopeBar = new QHBoxLayout(); scopeBar->setObjectName(QStringLiteral("commandPaletteScopeBar"));
    scopeBar->setContentsMargins(0, 0, 0, 0); scopeBar->setSpacing(4);
    struct TabSpec { Scope scope; const char* label; const char* objName; const char* tip; };
    for (const auto& ts : {TabSpec{Scope::All, "All", "scopeTabAll", "Search Everywhere (Double Shift)"},
                           TabSpec{Scope::Files, "Files", "scopeTabFiles", "Search Files (Ctrl+P)"},
                           TabSpec{Scope::Symbols, "Symbols", "scopeTabSymbols", "Search Symbols (@)"},
                           TabSpec{Scope::Text, "Text", "scopeTabText", "Search Workspace Text (/)"},
                           TabSpec{Scope::Actions, "Actions", "scopeTabActions", "Search Commands & Actions (Ctrl+Shift+P / >)"},
                           TabSpec{Scope::Ranks, "Ranks", "scopeTabRanks", "Search MPI Ranks (#)"}}) {
        auto* btn = new QToolButton(this); btn->setObjectName(QLatin1String(ts.objName)); btn->setProperty("gl_scope_tab", true);
        btn->setText(QLatin1String(ts.label)); btn->setToolTip(QStringLiteral("%1 — press Tab / Shift+Tab to cycle").arg(QLatin1String(ts.tip)));
        btn->setCheckable(true); btn->setFocusPolicy(Qt::NoFocus); btn->setCursor(Qt::PointingHandCursor);
        const Scope sc = ts.scope;
        connect(btn, &QToolButton::clicked, this, [this, sc]() { setActiveScope(sc); });
        m_scopeButtons.append(btn); scopeBar->addWidget(btn);
    }
    scopeBar->addStretch(1);

    m_countLabel = new QLabel(QStringLiteral("0 matches"), this); m_countLabel->setObjectName(QStringLiteral("CommandPaletteCountLabel"));
    m_countLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter); scopeBar->addWidget(m_countLabel);

    m_previewToggleBtn = new QToolButton(this); m_previewToggleBtn->setObjectName(QStringLiteral("commandPalettePreviewToggleBtn"));
    m_previewToggleBtn->setText(QStringLiteral("Preview"));
    m_previewToggleBtn->setToolTip(QStringLiteral("Toggle live source preview pane (Enter: jump, Ctrl+Enter: open in split)"));
    m_previewToggleBtn->setCheckable(true); m_previewToggleBtn->setChecked(true);
    m_previewToggleBtn->setFocusPolicy(Qt::NoFocus); m_previewToggleBtn->setCursor(Qt::PointingHandCursor);
    connect(m_previewToggleBtn, &QToolButton::toggled, this, &CommandPaletteDialog::setPreviewVisible);
    scopeBar->addWidget(m_previewToggleBtn);

    m_closeBtn = new QToolButton(this); m_closeBtn->setObjectName(QStringLiteral("commandPaletteCloseBtn"));
    m_closeBtn->setText(QStringLiteral("ESC")); m_closeBtn->setToolTip(QStringLiteral("Close Command Palette (Esc)"));
    m_closeBtn->setFocusPolicy(Qt::NoFocus); m_closeBtn->setCursor(Qt::PointingHandCursor); m_closeBtn->setFixedHeight(22);
    scopeBar->addWidget(m_closeBtn); rootLayout->addLayout(scopeBar);

    auto* topBarLayout = new QHBoxLayout(); topBarLayout->setContentsMargins(0, 0, 0, 0); topBarLayout->setSpacing(6);
    m_modePillLabel = new QLabel(QStringLiteral("ALL"), this); m_modePillLabel->setObjectName(QStringLiteral("CommandPaletteModePill"));
    m_modePillLabel->setAlignment(Qt::AlignCenter); m_modePillLabel->setFixedHeight(26);
    m_inputEdit = new QLineEdit(this); m_inputEdit->setObjectName(QStringLiteral("CommandPaletteInput"));
    m_inputEdit->setPlaceholderText(QStringLiteral("Search Everywhere (CamelCase e.g. MWC, > actions, @ symbols, / text, # ranks — Tab cycles scopes)..."));
    m_inputEdit->setClearButtonEnabled(true); m_inputEdit->setMinimumHeight(30);
    topBarLayout->addWidget(m_modePillLabel); topBarLayout->addWidget(m_inputEdit, 1); rootLayout->addLayout(topBarLayout);

    m_contentSplitter = new QSplitter(Qt::Horizontal, this); m_contentSplitter->setObjectName(QStringLiteral("commandPaletteSplitter"));
    m_contentSplitter->setChildrenCollapsible(false);

    m_resultsList = new QListWidget(m_contentSplitter); m_resultsList->setObjectName(QStringLiteral("CommandPaletteResults"));
    m_resultsList->setItemDelegate(new CommandPaletteItemDelegate(m_resultsList));
    m_resultsList->setMouseTracking(true); m_resultsList->setUniformItemSizes(true);
    m_resultsList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); m_resultsList->setSelectionMode(QAbstractItemView::SingleSelection);

    m_previewPane = new QWidget(m_contentSplitter); m_previewPane->setObjectName(QStringLiteral("commandPalettePreviewPane"));
    auto* prevLayout = new QVBoxLayout(m_previewPane); prevLayout->setContentsMargins(6, 0, 0, 0); prevLayout->setSpacing(4);
    m_previewHeaderLabel = new QLabel(QStringLiteral("Live Preview"), m_previewPane); m_previewHeaderLabel->setObjectName(QStringLiteral("commandPalettePreviewHeader"));
    m_previewEdit = new QPlainTextEdit(m_previewPane); m_previewEdit->setObjectName(QStringLiteral("commandPalettePreviewEdit"));
    m_previewEdit->setReadOnly(true); m_previewEdit->setLineWrapMode(QPlainTextEdit::NoWrap); m_previewEdit->setFocusPolicy(Qt::NoFocus);
    QFont mono(gridlock::core::ConfigManager::instance().getTerminalSettings().fontFamily, 9); mono.setStyleHint(QFont::Monospace); m_previewEdit->setFont(mono);
    m_previewHighlighter = new EditorSyntaxHighlighter(m_previewEdit->document());
    prevLayout->addWidget(m_previewHeaderLabel); prevLayout->addWidget(m_previewEdit, 1);

    m_contentSplitter->addWidget(m_resultsList); m_contentSplitter->addWidget(m_previewPane);
    m_contentSplitter->setStretchFactor(0, 5); m_contentSplitter->setStretchFactor(1, 5); m_contentSplitter->setSizes({430, 410});
    rootLayout->addWidget(m_contentSplitter, 1);

    auto* escShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escShortcut, &QShortcut::activated, this, &QDialog::reject);
    connect(m_closeBtn, &QToolButton::clicked, this, &QDialog::reject);
    installEventFilter(this);
    for (QWidget* child : findChildren<QWidget*>()) child->installEventFilter(this);

    connect(m_inputEdit, &QLineEdit::textChanged, this, [this]() { refreshFilteredResults(); });
    connect(m_resultsList, &QListWidget::currentRowChanged, this, &CommandPaletteDialog::updatePreviewForRow);
    connect(m_resultsList, &QListWidget::itemActivated, this, [this](QListWidgetItem*) { triggerSelected(false); });
    connect(m_resultsList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) { triggerSelected(false); });
    syncScopeTabButtons(m_activeScope); reloadStyle();
}

QToolButton* CommandPaletteDialog::scopeTabButton(Scope scope) const {
    const int idx = static_cast<int>(scope);
    return (idx >= 0 && idx < m_scopeButtons.size()) ? m_scopeButtons.at(idx) : nullptr;
}
bool CommandPaletteDialog::isPreviewVisible() const { return m_previewPane && !m_previewPane->isHidden(); }
void CommandPaletteDialog::setPreviewVisible(bool visible) {
    if (m_previewPane) m_previewPane->setVisible(visible);
    if (m_previewToggleBtn && m_previewToggleBtn->isChecked() != visible) {
        m_previewToggleBtn->blockSignals(true); m_previewToggleBtn->setChecked(visible); m_previewToggleBtn->blockSignals(false);
    }
    if (visible) updatePreviewForRow(selectedIndex());
}
void CommandPaletteDialog::syncScopeTabButtons(Scope visualScope) {
    for (int i = 0; i < m_scopeButtons.size(); ++i)
        if (QToolButton* b = m_scopeButtons.at(i)) { b->blockSignals(true); b->setChecked(i == static_cast<int>(visualScope)); b->blockSignals(false); }
}
void CommandPaletteDialog::setActiveScope(Scope scope) {
    m_activeScope = scope; syncScopeTabButtons(scope);
    if (m_inputEdit && !m_inputEdit->text().isEmpty()) {
        const QChar c0 = m_inputEdit->text().at(0);
        if (c0 == QLatin1Char('>') || c0 == QLatin1Char('@') || c0 == QLatin1Char('#') || c0 == QLatin1Char('/')) {
            m_inputEdit->blockSignals(true); m_inputEdit->setText(m_inputEdit->text().mid(1).trimmed()); m_inputEdit->blockSignals(false);
        }
    }
    refreshFilteredResults();
}
void CommandPaletteDialog::cycleScope(int delta) {
    int next = (static_cast<int>(m_activeScope) + delta) % 6;
    setActiveScope(static_cast<Scope>(next < 0 ? next + 6 : next));
}

void CommandPaletteDialog::reloadStyle() {
    const auto pal = gridlock::core::ThemeManager::paletteForTheme(gridlock::core::ThemeManager::instance().activeThemeId());
    const QString bg = pal.isDark ? QStringLiteral("#131722") : QStringLiteral("#f8fafc"), inputBg = pal.isDark ? QStringLiteral("#0d1117") : QStringLiteral("#ffffff");
    const QString muted = pal.isDark ? QStringLiteral("#94a3b8") : QStringLiteral("#475569"), capBg = pal.isDark ? QStringLiteral("#0f172a") : QStringLiteral("#e2e8f0");
    const QString capBdr = pal.isDark ? QStringLiteral("#334155") : QStringLiteral("#cbd5e1"), capFg = pal.isDark ? QStringLiteral("#cbd5e1") : QStringLiteral("#334155");
    setStyleSheet(QStringLiteral(
        "QDialog#CommandPaletteDialog { background-color: %1; border: 1px solid %2; border-radius: 8px; }"
        "QLineEdit#CommandPaletteInput { background-color: %3; color: %4; border: 1px solid %5; border-radius: 5px; padding: 4px 8px; font-size: 11pt; }"
        "QLabel#CommandPaletteModePill { background-color: %3; color: %5; border: 1px solid %5; border-radius: 4px; padding: 2px 8px; font-size: 8pt; font-weight: bold; }"
        "QLabel#CommandPaletteCountLabel, QLabel#commandPalettePreviewHeader { color: %6; font-size: 9pt; font-weight: 600; padding: 0 4px; }"
        "QToolButton[gl_scope_tab=\"true\"], QToolButton#commandPalettePreviewToggleBtn { background-color: transparent; color: %6; border: 1px solid transparent; border-radius: 4px; padding: 2px 9px; min-height: 20px; max-height: 22px; font-size: 9pt; font-weight: 600; }"
        "QToolButton[gl_scope_tab=\"true\"]:hover, QToolButton#commandPalettePreviewToggleBtn:hover { color: %4; border-color: %8; }"
        "QToolButton[gl_scope_tab=\"true\"]:checked, QToolButton#commandPalettePreviewToggleBtn:checked { background-color: %3; color: %5; border: 1px solid %5; }"
        "QToolButton#commandPaletteCloseBtn { background-color: %7; color: %9; border: 1px solid %8; border-radius: 4px; padding: 1px 7px; margin: 0px; min-width: 34px; max-width: 46px; min-height: 20px; max-height: 22px; font-size: 8pt; font-weight: bold; }"
        "QToolButton#commandPaletteCloseBtn:hover { border-color: %5; color: %4; }"
        "QListWidget#CommandPaletteResults { background-color: %1; color: %4; border: none; outline: none; }"
        "QPlainTextEdit#commandPalettePreviewEdit { background-color: %3; color: %4; border: 1px solid %2; border-radius: 5px; padding: 4px; }")
                      .arg(bg, pal.borderHex, inputBg, pal.textHex, pal.accentHex, muted, capBg, capBdr, capFg));
    if (m_previewHighlighter) m_previewHighlighter->reloadPalette();
    if (m_resultsList && m_resultsList->viewport()) m_resultsList->viewport()->update();
}

void CommandPaletteDialog::setItems(const QVector<PaletteItem>& items) { m_allItems = items; refreshFilteredResults(); }
void CommandPaletteDialog::addItem(const PaletteItem& item) { m_allItems.append(item); refreshFilteredResults(); }
void CommandPaletteDialog::clearItems() {
    m_allItems.clear(); m_filteredItems.clear(); m_filteredMatchIndices.clear();
    if (m_resultsList) m_resultsList->clear();
    if (m_countLabel) m_countLabel->setText(QStringLiteral("0 matches"));
}
void CommandPaletteDialog::setFilterText(const QString& text) {
    if (!m_inputEdit) return;
    if (m_inputEdit->text() == text) refreshFilteredResults(); else m_inputEdit->setText(text);
}
QString CommandPaletteDialog::filterText() const { return m_inputEdit ? m_inputEdit->text() : QString(); }

void CommandPaletteDialog::openWithScope(Scope scope, const QString& initialQuery) {
    m_activeScope = scope; syncScopeTabButtons(scope); reloadStyle();
    if (m_inputEdit) { m_inputEdit->blockSignals(true); m_inputEdit->setText(initialQuery); m_inputEdit->blockSignals(false); }
    refreshFilteredResults(); positionOverlayNearTopCenter(); show(); raise(); activateWindow();
    if (m_inputEdit) { m_inputEdit->setFocus(Qt::ShortcutFocusReason); m_inputEdit->setCursorPosition(m_inputEdit->text().size()); }
}
void CommandPaletteDialog::openWithPrefix(const QString& initialPrefix) {
    Scope sc = Scope::Files;
    if (initialPrefix.startsWith(QLatin1Char('>'))) sc = Scope::Actions;
    else if (initialPrefix.startsWith(QLatin1Char('@'))) sc = Scope::Symbols;
    else if (initialPrefix.startsWith(QLatin1Char('#')) || initialPrefix.startsWith(QStringLiteral("rank:"), Qt::CaseInsensitive)) sc = Scope::Ranks;
    else if (initialPrefix.startsWith(QLatin1Char('/'))) sc = Scope::Text;
    openWithScope(sc, initialPrefix);
}

int CommandPaletteDialog::resultCount() const { return m_resultsList ? m_resultsList->count() : 0; }
int CommandPaletteDialog::selectedIndex() const { return m_resultsList ? m_resultsList->currentRow() : -1; }
void CommandPaletteDialog::setSelectedIndex(int row) {
    if (m_resultsList && row >= 0 && row < m_resultsList->count()) { m_resultsList->setCurrentRow(row); m_resultsList->scrollToItem(m_resultsList->item(row)); }
}
PaletteItem CommandPaletteDialog::resultItemAt(int row) const { return (row >= 0 && row < m_filteredItems.size()) ? m_filteredItems.at(row) : PaletteItem{}; }
void CommandPaletteDialog::moveSelection(int delta) {
    const int total = resultCount();
    if (total <= 0) return;
    const int next = ((selectedIndex() < 0 ? 0 : selectedIndex()) + delta) % total;
    setSelectedIndex(next < 0 ? next + total : next);
}

bool CommandPaletteDialog::triggerSelected(bool openInSplit) {
    const int row = selectedIndex();
    if (row < 0 || row >= m_filteredItems.size()) return false;
    const PaletteItem item = m_filteredItems.at(row);
    if (item.category == Category::Action) {
        m_recentActions.removeAll(item.title); m_recentActions.prepend(item.title);
        while (m_recentActions.size() > 12) m_recentActions.removeLast();
    } else if (!item.filePath.isEmpty()) {
        m_recentFiles.removeAll(item.filePath); m_recentFiles.prepend(item.filePath);
        while (m_recentFiles.size() > 12) m_recentFiles.removeLast();
    }
    const auto cb = (openInSplit && item.splitCallback) ? item.splitCallback : item.callback;
    accept(); if (cb) cb();
    return true;
}

bool CommandPaletteDialog::eventFilter(QObject* watched, QEvent* event) {
    if (!event) return QDialog::eventFilter(watched, event);
    if (event->type() == QEvent::ChildAdded) {
        if (auto* cw = qobject_cast<QWidget*>(static_cast<QChildEvent*>(event)->child())) cw->installEventFilter(this);
    } else if (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Escape) { ke->accept(); if (isVisible()) reject(); return true; }
        if (ke->key() == Qt::Key_Tab || ke->key() == Qt::Key_Backtab) {
            ke->accept();
            if (event->type() == QEvent::KeyPress) cycleScope((ke->key() == Qt::Key_Backtab || (ke->modifiers() & Qt::ShiftModifier)) ? -1 : +1);
            return true;
        }
        if (event->type() == QEvent::KeyPress &&
            (watched == this || watched == m_inputEdit || watched == m_resultsList || (m_resultsList && watched == m_resultsList->viewport()))) {
            const int key = ke->key();
            const auto mods = ke->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier);
            if (key == Qt::Key_Down || (key == Qt::Key_N && mods == Qt::ControlModifier)) { moveSelection(+1); return true; }
            if (key == Qt::Key_Up || (key == Qt::Key_P && mods == Qt::ControlModifier)) { moveSelection(-1); return true; }
            if (key == Qt::Key_Return || key == Qt::Key_Enter) { triggerSelected((mods & Qt::ControlModifier) != 0); return true; }
        }
    }
    return QDialog::eventFilter(watched, event);
}

void CommandPaletteDialog::keyPressEvent(QKeyEvent* event) {
    if (event && event->key() == Qt::Key_Escape) { reject(); event->accept(); return; }
    QDialog::keyPressEvent(event);
}

void CommandPaletteDialog::positionOverlayNearTopCenter() {
    if (QWidget* p = parentWidget()) {
        const QRect g = p->geometry();
        const int w = qBound(640, (g.width() * 64) / 100, 940), h = qBound(380, (g.height() * 54) / 100, 540);
        resize(w, h); move(p->mapToGlobal(QPoint((g.width() - w) / 2, 40)));
    }
}

void CommandPaletteDialog::updatePreviewForRow(int row) {
    if (!m_previewEdit || !m_previewHeaderLabel) return;
    if (row < 0 || row >= m_filteredItems.size()) {
        m_previewHeaderLabel->setText(QStringLiteral("No Selection")); m_previewEdit->setPlainText(QString()); m_previewEdit->setExtraSelections({}); return;
    }
    const PaletteItem& item = m_filteredItems.at(row);
    int startLn = 1, hiBlock = -1;
    const QString snippet = !item.filePath.isEmpty() ? gridlock::core::utils::SearchEverywhereEngine::loadFilePreviewSnippet(item.filePath, item.lineNumber, 6, &startLn, &hiBlock) : QString();
    if (!snippet.isEmpty()) {
        if (m_previewHighlighter) m_previewHighlighter->setFilePath(item.filePath);
        m_previewHeaderLabel->setText(QStringLiteral("%1:%2  (lines %3..%4)").arg(QFileInfo(item.filePath).fileName()).arg(item.lineNumber).arg(startLn).arg(startLn + snippet.count(QLatin1Char('\n'))));
        m_previewEdit->setPlainText(snippet);
        if (hiBlock >= 0) {
            const auto pal = gridlock::core::ThemeManager::paletteForTheme(gridlock::core::ThemeManager::instance().activeThemeId());
            QTextEdit::ExtraSelection sel; QColor lineBg(pal.accentHex); lineBg.setAlpha(pal.isDark ? 65 : 50);
            sel.format.setBackground(lineBg); sel.format.setProperty(QTextFormat::FullWidthSelection, true);
            sel.cursor = QTextCursor(m_previewEdit->document()->findBlockByNumber(hiBlock));
            m_previewEdit->setExtraSelections({sel}); m_previewEdit->setTextCursor(sel.cursor); m_previewEdit->centerCursor();
        }
        return;
    }
    if (m_previewHighlighter) m_previewHighlighter->setLanguageMode(EditorSyntaxHighlighter::LanguageMode::PlainText);
    m_previewHeaderLabel->setText(item.title); m_previewEdit->setExtraSelections({});
    m_previewEdit->setPlainText(QStringLiteral("%1\n\n%2%3").arg(item.title, item.subtitle, item.badge.isEmpty() ? QString() : QStringLiteral("\nShortcut / Status: %1").arg(item.badge)));
}

void CommandPaletteDialog::refreshFilteredResults() {
    if (!m_resultsList) return;
    QString trimmedLeft = m_inputEdit ? m_inputEdit->text() : QString();
    while (!trimmedLeft.isEmpty() && trimmedLeft.at(0).isSpace()) trimmedLeft.remove(0, 1);
    Scope effectiveScope = m_activeScope; bool forceThemeOnly = false; QString query = trimmedLeft.trimmed();
    if (trimmedLeft.startsWith(QLatin1Char('>'))) { effectiveScope = Scope::Actions; query = trimmedLeft.mid(1).trimmed(); }
    else if (trimmedLeft.startsWith(QLatin1Char('@'))) { effectiveScope = Scope::Symbols; query = trimmedLeft.mid(1).trimmed(); }
    else if (trimmedLeft.startsWith(QLatin1Char('#'))) { effectiveScope = Scope::Ranks; query = trimmedLeft.mid(1).trimmed(); }
    else if (trimmedLeft.startsWith(QStringLiteral("rank:"), Qt::CaseInsensitive)) { effectiveScope = Scope::Ranks; query = trimmedLeft.mid(5).trimmed(); }
    else if (trimmedLeft.startsWith(QLatin1Char('/'))) { effectiveScope = Scope::Text; query = trimmedLeft.mid(1).trimmed(); }
    else if (trimmedLeft.startsWith(QStringLiteral("theme:"), Qt::CaseInsensitive)) { forceThemeOnly = true; query = trimmedLeft.mid(6).trimmed(); }

    syncScopeTabButtons(effectiveScope);
    static const QStringList kPills = {QStringLiteral("ALL"), QStringLiteral("FILES"), QStringLiteral("SYMBOLS (@)"), QStringLiteral("TEXT (/)"), QStringLiteral("ACTIONS (>)"), QStringLiteral("MPI RANKS (#)")};
    if (m_modePillLabel) m_modePillLabel->setText(forceThemeOnly ? QStringLiteral("THEMES") : kPills.value(static_cast<int>(effectiveScope)));

    auto matchesScope = [&](Category cat) -> bool {
        if (forceThemeOnly) return cat == Category::Theme;
        switch (effectiveScope) {
            case Scope::All: return true; case Scope::Files: return cat == Category::File;
            case Scope::Symbols: return cat == Category::Symbol; case Scope::Text: return cat == Category::Text;
            case Scope::Actions: return cat == Category::Action; case Scope::Ranks: return cat == Category::Rank;
        }
        return true;
    };

    QVector<ScoredEntry> scored; scored.reserve(m_allItems.size() + 64);
    for (int idx = 0; idx < m_allItems.size(); ++idx) {
        const PaletteItem& item = m_allItems.at(idx);
        if (!matchesScope(item.category)) continue;
        if (query.isEmpty()) {
            const int base = item.isRecent ? 900 : (item.category == Category::File ? 500 : (item.category == Category::Action ? 400 : 300));
            scored.append(ScoredEntry{idx, base, {}, {}, false}); continue;
        }
        QVector<int> matched; int sc = fuzzyScore(query, item.title, &matched);
        if (sc < 0 && !item.subtitle.isEmpty()) { const int subSc = fuzzyScore(query, item.subtitle, nullptr); if (subSc >= 0) sc = qMax(1, subSc - 20); }
        if (sc >= 0) scored.append(ScoredEntry{idx, sc + (item.isRecent ? 25 : 0), matched, {}, false});
    }

    if (!forceThemeOnly && !m_activeWorkspaceRoot.isEmpty() &&
        ((effectiveScope == Scope::Text && !query.isEmpty()) || (effectiveScope == Scope::All && query.size() >= 2))) {
        MainWindow* mw = m_boundMainWindow;
        for (const auto& h : m_engine.searchWorkspaceText(m_activeWorkspaceRoot, query, (effectiveScope == Scope::Text) ? 80 : 24)) {
            QVector<int> mIdx; const int sc = qMax(60, fuzzyScore(query, h.linePreview, &mIdx));
            const QString abs = h.absPath; const int ln = h.lineNumber;
            PaletteItem ti{Category::Text, h.linePreview, QStringLiteral("%1:%2").arg(h.relPath).arg(ln), QStringLiteral("L%1").arg(ln),
                           [mw, abs, ln]() { openFileInMainWindow(mw, abs, ln, false); }, abs, ln, false, [mw, abs, ln]() { openFileInMainWindow(mw, abs, ln, true); }};
            scored.append(ScoredEntry{static_cast<int>(scored.size()), sc, mIdx, std::move(ti), true});
        }
    }
    std::stable_sort(scored.begin(), scored.end(), [this, &query](const ScoredEntry& a, const ScoredEntry& b) {
        if (a.score != b.score) return a.score > b.score;
        if (!query.isEmpty() && !a.isDynamic && !b.isDynamic) {
            const int lenA = m_allItems.at(a.itemIndex).title.size(), lenB = m_allItems.at(b.itemIndex).title.size();
            if (lenA != lenB) return lenA < lenB;
        }
        return a.itemIndex < b.itemIndex;
    });

    m_filteredItems.clear(); m_filteredMatchIndices.clear(); m_resultsList->clear();
    const int maxVisible = qMin(static_cast<int>(scored.size()), 250);
    for (int i = 0; i < maxVisible; ++i) {
        const ScoredEntry& se = scored.at(i);
        const PaletteItem item = se.isDynamic ? se.dynamicItem : m_allItems.at(se.itemIndex);
        m_filteredItems.append(item); m_filteredMatchIndices.append(se.matchedIndices);
        auto* li = new QListWidgetItem(item.title, m_resultsList);
        li->setToolTip(item.subtitle.isEmpty() ? item.title : item.subtitle);
        li->setData(Qt::UserRole, item.title); li->setData(Qt::UserRole + 1, item.subtitle);
        li->setData(Qt::UserRole + 2, item.isRecent && query.isEmpty() ? QStringLiteral("Recent") : item.badge);
        li->setData(Qt::UserRole + 3, static_cast<int>(item.category));
        QVariantList matchVars; for (int pos : se.matchedIndices) matchVars.append(pos);
        li->setData(Qt::UserRole + 4, matchVars);
    }
    if (m_countLabel) m_countLabel->setText(QStringLiteral("%1 %2").arg(m_filteredItems.size()).arg(m_filteredItems.size() == 1 ? QStringLiteral("match") : QStringLiteral("matches")));
    if (m_resultsList->count() > 0) m_resultsList->setCurrentRow(0); else updatePreviewForRow(-1);
}

void CommandPaletteDialog::populateFromMainWindow(MainWindow* mainWindow) {
    if (!mainWindow) return;
    m_boundMainWindow = mainWindow;
    QVector<PaletteItem> catalog; catalog.reserve(512);
    QSet<QString> recentActSet(m_recentActions.begin(), m_recentActions.end());
    if (recentActSet.isEmpty()) { recentActSet.insert(QStringLiteral("Debug: Run / Launch Target")); recentActSet.insert(QStringLiteral("Debug: Attach to Process...")); }
    QSet<QString> seenActions;
    auto regAct = [&](const QString& title, const QString& sub, const QString& badge, std::function<void()> cb) {
        if (!title.isEmpty() && !seenActions.contains(title)) { seenActions.insert(title); catalog.append(PaletteItem{Category::Action, title, sub, badge, std::move(cb), {}, 1, recentActSet.contains(title), {}}); }
    };
    if (QMenuBar* mb = mainWindow->menuBar()) {
        auto cleanLabel = [](QString s) { return s.remove(QLatin1Char('&')).trimmed(); };
        std::function<void(QMenu*, const QString&)> walk = [&](QMenu* menu, const QString& pfx) {
            if (!menu) return;
            for (QAction* act : menu->actions()) {
                if (!act || act->isSeparator() || !act->isVisible()) continue;
                const QString t = cleanLabel(act->text());
                if (t.isEmpty() || t.startsWith(QStringLiteral("(No "))) continue;
                if (QMenu* sub = act->menu()) { walk(sub, pfx + QStringLiteral(" -> ") + t); continue; }
                const QString tip = act->toolTip().trimmed();
                regAct(QStringLiteral("%1: %2").arg(pfx.section(QStringLiteral(" -> "), -1), t), (!tip.isEmpty() && tip != t) ? tip : (QStringLiteral("Menu -> ") + pfx),
                       act->shortcut().toString(QKeySequence::NativeText), [act]() { if (act) act->trigger(); });
            }
        };
        for (QAction* top : mb->actions()) if (top && top->menu()) walk(top->menu(), cleanLabel(top->menu()->title()));
    }
    const QString sd = mainWindow->isAllRanksExecutionScope() ? QStringLiteral("All Ranks") : QStringLiteral("Rank %1").arg(mainWindow->activeSteppingRank());
    struct ActSpec { QString title; QString sub; QString badge; std::function<void()> cb; };
    for (const auto& a : {ActSpec{QStringLiteral("Debug: Attach to Process..."), QStringLiteral("Attach GDB or LLDB-DAP to running /proc PIDs"), QStringLiteral("Ctrl+Alt+P"), [mainWindow]() { mainWindow->attachToProcessWorkflow(); }},
                          ActSpec{QStringLiteral("Debug: Run / Launch Target"), QStringLiteral("Compile and start configured MPI session"), QStringLiteral("F5"), [mainWindow]() { mainWindow->startTargetWorkflow(); }},
                          ActSpec{QStringLiteral("Debug: Continue Execution"), QStringLiteral("Resume execution on %1").arg(sd), QStringLiteral("F5"), [mainWindow]() { mainWindow->continueExecutionWorkflow(); }},
                          ActSpec{QStringLiteral("Debug: Step Over"), QStringLiteral("Step over source line on %1").arg(sd), QStringLiteral("F10"), [mainWindow]() { mainWindow->stepOverWorkflow(); }},
                          ActSpec{QStringLiteral("Debug: Step Into"), QStringLiteral("Step into called function on %1").arg(sd), QStringLiteral("F11"), [mainWindow]() { mainWindow->stepIntoWorkflow(); }},
                          ActSpec{QStringLiteral("Debug: Step Out"), QStringLiteral("Finish stack frame on %1").arg(sd), QStringLiteral("Shift+F11"), [mainWindow]() { mainWindow->stepOutWorkflow(); }},
                          ActSpec{QStringLiteral("Debug: Step Instruction"), QStringLiteral("Single-step one machine instruction"), QStringLiteral("Alt+S"), [mainWindow]() { mainWindow->stepInstructionWorkflow(); }},
                          ActSpec{QStringLiteral("Debug: Step Over Instruction (nexti)"), QStringLiteral("Step over one machine instruction"), QStringLiteral("Ctrl+Shift+F10"), [mainWindow]() { mainWindow->nextInstructionWorkflow(); }},
                          ActSpec{QStringLiteral("Debug: Run to Cursor"), QStringLiteral("Execute until active source/disassembly line"), QStringLiteral("Ctrl+F10"), [mainWindow]() { mainWindow->runToCursorWorkflow(); }},
                          ActSpec{QStringLiteral("Debug: Pause All Ranks"), QStringLiteral("Interrupt all MPI ranks immediately"), QStringLiteral("F6"), [mainWindow]() { mainWindow->pauseAllRanksWorkflow(); }},
                          ActSpec{QStringLiteral("Debug: Stop Debugging Session"), QStringLiteral("Terminate all active MPI ranks"), QStringLiteral("Shift+F5"), [mainWindow]() { mainWindow->stopTargetWorkflow(); }},
                          ActSpec{QStringLiteral("View: Switch to Source Perspective"), QStringLiteral("Full-width source editor workspace"), QStringLiteral("Alt+1"), [mainWindow]() { mainWindow->setWorkspacePerspective(QStringLiteral("Source")); }},
                          ActSpec{QStringLiteral("View: Switch to Debug Perspective"), QStringLiteral("Source editor + Variables + Call Stack"), QStringLiteral("Alt+2"), [mainWindow]() { mainWindow->setWorkspacePerspective(QStringLiteral("Debug")); }},
                          ActSpec{QStringLiteral("View: Switch to Binary Perspective"), QStringLiteral("CFG disassembly, decompiler, registers, memory"), QStringLiteral("Alt+3"), [mainWindow]() { mainWindow->setWorkspacePerspective(QStringLiteral("Binary")); }},
                          ActSpec{QStringLiteral("View: Switch to MPI Cluster Perspective"), QStringLiteral("MPI deadlock diagnostics, watch grid, server rack"), QStringLiteral("Alt+8"), [mainWindow]() { mainWindow->setWorkspacePerspective(QStringLiteral("MPI")); }},
                          ActSpec{QStringLiteral("Programmer's Calculator"), QStringLiteral("Hex / Dec / Oct / Bin calculator & 64-bit grid"), QStringLiteral("Ctrl+Alt+C"), [mainWindow]() { mainWindow->openProgrammerCalculator(); }},
                          ActSpec{QStringLiteral("Signals & Catchpoints..."), QStringLiteral("Configure POSIX signals and C++/syscall catchpoints"), QStringLiteral("Ctrl+Alt+S"), [mainWindow]() { mainWindow->openSignalsAndCatchpoints(); }},
                          ActSpec{QStringLiteral("Process Checkpoints..."), QStringLiteral("Create/restore copy-on-write fork checkpoints"), QStringLiteral("Ctrl+Alt+K"), [mainWindow]() { mainWindow->openProcessCheckpoints(); }}}) regAct(a.title, a.sub, a.badge, a.cb);

    m_activeWorkspaceRoot = gridlock::core::utils::SearchEverywhereEngine::resolveWorkspaceRoot(
        mainWindow->activeWorkspaceDirectory(), mainWindow->projectExplorerWidget() ? mainWindow->projectExplorerWidget()->rootPath() : QString());
    QSet<QString> recentFileSet(m_recentFiles.begin(), m_recentFiles.end());
    if (auto* etm = mainWindow->editorTabManager()) for (SourceCodeView* v : etm->openViews()) if (v && !v->currentFile().isEmpty()) recentFileSet.insert(QFileInfo(v->currentFile()).absoluteFilePath());
    if (!mainWindow->activeTargetSourcePath().isEmpty()) recentFileSet.insert(QFileInfo(mainWindow->activeTargetSourcePath()).absoluteFilePath());
    for (const QString& rs : gridlock::core::ConfigManager::instance().recentSessions()) if (QFileInfo(rs).isFile()) recentFileSet.insert(QFileInfo(rs).absoluteFilePath());

    for (const auto& f : m_engine.discoverPrioritizedFiles(m_activeWorkspaceRoot)) {
        const QString abs = f.absPath;
        catalog.append(PaletteItem{Category::File, f.relPath, abs, f.badge, [mainWindow, abs]() { openFileInMainWindow(mainWindow, abs, 1, false); },
                                   abs, 1, recentFileSet.contains(QFileInfo(abs).absoluteFilePath()), [mainWindow, abs]() { openFileInMainWindow(mainWindow, abs, 1, true); }});
    }
    SourceCodeView* sv = mainWindow->sourceCodeView();
    const QString activeFile = (sv && !sv->currentFile().isEmpty()) ? sv->currentFile() : mainWindow->activeTargetSourcePath();
    for (const auto& sym : m_engine.collectSymbols(m_activeWorkspaceRoot, activeFile, sv ? sv->toPlainText() : QString())) {
        const QString abs = sym.absPath; const int ln = sym.lineNumber;
        catalog.append(PaletteItem{Category::Symbol, sym.name, QStringLiteral("%1:%2").arg(sym.relPath).arg(ln), sym.kind,
                                   [mainWindow, abs, ln]() { openFileInMainWindow(mainWindow, abs, ln, false); }, abs, ln, false, [mainWindow, abs, ln]() { openFileInMainWindow(mainWindow, abs, ln, true); }});
    }
    if (auto* elf = mainWindow->elfInspector(); elf && elf->isValid()) {
        for (const auto& sym : elf->symbols()) {
            if (sym.name.isEmpty() || sym.address == 0ULL) continue;
            const QString addr = QStringLiteral("0x%1").arg(sym.address, 8, 16, QLatin1Char('0'));
            catalog.append(PaletteItem{Category::Symbol, sym.name, QStringLiteral("ELF Symbol @ %1").arg(addr), sym.isFunction ? QStringLiteral("FUNC") : QStringLiteral("OBJ"), [mainWindow, addr]() { mainWindow->seekToAddressOrExpression(addr); }});
        }
    }
    const int rankCount = qBound(1, qMax(mainWindow->configuredRankCount(), mainWindow->serverRackView() ? mainWindow->serverRackView()->rankNodeCount() : 1), 64);
    const auto& states = mainWindow->latestRankStates();
    for (int r = 0; r < rankCount; ++r) {
        QString st = QStringLiteral("IDLE");
        if (auto it = states.find(r); it != states.end() && !it->second.currentState.isEmpty()) st = it->second.currentState.toUpper();
        catalog.append(PaletteItem{Category::Rank, QStringLiteral("Rank %1").arg(r), QStringLiteral("Focus MPI Rank %1 (%2)").arg(r).arg(st),
                                   (r == mainWindow->focusedRank()) ? QStringLiteral("%1 [FOCUSED]").arg(st) : st,
                                   [mainWindow, r]() { if (auto* rack = mainWindow->serverRackView()) rack->selectRank(r); mainWindow->onRankSelected(r); }});
    }
    auto& tm = gridlock::core::ThemeManager::instance();
    for (const QString& tid : tm.availableThemes()) {
        const QString dn = gridlock::core::ThemeManager::themeDisplayName(tid); const auto pal = gridlock::core::ThemeManager::paletteForTheme(tid);
        catalog.append(PaletteItem{Category::Theme, QStringLiteral("Theme: %1").arg(dn), QStringLiteral("Switch workstation palette to %1").arg(dn),
                                   (tid == tm.activeThemeId()) ? QStringLiteral("Active") : (pal.isDark ? QStringLiteral("Dark") : QStringLiteral("Light")),
                                   [mainWindow, tid]() { gridlock::core::ThemeManager::instance().applyTheme(tid); mainWindow->propagateThemeToAllWidgets(); }});
    }
    setItems(catalog);
}
}  // namespace gridlock::ui
