/**
 * @file SourceCodeView.cpp
 * @brief Core SourceCodeView editor initialization, styling, breakpoint model, and gutter painting.
 *
 * Implements SourceCodeView construction, clangd diagnostic/completion signal hooks,
 * large-file/plain-data fast paths, theme-aware reloadStyle(), per-rank and conditional
 * breakpoint state management, extra-selection line highlighting, and two-lane
 * LineNumberArea vector badge and Git diff gutter rendering.
 */

#include "SourceCodeView.hpp"

#include <QtWidgets>

#include "../../core/managers/ConfigManager.hpp"
#include "../../core/utils/ClangdLspClient.hpp"
#include "../dialogs/debugger/ConditionalBreakpointDialog.hpp"
#include "../widgets/InlineFindBar.hpp"

namespace gridlock::ui {

namespace {
bool isAppDarkThemeActive() {
    return (qApp && qApp->property("gridlock_dark_mode").isValid()) ? qApp->property("gridlock_dark_mode").toBool() : true;
}

QStringList activeAppSwatchColors() {
    if (qApp && qApp->property("gridlock_swatch_colors").isValid()) {
        const QStringList sw = qApp->property("gridlock_swatch_colors").toStringList();
        if (sw.size() >= 5) return sw;
    }
    return isAppDarkThemeActive()
               ? QStringList{QStringLiteral("#16181d"), QStringLiteral("#21252b"), QStringLiteral("#2c313a"), QStringLiteral("#00a2ff"), QStringLiteral("#d7dae0")}
               : QStringList{QStringLiteral("#f5f6f8"), QStringLiteral("#e6e9ef"), QStringLiteral("#ccd0da"), QStringLiteral("#1e66f5"), QStringLiteral("#1f2328")};
}
}  // namespace

SourceCodeView::SourceCodeView(QWidget* parent) : QPlainTextEdit(parent) {
    setFrameShape(QFrame::NoFrame);
    m_lineNumberArea = new LineNumberArea(this);
    connect(document(), &QTextDocument::blockCountChanged, this, &SourceCodeView::updateLineNumberAreaWidth);
    connect(this, &SourceCodeView::updateRequest, this, &SourceCodeView::updateLineNumberArea);
    updateLineNumberAreaWidth(0);
    setReadOnly(false); setTextInteractionFlags(Qt::TextEditorInteraction); setLineWrapMode(QPlainTextEdit::NoWrap);
    m_highlighter = new EditorSyntaxHighlighter(document());
    m_findBar = new InlineFindBar(this, true);
    connect(m_findBar, &InlineFindBar::matchesUpdated, this, qOverload<>(&SourceCodeView::highlightCurrentLine));
    reloadStyle();
    viewport()->setMouseTracking(true);
    m_hoverTimer = new QTimer(this); m_hoverTimer->setSingleShot(true);
    connect(m_hoverTimer, &QTimer::timeout, this, &SourceCodeView::handleHoverTimeout);
    m_lspSyncTimer = new QTimer(this); m_lspSyncTimer->setSingleShot(true);
    connect(m_lspSyncTimer, &QTimer::timeout, this, &SourceCodeView::syncLspDocument);
    m_gitDiffTimer = new QTimer(this); m_gitDiffTimer->setSingleShot(true);
    connect(m_gitDiffTimer, &QTimer::timeout, this, &SourceCodeView::refreshGitDiffGutter);
    m_completionTimer = new QTimer(this); m_completionTimer->setSingleShot(true);
    connect(m_completionTimer, &QTimer::timeout, this, [this]() {
        const QTextCursor cur = textCursor(); triggerCompletionPopupAtCursor(cur.block().text().left(cur.positionInBlock()), cur.blockNumber() + 1);
    });
    connect(document(), &QTextDocument::contentsChanged, this, [this]() {
        if (!m_currentFilePath.isEmpty()) { m_lspSyncTimer->start(250); m_gitDiffTimer->start(350); }
    });
    connect(&core::ClangdLspClient::instance(), &core::ClangdLspClient::diagnosticsPublished,
            this, [this](const QString& filePath, const QVector<core::LspDiagnostic>& diags) {
                if (m_currentFilePath.isEmpty()) return;
                const QString curCanon = QFileInfo::exists(m_currentFilePath) ? QFileInfo(m_currentFilePath).canonicalFilePath() : QFileInfo(m_currentFilePath).absoluteFilePath();
                const QString msgCanon = QFileInfo::exists(filePath) ? QFileInfo(filePath).canonicalFilePath() : QFileInfo(filePath).absoluteFilePath();
                if (!curCanon.isEmpty() && curCanon == msgCanon) setDiagnostics(diags);
            });
    connect(&core::ClangdLspClient::instance(), &core::ClangdLspClient::completionReady,
            this, [this](quint64 seq, const QString& filePath, const QVector<core::ClangdLspClient::LspCompletionItem>& items) {
                if (seq != m_latestCompletionSeq || m_currentFilePath.isEmpty() || items.isEmpty()) return;
                const QString curCanon = QFileInfo::exists(m_currentFilePath) ? QFileInfo(m_currentFilePath).canonicalFilePath() : QFileInfo(m_currentFilePath).absoluteFilePath();
                const QString msgCanon = QFileInfo::exists(filePath) ? QFileInfo(filePath).canonicalFilePath() : QFileInfo(filePath).absoluteFilePath();
                if (curCanon != msgCanon) return;
                ensureCompletionPopup(); m_completionPopup->setItems(items);
                m_completionPopup->positionBelow(mapToGlobal(cursorRect().bottomLeft() + QPoint(0, 4)), 320); m_completionPopup->show();
            });
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, qOverload<>(&SourceCodeView::highlightCurrentLine));
    connect(document(), &QTextDocument::modificationChanged, this, &SourceCodeView::modificationStateChanged);
}

