/**
 * @file SourceCodeViewInteraction.cpp
 * @brief Interactive mouse/gutter/context-menu handling, inline ghost painting, LSP navigation, and Git diffing for SourceCodeView.
 *
 * Implements context menus, ConditionalBreakpointDialog popovers, execution PC drag-and-drop,
 * hover variable inspection, inline paused variable ghost values, clangd completion/F12/rename
 * workflows, and background Git diff hunk parsing.
 */

#include "SourceCodeView.hpp"

#include <memory>

#include <QtWidgets>

#include "../../core/managers/ConfigManager.hpp"
#include "../../core/utils/ClangdLspClient.hpp"
#include "../dialogs/debugger/ConditionalBreakpointDialog.hpp"

namespace gridlock::ui {

namespace {
bool isCppKeywordToken(const QString& tok) {
    static const QSet<QString> kKw = {
        QStringLiteral("auto"), QStringLiteral("bool"), QStringLiteral("break"), QStringLiteral("case"),
        QStringLiteral("catch"), QStringLiteral("char"), QStringLiteral("class"), QStringLiteral("const"),
        QStringLiteral("constexpr"), QStringLiteral("continue"), QStringLiteral("default"), QStringLiteral("delete"),
        QStringLiteral("do"), QStringLiteral("double"), QStringLiteral("else"), QStringLiteral("enum"),
        QStringLiteral("explicit"), QStringLiteral("extern"), QStringLiteral("false"), QStringLiteral("float"),
        QStringLiteral("for"), QStringLiteral("if"), QStringLiteral("include"), QStringLiteral("inline"),
        QStringLiteral("int"), QStringLiteral("long"), QStringLiteral("namespace"), QStringLiteral("new"),
        QStringLiteral("nullptr"), QStringLiteral("override"), QStringLiteral("private"), QStringLiteral("protected"),
        QStringLiteral("public"), QStringLiteral("return"), QStringLiteral("short"), QStringLiteral("signed"),
        QStringLiteral("sizeof"), QStringLiteral("static"), QStringLiteral("std"), QStringLiteral("struct"),
        QStringLiteral("switch"), QStringLiteral("template"), QStringLiteral("this"), QStringLiteral("true"),
        QStringLiteral("typedef"), QStringLiteral("typename"), QStringLiteral("unsigned"), QStringLiteral("using"),
        QStringLiteral("virtual"), QStringLiteral("void"), QStringLiteral("volatile"), QStringLiteral("while")};
    return kKw.contains(tok);
}

QString maskCommentsAndStringsPreservingOffsets(const QString& line) {
    QString out = line;
    bool inStr = false, inChr = false;
    for (int i = 0; i < out.size(); ++i) {
        const QChar c = out[i];
        if (!inStr && !inChr && c == QLatin1Char('/') && i + 1 < out.size() && out[i + 1] == QLatin1Char('/')) {
            for (int j = i; j < out.size(); ++j) out[j] = QLatin1Char(' ');
            break;
        }
        if (!inChr && c == QLatin1Char('"')) { inStr = !inStr; out[i] = QLatin1Char(' '); continue; }
        if (!inStr && c == QLatin1Char('\'')) { inChr = !inChr; out[i] = QLatin1Char(' '); continue; }
        if (inStr || inChr) {
            if (c == QLatin1Char('\\') && i + 1 < out.size()) { out[i] = QLatin1Char(' '); out[++i] = QLatin1Char(' '); }
            else out[i] = QLatin1Char(' ');
        }
    }
    return out;
}

// Extracts variable/member expression at colInBlock, stopping before any segment immediately followed by \s*\(.
QString extractMemberExprAtColumn(const QString& lineText, int colInBlock, bool strictHover) {
    if (lineText.isEmpty() || colInBlock < 0 || colInBlock > lineText.size()) return QString();
    const QString clean = maskCommentsAndStringsPreservingOffsets(lineText);
    if (strictHover && (colInBlock >= clean.size() || clean[colInBlock].isSpace())) return QString();

    static const QRegularExpression kFullChainRe(
        QStringLiteral(R"(\b[A-Za-z_][A-Za-z0-9_]*(?:\[[^\]]*\])*(?:(?:\.|->)[A-Za-z_][A-Za-z0-9_]*(?:\[[^\]]*\])*)*)"));
    static const QRegularExpression kCallSuffixRe(QStringLiteral(R"(^\s*\()"));
    static const QRegularExpression kLastSegRe(QStringLiteral(R"((?:\.|->)[A-Za-z_][A-Za-z0-9_]*(?:\[[^\]]*\])*$)"));
    static const QRegularExpression kSegRe(
        QStringLiteral(R"((?:^|\.|->)[A-Za-z_][A-Za-z0-9_]*(?:\[[^\]]*\])*)"));

    for (auto it = kFullChainRe.globalMatch(clean); it.hasNext();) {
        const auto m = it.next();
        const int start = m.capturedStart(0), end = m.capturedEnd(0);
        if (colInBlock < start || (strictHover ? (colInBlock >= end) : (colInBlock > end))) continue;

        QString expr = lineText.mid(start, end - start);
        const bool endsAtCall = kCallSuffixRe.match(clean.mid(end)).hasMatch();
        if (endsAtCall) {
            const auto lastSeg = kLastSegRe.match(expr);
            if (!lastSeg.hasMatch()) return QString();
            expr.truncate(lastSeg.capturedStart(0));
        } else if (strictHover) {
            const int relCol = colInBlock - start;
            for (auto sit = kSegRe.globalMatch(expr); sit.hasNext();) {
                const auto sm = sit.next();
                if (relCol < sm.capturedEnd(0)) { expr.truncate(sm.capturedEnd(0)); break; }
            }
        }

        bool isNum = false;
        expr.toDouble(&isNum);
        const QString rootIdent = expr.section(QRegularExpression(QStringLiteral(R"([.\-\[])")), 0, 0);
        if (isNum || expr.isEmpty() || isCppKeywordToken(expr) || isCppKeywordToken(rootIdent)) return QString();
        return expr;
    }
    return QString();
}
}  // namespace

/** @brief Determines whether the standard editor context menu may open at pos (suppressing gutter regions). */
bool SourceCodeView::shouldOpenEditorContextMenuAt(const QPoint& pos) const {
    return pos.x() >= lineNumberAreaWidth() && !(m_lineNumberArea && m_lineNumberArea->geometry().contains(pos));
}

QString SourceCodeView::detectStructOrClassAtLine(const QString& lineText) {
    static const QRegularExpression kTypeRe(QStringLiteral(R"(^\s*(?:template\s*<[^>]*>\s*)?(?:typedef\s+)?(?:struct|class)\s+(?:\[\[[^\]]*\]\]\s*)?([A-Za-z_][A-Za-z0-9_]*)\b)"));
    const auto m = kTypeRe.match(lineText);
    return m.hasMatch() ? m.captured(1) : QString();
}

QMenu* SourceCodeView::createEditorContextMenuAt(const QPoint& editorPos) {
    if (!shouldOpenEditorContextMenuAt(editorPos)) return nullptr;
    const QTextCursor posCur = cursorForPosition(editorPos); const int clickedLine = qMax(1, posCur.blockNumber() + 1);
    const QString lineText = posCur.block().text();
    const QString selected = textCursor().hasSelection() ? textCursor().selectedText().replace(QChar::ParagraphSeparator, QLatin1Char(' ')).trimmed() : QString();
    QString exprTok = selected;
    if (exprTok.isEmpty() && !lineText.isEmpty()) {
        exprTok = extractMemberExprAtColumn(lineText, posCur.positionInBlock(), false);
    }
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
    if (!calcTok.isEmpty()) menu->addAction(QStringLiteral("Open Selection in Programmer's Calculator"), this, [this, calcTok]() { emit sendToCalculatorRequested(calcTok); });
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
    if (m_editorPluginMenuHook) {
        const int clickedCol = qMax(1, posCur.positionInBlock() + 1);
        m_editorPluginMenuHook(menu, m_currentFilePath, clickedLine, clickedCol, exprTok, selected);
    }
    return menu;
}

QMenu* SourceCodeView::createBreakpointContextMenuAtLine(int lineOneBased, QWidget* menuParent) {
    auto* gMenu = new QMenu(menuParent ? menuParent : this);
    if (lineOneBased <= 0) return gMenu;
    if (hasBreakpoint(lineOneBased)) {
        const bool en = isBreakpointEnabledForLine(lineOneBased);
        gMenu->addAction(en ? QStringLiteral("Disable Breakpoint") : QStringLiteral("Enable Breakpoint"),
                         this, [this, lineOneBased, en]() { setBreakpointEnabled(lineOneBased, !en); });
    }
    gMenu->addAction(QStringLiteral("Insert Temporary Breakpoint (tbreak)"),
                     this, [this, lineOneBased]() { setTemporaryBreakpoint(lineOneBased, -1); });
    if (hasBreakpoint(lineOneBased)) {
        gMenu->addAction(QStringLiteral("Remove Breakpoint"), this, [this, lineOneBased]() { toggleBreakpoint(lineOneBased); });
    }
    if (m_breakpointPluginMenuHook) {
        m_breakpointPluginMenuHook(gMenu, m_currentFilePath, lineOneBased, hasBreakpoint(lineOneBased) ? lineOneBased : -1);
    }
    return gMenu;
}

bool SourceCodeView::triggerContextActionAtLine(int lineOneBased, const QString& actionSubstring) {
    QTextBlock b = document()->findBlockByNumber(qMax(0, lineOneBased - 1)); if (!b.isValid()) return false;
    if (!textCursor().hasSelection()) { QTextCursor c(b); setTextCursor(c); }
    QPoint pos = cursorRect(QTextCursor(b)).center(); pos.setX(qMax(pos.x(), lineNumberAreaWidth() + 12)); pos.setY(qMax(4, pos.y()));
    std::unique_ptr<QMenu> menu(createEditorContextMenuAt(pos)); if (!menu) return false;
    for (QAction* act : menu->actions()) if (act && !act->isSeparator() && act->text().contains(actionSubstring, Qt::CaseInsensitive)) { act->trigger(); return true; }
    return false;
}

void SourceCodeView::contextMenuEvent(QContextMenuEvent* event) {
    if (!event) return;
    QPoint editorPos = event->pos();
    if (event->reason() == QContextMenuEvent::Mouse && !event->globalPos().isNull()) { const QPoint mapped = mapFromGlobal(event->globalPos()); if (rect().contains(mapped)) editorPos = mapped; }
    if (QMenu* menu = createEditorContextMenuAt(editorPos)) { menu->exec(event->globalPos()); delete menu; } else event->accept();
}

void SourceCodeView::handleBreakpointPopoverResult(int lineOneBased, bool wasPreviouslySet, bool accepted, bool enabledChecked, const QString& condition, const QString& suspendScope) {
    handleBreakpointPopoverResult(lineOneBased, wasPreviouslySet, accepted, enabledChecked, condition, suspendScope, !enabledChecked);
}

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
void SourceCodeView::lineNumberAreaMousePressEvent(QMouseEvent* event) {
    if (!event || (event->button() != Qt::LeftButton && event->button() != Qt::RightButton)) return;
    event->accept(); int top = 0, bottom = 0; const int lineNum = gutterLineAtY(event->pos().y(), &top, &bottom);
    if (lineNum <= 0) return;
    if (event->button() == Qt::RightButton && (event->modifiers() & Qt::ShiftModifier)) {
        std::unique_ptr<QMenu> gMenu(createBreakpointContextMenuAtLine(lineNum, this));
        gMenu->exec(m_lineNumberArea->mapToGlobal(event->pos())); return;
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
void SourceCodeView::lineNumberAreaMouseMoveEvent(QMouseEvent* event) {
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

void SourceCodeView::setExecutionLineOnly(int lineOneBased) {
    m_executionLine = (lineOneBased > 0) ? lineOneBased : -1;
    highlightCurrentLine(); if (m_lineNumberArea) m_lineNumberArea->update();
}

void SourceCodeView::highlightExecutionLine(int lineOneBased) {
    if (lineOneBased <= 0) { setExecutionLineOnly(-1); return; }
    m_executionLine = lineOneBased;
    if (QTextBlock block = document()->findBlockByLineNumber(lineOneBased - 1); block.isValid()) { setTextCursor(QTextCursor(block)); ensureCursorVisible(); centerCursor(); }
    highlightCurrentLine(); if (m_lineNumberArea) m_lineNumberArea->update();
}

void SourceCodeView::highlightCurrentLine(int lineNumber) { highlightExecutionLine(lineNumber); }

void SourceCodeView::selectSourceLine(int lineOneBased) {
    if (lineOneBased > 0) { highlightExecutionLine(lineOneBased); emit sourceLineSelected(lineOneBased); }
}

void SourceCodeView::updateLineNumberAreaWidth(int) { setViewportMargins(lineNumberAreaWidth(), 0, 0, 0); }

void SourceCodeView::updateLineNumberArea(const QRect& rect, int dy) {
    if (dy) m_lineNumberArea->scroll(0, dy);
    else m_lineNumberArea->update(0, rect.y(), m_lineNumberArea->width(), rect.height());
    if (rect.contains(viewport()->rect())) m_lineNumberArea->resize(lineNumberAreaWidth(), rect.height());
}

void SourceCodeView::resizeEvent(QResizeEvent* e) {
    QPlainTextEdit::resizeEvent(e);
    m_lineNumberArea->setGeometry(QRect(contentsRect().left(), contentsRect().top(), lineNumberAreaWidth(), contentsRect().height()));
}

void SourceCodeView::setSourceCode(const QString& code, int activeLine) {
    if (code.size() >= kLargeFileThresholdBytes && !m_largeFileMode) setLargeFileMode(true);
    if (!code.isEmpty() && (m_largeFileMode || toPlainText() != code)) { QPlainTextEdit::setPlainText(code); document()->setModified(false); }
    QList<QTextEdit::ExtraSelection> extraSelections;
    if (activeLine > 0) {
        if (QTextBlock block = document()->findBlockByLineNumber(activeLine - 1); block.isValid()) {
            QTextEdit::ExtraSelection sel;
            sel.format.setBackground(QColor(gridlock::core::ConfigManager::instance().getSourceActiveLine()));
            sel.format.setProperty(QTextFormat::FullWidthSelection, true);
            sel.cursor = QTextCursor(block); extraSelections.append(sel);
        }
    }
    setExtraSelections(extraSelections);
    if (!m_largeFileMode && !m_currentFilePath.isEmpty() && !code.trimmed().isEmpty()) syncLspDocument();
}

void SourceCodeView::mouseMoveEvent(QMouseEvent* event) {
    QPlainTextEdit::mouseMoveEvent(event);
    m_lastMousePos = event->pos(); m_lastGlobalMousePos = event->globalPosition().toPoint();
    m_hoverTimer->start(400);
}

void SourceCodeView::leaveEvent(QEvent* event) {
    m_hoverTimer->stop(); m_lastHoveredWord.clear();
    emit hideHoverTooltip(); QPlainTextEdit::leaveEvent(event);
}

void SourceCodeView::handleHoverTimeout() {
    QTextCursor cursor = cursorForPosition(m_lastMousePos);
    const int hoveredLine = cursor.blockNumber() + 1;
    for (const auto& diag : m_diagnostics) {
        if (diag.line == hoveredLine && !diag.message.isEmpty()) { QToolTip::showText(m_lastGlobalMousePos, diag.message, this); break; }
    }
    for (const auto& ann : m_pluginAnnotations) {
        if (ann.line == hoveredLine && (!ann.detailText.isEmpty() || !ann.badgeText.isEmpty())) {
            const QString tip = ann.detailText.isEmpty() ? ann.badgeText : QStringLiteral("[%1] %2").arg(ann.badgeText, ann.detailText);
            QToolTip::showText(m_lastGlobalMousePos, tip, this);
            break;
        }
    }
    const QString word = extractMemberExprAtColumn(cursor.block().text(), cursor.positionInBlock(), true);
    if (!word.isEmpty() && word != m_lastHoveredWord) {
        m_lastHoveredWord = word; emit hoverVariableRequested(word, m_lastGlobalMousePos);
    } else if (word.isEmpty()) {
        if (!m_lastHoveredWord.isEmpty()) emit hideHoverTooltip();
        m_lastHoveredWord.clear();
    }
}

void SourceCodeView::mousePressEvent(QMouseEvent* event) {
    if (event->modifiers() & Qt::ControlModifier) {
        QTextCursor cursor = cursorForPosition(event->pos()); cursor.select(QTextCursor::WordUnderCursor);
        static const QRegularExpression cVarRegex("^[a-zA-Z_][a-zA-Z0-9_]*$");
        if (cVarRegex.match(cursor.selectedText()).hasMatch()) emit pinVariableRequested(cursor.selectedText());
    }
    m_lastHoveredWord.clear(); emit hideHoverTooltip(); QPlainTextEdit::mousePressEvent(event);
    if (event->button() == Qt::LeftButton && !(event->modifiers() & Qt::ControlModifier))
        if (const int clickedLine = textCursor().blockNumber() + 1; clickedLine > 0) emit sourceLineSelected(clickedLine);
}

void SourceCodeView::mouseDoubleClickEvent(QMouseEvent* event) {
    QTextCursor cursor = cursorForPosition(event->pos()); cursor.select(QTextCursor::WordUnderCursor);
    static const QRegularExpression cVarRegex("^[a-zA-Z_][a-zA-Z0-9_]*$");
    if (cVarRegex.match(cursor.selectedText()).hasMatch()) emit pinVariableRequested(cursor.selectedText());
    QPlainTextEdit::mouseDoubleClickEvent(event);
}

void SourceCodeView::wheelEvent(QWheelEvent* event) {
    if (event->modifiers() & Qt::ControlModifier) {
        QFont f = font(); const int d = event->angleDelta().y();
        f.setPointSize(qBound(6, f.pointSize() + ((d > 0) ? 1 : ((d < 0) ? -1 : 0)), 72));
        setFont(f); updateTabStopDistance(); updateLineNumberAreaWidth(0); event->accept();
    } else QPlainTextEdit::wheelEvent(event);
}

void SourceCodeView::setInlineVariableValues(const QMap<QString, QString>& varValues) { m_inlineVarValues = varValues; if (viewport()) viewport()->update(); }
void SourceCodeView::mergeInlineVariableValue(const QString& key, const QString& val) { if (!key.isEmpty() && !val.isEmpty()) { m_inlineVarValues.insert(key, val); if (viewport()) viewport()->update(); } }
void SourceCodeView::clearInlineVariableValues() { if (!m_inlineVarValues.isEmpty()) { m_inlineVarValues.clear(); if (viewport()) viewport()->update(); } }

QString SourceCodeView::inlineGhostTextForLine(int oneBasedLine) const {
    if (m_inlineVarValues.isEmpty() || !gridlock::core::ConfigManager::instance().inlineVariableValuesEnabled()) return QString();
    const QTextBlock block = document()->findBlockByNumber(oneBasedLine - 1); if (!block.isValid()) return QString();
    QString line = block.text();
    static const QRegularExpression kStrRe(QStringLiteral("\"([^\"\\\\]|\\\\.)*\""));
    line.replace(kStrRe, QStringLiteral(" ")); if (const int cmt = line.indexOf(QStringLiteral("//")); cmt >= 0) line = line.left(cmt);
    static const QRegularExpression kExprRe(
        QStringLiteral(R"(\b([A-Za-z_][A-Za-z0-9_]*\b(?!\s*\()(?:(?:\.|->)[A-Za-z_][A-Za-z0-9_]*\b(?!\s*\())*))"));
    QStringList parts; QSet<QString> seen; auto it = kExprRe.globalMatch(line);
    while (it.hasNext() && parts.size() < 4) { const QString expr = it.next().captured(1), v = m_inlineVarValues.value(expr).trimmed(); if (seen.contains(expr) || v.isEmpty() || v == QLatin1String("{...}") || v.startsWith(QStringLiteral("{...")) || (v.startsWith(QLatin1Char('@')) && v.contains(QStringLiteral("{...}")))) continue; seen.insert(expr); parts.append(QStringLiteral("%1 = %2").arg(expr, v)); }
    if (parts.isEmpty()) return QString();
    QString summary = QStringLiteral("// ") + parts.join(QStringLiteral(", "));
    return (summary.size() > 96) ? (summary.left(93) + QStringLiteral("...")) : summary;
}

/** @brief Renders the editor viewport, PC drag drop-line indicator, and italic inline variable ghost values. */
void SourceCodeView::paintEvent(QPaintEvent* event) {
    QPlainTextEdit::paintEvent(event);
    if (m_draggingExecutionPc && m_dragTargetLine > 0) {
        if (const QTextBlock tb = document()->findBlockByLineNumber(m_dragTargetLine - 1); tb.isValid() && tb.isVisible()) {
            const QRectF g = blockBoundingGeometry(tb).translated(contentOffset()); QPainter p(viewport());
            p.fillRect(QRect(0, qRound(g.top()), viewport()->width(), qRound(g.height())), QColor(245, 158, 11, 32));
            p.setPen(QPen(QColor(245, 158, 11, 220), 1.5, Qt::DashLine)); p.drawLine(0, qRound(g.top()), viewport()->width(), qRound(g.top()));
        }
    }
    const bool drawGhostVars = !m_inlineVarValues.isEmpty() && gridlock::core::ConfigManager::instance().inlineVariableValuesEnabled();
    if (!drawGhostVars && m_pluginAnnotations.isEmpty()) return;
    QPainter painter(viewport()); painter.setRenderHint(QPainter::Antialiasing, true);
    QFont ghostFont = font(); ghostFont.setItalic(true);
    QFont badgeFont = font(); badgeFont.setBold(true); badgeFont.setPointSize(qMax(8, font().pointSize() - 1));
    QColor ghostColor(gridlock::core::ConfigManager::instance().getSourceText()); ghostColor.setAlpha(140);
    const QFontMetrics fm(font()), bfm(badgeFont); QTextBlock block = firstVisibleBlock(); const int vpBottom = viewport()->rect().bottom();
    while (block.isValid()) {
        const QRectF geom = blockBoundingGeometry(block).translated(contentOffset());
        if (geom.top() > vpBottom) break;
        if (block.isVisible() && geom.bottom() >= 0) {
            const int lineOneBased = block.blockNumber() + 1;
            int curX = qRound(geom.left()) + fm.horizontalAdvance(block.text()) + 20;
            if (drawGhostVars) if (const QString ghost = inlineGhostTextForLine(lineOneBased); !ghost.isEmpty()) {
                painter.setFont(ghostFont); painter.setPen(ghostColor); painter.setBrush(Qt::NoBrush);
                painter.drawText(QRect(curX, qRound(geom.top()), viewport()->width() - curX - 4, qRound(geom.height())), Qt::AlignLeft | Qt::AlignVCenter, ghost);
                curX += QFontMetrics(ghostFont).horizontalAdvance(ghost) + 14;
            }
            for (const auto& ann : m_pluginAnnotations) {
                if (ann.line != lineOneBased || curX >= viewport()->width() - 24) continue;
                const QString tok = ann.colorToken.trimmed().toLower();
                const QColor ac = (tok == QStringLiteral("error")) ? QColor(QStringLiteral("#ef4444"))
                                  : (tok == QStringLiteral("warning")) ? QColor(QStringLiteral("#f59e0b"))
                                  : (QColor::isValidColorName(ann.colorToken) ? QColor(ann.colorToken) : QColor(QStringLiteral("#38bdf8")));
                const int pillW = bfm.horizontalAdvance(ann.badgeText) + 12;
                const int pillH = qMax(14, qRound(geom.height()) - 4);
                const QRect pillRect(curX, qRound(geom.top()) + (qRound(geom.height()) - pillH) / 2, pillW, pillH);
                QColor fill = ac; fill.setAlpha(42);
                painter.setPen(QPen(ac, 1.0)); painter.setBrush(fill); painter.drawRoundedRect(pillRect, 3.0, 3.0);
                painter.setFont(badgeFont); painter.setPen(ac); painter.drawText(pillRect, Qt::AlignCenter, ann.badgeText);
                curX += pillW + 8;
                if (!ann.detailText.isEmpty() && curX < viewport()->width() - 16) {
                    painter.setFont(ghostFont); QColor dc = ac; dc.setAlpha(205); painter.setPen(dc); painter.setBrush(Qt::NoBrush);
                    const QString elided = QFontMetrics(ghostFont).elidedText(ann.detailText, Qt::ElideRight, viewport()->width() - curX - 6);
                    painter.drawText(QRect(curX, qRound(geom.top()), viewport()->width() - curX - 6, qRound(geom.height())), Qt::AlignLeft | Qt::AlignVCenter, elided);
                    curX += QFontMetrics(ghostFont).horizontalAdvance(elided) + 12;
                }
            }
        }
        block = block.next();
    }
}

QStringList SourceCodeView::editorCompletionsAtLine(int lineOneBased, const QString& linePrefix) const {
    const auto items = core::ClangdLspClient::completeDetailedAtLine(m_currentFilePath, toPlainText(), lineOneBased, linePrefix, 0);
    QStringList labels; labels.reserve(items.size()); for (const auto& it : items) labels.append(it.label); return labels;
}

void SourceCodeView::ensureCompletionPopup() {
    if (m_completionPopup) return;
    m_completionPopup = new LspCompletionPopup(this);
    connect(m_completionPopup, &LspCompletionPopup::itemChosen, this, [this](const QString& insertText) {
        QTextCursor cur = textCursor(); const QString line = cur.block().text().left(cur.positionInBlock());
        const int sepPos = qMax(line.lastIndexOf(QLatin1Char('.')), qMax(line.lastIndexOf(QStringLiteral("->")) + 1, line.lastIndexOf(QStringLiteral("::")) + 1));
        if (sepPos >= 0 && line.length() > sepPos + 1) cur.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, line.length() - (sepPos + 1));
        cur.insertText(insertText); setTextCursor(cur);
    });
}

void SourceCodeView::triggerCompletionPopupAtCursor(const QString& linePrefix, int lineOneBased) {
    if (m_largeFileMode) return;
    const auto items = (document()->blockCount() <= 500) ? core::ClangdLspClient::completeDetailedAtLine(m_currentFilePath, toPlainText(), lineOneBased, linePrefix, 0) : QVector<core::ClangdLspClient::LspCompletionItem>{};
    if (!items.isEmpty()) { ensureCompletionPopup(); m_completionPopup->setItems(items); m_completionPopup->positionBelow(mapToGlobal(cursorRect().bottomLeft() + QPoint(0, 4)), 320); m_completionPopup->show(); }
    else if (m_currentFilePath.isEmpty() && m_completionPopup) m_completionPopup->hide();
    if (!m_currentFilePath.isEmpty()) {
        const QTextCursor cur = textCursor(); const int zeroCol = (cur.blockNumber() + 1 == lineOneBased) ? cur.positionInBlock() : linePrefix.length();
        m_latestCompletionSeq = core::ClangdLspClient::instance().requestDocumentCompletionAsync(m_currentFilePath, toPlainText(), qMax(0, lineOneBased - 1), qMax(0, zeroCol), linePrefix.right(1));
    }
}

bool SourceCodeView::saveToFile(const QString& filePath) {
    const QString target = filePath.isEmpty() ? m_currentFilePath : filePath;
    if (target.isEmpty()) return false;
    QFile file(target); if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream(&file) << toPlainText(); file.close(); document()->setModified(false); refreshGitDiffGutter(); return true;
}

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

bool SourceCodeView::triggerGoToDefinition() {
    const QTextCursor cur = textCursor();
    const auto loc = !m_currentFilePath.isEmpty()
                         ? core::ClangdLspClient::instance().findDefinition(m_currentFilePath, toPlainText(), cur.blockNumber(), cur.positionInBlock())
                         : core::ClangdLspClient::instance().findDefinition(toPlainText(), cur.blockNumber() + 1, cur.positionInBlock());
    if (loc.line <= 0) return false;
    if (!m_currentFilePath.isEmpty() && !loc.filePath.isEmpty() && QFileInfo::exists(loc.filePath)) {
        const QString curCanon = QFileInfo::exists(m_currentFilePath) ? QFileInfo(m_currentFilePath).canonicalFilePath() : QFileInfo(m_currentFilePath).absoluteFilePath();
        const QString dstCanon = QFileInfo(loc.filePath).canonicalFilePath();
        if (!dstCanon.isEmpty() && dstCanon != curCanon) { emit definitionJumpRequested(dstCanon, loc.line, loc.column); return true; }
    }
    QTextBlock b = document()->findBlockByNumber(loc.line - 1); if (!b.isValid()) return false;
    QTextCursor dest(b); dest.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, qMax(0, loc.column));
    setTextCursor(dest); ensureCursorVisible(); return true;
}

QHash<int, SourceCodeView::GitLineState> SourceCodeView::parseUnifiedGitDiff(const QString& unifiedDiff) {
    QHash<int, GitLineState> states;
    static const QRegularExpression hunkRe(QStringLiteral("^@@\\s+-(\\d+)(?:,(\\d+))?\\s+\\+(\\d+)(?:,(\\d+))?\\s+@@"), QRegularExpression::MultilineOption);
    auto it = hunkRe.globalMatch(unifiedDiff);
    while (it.hasNext()) {
        const auto m = it.next();
        const int oldCount = m.captured(2).isEmpty() ? 1 : m.captured(2).toInt(), newStart = m.captured(3).toInt(), newCount = m.captured(4).isEmpty() ? 1 : m.captured(4).toInt();
        if (newCount > 0 && oldCount == 0) { for (int i = 0; i < newCount; ++i) states.insert(newStart + i, GitLineState::Added); }
        else if (newCount > 0 && oldCount > 0) { for (int i = 0; i < newCount; ++i) states.insert(newStart + i, GitLineState::Modified); }
        else if (newCount == 0 && oldCount > 0) { states.insert(qMax(1, newStart), GitLineState::Deleted); }
    }
    return states;
}

void SourceCodeView::setGitLineStates(const QHash<int, GitLineState>& states) {
    m_gitLineStates = states;
    if (m_lineNumberArea) m_lineNumberArea->update();
}

void SourceCodeView::refreshGitDiffGutter() {
    if (m_gitDiffTimer) m_gitDiffTimer->stop();
    const quint64 seq = ++m_gitDiffSeq; const QString filePath = m_currentFilePath;
    if (filePath.isEmpty()) { setGitLineStates({}); return; }
    const QString bufferText = toPlainText(); const bool dirty = document() && document()->isModified();
    QPointer<SourceCodeView> self(this);
    QThreadPool::globalInstance()->start([self, seq, filePath, bufferText, dirty]() {
        const QString diff = computeUnifiedGitDiffForBuffer(filePath, bufferText, dirty);
        const auto states = diff.isEmpty() ? QHash<int, GitLineState>{} : parseUnifiedGitDiff(diff);
        if (self) QMetaObject::invokeMethod(self, [self, seq, states]() { if (self && self->m_gitDiffSeq.load() == seq) self->setGitLineStates(states); }, Qt::QueuedConnection);
    });
}

bool SourceCodeView::renameSymbolAtCursor(const QString& newName) {
    const QTextCursor cur = textCursor();
    if (!m_currentFilePath.isEmpty() && QFileInfo::exists(m_currentFilePath)) {
        const auto edits = core::ClangdLspClient::instance().renameSymbol(m_currentFilePath, toPlainText(), cur.blockNumber(), cur.positionInBlock(), newName);
        const QString curCanon = QFileInfo(m_currentFilePath).canonicalFilePath();
        if (edits.contains(curCanon) && edits.value(curCanon) != toPlainText()) { setPlainText(edits.value(curCanon)); document()->setModified(true); return true; }
    }
    const QString updated = core::ClangdLspClient::instance().renameSymbolInSource(toPlainText(), cur.blockNumber() + 1, cur.positionInBlock(), newName);
    if (updated == toPlainText()) return false;
    setPlainText(updated); document()->setModified(true); return true;
}

void SourceCodeView::keyPressEvent(QKeyEvent* event) {
    if (!event) return;
    if (event->key() == Qt::Key_F12 && !(event->modifiers() & Qt::ShiftModifier)) { triggerGoToDefinition(); event->accept(); return; }
    if (m_completionPopup && m_completionPopup->isVisible()) {
        if (event->key() == Qt::Key_Down) { m_completionPopup->selectNext(); event->accept(); return; }
        if (event->key() == Qt::Key_Up) { m_completionPopup->selectPrevious(); event->accept(); return; }
        if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter || event->key() == Qt::Key_Tab) && m_completionPopup->selectCurrent()) { event->accept(); return; }
        if (event->key() == Qt::Key_Escape) { m_completionPopup->hide(); event->accept(); return; }
    }
    if ((event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_Space) {
        QTextCursor cur = textCursor(); triggerCompletionPopupAtCursor(cur.block().text().left(cur.positionInBlock()), cur.blockNumber() + 1); event->accept(); return;
    }
    const Qt::KeyboardModifiers navMods = event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    if (navMods == Qt::NoModifier && (event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab)) {
        const bool isUnindent = (event->key() == Qt::Key_Backtab) || (event->modifiers() & Qt::ShiftModifier);
        const int width = qBound(1, m_tabWidth, 16);
        QTextCursor cur = textCursor();
        const int selStart = cur.selectionStart(), selEnd = cur.selectionEnd();
        QTextBlock startBlk = document()->findBlock(selStart), endBlk = document()->findBlock(selEnd);
        if (cur.hasSelection() && endBlk != startBlk && selEnd == endBlk.position()) endBlk = endBlk.previous();
        if (isUnindent) {
            cur.beginEditBlock();
            for (QTextBlock blk = startBlk; blk.isValid(); blk = blk.next()) {
                const QString lineTxt = blk.text();
                int removeCnt = 0;
                if (!lineTxt.isEmpty() && lineTxt.at(0) == QLatin1Char('\t')) removeCnt = 1;
                else while (removeCnt < width && removeCnt < lineTxt.size() && lineTxt.at(removeCnt) == QLatin1Char(' ')) ++removeCnt;
                if (removeCnt > 0) {
                    QTextCursor lineCur(blk); lineCur.setPosition(blk.position() + removeCnt, QTextCursor::KeepAnchor); lineCur.removeSelectedText();
                }
                if (blk == endBlk) break;
            }
            cur.endEditBlock();
        } else if (cur.hasSelection() && startBlk != endBlk) {
            const QString pad(width, QLatin1Char(' '));
            cur.beginEditBlock();
            for (QTextBlock blk = startBlk; blk.isValid(); blk = blk.next()) {
                QTextCursor lineCur(blk); lineCur.insertText(pad);
                if (blk == endBlk) break;
            }
            cur.endEditBlock();
        } else {
            cur.insertText(QString(width, QLatin1Char(' ')));
            setTextCursor(cur);
        }
        event->accept(); return;
    }
    QPlainTextEdit::keyPressEvent(event); const QString txt = event->text();
    if (txt == QStringLiteral(".") || txt == QStringLiteral(">") || txt == QStringLiteral(":")) {
        QTextCursor cur = textCursor(); triggerCompletionPopupAtCursor(cur.block().text().left(cur.positionInBlock()), cur.blockNumber() + 1);
    } else if (m_completionPopup && m_completionPopup->isVisible() && !txt.isEmpty() && (txt.at(0).isLetterOrNumber() || txt.at(0) == QLatin1Char('_')) && m_completionTimer) m_completionTimer->start(75);
}

}  // namespace gridlock::ui
