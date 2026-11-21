/**
 * @file SourceCodeView.cpp
 * @brief Source code editor implementation with syntax highlighting and gutter controls.
 *
 * Implements the SourceCodeView text editor (backed by EditorSyntaxHighlighter)
 * and LineNumberArea for gutter rendering, Git diff markers, and breakpoint interactions.
 */

#include "SourceCodeView.hpp"
#include <QtWidgets>
#include "../../core/managers/ConfigManager.hpp"
#include "../../core/utils/ClangdLspClient.hpp"
#include "../dialogs/ConditionalBreakpointDialog.hpp"
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
bool isCppKeywordToken(const QString& tok) {
    static const QSet<QString> kKw = {QStringLiteral("auto"), QStringLiteral("bool"), QStringLiteral("break"), QStringLiteral("case"), QStringLiteral("catch"), QStringLiteral("char"), QStringLiteral("class"), QStringLiteral("const"), QStringLiteral("constexpr"), QStringLiteral("continue"), QStringLiteral("default"), QStringLiteral("delete"), QStringLiteral("do"), QStringLiteral("double"), QStringLiteral("else"), QStringLiteral("enum"), QStringLiteral("explicit"), QStringLiteral("extern"), QStringLiteral("false"), QStringLiteral("float"), QStringLiteral("for"), QStringLiteral("if"), QStringLiteral("include"), QStringLiteral("inline"), QStringLiteral("int"), QStringLiteral("long"), QStringLiteral("namespace"), QStringLiteral("new"), QStringLiteral("nullptr"), QStringLiteral("override"), QStringLiteral("private"), QStringLiteral("protected"), QStringLiteral("public"), QStringLiteral("return"), QStringLiteral("short"), QStringLiteral("signed"), QStringLiteral("sizeof"), QStringLiteral("static"), QStringLiteral("std"), QStringLiteral("struct"), QStringLiteral("switch"), QStringLiteral("template"), QStringLiteral("this"), QStringLiteral("true"), QStringLiteral("typedef"), QStringLiteral("typename"), QStringLiteral("unsigned"), QStringLiteral("using"), QStringLiteral("virtual"), QStringLiteral("void"), QStringLiteral("volatile"), QStringLiteral("while")};
    return kKw.contains(tok);
}
}  // namespace