void SourceCodeView::openFindBar() { if (m_findBar) m_findBar->openAndFocus(); }
void SourceCodeView::closeFindBar() { if (m_findBar) m_findBar->closeBar(); }

bool SourceCodeView::isPlainDataFile(const QString& filePath) {
    const QString ext = QFileInfo(filePath).suffix().toLower();
    return ext == QStringLiteral("csv") || ext == QStringLiteral("tsv") || ext == QStringLiteral("dat") || ext == QStringLiteral("log");
}

void SourceCodeView::setLargeFileMode(bool enabled) {
    m_largeFileMode = enabled;
    if (enabled) { if (m_highlighter) m_highlighter->setDocument(nullptr); setLineWrapMode(QPlainTextEdit::NoWrap); }
    else if (m_highlighter && !m_highlighter->document()) m_highlighter->setDocument(document());
}

void SourceCodeView::setCurrentFile(const QString& filePath) {
    const QFileInfo fi(filePath);
    m_currentFilePath = (!filePath.isEmpty() && fi.exists() && !fi.canonicalFilePath().isEmpty()) ? fi.canonicalFilePath() : filePath;
    const bool plainData = isPlainDataFile(m_currentFilePath);
    const qint64 limit = plainData ? (256 * 1024) : kLargeFileThresholdBytes;
    if (fi.exists() && fi.size() >= limit) setLargeFileMode(true);
    if (m_largeFileMode) return;
    if (m_highlighter) m_highlighter->setFilePath(m_currentFilePath);
    if (!plainData && !m_currentFilePath.isEmpty()) {
        setDiagnostics(core::ClangdLspClient::instance().cachedDiagnosticsForFile(m_currentFilePath));
        if (!toPlainText().trimmed().isEmpty()) syncLspDocument();
    }
    if (!plainData) refreshGitDiffGutter();
}

void SourceCodeView::syncLspDocument() {
    if (!m_largeFileMode && !m_currentFilePath.isEmpty() && !isPlainDataFile(m_currentFilePath)) {
        core::ClangdLspClient::instance().syncDocument(m_currentFilePath, toPlainText());
    }
}

int SourceCodeView::lineNumberAreaWidth() const {
    int digits = 3, maxBlocks = qMax(1, blockCount());
    while (maxBlocks >= 1000) { maxBlocks /= 10; ++digits; }
    return qMax(48, gutterIconLaneWidth() + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits + 18);
}

QColor SourceCodeView::colorForRankBreakpoint(int rankFilter) {
    if (rankFilter < 0) return QColor(QStringLiteral("#ef4444"));
    static const QColor kPalette[8] = {QColor(QStringLiteral("#06b6d4")), QColor(QStringLiteral("#f59e0b")), QColor(QStringLiteral("#10b981")), QColor(QStringLiteral("#a855f7")), QColor(QStringLiteral("#ec4899")), QColor(QStringLiteral("#84cc16")), QColor(QStringLiteral("#3b82f6")), QColor(QStringLiteral("#f97316"))};
    return (rankFilter < 8) ? kPalette[rankFilter] : QColor::fromHsl((45 + rankFilter * 137) % 330 + 20, 215, 135);
}

void SourceCodeView::setBreakpoints(const QSet<int>& bps) {
    const auto prevInfo = m_breakpointInfo; m_breakpoints.clear(); m_breakpointSuspendScopes.clear(); m_breakpointInfo.clear();
    for (int bp : bps) {
        const BreakpointVisualInfo info = prevInfo.value(bp, BreakpointVisualInfo{-1, QString(), 0, QString()});
        m_breakpointInfo[bp] = info; m_breakpoints[bp] = info.condition;
        m_breakpointSuspendScopes[bp] = (info.rankFilter >= 0) ? QStringLiteral("Rank") : QStringLiteral("All");
    }
    highlightCurrentLine(); if (m_lineNumberArea) m_lineNumberArea->update();
}

void SourceCodeView::syncBreakpointMetadata(const QMap<int, BreakpointVisualInfo>& metaByLine) {
    m_breakpoints.clear(); m_breakpointSuspendScopes.clear(); m_breakpointInfo.clear();
    for (auto it = metaByLine.constBegin(); it != metaByLine.constEnd(); ++it) {
        if (it.key() <= 0) continue;
        BreakpointVisualInfo clean{it.value().rankFilter < 0 ? -1 : it.value().rankFilter, it.value().condition.trimmed(), qMax(0, it.value().ignoreCount), it.value().logMessage.trimmed(), it.value().enabled, it.value().temporary, it.value().commands};
        m_breakpointInfo[it.key()] = clean; m_breakpoints[it.key()] = clean.condition;
        m_breakpointSuspendScopes[it.key()] = (clean.rankFilter >= 0) ? QStringLiteral("Rank") : QStringLiteral("All");
    }
    highlightCurrentLine(); if (m_lineNumberArea) m_lineNumberArea->update();
}

QSet<int> SourceCodeView::breakpoints() const {
    QSet<int> result; for (auto it = m_breakpoints.constBegin(); it != m_breakpoints.constEnd(); ++it) result.insert(it.key()); return result;
}
bool SourceCodeView::hasBreakpoint(int lineOneBased) const { return m_breakpoints.contains(lineOneBased); }
QColor SourceCodeView::activeCanvasBackgroundColor() const {
    return QColor(activeAppSwatchColors().value(0, isAppDarkThemeActive() ? QStringLiteral("#16181d") : QStringLiteral("#f5f6f8")));
}

/** @brief Reloads font size, font family, word wrap, tab width, and active theme surface from user configuration. */
void SourceCodeView::reloadStyle() {
    QSettings s("gridlock", "debugger");
    const int codeFontSize = s.value("appearance/code_font_size", 11).toInt();
    const QString codeFontFamily = s.value("appearance/code_font_family", QStringLiteral("JetBrains Mono")).toString();
    m_tabWidth = qBound(1, s.value("editing/tab_width", 4).toInt(), 16);
    setLineWrapMode((!m_largeFileMode && s.value("editing/word_wrap", false).toBool()) ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
    QFont codeFont = font();
    if (!codeFontFamily.trimmed().isEmpty()) codeFont.setFamily(codeFontFamily.trimmed());
    codeFont.setStyleHint(QFont::Monospace); codeFont.setPointSize(codeFontSize); setFont(codeFont);
    const bool isDark = isAppDarkThemeActive();
    const QStringList sw = activeAppSwatchColors();
    const QString canvasHex = sw.value(0, isDark ? QStringLiteral("#16181d") : QStringLiteral("#f5f6f8"));
    const QString textHex = sw.value(4, isDark ? QStringLiteral("#d7dae0") : QStringLiteral("#1f2328"));
    this->setStyleSheet(QStringLiteral(
        "QPlainTextEdit, QPlainTextEdit QWidget { background-color: %3; color: %4; border: none; "
        "font-family: '%2', 'JetBrains Mono', 'Fira Code', 'Monospace', monospace; font-size: %1pt; }"
    ).arg(codeFontSize).arg(codeFontFamily.isEmpty() ? QStringLiteral("JetBrains Mono") : codeFontFamily).arg(canvasHex, textHex));
    if (viewport()) {
        QPalette pal = viewport()->palette();
        pal.setColor(QPalette::Base, QColor(canvasHex)); pal.setColor(QPalette::Window, QColor(canvasHex));
        pal.setColor(QPalette::Text, QColor(textHex)); viewport()->setPalette(pal); viewport()->setAutoFillBackground(true);
    }
    updateTabStopDistance();
    if (m_highlighter && !m_largeFileMode) m_highlighter->reloadPalette();
    highlightCurrentLine(); updateLineNumberAreaWidth(0); if (m_lineNumberArea) m_lineNumberArea->update();
}

void SourceCodeView::setTabWidth(int spaces) {
    m_tabWidth = qBound(1, spaces, 16);
    updateTabStopDistance();
}

void SourceCodeView::updateTabStopDistance() {
    const qreal spacePx = QFontMetricsF(font()).horizontalAdvance(QLatin1Char(' '));
    if (spacePx > 0.0) setTabStopDistance(spacePx * static_cast<qreal>(m_tabWidth));
}

void SourceCodeView::changeEvent(QEvent* event) {
    QPlainTextEdit::changeEvent(event);
    if (event && (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange)) {
        updateTabStopDistance();
    }
}

void SourceCodeView::setLanguageMode(LanguageMode mode) { if (m_highlighter) m_highlighter->setLanguageMode(mode); }
SourceCodeView::LanguageMode SourceCodeView::languageMode() const { return m_highlighter ? m_highlighter->languageMode() : LanguageMode::PlainText; }
void SourceCodeView::setRainbowBracketsEnabled(bool enabled) { if (m_highlighter) m_highlighter->setRainbowBracketsEnabled(enabled); }
bool SourceCodeView::rainbowBracketsEnabled() const { return m_highlighter ? m_highlighter->rainbowBracketsEnabled() : false; }
void SourceCodeView::setDataDocHighlightingEnabled(bool enabled) { if (m_highlighter) m_highlighter->setDataDocHighlightingEnabled(enabled); }
bool SourceCodeView::dataDocHighlightingEnabled() const { return m_highlighter ? m_highlighter->dataDocHighlightingEnabled() : false; }
void SourceCodeView::rehighlightDocument() { if (m_highlighter) m_highlighter->reloadPalette(); if (viewport()) viewport()->update(); }

/** @brief Toggles single-line '//' comments on the active line in the document. */
void SourceCodeView::toggleLineComment() {
    QTextCursor tc = textCursor(); QTextBlock block = tc.block();
    if (!block.isValid()) return;
    const QString lineText = block.text();
    int firstNonSpace = 0;
    while (firstNonSpace < lineText.size() && lineText.at(firstNonSpace).isSpace()) ++firstNonSpace;

    tc.beginEditBlock();
    QTextCursor editCursor(block);
    editCursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, firstNonSpace);
    if (lineText.mid(firstNonSpace).startsWith(QStringLiteral("// "))) {
        editCursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 3); editCursor.removeSelectedText();
    } else if (lineText.mid(firstNonSpace).startsWith(QStringLiteral("//"))) {
        editCursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 2); editCursor.removeSelectedText();
    } else {
        editCursor.insertText(QStringLiteral("// "));
    }
    tc.endEditBlock();
}