SourceCodeView::SourceCodeView(QWidget *parent) : QPlainTextEdit(parent) {
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

void SourceCodeView::setCurrentFile(const QString& filePath) {
    m_currentFilePath = (!filePath.isEmpty() && QFileInfo::exists(filePath) && !QFileInfo(filePath).canonicalFilePath().isEmpty())
                            ? QFileInfo(filePath).canonicalFilePath() : filePath;
    if (m_highlighter) m_highlighter->setFilePath(m_currentFilePath);
    if (!m_currentFilePath.isEmpty()) {
        setDiagnostics(core::ClangdLspClient::instance().cachedDiagnosticsForFile(m_currentFilePath));
        if (!toPlainText().trimmed().isEmpty()) syncLspDocument();
    }
    refreshGitDiffGutter();
}
void SourceCodeView::syncLspDocument() {
    if (!m_currentFilePath.isEmpty()) core::ClangdLspClient::instance().syncDocument(m_currentFilePath, toPlainText());
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
        m_breakpointInfo[bp] = info; m_breakpoints[bp] = info.condition; m_breakpointSuspendScopes[bp] = (info.rankFilter >= 0) ? QStringLiteral("Rank") : QStringLiteral("All");
    }
    highlightCurrentLine(); if (m_lineNumberArea) m_lineNumberArea->update();
}
void SourceCodeView::syncBreakpointMetadata(const QMap<int, BreakpointVisualInfo>& metaByLine) {
    m_breakpoints.clear(); m_breakpointSuspendScopes.clear(); m_breakpointInfo.clear();
    for (auto it = metaByLine.constBegin(); it != metaByLine.constEnd(); ++it) {
        if (it.key() <= 0) continue;
        BreakpointVisualInfo clean{it.value().rankFilter < 0 ? -1 : it.value().rankFilter, it.value().condition.trimmed(), qMax(0, it.value().ignoreCount), it.value().logMessage.trimmed(), it.value().enabled, it.value().temporary, it.value().commands};
        m_breakpointInfo[it.key()] = clean; m_breakpoints[it.key()] = clean.condition; m_breakpointSuspendScopes[it.key()] = (clean.rankFilter >= 0) ? QStringLiteral("Rank") : QStringLiteral("All");
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

/** @brief Reloads font size, font family, word wrap, and active theme surface from user configuration. */
void SourceCodeView::reloadStyle() {
    // Query user appearance and editing settings from persistent QSettings store.
    QSettings s("gridlock", "debugger");
    const int codeFontSize = s.value("appearance/code_font_size", 11).toInt();
    const QString codeFontFamily = s.value("appearance/code_font_family", QStringLiteral("JetBrains Mono")).toString();
    setLineWrapMode(s.value("editing/word_wrap", false).toBool() ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
    // Update widget QFont directly so font().pointSize() and fontMetrics() reflect live changes.
    QFont codeFont = font();
    if (!codeFontFamily.trimmed().isEmpty()) codeFont.setFamily(codeFontFamily.trimmed());
    codeFont.setStyleHint(QFont::Monospace); codeFont.setPointSize(codeFontSize); setFont(codeFont);
    // Resolve active canvas and text tokens from ThemeManager so Light and Dark themes render natively.
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
    if (m_highlighter) m_highlighter->reloadPalette();
    highlightCurrentLine(); updateLineNumberAreaWidth(0); if (m_lineNumberArea) m_lineNumberArea->update();
}

/** @brief Overrides the active highlighting language mode on the editor highlighter. */
void SourceCodeView::setLanguageMode(LanguageMode mode) {
    if (m_highlighter) m_highlighter->setLanguageMode(mode);
}

/** @brief Returns the active highlighting language mode. */
SourceCodeView::LanguageMode SourceCodeView::languageMode() const { return m_highlighter ? m_highlighter->languageMode() : LanguageMode::PlainText; }
/** @brief Enables or disables nested Rainbow Brackets depth coloring. */
void SourceCodeView::setRainbowBracketsEnabled(bool enabled) { if (m_highlighter) m_highlighter->setRainbowBracketsEnabled(enabled); }
/** @brief Returns whether Rainbow Brackets depth coloring is enabled. */
bool SourceCodeView::rainbowBracketsEnabled() const { return m_highlighter ? m_highlighter->rainbowBracketsEnabled() : false; }
/** @brief Enables or disables TOML/YAML/Markdown/CSV syntax highlighting. */
void SourceCodeView::setDataDocHighlightingEnabled(bool enabled) { if (m_highlighter) m_highlighter->setDataDocHighlightingEnabled(enabled); }
/** @brief Returns whether TOML/YAML/Markdown/CSV syntax highlighting is enabled. */
bool SourceCodeView::dataDocHighlightingEnabled() const { return m_highlighter ? m_highlighter->dataDocHighlightingEnabled() : false; }
/** @brief Rebuilds syntax palettes from active config/theme and rehighlights the document. */
void SourceCodeView::rehighlightDocument() { if (m_highlighter) m_highlighter->reloadPalette(); if (viewport()) viewport()->update(); }

/**
 * @brief Toggles single-line '//' comments on the active line in the document.
 */
void SourceCodeView::toggleLineComment() {
    QTextCursor tc = textCursor(); QTextBlock block = tc.block();
    if (!block.isValid()) return;
    // Locate the first non-whitespace character in the active block.
    const QString lineText = block.text();
    int firstNonSpace = 0;
    while (firstNonSpace < lineText.size() && lineText.at(firstNonSpace).isSpace()) ++firstNonSpace;

    // Insert or strip '//' prefix inside an atomic undo/redo edit block.
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

/**
 * @brief Retrieves the 1-based line number containing the text cursor.
 * @return 1-based line number.
 */
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

/**
 * @brief Sets or updates a breakpoint with an explicit rank filter (-1 = All Ranks) and condition.
 * @param lineOneBased 1-based target source line.
 * @param rankFilter Target rank index, or -1 for All Ranks.
 * @param condition Optional C/C++ condition expression.
 */
void SourceCodeView::setRankBreakpoint(int lineOneBased, int rankFilter, const QString& condition) {
    if (lineOneBased <= 0) return;
    // Normalize negative rank filters to -1 (All Ranks) and preserve existing ignoreCount/logMessage/enabled/temporary/commands.
    const int normalizedRank = (rankFilter < 0) ? -1 : rankFilter; const QString cleanCond = condition.trimmed();
    const BreakpointVisualInfo prev = m_breakpointInfo.value(lineOneBased);
    m_breakpointInfo.insert(lineOneBased, BreakpointVisualInfo{normalizedRank, cleanCond, prev.ignoreCount, prev.logMessage, prev.enabled, prev.temporary, prev.commands});
    m_breakpoints.insert(lineOneBased, cleanCond); m_breakpointSuspendScopes.insert(lineOneBased, (normalizedRank >= 0) ? QStringLiteral("Rank") : QStringLiteral("All"));
    highlightCurrentLine(); if (m_lineNumberArea) { if (m_hoveredGutterLine == lineOneBased) m_lineNumberArea->setToolTip(breakpointTooltipForLine(lineOneBased)); m_lineNumberArea->update(); }
    // Emit rank metadata before breakpointToggled so MainWindowWorkspace syncs the active rank filter.
    emit rankBreakpointChanged(m_currentFilePath, lineOneBased, true, normalizedRank, cleanCond); emit breakpointToggled(m_currentFilePath, lineOneBased, true, cleanCond);
}
/** @brief Registers or updates a conditional breakpoint while preserving its current rank filter. */
void SourceCodeView::setConditionalBreakpoint(int lineOneBased, const QString& condition) {
    if (lineOneBased > 0) setRankBreakpoint(lineOneBased, breakpointRankFilterForLine(lineOneBased), condition);
}
/** @brief Configures condition, hit ignore count, and non-suspending Logpoint message on a breakpoint line. */
void SourceCodeView::setBreakpointOptions(int lineOneBased, const QString& condition, int ignoreCount, const QString& logMessage) {
    if (lineOneBased <= 0) return;
    // Update stored metadata and emit breakpointOptionsChanged without duplicating -break-insert when already set.
    const int rf = breakpointRankFilterForLine(lineOneBased); const QString cleanCond = condition.trimmed(), cleanLog = logMessage.trimmed();
    const bool needInsert = !hasBreakpoint(lineOneBased) || (m_breakpoints.value(lineOneBased) != cleanCond);
    const BreakpointVisualInfo prev = m_breakpointInfo.value(lineOneBased);
    m_breakpointInfo.insert(lineOneBased, BreakpointVisualInfo{rf, cleanCond, qMax(0, ignoreCount), cleanLog, prev.enabled, prev.temporary, prev.commands}); m_breakpoints.insert(lineOneBased, cleanCond);
    if (needInsert) setRankBreakpoint(lineOneBased, rf, cleanCond);
    else { highlightCurrentLine(); if (m_lineNumberArea) { if (m_hoveredGutterLine == lineOneBased) m_lineNumberArea->setToolTip(breakpointTooltipForLine(lineOneBased)); m_lineNumberArea->update(); } }
    emit breakpointOptionsChanged(m_currentFilePath, lineOneBased, qMax(0, ignoreCount), cleanLog);
}
/** @brief Returns true when lineOneBased has a non-suspending Logpoint message configured. */
bool SourceCodeView::isLogpointForLine(int lineOneBased) const { return hasBreakpoint(lineOneBased) && !m_breakpointInfo.value(lineOneBased).logMessage.trimmed().isEmpty(); }
/** @brief Returns the configured pass/ignore hit count for lineOneBased (0 if unset). */
int SourceCodeView::breakpointIgnoreCountForLine(int lineOneBased) const { return hasBreakpoint(lineOneBased) ? qMax(0, m_breakpointInfo.value(lineOneBased).ignoreCount) : 0; }
/** @brief Returns the configured Logpoint trace message for lineOneBased (empty if unset). */
QString SourceCodeView::breakpointLogMessageForLine(int lineOneBased) const { return hasBreakpoint(lineOneBased) ? m_breakpointInfo.value(lineOneBased).logMessage.trimmed() : QString(); }
/** @brief Returns stored visual metadata for a 1-based breakpoint line. */
SourceCodeView::BreakpointVisualInfo SourceCodeView::breakpointInfoForLine(int lineOneBased) const { return m_breakpointInfo.value(lineOneBased, BreakpointVisualInfo{-1, m_breakpoints.value(lineOneBased).trimmed(), 0, QString()}); }
/** @brief Returns deterministic badge fill color for lineOneBased (#ef4444 for All Ranks). */
QColor SourceCodeView::breakpointColorForLine(int lineOneBased) const { return colorForRankBreakpoint(breakpointRankFilterForLine(lineOneBased)); }
/** @brief Returns true when lineOneBased holds an All-Ranks breakpoint rendered with a white outer ring. */
bool SourceCodeView::breakpointHasWhiteOutlineForLine(int lineOneBased) const { return hasBreakpoint(lineOneBased) && breakpointRankFilterForLine(lineOneBased) < 0; }
/** @brief Returns the configured 0-based rank filter for lineOneBased (-1 for All Ranks or unset). */
int SourceCodeView::breakpointRankFilterForLine(int lineOneBased) const { return m_breakpointInfo.contains(lineOneBased) ? m_breakpointInfo.value(lineOneBased).rankFilter : -1; }
/** @brief Returns formatted hover tooltip text for a breakpoint line, or empty string if unset. */
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
/** @brief Updates the active focused rank for rank-scoped quick context menu actions. */
void SourceCodeView::setActiveFocusRank(int rankId) { m_activeFocusRank = qMax(0, rankId); }
/** @brief Returns the active C/C++ boolean condition attached to a 1-based breakpoint line. */
QString SourceCodeView::breakpointCondition(int lineOneBased) const { return m_breakpoints.value(lineOneBased).trimmed(); }
/** @brief Returns the configured rank suspension scope ("All" or "Rank") for a breakpoint line. */
QString SourceCodeView::breakpointSuspendScope(int lineOneBased) const { return m_breakpointSuspendScopes.value(lineOneBased, QStringLiteral("All")); }
/** @brief Updates the 1-based hovered gutter line, tooltip, and schedules a gutter repaint. */
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
/** @brief Resolves the composite gutter marker badge state for a 1-based source line. */
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
    // Layer subtle translucent per-rank (alpha = 28 enabled, 10 disabled) background tint across breakpoint lines.
    for (auto it = m_breakpoints.constBegin(); it != m_breakpoints.constEnd(); ++it) {
        const int bpLine = it.key(); if (bpLine <= 0 || bpLine == m_executionLine) continue;
        if (QTextBlock b = document()->findBlockByLineNumber(bpLine - 1); b.isValid()) {
            QColor tint = breakpointColorForLine(bpLine); tint.setAlpha(isBreakpointEnabledForLine(bpLine) ? 28 : 10);
            addFullSel(QTextCursor(b), tint);
        }
    }
    // If a 1-based debugger execution line is active, layer a distinct emerald-slate highlight band.
    if (m_executionLine > 0) if (QTextBlock eb = document()->findBlockByLineNumber(m_executionLine - 1); eb.isValid())
        addFullSel(QTextCursor(eb), isDark ? QColor("#20342c") : QColor(46, 160, 67, 45));
    // Render inline wavy underlines (#f7768e crimson for errors, #e0af68 amber for warnings).
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
/** @brief Applies compiler diagnostics as wavy underlines and updates gutter markers. */
void SourceCodeView::setDiagnostics(const QVector<core::utils::LspDiagnostic>& diagnostics) { m_diagnostics = diagnostics; highlightCurrentLine(); if (m_lineNumberArea) m_lineNumberArea->update(); }

/**
 * @brief Paints the two-lane gutter with hover previews, vector badges, and right-aligned line numbers.
 *
 * Left Icon Lane (x = 0..20): Renders centered vector badges for HoverPreview (translucent crimson ghost
 * circle), Breakpoint (crimson coral circle), ConditionalBreakpoint (amber diamond with dark center dot),
 * ExecutionPc (emerald chevron), and BreakpointAndExecutionPc (crimson outer ring nesting an emerald chevron).
 * Right Line-Number Lane (x = 22..width-8): Renders right-aligned monospace line digits with zero icon collision.
 *
 * @param event The QPaintEvent for the gutter rect.
 */
void SourceCodeView::lineNumberAreaPaintEvent(QPaintEvent *event) {
    QPainter painter(m_lineNumberArea);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const int gutterW = m_lineNumberArea->width();
    const int iconLaneW = gutterIconLaneWidth();
    const QFont baseGutterFont = painter.font();

    // Fill gutter background and right-hand separator border from active theme palette.
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

    // Render visible blocks in view across two non-overlapping horizontal lanes.
    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            const int lineOneBased = blockNumber + 1;
            const int rowH = qMax(14, bottom - top);
            const qreal cx = iconLaneW * 0.5, cy = top + rowH * 0.5;
            const bool isHovered = (lineOneBased == m_hoveredGutterLine);
            const QString state = gutterMarkerStateForLine(lineOneBased);

            // Reset painter brush/pen explicitly at the top of each row iteration.
            painter.setBrush(Qt::NoBrush); painter.setPen(Qt::NoPen);

            // Illuminate hovered gutter row or active PC drag drop-target row.
            const bool isDragTarget = m_draggingExecutionPc && (lineOneBased == m_dragTargetLine);
            if (isDragTarget) { painter.fillRect(QRect(0, top, gutterW - 1, rowH), QColor(245, 158, 11, 56)); painter.setPen(QPen(QColor(245, 158, 11, 220), 1.5, Qt::DashLine)); painter.drawLine(0, top, gutterW - 1, top); painter.setPen(Qt::NoPen); }
            else if (isHovered) painter.fillRect(QRect(0, top, gutterW - 1, rowH), QColor(82, 139, 255, 36));

            // 1. Left Icon Lane (x = 0..20): Antialiased vector badges for HoverPreview, All-Ranks, Per-Rank, & ExecutionPc.
            const int bpRank = breakpointRankFilterForLine(lineOneBased); const QColor bpColor = colorForRankBreakpoint(bpRank);
            if (isDragTarget) { QPainterPath chev; chev.moveTo(cx - 3.6, cy - 4.5); chev.lineTo(cx + 4.6, cy); chev.lineTo(cx - 3.6, cy + 4.5); chev.lineTo(cx - 1.7, cy); chev.closeSubpath(); painter.setPen(QPen(QColor("#fbbf24"), 1.1)); painter.setBrush(QColor(245, 158, 11, 185)); painter.drawPath(chev); }
            else if (state == QStringLiteral("DisabledBreakpoint")) {
                // Hollow 2px red/amber ring indicating a non-destructively disabled breakpoint.
                const QColor ringCol = m_breakpoints.value(lineOneBased).trimmed().isEmpty() ? bpColor : QColor(QStringLiteral("#f59e0b"));
                painter.setPen(QPen(ringCol, 2.0)); painter.setBrush(Qt::NoBrush); painter.drawEllipse(QPointF(cx, cy), 5.0, 5.0);
            } else if (state == QStringLiteral("TemporaryBreakpoint")) {
                // Dashed ring with centered 'T' glyph indicating a one-shot temporary (tbreak) breakpoint.
                painter.setPen(QPen(QColor(QStringLiteral("#38bdf8")), 1.6, Qt::DashLine)); painter.setBrush(QColor(56, 189, 248, 42)); painter.drawEllipse(QPointF(cx, cy), 5.6, 5.6);
                QFont tFont = baseGutterFont; tFont.setBold(true); tFont.setPixelSize(8); painter.setFont(tFont); painter.setPen(QColor(QStringLiteral("#38bdf8"))); painter.drawText(QRectF(cx - 5.5, cy - 5.5, 11.0, 11.0), Qt::AlignCenter, QStringLiteral("T"));
            } else if (state == QStringLiteral("HoverPreview")) {
                painter.setPen(QPen(QColor(255, 255, 255, 160), 1.0)); painter.setBrush(QColor(239, 68, 68, 102));
                painter.drawEllipse(QPointF(cx, cy), 4.8, 4.8);
            } else if (state == QStringLiteral("Breakpoint") || state == QStringLiteral("ConditionalBreakpoint")) {
                const bool isCond = (state == QStringLiteral("ConditionalBreakpoint"));
                if (isLogpointForLine(lineOneBased)) {
                    // Logpoint: crisp Amber/Cyan diamond badge indicating non-suspending trace output.
                    const QPolygonF diamond({QPointF(cx, cy - 5.6), QPointF(cx + 5.6, cy), QPointF(cx, cy + 5.6), QPointF(cx - 5.6, cy)});
                    painter.setPen(QPen(QColor(QStringLiteral("#22d3ee")), 1.3)); painter.setBrush(QColor(QStringLiteral("#f59e0b")));
                    painter.drawPolygon(diamond);
                } else if (bpRank < 0) {
                    // All Ranks: crimson red (#ef4444) circle with crisp 1.5px white (#ffffff) outer ring.
                    painter.setPen(QPen(QColor(QStringLiteral("#ffffff")), 1.5)); painter.setBrush(bpColor);
                    painter.drawEllipse(QPointF(cx, cy), 5.2, 5.2);
                    if (isCond) { painter.setPen(Qt::NoPen); painter.setBrush(QColor(QStringLiteral("#fbbf24"))); painter.drawEllipse(QPointF(cx, cy), 1.7, 1.7); }
                } else {
                    // Specific Rank: accordion rank color with high-contrast ring and bold rank number.
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

            // 2. Right Line-Number Lane (x = 22..gutterW-8): Right-aligned digits with hover emphasis.
            QFont rowFont = baseGutterFont; rowFont.setBold(isHovered || lineOneBased == m_executionLine); painter.setFont(rowFont);
            const QColor numColor = (lineOneBased == m_executionLine) ? QColor("#4ec9b0") : isHovered ? QColor("#61afef") : m_breakpoints.contains(lineOneBased) ? bpColor : QColor("#6b7280");
            painter.setPen(numColor);
            const int numLeft = iconLaneW + 2, numWidth = qMax(12, gutterW - numLeft - 8);
            painter.drawText(numLeft, top, numWidth, rowH, Qt::AlignRight | Qt::AlignVCenter, QString::number(lineOneBased));

            // 3. Right-edge Git diff gutter indicator bar (3px wide along gutter right border).
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
/** @brief Determines whether the standard editor context menu may open at pos (suppressing gutter regions). */
bool SourceCodeView::shouldOpenEditorContextMenuAt(const QPoint& pos) const {
    return pos.x() >= lineNumberAreaWidth() && !(m_lineNumberArea && m_lineNumberArea->geometry().contains(pos));
}
QString SourceCodeView::detectStructOrClassAtLine(const QString& lineText) {
    static const QRegularExpression kTypeRe(QStringLiteral(R"(^\s*(?:template\s*<[^>]*>\s*)?(?:typedef\s+)?(?:struct|class)\s+(?:\[\[[^\]]*\]\]\s*)?([A-Za-z_][A-Za-z0-9_]*)\b)"));
    const auto m = kTypeRe.match(lineText); return m.hasMatch() ? m.captured(1) : QString();
}
QMenu* SourceCodeView::createEditorContextMenuAt(const QPoint& editorPos) {
    if (!shouldOpenEditorContextMenuAt(editorPos)) return nullptr;
    const QTextCursor posCur = cursorForPosition(editorPos); const int clickedLine = qMax(1, posCur.blockNumber() + 1);
    const QString lineText = posCur.block().text(), selected = textCursor().hasSelection() ? textCursor().selectedText().replace(QChar::ParagraphSeparator, QLatin1Char(' ')).trimmed() : QString();
    QString exprTok = selected;
    if (exprTok.isEmpty() && !lineText.isEmpty()) {
        int l = qBound(0, posCur.positionInBlock(), lineText.size()), r = l;
        auto isExprCh = [](QChar ch) { return ch.isLetterOrNumber() || ch == QLatin1Char('_') || ch == QLatin1Char('.') || ch == QLatin1Char('[') || ch == QLatin1Char(']') || ch == QLatin1Char('-') || ch == QLatin1Char('>') || ch == QLatin1Char('*'); };
        while (l > 0 && isExprCh(lineText[l - 1])) --l;
        while (r < lineText.size() && isExprCh(lineText[r])) ++r;
        exprTok = lineText.mid(l, r - l).trimmed();
    }
    while (exprTok.startsWith(QLatin1Char('.')) || exprTok.startsWith(QLatin1Char('-')) || exprTok.startsWith(QLatin1Char('>'))) exprTok.remove(0, 1);
    while (exprTok.endsWith(QLatin1Char('.')) || exprTok.endsWith(QLatin1Char('-')) || exprTok.endsWith(QLatin1Char('>'))) exprTok.chop(1);
    QString structType = detectStructOrClassAtLine(lineText);
    if (structType.isEmpty() && !exprTok.isEmpty()) {
        static const QRegularExpression kAllStructs(QStringLiteral(R"(\b(?:struct|class)\s+([A-Za-z_][A-Za-z0-9_]*)\b)"));
        for (auto it = kAllStructs.globalMatch(toPlainText()); it.hasNext();) { const QString tn = it.next().captured(1); if (tn == exprTok) { structType = tn; break; } }
    }
    bool isNum = false; exprTok.toDouble(&isNum); if (isNum || isCppKeywordToken(exprTok)) exprTok.clear();
    QString calcTok = selected;
    if (calcTok.isEmpty()) { QTextCursor wc = posCur; wc.select(QTextCursor::WordUnderCursor); calcTok = wc.selectedText().trimmed(); }
    if (!calcTok.isEmpty() && m_inlineVarValues.contains(calcTok)) calcTok = m_inlineVarValues.value(calcTok);
    QMenu* menu = createStandardContextMenu(); menu->addSeparator();
    menu->addAction(QStringLiteral("Go to Definition"), this, &SourceCodeView::triggerGoToDefinition)->setShortcut(QKeySequence(Qt::Key_F12));
    if (!exprTok.isEmpty() && exprTok != structType) menu->addAction(QStringLiteral("Graph '%1' in Pointer Graph").arg(exprTok), this, [this, exprTok]() { emit graphExpressionRequested(exprTok); });
    if (!structType.isEmpty()) menu->addAction(QStringLiteral("Graph '%1' Struct Instances").arg(structType), this, [this, structType]() { emit graphStructTypeRequested(structType); });
    if (!calcTok.isEmpty()) menu->addAction(QStringLiteral("Open Selection in Programmer's Calculator"), this, [this, calcTok]() { emit sendToCalculatorRequested(calcTok); if (QWidget* win = window()) QMetaObject::invokeMethod(win, "openProgrammerCalculatorWithText", Q_ARG(QString, calcTok)); });
    menu->addSeparator();
    QAction* rtcAct = menu->addAction(QStringLiteral("Run to Cursor"), this, [this, clickedLine]() { emit runToCursorRequested(m_currentFilePath, clickedLine); });
    const QAction* winRtc = window() ? window()->findChild<QAction*>(QStringLiteral("actionRunToCursor")) : nullptr;
    const QKeySequence rtcSeq = winRtc ? winRtc->shortcut() : core::ConfigManager::instance().customShortcuts().value(QStringLiteral("actionRunToCursor"), QKeySequence(Qt::CTRL | Qt::Key_F10));
    rtcAct->setShortcut(rtcSeq);
    menu->addAction(QStringLiteral("Set Next Statement / Jump Here"), this, [this, clickedLine]() { emit jumpToLineRequested(m_currentFilePath, clickedLine); });
    menu->addSeparator();
    if (hasBreakpoint(clickedLine)) { const bool en = isBreakpointEnabledForLine(clickedLine); menu->addAction(en ? QStringLiteral("Disable Breakpoint") : QStringLiteral("Enable Breakpoint"), this, [this, clickedLine, en]() { setBreakpointEnabled(clickedLine, !en); }); }
    menu->addAction(QStringLiteral("Insert Temporary Breakpoint (tbreak)"), this, [this, clickedLine]() { setTemporaryBreakpoint(clickedLine, -1); });
    menu->addAction(QStringLiteral("Set Breakpoint (All Ranks)"), this, [this, clickedLine]() { setRankBreakpoint(clickedLine, -1); });
    menu->addAction(QStringLiteral("Set Breakpoint for Active Rank (Rank %1)").arg(m_activeFocusRank), this, [this, clickedLine]() { setRankBreakpoint(clickedLine, m_activeFocusRank); });
    menu->addAction(QStringLiteral("Set Breakpoint for Rank..."), this, [this, clickedLine]() { bool ok = false; const int r = QInputDialog::getInt(this, QStringLiteral("Breakpoint Rank Filter"), QStringLiteral("Target MPI Rank (0+):"), m_activeFocusRank, 0, 4095, 1, &ok); if (ok) setRankBreakpoint(clickedLine, r); });
    return menu;
}
bool SourceCodeView::triggerContextActionAtLine(int lineOneBased, const QString& actionSubstring) {
    QTextBlock b = document()->findBlockByNumber(qMax(0, lineOneBased - 1)); if (!b.isValid()) return false;
    if (!textCursor().hasSelection()) { QTextCursor c(b); setTextCursor(c); }
    QPoint pos = cursorRect(QTextCursor(b)).center(); pos.setX(qMax(pos.x(), lineNumberAreaWidth() + 12)); pos.setY(qMax(4, pos.y()));
    std::unique_ptr<QMenu> menu(createEditorContextMenuAt(pos)); if (!menu) return false;
    for (QAction* act : menu->actions()) if (act && !act->isSeparator() && act->text().contains(actionSubstring, Qt::CaseInsensitive)) { act->trigger(); return true; }
    return false;
}
void SourceCodeView::contextMenuEvent(QContextMenuEvent *event) {
    if (!event) return;
    QPoint editorPos = event->pos();
    if (event->reason() == QContextMenuEvent::Mouse && !event->globalPos().isNull()) { const QPoint mapped = mapFromGlobal(event->globalPos()); if (rect().contains(mapped)) editorPos = mapped; }
    if (QMenu* menu = createEditorContextMenuAt(editorPos)) { menu->exec(event->globalPos()); delete menu; } else event->accept();
}
/** @brief Legacy 6-arg overload preserving deletion semantics when unchecking without explicit removeRequested. */
void SourceCodeView::handleBreakpointPopoverResult(int lineOneBased, bool wasPreviouslySet, bool accepted, bool enabledChecked, const QString& condition, const QString& suspendScope) {
    handleBreakpointPopoverResult(lineOneBased, wasPreviouslySet, accepted, enabledChecked, condition, suspendScope, !enabledChecked);
}
/** @brief Commits non-destructive enable/disable, updates condition/scope, or removes breakpoint on explicit removal. */
void SourceCodeView::handleBreakpointPopoverResult(int lineOneBased, bool wasPreviouslySet, bool accepted, bool enabledChecked, const QString& condition, const QString& suspendScope, bool removeRequested) {
    if (lineOneBased <= 0) return;
    if ((!accepted && !wasPreviouslySet) || (accepted && removeRequested)) {
        if (m_breakpoints.contains(lineOneBased)) {
            const int prevRank = breakpointRankFilterForLine(lineOneBased);
            m_breakpoints.remove(lineOneBased); m_breakpointSuspendScopes.remove(lineOneBased); m_breakpointInfo.remove(lineOneBased);
            highlightCurrentLine(); if (m_lineNumberArea) m_lineNumberArea->update();
            emit rankBreakpointChanged(m_currentFilePath, lineOneBased, false, prevRank, QString()); emit breakpointToggled(m_currentFilePath, lineOneBased, false, QString());
        }
        return;
    }
    if (!accepted) return;
    const QString cleanScope = suspendScope.trimmed().isEmpty() ? QStringLiteral("All") : suspendScope.trimmed();
    int resolvedRank = -1;
    if (cleanScope.compare(QStringLiteral("Rank"), Qt::CaseInsensitive) == 0) resolvedRank = qMax(0, m_activeFocusRank);
    else if (cleanScope.startsWith(QStringLiteral("Rank "), Qt::CaseInsensitive)) { bool ok = false; const int parsed = cleanScope.mid(5).trimmed().toInt(&ok); resolvedRank = ok ? qMax(0, parsed) : qMax(0, m_activeFocusRank); }
    setRankBreakpoint(lineOneBased, resolvedRank, condition.trimmed());
    setBreakpointEnabled(lineOneBased, enabledChecked);
}
/** @brief Handles gutter mouse clicks: arms PC arrow drag on execution line, toggles breakpoint, or opens popover/menu. */
void SourceCodeView::lineNumberAreaMousePressEvent(QMouseEvent *event) {
    if (!event || (event->button() != Qt::LeftButton && event->button() != Qt::RightButton)) return;
    event->accept(); int top = 0, bottom = 0; const int lineNum = gutterLineAtY(event->pos().y(), &top, &bottom);
    if (lineNum <= 0) return;
    if (event->button() == Qt::RightButton && (event->modifiers() & Qt::ShiftModifier)) {
        QMenu gMenu(this);
        if (hasBreakpoint(lineNum)) { const bool en = isBreakpointEnabledForLine(lineNum); gMenu.addAction(en ? QStringLiteral("Disable Breakpoint") : QStringLiteral("Enable Breakpoint"), this, [this, lineNum, en]() { setBreakpointEnabled(lineNum, !en); }); }
        gMenu.addAction(QStringLiteral("Insert Temporary Breakpoint (tbreak)"), this, [this, lineNum]() { setTemporaryBreakpoint(lineNum, -1); });
        if (hasBreakpoint(lineNum)) gMenu.addAction(QStringLiteral("Remove Breakpoint"), this, [this, lineNum]() { toggleBreakpoint(lineNum); });
        gMenu.exec(m_lineNumberArea->mapToGlobal(event->pos())); return;
    }
    if (event->button() == Qt::RightButton) {
        const bool wasPreviouslySet = hasBreakpoint(lineNum); if (!wasPreviouslySet) toggleBreakpoint(lineNum);
        const QPoint iconGlobalPos = m_lineNumberArea->mapToGlobal(QPoint(gutterIconLaneWidth() / 2, top + (qMax(14, bottom - top) / 2) + 5));
        const BreakpointVisualInfo curMeta = breakpointInfoForLine(lineNum);
        ConditionalBreakpointDialog dlg(m_breakpoints.value(lineNum), this);
        dlg.setTargetLineContext(lineNum, document()->findBlockByNumber(lineNum - 1).text().trimmed()); dlg.setSourceDocumentContext(m_currentFilePath, toPlainText(), lineNum);
        dlg.setBreakpointEnabled(curMeta.enabled); dlg.setTemporary(curMeta.temporary); dlg.setCommands(curMeta.commands);
        dlg.setSuspendScope(m_breakpointSuspendScopes.value(lineNum, QStringLiteral("All"))); dlg.setIgnoreCount(curMeta.ignoreCount); dlg.setLogMessage(curMeta.logMessage); dlg.positionNearAnchor(iconGlobalPos);
        const bool accepted = (dlg.exec() == QDialog::Accepted);
        handleBreakpointPopoverResult(lineNum, wasPreviouslySet, accepted, dlg.isBreakpointEnabled(), dlg.getCondition(), dlg.suspendScope(), dlg.isRemoveRequested());
        if (accepted && !dlg.isRemoveRequested()) {
            setBreakpointOptions(lineNum, dlg.getCondition(), dlg.ignoreCount(), dlg.logMessage());
            if (dlg.isTemporary() && !curMeta.temporary) setTemporaryBreakpoint(lineNum, breakpointRankFilterForLine(lineNum), dlg.getCondition());
            setBreakpointCommands(lineNum, dlg.commands());
        }
    } else if (m_executionLine > 0 && lineNum == m_executionLine) {
        m_pcDragCandidate = true; m_draggingExecutionPc = false; m_pcDragPressPos = event->pos(); m_dragTargetLine = m_executionLine;
    } else toggleBreakpoint(lineNum);
    m_lineNumberArea->update();
}
/** @brief Tracks gutter hover preview and active execution PC arrow dragging across visible source lines. */
void SourceCodeView::lineNumberAreaMouseMoveEvent(QMouseEvent *event) {
    if (!event) return;
    const int hoveredLine = gutterLineAtY(event->pos().y());
    if (m_pcDragCandidate && (event->buttons() & Qt::LeftButton)) {
        if (!m_draggingExecutionPc && ((event->pos() - m_pcDragPressPos).manhattanLength() >= 4 || (hoveredLine > 0 && hoveredLine != m_executionLine))) {
            m_draggingExecutionPc = true; if (m_lineNumberArea) m_lineNumberArea->setCursor(Qt::ClosedHandCursor);
        }
        if (m_draggingExecutionPc && hoveredLine > 0 && hoveredLine != m_dragTargetLine) {
            m_dragTargetLine = hoveredLine; if (m_lineNumberArea) m_lineNumberArea->update(); if (viewport()) viewport()->update();
        }
        return;
    }
    if (m_lineNumberArea) m_lineNumberArea->setCursor((m_executionLine > 0 && hoveredLine == m_executionLine) ? Qt::OpenHandCursor : Qt::ArrowCursor);
    setHoveredGutterLine(hoveredLine);
}

/** @brief Updates the gutter execution PC marker without moving cursor, scroll, or active tab. */
void SourceCodeView::setExecutionLineOnly(int lineOneBased) {
    m_executionLine = (lineOneBased > 0) ? lineOneBased : -1;
    highlightCurrentLine();
    if (m_lineNumberArea) m_lineNumberArea->update();
}

/** @brief Highlights a 1-based line as the active debugger execution line (or clears if <= 0). */
void SourceCodeView::highlightExecutionLine(int lineOneBased) {
    if (lineOneBased <= 0) { setExecutionLineOnly(-1); return; }
    m_executionLine = lineOneBased;
    QTextBlock block = document()->findBlockByLineNumber(lineOneBased - 1);
    if (block.isValid()) { setTextCursor(QTextCursor(block)); ensureCursorVisible(); centerCursor(); }
    highlightCurrentLine();
    if (m_lineNumberArea) m_lineNumberArea->update();
}

/** @brief Highlights a specific 1-based execution line and scrolls into view. */
void SourceCodeView::highlightCurrentLine(int lineNumber) { highlightExecutionLine(lineNumber); }

/** @brief Selects a 1-based source line, highlights it as active, and emits sourceLineSelected. */
void SourceCodeView::selectSourceLine(int lineOneBased) {
    if (lineOneBased <= 0) return;
    highlightExecutionLine(lineOneBased); emit sourceLineSelected(lineOneBased);
}

/** @brief Adjusts viewport margins to accommodate the gutter width with zero right margin. */
void SourceCodeView::updateLineNumberAreaWidth(int) { setViewportMargins(lineNumberAreaWidth(), 0, 0, 0); }

/** @brief Handles scroll and geometry updates for the line number area. */
void SourceCodeView::updateLineNumberArea(const QRect &rect, int dy) {
    if (dy) m_lineNumberArea->scroll(0, dy);
    else m_lineNumberArea->update(0, rect.y(), m_lineNumberArea->width(), rect.height());
    if (rect.contains(viewport()->rect())) m_lineNumberArea->resize(lineNumberAreaWidth(), rect.height());
}

/** @brief Synchronizes gutter height upon editor resize. */
void SourceCodeView::resizeEvent(QResizeEvent *e) {
    QPlainTextEdit::resizeEvent(e);
    m_lineNumberArea->setGeometry(QRect(contentsRect().left(), contentsRect().top(), lineNumberAreaWidth(), contentsRect().height()));
}

/** @brief Populates the editor with source code, highlights activeLine, and syncs live clangd diagnostics. */
void SourceCodeView::setSourceCode(const QString& code, int activeLine) {
    if (!code.isEmpty() && toPlainText() != code) { QPlainTextEdit::setPlainText(code); document()->setModified(false); }
    QList<QTextEdit::ExtraSelection> extraSelections;
    if (activeLine > 0) {
        QTextBlock block = document()->findBlockByLineNumber(activeLine - 1);
        if (block.isValid()) {
            QTextEdit::ExtraSelection sel;
            sel.format.setBackground(QColor(gridlock::core::ConfigManager::instance().getSourceActiveLine()));
            sel.format.setProperty(QTextFormat::FullWidthSelection, true);
            sel.cursor = QTextCursor(block); extraSelections.append(sel);
        }
    }
    setExtraSelections(extraSelections);
    if (!m_currentFilePath.isEmpty() && !code.trimmed().isEmpty()) syncLspDocument();
}

/** @brief Restarts the hover evaluation debounce timer on mouse movement. */
void SourceCodeView::mouseMoveEvent(QMouseEvent *event) {
    QPlainTextEdit::mouseMoveEvent(event);
    m_lastMousePos = event->pos(); m_lastGlobalMousePos = event->globalPosition().toPoint();
    m_hoverTimer->start(400);
}

/** @brief Cancels active hover timers and hides tooltips when cursor exits the editor. */
void SourceCodeView::leaveEvent(QEvent *event) {
    m_hoverTimer->stop(); m_lastHoveredWord.clear();
    emit hideHoverTooltip(); QPlainTextEdit::leaveEvent(event);
}

/** @brief Slot triggered after mouse rests on a token, showing diagnostic tooltips or variable hover cards. */
void SourceCodeView::handleHoverTimeout() {
    QTextCursor cursor = cursorForPosition(m_lastMousePos);
    const int hoveredLine = cursor.blockNumber() + 1;
    // Surface clangd compiler diagnostic tooltip when hovering a wavy-underlined error or warning line.
    for (const auto& diag : m_diagnostics) {
        if (diag.line == hoveredLine && !diag.message.isEmpty()) { QToolTip::showText(m_lastGlobalMousePos, diag.message, this); break; }
    }
    cursor.select(QTextCursor::WordUnderCursor);
    const QString word = cursor.selectedText();
    static const QRegularExpression cVarRegex("^[a-zA-Z_][a-zA-Z0-9_]*$");
    if (cVarRegex.match(word).hasMatch() && word != m_lastHoveredWord) {
        m_lastHoveredWord = word; emit hoverVariableRequested(word, m_lastGlobalMousePos);
    } else if (!cVarRegex.match(word).hasMatch()) {
        if (!m_lastHoveredWord.isEmpty()) emit hideHoverTooltip();
        m_lastHoveredWord.clear();
    }
}

/**
 * @brief Handles mouse clicks in the editor body, supporting Ctrl+Click variable pinning and line selection sync.
 */
void SourceCodeView::mousePressEvent(QMouseEvent *event) {
    if (event->modifiers() & Qt::ControlModifier) {
        QTextCursor cursor = cursorForPosition(event->pos()); cursor.select(QTextCursor::WordUnderCursor);
        static const QRegularExpression cVarRegex("^[a-zA-Z_][a-zA-Z0-9_]*$");
        if (cVarRegex.match(cursor.selectedText()).hasMatch()) emit pinVariableRequested(cursor.selectedText());
    }
    m_lastHoveredWord.clear(); emit hideHoverTooltip(); QPlainTextEdit::mousePressEvent(event);
    if (event->button() == Qt::LeftButton && !(event->modifiers() & Qt::ControlModifier))
        if (const int clickedLine = textCursor().blockNumber() + 1; clickedLine > 0) emit sourceLineSelected(clickedLine);
}

/** @brief Handles double click events to pin variables to the watch list. */
void SourceCodeView::mouseDoubleClickEvent(QMouseEvent *event) {
    QTextCursor cursor = cursorForPosition(event->pos()); cursor.select(QTextCursor::WordUnderCursor);
    static const QRegularExpression cVarRegex("^[a-zA-Z_][a-zA-Z0-9_]*$");
    if (cVarRegex.match(cursor.selectedText()).hasMatch()) emit pinVariableRequested(cursor.selectedText());
    QPlainTextEdit::mouseDoubleClickEvent(event);
}

/** @brief Handles Ctrl+MouseWheel zooming of editor font size. */
void SourceCodeView::wheelEvent(QWheelEvent *event) {
    if (event->modifiers() & Qt::ControlModifier) {
        QFont f = font(); const int d = event->angleDelta().y();
        f.setPointSize(qBound(6, f.pointSize() + ((d > 0) ? 1 : ((d < 0) ? -1 : 0)), 72));
        setFont(f); updateLineNumberAreaWidth(0); event->accept();
    } else QPlainTextEdit::wheelEvent(event);
}

/** @brief Sets live paused local variable values for inline ghost rendering and repaints viewport. */
void SourceCodeView::setInlineVariableValues(const QMap<QString, QString>& varValues) { m_inlineVarValues = varValues; if (viewport()) viewport()->update(); }
/** @brief Merges a single resolved dotted/arrow member expression value into the inline ghost map. */
void SourceCodeView::mergeInlineVariableValue(const QString& key, const QString& val) { if (!key.isEmpty() && !val.isEmpty()) { m_inlineVarValues.insert(key, val); if (viewport()) viewport()->update(); } }
/** @brief Clears inline variable ghost values when resuming, stopping, or switching workspaces. */
void SourceCodeView::clearInlineVariableValues() { if (!m_inlineVarValues.isEmpty()) { m_inlineVarValues.clear(); if (viewport()) viewport()->update(); } }

/** @brief Builds a deduplicated `// var = val, ...` ghost summary for oneBasedLine (<= 4 vars, <= 96 chars). */
QString SourceCodeView::inlineGhostTextForLine(int oneBasedLine) const {
    if (m_inlineVarValues.isEmpty() || !gridlock::core::ConfigManager::instance().inlineVariableValuesEnabled()) return QString();
    const QTextBlock block = document()->findBlockByNumber(oneBasedLine - 1); if (!block.isValid()) return QString();
    // Strip string literals and trailing // comments before matching dotted/arrow expressions.
    QString line = block.text();
    static const QRegularExpression kStrRe(QStringLiteral("\"([^\"\\\\]|\\\\.)*\""));
    line.replace(kStrRe, QStringLiteral(" ")); if (const int cmt = line.indexOf(QStringLiteral("//")); cmt >= 0) line = line.left(cmt);
    // Match member chains (e.g. `cfg.mesh.dims`) and plain locals, skipping opaque `{...}` summaries.
    static const QRegularExpression kExprRe(QStringLiteral("\\b([A-Za-z_][A-Za-z0-9_]*(?:(?:\\.|->)[A-Za-z_][A-Za-z0-9_]*)*)\\b"));
    QStringList parts; QSet<QString> seen; auto it = kExprRe.globalMatch(line);
    while (it.hasNext() && parts.size() < 4) { const QString expr = it.next().captured(1), v = m_inlineVarValues.value(expr).trimmed(); if (seen.contains(expr) || v.isEmpty() || v == QLatin1String("{...}") || v.startsWith(QStringLiteral("{...")) || (v.startsWith(QLatin1Char('@')) && v.contains(QStringLiteral("{...}")))) continue; seen.insert(expr); parts.append(QStringLiteral("%1 = %2").arg(expr, v)); }
    if (parts.isEmpty()) return QString();
    QString summary = QStringLiteral("// ") + parts.join(QStringLiteral(", "));
    return (summary.size() > 96) ? (summary.left(93) + QStringLiteral("...")) : summary;
}

/** @brief Renders the editor viewport, PC drag drop-line indicator, and italic inline variable ghost values. */
void SourceCodeView::paintEvent(QPaintEvent *event) {
    QPlainTextEdit::paintEvent(event);
    if (m_draggingExecutionPc && m_dragTargetLine > 0) {
        if (const QTextBlock tb = document()->findBlockByLineNumber(m_dragTargetLine - 1); tb.isValid() && tb.isVisible()) {
            const QRectF g = blockBoundingGeometry(tb).translated(contentOffset()); QPainter p(viewport());
            p.fillRect(QRect(0, qRound(g.top()), viewport()->width(), qRound(g.height())), QColor(245, 158, 11, 32));
            p.setPen(QPen(QColor(245, 158, 11, 220), 1.5, Qt::DashLine)); p.drawLine(0, qRound(g.top()), viewport()->width(), qRound(g.top()));
        }
    }
    if (m_inlineVarValues.isEmpty() || !gridlock::core::ConfigManager::instance().inlineVariableValuesEnabled()) return;
    QPainter painter(viewport()); QFont ghostFont = font(); ghostFont.setItalic(true); painter.setFont(ghostFont);
    QColor ghostColor(gridlock::core::ConfigManager::instance().getSourceText()); ghostColor.setAlpha(140); painter.setPen(ghostColor);
    const QFontMetrics fm(font()); QTextBlock block = firstVisibleBlock(); const int vpBottom = viewport()->rect().bottom();
    // Draw ghost values only for blocks intersecting the visible viewport rect.
    while (block.isValid()) {
        const QRectF geom = blockBoundingGeometry(block).translated(contentOffset());
        if (geom.top() > vpBottom) break;
        if (block.isVisible() && geom.bottom() >= 0) if (const QString ghost = inlineGhostTextForLine(block.blockNumber() + 1); !ghost.isEmpty()) {
            const int x = qRound(geom.left()) + fm.horizontalAdvance(block.text()) + 20;
            painter.drawText(QRect(x, qRound(geom.top()), viewport()->width() - x - 4, qRound(geom.height())), Qt::AlignLeft | Qt::AlignVCenter, ghost);
        }
        block = block.next();
    }
}

/** @brief Returns member and variable completion strings for linePrefix at lineOneBased. */
QStringList SourceCodeView::editorCompletionsAtLine(int lineOneBased, const QString& linePrefix) const {
    const auto items = core::ClangdLspClient::completeDetailedAtLine(m_currentFilePath, toPlainText(), lineOneBased, linePrefix, 0);
    QStringList labels; labels.reserve(items.size()); for (const auto& it : items) labels.append(it.label); return labels;
}

/** @brief Lazily constructs the floating LspCompletionPopup and wires item selection replacement. */
void SourceCodeView::ensureCompletionPopup() {
    if (m_completionPopup) return;
    m_completionPopup = new LspCompletionPopup(this);
    // Replace any partially typed identifier suffix after '.', '->', or '::' when a completion is chosen.
    connect(m_completionPopup, &LspCompletionPopup::itemChosen, this, [this](const QString& insertText) {
        QTextCursor cur = textCursor(); const QString line = cur.block().text().left(cur.positionInBlock());
        const int sepPos = qMax(line.lastIndexOf(QLatin1Char('.')), qMax(line.lastIndexOf(QStringLiteral("->")) + 1, line.lastIndexOf(QStringLiteral("::")) + 1));
        if (sepPos >= 0 && line.length() > sepPos + 1) cur.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, line.length() - (sepPos + 1));
        cur.insertText(insertText); setTextCursor(cur);
    });
}

/** @brief Triggers non-blocking floating LSP IntelliSense below cursorRect() at lineOneBased. */
void SourceCodeView::triggerCompletionPopupAtCursor(const QString& linePrefix, int lineOneBased) {
    // Use fast 0ms structural completion only on compact buffers (<= 500 blocks) to avoid GUI stalls.
    const auto items = (document()->blockCount() <= 500) ? core::ClangdLspClient::completeDetailedAtLine(m_currentFilePath, toPlainText(), lineOneBased, linePrefix, 0) : QVector<core::ClangdLspClient::LspCompletionItem>{};
    if (!items.isEmpty()) { ensureCompletionPopup(); m_completionPopup->setItems(items); m_completionPopup->positionBelow(mapToGlobal(cursorRect().bottomLeft() + QPoint(0, 4)), 320); m_completionPopup->show(); }
    else if (m_currentFilePath.isEmpty() && m_completionPopup) m_completionPopup->hide();
    // Dispatch non-blocking real-document clangd completion when backed by a workspace file path.
    if (!m_currentFilePath.isEmpty()) {
        const QTextCursor cur = textCursor(); const int zeroCol = (cur.blockNumber() + 1 == lineOneBased) ? cur.positionInBlock() : linePrefix.length();
        m_latestCompletionSeq = core::ClangdLspClient::instance().requestDocumentCompletionAsync(m_currentFilePath, toPlainText(), qMax(0, lineOneBased - 1), qMax(0, zeroCol), linePrefix.right(1));
    }
}

/** @brief Saves the current editor document to disk and clears its modified state. */
bool SourceCodeView::saveToFile(const QString& filePath) {
    const QString target = filePath.isEmpty() ? m_currentFilePath : filePath;
    if (target.isEmpty()) return false;
    QFile file(target); if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream(&file) << toPlainText(); file.close(); document()->setModified(false); refreshGitDiffGutter(); return true;
}

/** @brief Computes the enclosing C/C++ lexical scope breadcrumb ("filename > Class/Struct > function"). */
QString SourceCodeView::currentScopeBreadcrumb() const {
    const QString fileLabel = m_currentFilePath.isEmpty() ? QStringLiteral("buffer.cpp") : QFileInfo(m_currentFilePath).fileName();
    const int targetBlock = textCursor().blockNumber();
    static const QRegularExpression typeRe(QStringLiteral("^\\s*(?:struct|class)\\s+([A-Za-z_][A-Za-z0-9_]*)"));
    static const QRegularExpression funcRe(QStringLiteral("^\\s*(?:[A-Za-z_][A-Za-z0-9_:<>*&\\s]+\\s+)?([A-Za-z_][A-Za-z0-9_:]*)\\s*\\([^;]*\\)\\s*(?:const\\s*)?\\{?\\s*$"));
    QString activeType, activeFunc;
    for (QTextBlock b = document()->begin(); b.isValid() && b.blockNumber() <= targetBlock; b = b.next()) {
        const QString line = b.text();
        if (const auto tm = typeRe.match(line); tm.hasMatch()) activeType = tm.captured(1);
        if (const auto fm = funcRe.match(line); fm.hasMatch()) {
            const QString fn = fm.captured(1);
            if (fn != QStringLiteral("if") && fn != QStringLiteral("for") && fn != QStringLiteral("while") && fn != QStringLiteral("switch")) activeFunc = fn;
        }
    }
    QStringList parts{fileLabel};
    if (!activeType.isEmpty()) parts.append(activeType);
    if (!activeFunc.isEmpty()) parts.append(activeFunc);
    return parts.join(QStringLiteral(" > "));
}

/** @brief Jumps the editor cursor to the declaration/definition of the symbol under the cursor (F12). */
bool SourceCodeView::triggerGoToDefinition() {
    const QTextCursor cur = textCursor();
    const auto loc = !m_currentFilePath.isEmpty()
                         ? core::ClangdLspClient::instance().findDefinition(m_currentFilePath, toPlainText(), cur.blockNumber(), cur.positionInBlock())
                         : core::ClangdLspClient::instance().findDefinition(toPlainText(), cur.blockNumber() + 1, cur.positionInBlock());
    if (loc.line <= 0) return false;
    // Dispatch cross-file jump request when clangd resolves a declaration in a sibling header or TU.
    if (!m_currentFilePath.isEmpty() && !loc.filePath.isEmpty() && QFileInfo::exists(loc.filePath)) {
        const QString curCanon = QFileInfo::exists(m_currentFilePath) ? QFileInfo(m_currentFilePath).canonicalFilePath() : QFileInfo(m_currentFilePath).absoluteFilePath();
        const QString dstCanon = QFileInfo(loc.filePath).canonicalFilePath();
        if (!dstCanon.isEmpty() && dstCanon != curCanon) { emit definitionJumpRequested(dstCanon, loc.line, loc.column); return true; }
    }
    QTextBlock b = document()->findBlockByNumber(loc.line - 1); if (!b.isValid()) return false;
    QTextCursor dest(b); dest.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, qMax(0, loc.column));
    setTextCursor(dest); ensureCursorVisible(); return true;
}

/** @brief Parses unified Git diff (`git diff -U0`) hunk headers into 1-based line change states. */
QHash<int, SourceCodeView::GitLineState> SourceCodeView::parseUnifiedGitDiff(const QString& unifiedDiff) {
    QHash<int, GitLineState> states;
    // Match unified diff hunk headers of the form @@ -oldStart[,oldCount] +newStart[,newCount] @@.
    static const QRegularExpression hunkRe(QStringLiteral("^@@\\s+-(\\d+)(?:,(\\d+))?\\s+\\+(\\d+)(?:,(\\d+))?\\s+@@"), QRegularExpression::MultilineOption);
    auto it = hunkRe.globalMatch(unifiedDiff);
    while (it.hasNext()) {
        const auto m = it.next();
        const int oldCount = m.captured(2).isEmpty() ? 1 : m.captured(2).toInt(), newStart = m.captured(3).toInt(), newCount = m.captured(4).isEmpty() ? 1 : m.captured(4).toInt();
        // Classify hunk range as Added, Modified, or Deleted based on oldCount and newCount.
        if (newCount > 0 && oldCount == 0) { for (int i = 0; i < newCount; ++i) states.insert(newStart + i, GitLineState::Added); }
        else if (newCount > 0 && oldCount > 0) { for (int i = 0; i < newCount; ++i) states.insert(newStart + i, GitLineState::Modified); }
        else if (newCount == 0 && oldCount > 0) { states.insert(qMax(1, newStart), GitLineState::Deleted); }
    }
    return states;
}

/** @brief Sets the active per-line Git diff gutter states and schedules a gutter repaint. */
void SourceCodeView::setGitLineStates(const QHash<int, GitLineState>& states) {
    m_gitLineStates = states;
    if (m_lineNumberArea) m_lineNumberArea->update();
}

/** @brief Asynchronously refreshes live Git working-tree and unsaved-buffer diff gutter states on QThreadPool. */
void SourceCodeView::refreshGitDiffGutter() {
    if (m_gitDiffTimer) m_gitDiffTimer->stop();
    const quint64 seq = ++m_gitDiffSeq; const QString filePath = m_currentFilePath;
    if (filePath.isEmpty()) { setGitLineStates({}); return; }
    const QString bufferText = toPlainText(); const bool dirty = document() && document()->isModified();
    QPointer<SourceCodeView> self(this);
    // Run external git subprocesses on QThreadPool and marshal parsed line states back via QueuedConnection.
    QThreadPool::globalInstance()->start([self, seq, filePath, bufferText, dirty]() {
        const QString diff = computeUnifiedGitDiffForBuffer(filePath, bufferText, dirty);
        const auto states = diff.isEmpty() ? QHash<int, GitLineState>{} : parseUnifiedGitDiff(diff);
        if (self) QMetaObject::invokeMethod(self, [self, seq, states]() { if (self && self->m_gitDiffSeq.load() == seq) self->setGitLineStates(states); }, Qt::QueuedConnection);
    });
}
/** @brief Renames all AST references to the symbol under the cursor (Shift+F6) via clangd `WorkspaceEdit`. */
bool SourceCodeView::renameSymbolAtCursor(const QString& newName) {
    const QTextCursor cur = textCursor();
    // Prefer clangd AST WorkspaceEdit when backed by a real workspace file on disk.
    if (!m_currentFilePath.isEmpty() && QFileInfo::exists(m_currentFilePath)) {
        const auto edits = core::ClangdLspClient::instance().renameSymbol(m_currentFilePath, toPlainText(), cur.blockNumber(), cur.positionInBlock(), newName);
        const QString curCanon = QFileInfo(m_currentFilePath).canonicalFilePath();
        if (edits.contains(curCanon) && edits.value(curCanon) != toPlainText()) { setPlainText(edits.value(curCanon)); document()->setModified(true); return true; }
    }
    const QString updated = core::ClangdLspClient::instance().renameSymbolInSource(toPlainText(), cur.blockNumber() + 1, cur.positionInBlock(), newName);
    if (updated == toPlainText()) return false;
    setPlainText(updated); document()->setModified(true); return true;
}
/**
 * @brief Handles key press events for non-blocking LSP autocomplete navigation, F12 Go-to-Definition, and invocation.
 */
void SourceCodeView::keyPressEvent(QKeyEvent *event) {
    if (!event) return;
    if (event->key() == Qt::Key_F12 && !(event->modifiers() & Qt::ShiftModifier)) { triggerGoToDefinition(); event->accept(); return; }
    // Forward Up/Down/Enter/Escape to the active clangd completion popup before normal text insertion.
    if (m_completionPopup && m_completionPopup->isVisible()) {
        if (event->key() == Qt::Key_Down) { m_completionPopup->selectNext(); event->accept(); return; }
        if (event->key() == Qt::Key_Up) { m_completionPopup->selectPrevious(); event->accept(); return; }
        if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter || event->key() == Qt::Key_Tab) && m_completionPopup->selectCurrent()) { event->accept(); return; }
        if (event->key() == Qt::Key_Escape) { m_completionPopup->hide(); event->accept(); return; }
    }
    if ((event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_Space) {
        QTextCursor cur = textCursor(); triggerCompletionPopupAtCursor(cur.block().text().left(cur.positionInBlock()), cur.blockNumber() + 1); event->accept(); return;
    }
    QPlainTextEdit::keyPressEvent(event); const QString txt = event->text();
    // Trigger immediate async completion on member access punctuation and debounce identifier filtering (75ms).
    if (txt == QStringLiteral(".") || txt == QStringLiteral(">") || txt == QStringLiteral(":")) {
        QTextCursor cur = textCursor(); triggerCompletionPopupAtCursor(cur.block().text().left(cur.positionInBlock()), cur.blockNumber() + 1);
    } else if (m_completionPopup && m_completionPopup->isVisible() && !txt.isEmpty() && (txt.at(0).isLetterOrNumber() || txt.at(0) == QLatin1Char('_')) && m_completionTimer) m_completionTimer->start(75);
}

}  // namespace gridlock::ui