int SourceCodeView::getCurrentLineNumber() const { return textCursor().blockNumber() + 1; }

/** @brief Toggles a breakpoint on the specified 1-based line. */
void SourceCodeView::toggleBreakpoint(int lineNum) {
    if (lineNum <= 0) return;
    const bool isSet = !m_breakpoints.contains(lineNum);
    const int prevRank = breakpointRankFilterForLine(lineNum);
    if (isSet) {
        m_breakpoints.insert(lineNum, QString()); m_breakpointSuspendScopes.insert(lineNum, QStringLiteral("All"));
        m_breakpointInfo.insert(lineNum, BreakpointVisualInfo{-1, QString(), 0, QString()});
    } else {
        m_breakpoints.remove(lineNum); m_breakpointSuspendScopes.remove(lineNum); m_breakpointInfo.remove(lineNum);
    }
    highlightCurrentLine();
    if (m_lineNumberArea) { if (m_hoveredGutterLine == lineNum) m_lineNumberArea->setToolTip(breakpointTooltipForLine(lineNum)); m_lineNumberArea->update(); }
    emit rankBreakpointChanged(m_currentFilePath, lineNum, isSet, isSet ? -1 : prevRank, QString());
    emit breakpointToggled(m_currentFilePath, lineNum, isSet, QString());
}

/** @brief Sets or updates a breakpoint with an explicit rank filter (-1 = All Ranks) and condition. */
void SourceCodeView::setRankBreakpoint(int lineOneBased, int rankFilter, const QString& condition) {
    if (lineOneBased <= 0) return;
    const int normalizedRank = (rankFilter < 0) ? -1 : rankFilter; const QString cleanCond = condition.trimmed();
    const BreakpointVisualInfo prev = m_breakpointInfo.value(lineOneBased);
    m_breakpointInfo.insert(lineOneBased, BreakpointVisualInfo{normalizedRank, cleanCond, prev.ignoreCount, prev.logMessage, prev.enabled, prev.temporary, prev.commands});
    m_breakpoints.insert(lineOneBased, cleanCond); m_breakpointSuspendScopes.insert(lineOneBased, (normalizedRank >= 0) ? QStringLiteral("Rank") : QStringLiteral("All"));
    highlightCurrentLine(); if (m_lineNumberArea) { if (m_hoveredGutterLine == lineOneBased) m_lineNumberArea->setToolTip(breakpointTooltipForLine(lineOneBased)); m_lineNumberArea->update(); }
    emit rankBreakpointChanged(m_currentFilePath, lineOneBased, true, normalizedRank, cleanCond);
    emit breakpointToggled(m_currentFilePath, lineOneBased, true, cleanCond);
}

void SourceCodeView::setConditionalBreakpoint(int lineOneBased, const QString& condition) {
    if (lineOneBased > 0) setRankBreakpoint(lineOneBased, breakpointRankFilterForLine(lineOneBased), condition);
}

void SourceCodeView::setBreakpointOptions(int lineOneBased, const QString& condition, int ignoreCount, const QString& logMessage) {
    if (lineOneBased <= 0) return;
    const int rf = breakpointRankFilterForLine(lineOneBased); const QString cleanCond = condition.trimmed(), cleanLog = logMessage.trimmed();
    const bool needInsert = !hasBreakpoint(lineOneBased) || (m_breakpoints.value(lineOneBased) != cleanCond);
    const BreakpointVisualInfo prev = m_breakpointInfo.value(lineOneBased);
    m_breakpointInfo.insert(lineOneBased, BreakpointVisualInfo{rf, cleanCond, qMax(0, ignoreCount), cleanLog, prev.enabled, prev.temporary, prev.commands});
    m_breakpoints.insert(lineOneBased, cleanCond);
    if (needInsert) setRankBreakpoint(lineOneBased, rf, cleanCond);
    else { highlightCurrentLine(); if (m_lineNumberArea) { if (m_hoveredGutterLine == lineOneBased) m_lineNumberArea->setToolTip(breakpointTooltipForLine(lineOneBased)); m_lineNumberArea->update(); } }
    emit breakpointOptionsChanged(m_currentFilePath, lineOneBased, qMax(0, ignoreCount), cleanLog);
}

bool SourceCodeView::isLogpointForLine(int lineOneBased) const { return hasBreakpoint(lineOneBased) && !m_breakpointInfo.value(lineOneBased).logMessage.trimmed().isEmpty(); }
int SourceCodeView::breakpointIgnoreCountForLine(int lineOneBased) const { return hasBreakpoint(lineOneBased) ? qMax(0, m_breakpointInfo.value(lineOneBased).ignoreCount) : 0; }
QString SourceCodeView::breakpointLogMessageForLine(int lineOneBased) const { return hasBreakpoint(lineOneBased) ? m_breakpointInfo.value(lineOneBased).logMessage.trimmed() : QString(); }
SourceCodeView::BreakpointVisualInfo SourceCodeView::breakpointInfoForLine(int lineOneBased) const { return m_breakpointInfo.value(lineOneBased, BreakpointVisualInfo{-1, m_breakpoints.value(lineOneBased).trimmed(), 0, QString()}); }
QColor SourceCodeView::breakpointColorForLine(int lineOneBased) const { return colorForRankBreakpoint(breakpointRankFilterForLine(lineOneBased)); }
bool SourceCodeView::breakpointHasWhiteOutlineForLine(int lineOneBased) const { return hasBreakpoint(lineOneBased) && breakpointRankFilterForLine(lineOneBased) < 0; }
int SourceCodeView::breakpointRankFilterForLine(int lineOneBased) const { return m_breakpointInfo.contains(lineOneBased) ? m_breakpointInfo.value(lineOneBased).rankFilter : -1; }

QString SourceCodeView::breakpointTooltipForLine(int lineOneBased) const {
    if (!hasBreakpoint(lineOneBased)) return QString();
    const BreakpointVisualInfo info = breakpointInfoForLine(lineOneBased);
    QString tip = QStringLiteral("%1Breakpoint at line %2 (%3)").arg(!info.enabled ? QStringLiteral("[DISABLED] ") : info.temporary ? QStringLiteral("[Temporary tbreak] ") : QString()).arg(lineOneBased).arg((info.rankFilter < 0) ? QStringLiteral("All Ranks") : QStringLiteral("Rank %1 only").arg(info.rankFilter));
    if (!info.condition.trimmed().isEmpty()) tip += QStringLiteral("\nCondition: %1").arg(info.condition.trimmed());
    if (info.ignoreCount > 0) tip += QStringLiteral("\nIgnore first %1 hits").arg(info.ignoreCount);
    if (!info.logMessage.trimmed().isEmpty()) tip += QStringLiteral("\nLogpoint: %1").arg(info.logMessage.trimmed());
    if (!info.commands.isEmpty()) tip += QStringLiteral("\nGDB Commands: %1 line(s)").arg(info.commands.size());
    return tip;
}

void SourceCodeView::setActiveFocusRank(int rankId) { m_activeFocusRank = qMax(0, rankId); }
QString SourceCodeView::breakpointCondition(int lineOneBased) const { return m_breakpoints.value(lineOneBased).trimmed(); }
QString SourceCodeView::breakpointSuspendScope(int lineOneBased) const { return m_breakpointSuspendScopes.value(lineOneBased, QStringLiteral("All")); }

void SourceCodeView::setHoveredGutterLine(int lineOneBased) {
    const int normalized = (lineOneBased > 0) ? lineOneBased : -1;
    if (m_hoveredGutterLine == normalized) return;
    m_hoveredGutterLine = normalized;
    if (m_lineNumberArea) {
        m_lineNumberArea->setCursor(normalized > 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        m_lineNumberArea->setToolTip(normalized > 0 ? breakpointTooltipForLine(normalized) : QString());
        m_lineNumberArea->update();
    }
}

QString SourceCodeView::gutterMarkerStateForLine(int lineOneBased) const {
    const bool hasBp = m_breakpoints.contains(lineOneBased);
    const bool isExec = (lineOneBased > 0 && lineOneBased == m_executionLine);
    if (hasBp && isExec) return QStringLiteral("BreakpointAndExecutionPc");
    if (hasBp) {
        if (!isBreakpointEnabledForLine(lineOneBased)) return QStringLiteral("DisabledBreakpoint");
        if (isTemporaryBreakpointForLine(lineOneBased)) return QStringLiteral("TemporaryBreakpoint");
        return m_breakpoints.value(lineOneBased).trimmed().isEmpty() ? QStringLiteral("Breakpoint") : QStringLiteral("ConditionalBreakpoint");
    }
    if (isExec) return QStringLiteral("ExecutionPc");
    if (lineOneBased > 0 && lineOneBased == m_hoveredGutterLine) return QStringLiteral("HoverPreview");
    return QStringLiteral("None");
}

/** @brief Highlights the current cursor line, per-rank breakpoint tint bands, and execution PC line. */
void SourceCodeView::highlightCurrentLine() {
    QList<QTextEdit::ExtraSelection> selections; const bool isDark = isAppDarkThemeActive();
    auto addFullSel = [&](const QTextCursor& c, const QColor& bg) {
        QTextEdit::ExtraSelection s; s.format.setBackground(bg); s.format.setProperty(QTextFormat::FullWidthSelection, true);
        s.cursor = c; s.cursor.clearSelection(); selections.append(s);
    };
    addFullSel(textCursor(), isDark ? QColor("#1e222a") : activeCanvasBackgroundColor().darker(104));
    for (auto it = m_breakpoints.constBegin(); it != m_breakpoints.constEnd(); ++it) {
        const int bpLine = it.key(); if (bpLine <= 0 || bpLine == m_executionLine) continue;
        if (QTextBlock b = document()->findBlockByLineNumber(bpLine - 1); b.isValid()) {
            QColor tint = breakpointColorForLine(bpLine); tint.setAlpha(isBreakpointEnabledForLine(bpLine) ? 28 : 10);
            addFullSel(QTextCursor(b), tint);
        }
    }
    if (m_executionLine > 0) if (QTextBlock eb = document()->findBlockByLineNumber(m_executionLine - 1); eb.isValid())
        addFullSel(QTextCursor(eb), isDark ? QColor("#20342c") : QColor(46, 160, 67, 45));
    for (const auto& diag : m_diagnostics) {
        QTextBlock dBlock = document()->findBlockByLineNumber(qMax(0, diag.line - 1)); if (!dBlock.isValid()) continue;
        QTextEdit::ExtraSelection dSel; dSel.format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
        dSel.format.setUnderlineColor(diag.severity == 1 ? QColor(QStringLiteral("#f7768e")) : QColor(QStringLiteral("#e0af68")));
        dSel.format.setToolTip(diag.message); dSel.cursor = QTextCursor(dBlock);
        const int sCol = qBound(0, diag.column, qMax(0, dBlock.length() - 1));
        const int eCol = (diag.endColumn > sCol) ? qMin(diag.endColumn, qMax(0, dBlock.length() - 1)) : qMax(0, dBlock.length() - 1);
        dSel.cursor.setPosition(dBlock.position() + sCol); dSel.cursor.setPosition(dBlock.position() + eCol, QTextCursor::KeepAnchor);
        selections.append(dSel);
    }
    if (m_findBar && m_findBar->isVisible()) selections.append(m_findBar->matchSelections());
    setExtraSelections(selections);
}

void SourceCodeView::setDiagnostics(const QVector<core::utils::LspDiagnostic>& diagnostics) {
    m_diagnostics = diagnostics; highlightCurrentLine(); if (m_lineNumberArea) m_lineNumberArea->update();
}

/** @brief Paints the two-lane gutter with hover previews, vector badges, line numbers, and Git diff bars. */
void SourceCodeView::lineNumberAreaPaintEvent(QPaintEvent* event) {
    QPainter painter(m_lineNumberArea);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const int gutterW = m_lineNumberArea->width();
    const int iconLaneW = gutterIconLaneWidth();
    const QFont baseGutterFont = painter.font();

    const bool isDark = isAppDarkThemeActive();
    const QStringList sw = activeAppSwatchColors();
    const QColor gutterBg(sw.value(0, isDark ? QStringLiteral("#16181d") : QStringLiteral("#f5f6f8")));
    const QColor gutterBorder(sw.value(2, isDark ? QStringLiteral("#252932") : QStringLiteral("#d0d4dc")));
    painter.fillRect(event->rect(), gutterBg);
    painter.setPen(gutterBorder);
    painter.drawLine(gutterW - 1, event->rect().top(), gutterW - 1, event->rect().bottom());

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            const int lineOneBased = blockNumber + 1;
            const int rowH = qMax(14, bottom - top);
            const qreal cx = iconLaneW * 0.5, cy = top + rowH * 0.5;
            const bool isHovered = (lineOneBased == m_hoveredGutterLine);
            const QString state = gutterMarkerStateForLine(lineOneBased);

            // Reset painter brush/pen explicitly at the top of each row iteration.
            painter.setBrush(Qt::NoBrush); painter.setPen(Qt::NoPen);

            const bool isDragTarget = m_draggingExecutionPc && (lineOneBased == m_dragTargetLine);
            if (isDragTarget) { painter.fillRect(QRect(0, top, gutterW - 1, rowH), QColor(245, 158, 11, 56)); painter.setPen(QPen(QColor(245, 158, 11, 220), 1.5, Qt::DashLine)); painter.drawLine(0, top, gutterW - 1, top); painter.setPen(Qt::NoPen); }
            else if (isHovered) painter.fillRect(QRect(0, top, gutterW - 1, rowH), QColor(82, 139, 255, 36));

            const int bpRank = breakpointRankFilterForLine(lineOneBased); const QColor bpColor = colorForRankBreakpoint(bpRank);
            if (isDragTarget) { QPainterPath chev; chev.moveTo(cx - 3.6, cy - 4.5); chev.lineTo(cx + 4.6, cy); chev.lineTo(cx - 3.6, cy + 4.5); chev.lineTo(cx - 1.7, cy); chev.closeSubpath(); painter.setPen(QPen(QColor("#fbbf24"), 1.1)); painter.setBrush(QColor(245, 158, 11, 185)); painter.drawPath(chev); }
            else if (state == QStringLiteral("DisabledBreakpoint")) {
                const QColor ringCol = m_breakpoints.value(lineOneBased).trimmed().isEmpty() ? bpColor : QColor(QStringLiteral("#f59e0b"));
                painter.setPen(QPen(ringCol, 2.0)); painter.setBrush(Qt::NoBrush); painter.drawEllipse(QPointF(cx, cy), 5.0, 5.0);
            } else if (state == QStringLiteral("TemporaryBreakpoint")) {
                painter.setPen(QPen(QColor(QStringLiteral("#38bdf8")), 1.6, Qt::DashLine)); painter.setBrush(QColor(56, 189, 248, 42)); painter.drawEllipse(QPointF(cx, cy), 5.6, 5.6);
                QFont tFont = baseGutterFont; tFont.setBold(true); tFont.setPixelSize(8); painter.setFont(tFont); painter.setPen(QColor(QStringLiteral("#38bdf8"))); painter.drawText(QRectF(cx - 5.5, cy - 5.5, 11.0, 11.0), Qt::AlignCenter, QStringLiteral("T"));
            } else if (state == QStringLiteral("HoverPreview")) {
                painter.setPen(QPen(QColor(255, 255, 255, 160), 1.0)); painter.setBrush(QColor(239, 68, 68, 102));
                painter.drawEllipse(QPointF(cx, cy), 4.8, 4.8);
            } else if (state == QStringLiteral("Breakpoint") || state == QStringLiteral("ConditionalBreakpoint")) {
                const bool isCond = (state == QStringLiteral("ConditionalBreakpoint"));
                if (isLogpointForLine(lineOneBased)) {
                    const QPolygonF diamond({QPointF(cx, cy - 5.6), QPointF(cx + 5.6, cy), QPointF(cx, cy + 5.6), QPointF(cx - 5.6, cy)});
                    painter.setPen(QPen(QColor(QStringLiteral("#22d3ee")), 1.3)); painter.setBrush(QColor(QStringLiteral("#f59e0b")));
                    painter.drawPolygon(diamond);
                } else if (bpRank < 0) {
                    painter.setPen(QPen(QColor(QStringLiteral("#ffffff")), 1.5)); painter.setBrush(bpColor);
                    painter.drawEllipse(QPointF(cx, cy), 5.2, 5.2);
                    if (isCond) { painter.setPen(Qt::NoPen); painter.setBrush(QColor(QStringLiteral("#fbbf24"))); painter.drawEllipse(QPointF(cx, cy), 1.7, 1.7); }
                } else {
                    painter.setPen(QPen(isDark ? QColor(QStringLiteral("#ffffff")) : QColor(QStringLiteral("#1f2328")), 1.2));
                    painter.setBrush(bpColor); painter.drawEllipse(QPointF(cx, cy), 5.8, 5.8);
                    const double luma = 0.2126 * bpColor.red() + 0.7152 * bpColor.green() + 0.0722 * bpColor.blue();
                    QFont badgeFont = baseGutterFont; badgeFont.setBold(true); badgeFont.setPixelSize(bpRank >= 10 ? 7 : 8);
                    painter.setFont(badgeFont);
                    painter.setPen(luma > 150.0 ? QColor(QStringLiteral("#0f172a")) : QColor(QStringLiteral("#ffffff")));
                    painter.drawText(QRectF(cx - 5.5, cy - 5.5, 11.0, 11.0), Qt::AlignCenter, QString::number(bpRank));
                    if (isCond) { painter.setPen(Qt::NoPen); painter.setBrush(QColor(QStringLiteral("#fbbf24"))); painter.drawEllipse(QPointF(cx + 4.2, cy - 4.2), 1.6, 1.6); }
                }
            } else if (state == QStringLiteral("ExecutionPc")) {
                QPainterPath chev; chev.moveTo(cx - 3.6, cy - 4.5); chev.lineTo(cx + 4.6, cy);
                chev.lineTo(cx - 3.6, cy + 4.5); chev.lineTo(cx - 1.7, cy); chev.closeSubpath();
                painter.setPen(QPen(QColor("#98c379"), 1.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                painter.setBrush(QColor("#4ec9b0")); painter.drawPath(chev);
                painter.fillRect(QRect(gutterW - 3, top, 2, rowH), QColor("#4ec9b0"));
            } else if (state == QStringLiteral("BreakpointAndExecutionPc")) {
                if (bpRank < 0) { painter.setPen(QPen(QColor(QStringLiteral("#ffffff")), 1.4)); painter.setBrush(Qt::NoBrush); painter.drawEllipse(QPointF(cx, cy), 6.1, 6.1); }
                painter.setPen(QPen(bpColor, 1.8)); painter.setBrush(QColor("#24181c")); painter.drawEllipse(QPointF(cx, cy), 5.1, 5.1);
                QPainterPath innerChev; innerChev.moveTo(cx - 2.2, cy - 3.0); innerChev.lineTo(cx + 3.2, cy);
                innerChev.lineTo(cx - 2.2, cy + 3.0); innerChev.lineTo(cx - 1.0, cy); innerChev.closeSubpath();
                painter.setPen(QPen(QColor("#98c379"), 0.8)); painter.setBrush(QColor("#4ec9b0")); painter.drawPath(innerChev);
                painter.fillRect(QRect(gutterW - 3, top, 2, rowH), QColor("#4ec9b0"));
            }

            QFont rowFont = baseGutterFont; rowFont.setBold(isHovered || lineOneBased == m_executionLine); painter.setFont(rowFont);
            const QColor numColor = (lineOneBased == m_executionLine) ? QColor("#4ec9b0") : isHovered ? QColor("#61afef") : m_breakpoints.contains(lineOneBased) ? bpColor : QColor("#6b7280");
            painter.setPen(numColor);
            const int numLeft = iconLaneW + 2, numWidth = qMax(12, gutterW - numLeft - 8);
            painter.drawText(numLeft, top, numWidth, rowH, Qt::AlignRight | Qt::AlignVCenter, QString::number(lineOneBased));

            const GitLineState gitState = gitLineState(lineOneBased);
            if (gitState == GitLineState::Added) painter.fillRect(QRect(gutterW - 4, top, 3, rowH), QColor("#9ece6a"));
            else if (gitState == GitLineState::Modified) painter.fillRect(QRect(gutterW - 4, top, 3, rowH), QColor("#7aa2f7"));
            else if (gitState == GitLineState::Deleted) {
                QPainterPath delTri; delTri.moveTo(gutterW - 6, top); delTri.lineTo(gutterW - 1, top + 4); delTri.lineTo(gutterW - 6, top + 8); delTri.closeSubpath();
                painter.fillPath(delTri, QColor("#f7768e"));
            }
        }
        block = block.next(); top = bottom; bottom = top + qRound(blockBoundingRect(block).height()); ++blockNumber;
    }
}

}  // namespace gridlock::ui
