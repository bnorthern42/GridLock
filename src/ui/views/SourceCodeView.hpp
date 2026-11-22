#pragma once

/**
 * @file SourceCodeView.hpp
 * @brief High-performance C/C++ source code editor and debugger gutter view.
 *
 * Implements SourceCodeView and CppSyntaxHighlighter for source navigation,
 * breakpoint toggling, active line stepping illumination, and variable hover inspection.
 */

#include <QWidget>
#include <QString>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTimer>
#include <QPoint>
#include <QContextMenuEvent>
#include <QDir>
#include <QFileInfo>
#include <QMouseEvent>
#include <QProcess>
#include <QPointer>
#include <QTemporaryFile>
#include <QThreadPool>
#include <atomic>
#include "../../core/utils/ClangdLspClient.hpp"
#include "EditorSyntaxHighlighter.hpp"

namespace gridlock::ui {

class InlineFindBar;
class LineNumberArea;
class LspCompletionPopup;

/**
 * @class SourceCodeView
 * @brief Monospace source code editor widget with line numbering, breakpoints, and hover inspection.
 */
class SourceCodeView : public QPlainTextEdit {
    Q_OBJECT

public:
    /**
     * @brief Constructs a new SourceCodeView.
     * @param parent Optional parent widget.
     */
    explicit SourceCodeView(QWidget *parent = nullptr);

    /**
     * @brief Destructor.
     */
    ~SourceCodeView() override = default;

    /** @brief Opens and focuses the compact inline search bar (`Ctrl+F`) inside this editor. */
    void openFindBar();
    /** @brief Closes the inline search bar (`Esc`) and restores focus to this editor. */
    void closeFindBar();
    /** @brief Returns the inline search bar widget owned by this editor. */
    InlineFindBar* findBar() const { return m_findBar; }

    /**
     * @brief Sets source code content and highlights an initial active line.
     * @param code Full text of the source file.
     * @param activeLine Optional 1-based active line number.
     */
    void setSourceCode(const QString& code, int activeLine = -1);

    /**
     * @brief Sets the path of the currently open source file and synchronizes live clangd diagnostics.
     * @param filePath Filesystem path.
     */
    void setCurrentFile(const QString& filePath);

    /** @brief Returns the path of the currently open source file. */
    QString currentFile() const { return m_currentFilePath; }

    /// Byte threshold (512 KB) above which syntax highlighting, soft-wrap, and clangd are bypassed.
    static constexpr qint64 kLargeFileThresholdBytes = 512 * 1024;

    /** @brief Returns true for plain tabular/data extensions (.csv, .tsv, .dat, .log) that bypass C++ highlighting and clangd. */
    static bool isPlainDataFile(const QString& filePath);

    /** @brief Enables or disables fast large-file mode (detaches highlighter, enforces NoWrap, skips clangd/git-diff). */
    void setLargeFileMode(bool enabled);

    /** @brief Returns true when fast large-file/plain-data mode is active. */
    bool isLargeFileMode() const { return m_largeFileMode; }

    /**
     * @brief Highlights a specific 1-based line as the active execution pointer.
     * @param lineNumber 1-based line index.
     */
    void highlightCurrentLine(int lineNumber);

    /**
     * @brief Highlights a 1-based line as the active debugger execution line (or clears if <= 0).
     * @param lineOneBased 1-based source line number, or <= 0 to clear execution highlight.
     */
    void highlightExecutionLine(int lineOneBased);

    /**
     * @brief Returns the currently highlighted 1-based debugger execution line (-1 if none).
     * @return 1-based execution line number, or -1 when idle.
     */
    int executionLine() const { return m_executionLine; }

    /**
     * @brief Selects a 1-based source line, highlights it, and emits sourceLineSelected.
     * @param lineOneBased 1-based source line number.
     */
    void selectSourceLine(int lineOneBased);

    /**
     * @brief Returns the plain text contents of the source document.
     * @return Plain text string.
     */
    QString getPlainText() const { return toPlainText(); }

    /**
     * @brief Returns viewport margins configured for the line number area.
     * @return QMargins.
     */
    QMargins getViewportMargins() const { return viewportMargins(); }

    /**
     * @brief Returns the pixel width reserved for the dedicated left vector icon lane in the gutter.
     * @return Icon lane width in pixels (20px).
     */
    int gutterIconLaneWidth() const { return 20; }

    /**
     * @brief Returns the total pixel width of the two-lane left gutter (icon lane + right-aligned line numbers).
     * @return Gutter width in pixels (>= 48px).
     */
    int lineNumberAreaWidth() const;

    /**
     * @brief Returns the right viewport margin reservation in pixels (strictly 0).
     * @return Right viewport margin in pixels.
     */
    int rightViewportMargin() const { return viewportMargins().right(); }

    /**
     * @brief Indicates whether any 80-column right-margin fill or split band is active.
     * @return Always false for the uniform dark-slate editor canvas.
     */
    bool hasRightMarginFill() const { return false; }

    /**
     * @brief Visual and rank-filtering metadata stored for an active 1-based breakpoint line.
     */
    struct BreakpointVisualInfo {
        int rankFilter = -1;  ///< -1 for All Ranks, or >= 0 for a specific 0-based MPI rank.
        QString condition;    ///< Optional C/C++ boolean condition predicate.
        int ignoreCount = 0;  ///< Number of hits to ignore before stopping (0 = stop immediately).
        QString logMessage;   ///< Non-empty trace expression message for non-suspending Logpoints.
        bool enabled = true;  ///< False when breakpoint is non-destructively disabled (hollow gutter ring).
        bool temporary = false; ///< True for one-shot temporary (`tbreak`) breakpoints.
        QStringList commands = {}; ///< Automated GDB commands executed on hit (`commands ... end`).
    };

    /**
     * @brief Computes deterministic high-contrast badge color for a rank filter (-1 = Crimson Red #ef4444).
     * @param rankFilter Target 0-based MPI rank, or -1 for All Ranks.
     * @return QColor badge fill color.
     */
    static QColor colorForRankBreakpoint(int rankFilter);

    /**
     * @brief Sets the full set of active breakpoint lines.
     * @param bps Set of 1-based line numbers.
     */
    void setBreakpoints(const QSet<int>& bps);

    /**
     * @brief Restores per-line rank-filtered breakpoint metadata without emitting toggle signals.
     * @param metaByLine Map of 1-based line number to BreakpointVisualInfo.
     */
    void syncBreakpointMetadata(const QMap<int, BreakpointVisualInfo>& metaByLine);

    /**
     * @brief Returns the set of active 1-based breakpoint line numbers.
     * @return QSet<int> of 1-based breakpoint lines.
     */
    QSet<int> breakpoints() const;

    /**
     * @brief Checks whether a breakpoint is set on the given 1-based line number.
     * @param lineOneBased 1-based source line number.
     * @return True if an active breakpoint is registered on lineOneBased.
     */
    bool hasBreakpoint(int lineOneBased) const;

    /**
     * @brief Toggles a breakpoint at the given 1-based line.
     * @param lineNum 1-based line number.
     */
    void toggleBreakpoint(int lineNum);

    /**
     * @brief Sets or updates a breakpoint with an explicit rank filter and optional condition.
     * @param lineOneBased 1-based source line number.
     * @param rankFilter Target 0-based MPI rank, or -1 for All Ranks.
     * @param condition Optional C/C++ boolean condition expression.
     */
    void setRankBreakpoint(int lineOneBased, int rankFilter, const QString& condition = QString());

    /**
     * @brief Sets or updates a conditional/rank-filtered breakpoint on a 1-based source line.
     * @param lineOneBased 1-based source line number.
     * @param condition Conditional expression (e.g., "rank == 0").
     */
    void setConditionalBreakpoint(int lineOneBased, const QString& condition);

    /** @brief Configures condition, pass/ignore count, and non-suspending Logpoint message on a breakpoint line. */
    void setBreakpointOptions(int lineOneBased, const QString& condition, int ignoreCount, const QString& logMessage);
    /** @brief Non-destructively enables or disables an existing breakpoint on @p lineOneBased. */
    inline void setBreakpointEnabled(int lineOneBased, bool enabled);
    /** @brief Returns true when @p lineOneBased has an enabled breakpoint. */
    bool isBreakpointEnabledForLine(int lineOneBased) const { return hasBreakpoint(lineOneBased) && m_breakpointInfo.value(lineOneBased).enabled; }
    /** @brief Inserts a one-shot temporary (`tbreak`) breakpoint on @p lineOneBased. */
    inline void setTemporaryBreakpoint(int lineOneBased, int rankFilter = -1, const QString& condition = QString());
    /** @brief Returns true when @p lineOneBased holds a one-shot temporary (`tbreak`) breakpoint. */
    bool isTemporaryBreakpointForLine(int lineOneBased) const { return hasBreakpoint(lineOneBased) && m_breakpointInfo.value(lineOneBased).temporary; }
    /** @brief Attaches or clears GDB hit command lines (`commands ... end`) on @p lineOneBased. */
    inline void setBreakpointCommands(int lineOneBased, const QStringList& commands);
    /** @brief Returns GDB hit command lines attached to @p lineOneBased. */
    QStringList breakpointCommandsForLine(int lineOneBased) const { return hasBreakpoint(lineOneBased) ? m_breakpointInfo.value(lineOneBased).commands : QStringList{}; }
    /** @brief Removes a breakpoint from UI state without emitting toggle signals (used after one-shot `tbreak` hit). */
    inline void removeBreakpointQuietly(int lineOneBased);
    /** @brief Returns true when lineOneBased has a non-suspending Logpoint message configured. */
    bool isLogpointForLine(int lineOneBased) const;
    /** @brief Returns the configured pass/ignore hit count for lineOneBased (0 if unset). */
    int breakpointIgnoreCountForLine(int lineOneBased) const;
    /** @brief Returns the configured Logpoint trace message for lineOneBased (empty if unset). */
    QString breakpointLogMessageForLine(int lineOneBased) const;

    /** @brief Returns stored visual metadata for a 1-based breakpoint line. */
    BreakpointVisualInfo breakpointInfoForLine(int lineOneBased) const;

    /** @brief Returns the rendered badge fill color for a 1-based line (`#ef4444` when All Ranks). */
    QColor breakpointColorForLine(int lineOneBased) const;

    /** @brief Returns true when the breakpoint on lineOneBased renders the crisp white All-Ranks outline. */
    bool breakpointHasWhiteOutlineForLine(int lineOneBased) const;

    /** @brief Returns the configured 0-based rank filter for lineOneBased (-1 for All Ranks or unset). */
    int breakpointRankFilterForLine(int lineOneBased) const;

    /** @brief Returns formatted gutter hover tooltip text for an active breakpoint line (empty if unset). */
    QString breakpointTooltipForLine(int lineOneBased) const;

    /** @brief Updates the active focused rank used by "Set Breakpoint for Active Rank (Rank N)". */
    void setActiveFocusRank(int rankId);

    /** @brief Returns the currently focused rank index used for rank-scoped quick actions. */
    int activeFocusRank() const { return m_activeFocusRank; }

    /** @brief Updates the gutter execution PC marker without moving cursor, scroll, or active tab. */
    void setExecutionLineOnly(int lineOneBased);
    /** @brief Sets live paused local variable name -> value mappings for inline ghost rendering. */
    void setInlineVariableValues(const QMap<QString, QString>& varValues);
    /** @brief Merges a single resolved dotted/arrow member expression value into inline ghost values. */
    void mergeInlineVariableValue(const QString& key, const QString& val);
    /** @brief Clears all inline variable ghost values when resuming, stopping, or switching workspaces. */
    void clearInlineVariableValues();
    /** @brief Builds the deduplicated `// name = val, ...` ghost string for oneBasedLine (empty if disabled/none). */
    QString inlineGhostTextForLine(int oneBasedLine) const;

    /**
     * @brief Returns the active C/C++ boolean condition attached to a 1-based breakpoint line.
     * @param lineOneBased 1-based source line number.
     * @return Trimmed condition expression, or empty string if unconditional/unset.
     */
    QString breakpointCondition(int lineOneBased) const;

    /**
     * @brief Returns the configured rank suspension scope ("All" or "Rank") for a breakpoint line.
     * @param lineOneBased 1-based source line number.
     * @return Suspension scope identifier string.
     */
    QString breakpointSuspendScope(int lineOneBased) const;

    /**
     * @brief Returns the 1-based line number currently hovered in the gutter (-1 when none).
     * @return 1-based hovered gutter line number, or -1.
     */
    int hoveredGutterLine() const { return m_hoveredGutterLine; }

    /**
     * @brief Updates the 1-based hovered gutter line and triggers ghost breakpoint preview repaint.
     * @param lineOneBased 1-based line number under cursor, or -1 to clear hover state.
     */
    void setHoveredGutterLine(int lineOneBased);

    /**
     * @brief Returns the composite gutter badge state for a 1-based source line.
     * @param lineOneBased 1-based source line number.
     * @return One of "None", "HoverPreview", "Breakpoint", "ConditionalBreakpoint", "ExecutionPc", or "BreakpointAndExecutionPc".
     */
    QString gutterMarkerStateForLine(int lineOneBased) const;

    /**
     * @brief Returns the current cursor line number (1-based).
     * @return 1-based line number.
     */
    int getCurrentLineNumber() const;

    /**
     * @brief Toggles single-line '//' comments on the active line or selection.
     */
    void toggleLineComment();

    /**
     * @brief Returns member and variable completion strings for linePrefix at lineOneBased.
     * @param lineOneBased 1-based line number.
     * @param linePrefix Expression prefix before cursor (e.g., "data[i].").
     * @return List of completion labels.
     */
    QStringList editorCompletionsAtLine(int lineOneBased, const QString& linePrefix) const;

    /**
     * @brief Triggers the floating LSP IntelliSense popup below cursorRect() at lineOneBased.
     * @param linePrefix Expression prefix before cursor.
     * @param lineOneBased 1-based line number.
     */
    void triggerCompletionPopupAtCursor(const QString& linePrefix, int lineOneBased);

    /**
     * @brief Saves the current editor document to disk.
     * @param filePath Target file path, or empty to overwrite currentFile().
     * @return True if successfully written; false otherwise.
     */
    bool saveToFile(const QString& filePath = QString());

    /**
     * @brief Computes the enclosing C/C++ lexical scope breadcrumb at the current cursor line.
     * @return Breadcrumb string formatted as "filename > Class/Struct > function".
     */
    QString currentScopeBreadcrumb() const;

    /**
     * @brief Reloads color palette and styling from ConfigManager and ThemeManager.
     */
    Q_INVOKABLE void reloadStyle();

    /// Supported editor highlighting modes forwarded to EditorSyntaxHighlighter.
    using LanguageMode = EditorSyntaxHighlighter::LanguageMode;

    /** @brief Overrides the active syntax highlighting language mode and rehighlights. */
    void setLanguageMode(LanguageMode mode);

    /** @brief Returns the active syntax highlighting language mode. */
    LanguageMode languageMode() const;

    /** @brief Enables or disables Rainbow Brackets depth coloring on this view. */
    void setRainbowBracketsEnabled(bool enabled);

    /** @brief Returns whether Rainbow Brackets depth coloring is enabled on this view. */
    bool rainbowBracketsEnabled() const;

    /** @brief Enables or disables TOML/YAML/Markdown/CSV syntax highlighting on this view. */
    void setDataDocHighlightingEnabled(bool enabled);

    /** @brief Returns whether TOML/YAML/Markdown/CSV syntax highlighting is enabled on this view. */
    bool dataDocHighlightingEnabled() const;

    /** @brief Rebuilds token formats from current theme/config and rehighlights the document. */
    void rehighlightDocument();

    /**
     * @brief Returns the active canvas background QColor resolved from ThemeManager.
     * @return Editor viewport background color (light >= 220 lightness in Light Mode).
     */
    QColor activeCanvasBackgroundColor() const;

    /**
     * @brief Paints the gutter line numbers, hover highlights, and breakpoint markers.
     * @param event The paint event.
     */
    void lineNumberAreaPaintEvent(QPaintEvent *event);

    /**
     * @brief Handles mouse clicks in the gutter to toggle breakpoints or open the anchored popover.
     * @param event Mouse press event.
     */
    void lineNumberAreaMousePressEvent(QMouseEvent *event);

    /**
     * @brief Tracks cursor movement over the gutter to illuminate hovered line and ghost preview dot.
     * @param event Mouse move event inside LineNumberArea.
     */
    void lineNumberAreaMouseMoveEvent(QMouseEvent *event);

    /**
     * @brief Completes a gutter PC arrow drag (emitting jumpToLineRequested) or plain click breakpoint toggle.
     * @param event Mouse release event inside LineNumberArea.
     */
    inline void lineNumberAreaMouseReleaseEvent(QMouseEvent *event);

    /** @brief Returns true while the user is actively dragging the gutter execution PC marker. */
    bool isDraggingExecutionPc() const { return m_draggingExecutionPc; }

    /** @brief Returns the 1-based line currently targeted by an in-progress gutter PC drag (`-1` if none). */
    int dragTargetLine() const { return m_dragTargetLine; }

    /**
     * @brief Clears gutter hover state when the mouse cursor exits LineNumberArea.
     * @param event Leave event from LineNumberArea.
     */
    inline void lineNumberAreaLeaveEvent(QEvent *event);

    /**
     * @brief Determines whether the standard editor context menu may open at the given widget coordinate.
     * @param pos Position in SourceCodeView coordinates.
     * @return False if pos falls within the gutter width or LineNumberArea geometry; true otherwise.
     */
    bool shouldOpenEditorContextMenuAt(const QPoint& pos) const;

    /**
     * @brief Commits or rolls back tentative breakpoint state after the anchored breakpoint popover closes.
     * @param lineOneBased 1-based source line number.
     * @param wasPreviouslySet True if a breakpoint already existed on lineOneBased prior to right-click.
     * @param accepted True if the popover closed with QDialog::Accepted; false if canceled or dismissed.
     * @param enabledChecked True if the breakpoint remains enabled (not unchecked and not removed).
     * @param condition Optional C/C++ boolean condition expression.
     * @param suspendScope Rank suspension scope ("All", "Rank", or "None").
     */
    void handleBreakpointPopoverResult(int lineOneBased, bool wasPreviouslySet, bool accepted,
                                       bool enabledChecked, const QString& condition,
                                       const QString& suspendScope);

    /** @brief Non-destructive overload separating explicit F9 removal (@p removeRequested) from disablement (@p enabledChecked). */
    void handleBreakpointPopoverResult(int lineOneBased, bool wasPreviouslySet, bool accepted,
                                       bool enabledChecked, const QString& condition,
                                       const QString& suspendScope, bool removeRequested);

    /**
     * @brief Applies compiler diagnostics as wavy underlines (#f7768e errors, #e0af68 warnings) and tooltips.
     * @param diagnostics Vector of LspDiagnostic records.
     */
    void setDiagnostics(const QVector<core::utils::LspDiagnostic>& diagnostics);

    /**
     * @brief Returns the currently active compiler diagnostics on this editor buffer.
     * @return Vector of LspDiagnostic records.
     */
    QVector<core::utils::LspDiagnostic> diagnostics() const { return m_diagnostics; }

    /**
     * @brief Jumps the editor cursor to the declaration/definition of the symbol under the cursor (F12).
     * @return True if a definition location was resolved and jumped to.
     */
    bool triggerGoToDefinition();

    /**
     * @enum GitLineState
     * @brief Per-line unified Git diff change state rendered in the SourceCodeView gutter.
     */
    enum class GitLineState { Unchanged, Added, Modified, Deleted };

    /**
     * @brief Parses unified Git diff (`git diff -U0`) output into 1-based line change states.
     * @param unifiedDiff Unified diff output string containing `@@ -old +new @@` hunk headers.
     * @return Map of 1-based line numbers to GitLineState values.
     */
    static QHash<int, GitLineState> parseUnifiedGitDiff(const QString& unifiedDiff);

    /**
     * @brief Sets the active per-line Git diff gutter states and schedules a gutter repaint.
     * @param states Map of 1-based line numbers to GitLineState values.
     */
    void setGitLineStates(const QHash<int, GitLineState>& states);

    /**
     * @brief Returns the Git diff change state for a 1-based source line.
     * @param oneBasedLine 1-based source line number.
     * @return GitLineState (Unchanged if not present in the diff map).
     */
    GitLineState gitLineState(int oneBasedLine) const {
        return m_gitLineStates.value(oneBasedLine, GitLineState::Unchanged);
    }

    /**
     * @brief Refreshes live Git working-tree and unsaved-buffer diff gutter states against HEAD.
     */
    Q_INVOKABLE void refreshGitDiffGutter();

    /**
     * @brief Computes unified `git diff -U0` text against `HEAD` for either saved disk state or live unsaved buffer text.
     * @param filePath Canonical or absolute path to the open source file.
     * @param bufferText Current plain text of the editor buffer.
     * @param isModified True when the buffer has unsaved in-memory edits differing from disk.
     * @return Unified diff text suitable for `parseUnifiedGitDiff`, or empty string when clean/untracked.
     */
    static inline QString computeUnifiedGitDiffForBuffer(
        const QString& filePath, const QString& bufferText, bool isModified) {
        if (filePath.isEmpty()) return {};
        const QFileInfo fi(filePath);
        if (!fi.exists() || !fi.isFile()) return {};
        const QString canonPath = fi.canonicalFilePath().isEmpty() ? fi.absoluteFilePath() : fi.canonicalFilePath();
        const QString workDir = QFileInfo(canonPath).absolutePath();
        auto runGit = [&](const QStringList& args, int* exitOut = nullptr) -> QByteArray {
            QProcess proc;
            proc.setWorkingDirectory(workDir);
            proc.setProcessChannelMode(QProcess::SeparateChannels);
            proc.start(QStringLiteral("git"), args);
            if (!proc.waitForFinished(1500)) {
                proc.kill();
                proc.waitForFinished(200);
                if (exitOut) *exitOut = -1;
                return {};
            }
            if (exitOut) *exitOut = proc.exitCode();
            return proc.readAllStandardOutput();
        };
        // Verify the file belongs to a Git working tree and is tracked before querying HEAD diffs.
        int lsExit = -1;
        const QString relPath = QString::fromUtf8(runGit(
            {QStringLiteral("-C"), workDir, QStringLiteral("ls-files"),
             QStringLiteral("--full-name"), QStringLiteral("--error-unmatch"), QStringLiteral("--"), canonPath},
            &lsExit)).trimmed();
        if (lsExit != 0 || relPath.isEmpty()) return {};

        if (!isModified) {
            int diffExit = -1;
            const QByteArray out = runGit(
                {QStringLiteral("-C"), workDir, QStringLiteral("diff"),
                 QStringLiteral("--no-color"), QStringLiteral("--no-ext-diff"), QStringLiteral("-U0"),
                 QStringLiteral("HEAD"), QStringLiteral("--"), canonPath},
                &diffExit);
            return (diffExit == 0) ? QString::fromUtf8(out) : QString();
        }

        // Compare live unsaved buffer bytes against the HEAD blob using temporary files so git diff -U0 stays exact.
        int showExit = -1;
        QByteArray headBytes = runGit({QStringLiteral("-C"), workDir, QStringLiteral("show"), QStringLiteral("HEAD:") + relPath}, &showExit);
        if (showExit != 0) return {};
        headBytes.replace("\r\n", "\n");
        QTemporaryFile headTmp(QDir::tempPath() + QStringLiteral("/gridlock_git_head_XXXXXX"));
        QTemporaryFile bufTmp(QDir::tempPath() + QStringLiteral("/gridlock_git_buf_XXXXXX"));
        if (!headTmp.open() || !bufTmp.open()) return {};
        headTmp.write(headBytes);
        headTmp.flush();
        bufTmp.write(bufferText.toUtf8());
        bufTmp.flush();

        int noIdxExit = -1;
        const QByteArray diffOut = runGit(
            {QStringLiteral("diff"), QStringLiteral("--no-index"), QStringLiteral("--no-color"),
             QStringLiteral("--no-ext-diff"), QStringLiteral("-U0"), QStringLiteral("--"),
             headTmp.fileName(), bufTmp.fileName()},
            &noIdxExit);
        // git diff --no-index returns 0 when identical and 1 when hunks differ.
        return (noIdxExit == 0 || noIdxExit == 1) ? QString::fromUtf8(diffOut) : QString();
    }

    /**
     * @brief Renames all AST references to the symbol under the cursor (Shift+F6) without corrupting comments.
     * @param newName Replacement identifier name.
     * @return True if the symbol was renamed in the document.
     */
    bool renameSymbolAtCursor(const QString& newName);

    /** @brief Extracts `<TypeName>` when @p lineText declares a C/C++ `struct <TypeName>` or `class <TypeName>`. */
    static QString detectStructOrClassAtLine(const QString& lineText);

    /** @brief Builds and returns a caller-owned editor context menu for @p editorPos (`nullptr` over the gutter). */
    QMenu* createEditorContextMenuAt(const QPoint& editorPos);

    /** @brief Headless-safe helper invoking the first context-menu action matching @p actionSubstring on @p lineOneBased. */
    bool triggerContextActionAtLine(int lineOneBased, const QString& actionSubstring);

signals:
    /** @brief Emitted to request running the target application. */
    void runTargetRequested();

    /** @brief Emitted to request continuing execution. */
    void continueRequested();

    /** @brief Emitted to request single instruction stepping. */
    void stepInstRequested();

    /** @brief Emitted to request toggling a breakpoint at a location string. */
    void toggleBreakpointRequested(const QString& location);

    /** @brief Emitted when a breakpoint is toggled at a file and line. */
    void breakpointToggled(const QString& file, int line, bool isSet, const QString& condition = QString());

    /** @brief Emitted with explicit rank filter metadata whenever a line breakpoint is added, modified, or removed. */
    void rankBreakpointChanged(const QString& file, int line, bool isSet, int rankFilter,
                               const QString& condition = QString());

    /** @brief Emitted whenever breakpoint ignoreCount or Logpoint message is updated on a line. */
    void breakpointOptionsChanged(const QString& file, int line, int ignoreCount, const QString& logMessage);

    /** @brief Emitted when a breakpoint is non-destructively enabled or disabled (`-break-enable` / `-break-disable`). */
    void breakpointEnabledToggled(const QString& file, int line, bool enabled);

    /** @brief Emitted when a one-shot temporary (`tbreak`) breakpoint is inserted on a line. */
    void temporaryBreakpointInserted(const QString& file, int line, const QString& condition, int rankFilter);

    /** @brief Emitted when GDB hit commands (`-break-commands`) are updated for a breakpoint line. */
    void breakpointCommandsChanged(const QString& file, int line, const QStringList& commands);

    /** @brief Emitted when hovering over a variable identifier. */
    void hoverVariableRequested(const QString& varName, const QPoint& globalPos);

    /** @brief Emitted to hide any active hover tooltip. */
    void hideHoverTooltip();

    /** @brief Emitted to pin a variable into the watch view. */
    void pinVariableRequested(const QString& varName);

    /** @brief Emitted when a 1-based source line is selected or clicked by the user. */
    void sourceLineSelected(int lineOneBased);

    /** @brief Emitted when the underlying QTextDocument modification state changes. */
    void modificationStateChanged(bool modified);

    /** @brief Emitted when F12 Go-to-Definition resolves to a target location in another workspace file. */
    void definitionJumpRequested(const QString& filePath, int oneBasedLine, int zeroBasedColumn);

    /** @brief Emitted when the user chooses "Open Selection in Programmer's Calculator" from the editor context menu. */
    void sendToCalculatorRequested(const QString& text);

    /** @brief Emitted when the user chooses "Graph '<expr>' in Pointer Graph" from the editor context menu. */
    void graphExpressionRequested(const QString& expression);

    /** @brief Emitted when the user chooses "Graph '<TypeName>' Struct Instances" on a struct/class line. */
    void graphStructTypeRequested(const QString& typeName);

    /** @brief Emitted when the user selects "Run to Cursor" for a 1-based source line. */
    void runToCursorRequested(const QString& filePath, int lineOneBased);

    /** @brief Emitted when the user drags the gutter PC arrow or selects "Set Next Statement / Jump Here". */
    void jumpToLineRequested(const QString& filePath, int lineOneBased);

protected:
    void resizeEvent(QResizeEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void changeEvent(QEvent *event) override;

public:
    /** @brief Returns the active soft-tab and tab-stop width in monospace columns (default 4). */
    int tabWidth() const { return m_tabWidth; }
    /** @brief Sets the soft-tab and tab-stop width in monospace columns (clamped 1..16) and updates tabStopDistance(). */
    void setTabWidth(int spaces);
    /** @brief Recomputes QPlainTextEdit::setTabStopDistance() in exact pixels from the active monospace font metrics. */
    void updateTabStopDistance();

private slots:
    void updateLineNumberAreaWidth(int newBlockCount);
    void updateLineNumberArea(const QRect &rect, int dy);
    void handleHoverTimeout();
    void highlightCurrentLine();
    void syncLspDocument();

private:
    void ensureCompletionPopup();

    /** @brief Resolves the 1-based source line at gutter pixel coordinate @p y (`-1` if none). */
    int gutterLineAtY(int y, int* outTop = nullptr, int* outBottom = nullptr) const {
        QTextBlock block = firstVisibleBlock();
        int blockNumber = block.blockNumber();
        int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
        int bottom = top + qRound(blockBoundingRect(block).height());
        while (block.isValid()) {
            if (block.isVisible() && y >= top && y <= bottom) {
                if (outTop) *outTop = top;
                if (outBottom) *outBottom = bottom;
                return blockNumber + 1;
            }
            block = block.next();
            top = bottom;
            bottom = top + qRound(blockBoundingRect(block).height());
            ++blockNumber;
        }
        return -1;
    }

    LineNumberArea* m_lineNumberArea = nullptr;
    EditorSyntaxHighlighter* m_highlighter = nullptr;
    bool m_largeFileMode = false;
    LspCompletionPopup* m_completionPopup = nullptr;
    InlineFindBar* m_findBar = nullptr;
    QMap<int, QString> m_breakpoints;
    QMap<int, QString> m_breakpointSuspendScopes;
    QMap<int, BreakpointVisualInfo> m_breakpointInfo;
    QHash<int, GitLineState> m_gitLineStates;
    QVector<core::utils::LspDiagnostic> m_diagnostics;
    QMap<QString, QString> m_inlineVarValues;
    QString m_currentFilePath;
    QString m_lastHoveredWord;
    QTimer* m_hoverTimer = nullptr;
    QTimer* m_lspSyncTimer = nullptr;
    QTimer* m_gitDiffTimer = nullptr;
    QTimer* m_completionTimer = nullptr;
    quint64 m_latestCompletionSeq = 0;
    std::atomic<quint64> m_gitDiffSeq{0};
    QPoint m_lastMousePos;
    QPoint m_lastGlobalMousePos;
    int m_executionLine = -1;
    int m_hoveredGutterLine = -1;
    int m_activeFocusRank = 0;
    bool m_pcDragCandidate = false;
    bool m_draggingExecutionPc = false;
    int m_dragTargetLine = -1;
    int m_tabWidth = 4;
    QPoint m_pcDragPressPos;
};

/**
 * @class LineNumberArea
 * @brief Gutter widget displaying line numbers, hover previews, and breakpoint markers adjacent to the editor.
 */
class LineNumberArea : public QWidget {
public:
    /**
     * @brief Constructs a new LineNumberArea with live mouse hover tracking enabled.
     * @param editor Pointer to the associated SourceCodeView.
     */
    explicit LineNumberArea(SourceCodeView *editor) : QWidget(editor), sourceCodeView(editor) {
        setMouseTracking(true);
    }

    /**
     * @brief Returns the dynamic two-lane width size hint for the gutter.
     * @return QSize with width >= 48px.
     */
    QSize sizeHint() const override { return QSize(sourceCodeView->lineNumberAreaWidth(), 0); }

protected:
    /** @brief Delegates gutter painting to SourceCodeView. */
    void paintEvent(QPaintEvent *event) override { sourceCodeView->lineNumberAreaPaintEvent(event); }

    /** @brief Delegates left/right gutter clicks to SourceCodeView. */
    void mousePressEvent(QMouseEvent *event) override { sourceCodeView->lineNumberAreaMousePressEvent(event); }

    /** @brief Delegates gutter mouse releases to SourceCodeView for PC arrow drag completion and clean click handling. */
    void mouseReleaseEvent(QMouseEvent *event) override { sourceCodeView->lineNumberAreaMouseReleaseEvent(event); }

    /** @brief Delegates cursor hover tracking over the gutter to SourceCodeView. */
    void mouseMoveEvent(QMouseEvent *event) override { sourceCodeView->lineNumberAreaMouseMoveEvent(event); }

    /** @brief Clears hovered gutter state when cursor leaves LineNumberArea. */
    void leaveEvent(QEvent *event) override { sourceCodeView->lineNumberAreaLeaveEvent(event); }

    /** @brief Consumes QContextMenuEvent inside the gutter so right-clicks never bubble up to SourceCodeView. */
    void contextMenuEvent(QContextMenuEvent *event) override {
        if (event) {
            event->accept();
        }
    }

private:
    SourceCodeView *sourceCodeView = nullptr;
};

inline void SourceCodeView::lineNumberAreaMouseReleaseEvent(QMouseEvent *event) {
    if (event) event->accept();
    if (!event || event->button() != Qt::LeftButton || (!m_pcDragCandidate && !m_draggingExecutionPc)) return;
    const int releaseLine = gutterLineAtY(event->pos().y());
    const int targetLine = (releaseLine > 0) ? releaseLine : m_dragTargetLine;
    const bool wasDragging = m_draggingExecutionPc;
    m_pcDragCandidate = false; m_draggingExecutionPc = false; m_dragTargetLine = -1;
    if (m_lineNumberArea) { m_lineNumberArea->setCursor(Qt::ArrowCursor); m_lineNumberArea->update(); }
    if (viewport()) viewport()->update();
    if (wasDragging && targetLine > 0 && targetLine != m_executionLine) emit jumpToLineRequested(m_currentFilePath, targetLine);
    else if (!wasDragging && m_executionLine > 0) toggleBreakpoint(m_executionLine);
}

inline void SourceCodeView::lineNumberAreaLeaveEvent(QEvent *event) {
    Q_UNUSED(event);
    if (!m_draggingExecutionPc) {
        setHoveredGutterLine(-1);
        if (m_lineNumberArea) m_lineNumberArea->setCursor(Qt::ArrowCursor);
    }
}

inline void SourceCodeView::setBreakpointEnabled(int lineOneBased, bool enabled) {
    if (lineOneBased <= 0 || !hasBreakpoint(lineOneBased)) return;
    BreakpointVisualInfo info = breakpointInfoForLine(lineOneBased);
    info.enabled = enabled;
    m_breakpointInfo.insert(lineOneBased, info);
    highlightCurrentLine();
    if (m_lineNumberArea) {
        if (m_hoveredGutterLine == lineOneBased) m_lineNumberArea->setToolTip(breakpointTooltipForLine(lineOneBased));
        m_lineNumberArea->update();
    }
    emit breakpointEnabledToggled(m_currentFilePath, lineOneBased, enabled);
}

inline void SourceCodeView::setTemporaryBreakpoint(int lineOneBased, int rankFilter, const QString& condition) {
    if (lineOneBased <= 0) return;
    const int normRank = (rankFilter < 0) ? -1 : rankFilter;
    const QString cleanCond = condition.trimmed();
    BreakpointVisualInfo info = m_breakpointInfo.value(lineOneBased);
    info.rankFilter = normRank;
    info.condition = cleanCond;
    info.enabled = true;
    info.temporary = true;
    m_breakpointInfo.insert(lineOneBased, info);
    m_breakpoints.insert(lineOneBased, cleanCond);
    m_breakpointSuspendScopes.insert(lineOneBased, (normRank >= 0) ? QStringLiteral("Rank") : QStringLiteral("All"));
    highlightCurrentLine();
    if (m_lineNumberArea) {
        if (m_hoveredGutterLine == lineOneBased) m_lineNumberArea->setToolTip(breakpointTooltipForLine(lineOneBased));
        m_lineNumberArea->update();
    }
    emit temporaryBreakpointInserted(m_currentFilePath, lineOneBased, cleanCond, normRank);
}

inline void SourceCodeView::setBreakpointCommands(int lineOneBased, const QStringList& commands) {
    if (lineOneBased <= 0 || !hasBreakpoint(lineOneBased)) return;
    QStringList cleaned;
    for (const QString& ln : commands) if (const QString t = ln.trimmed(); !t.isEmpty()) cleaned.append(t);
    BreakpointVisualInfo info = breakpointInfoForLine(lineOneBased);
    info.commands = cleaned;
    m_breakpointInfo.insert(lineOneBased, info);
    if (m_lineNumberArea) {
        if (m_hoveredGutterLine == lineOneBased) m_lineNumberArea->setToolTip(breakpointTooltipForLine(lineOneBased));
        m_lineNumberArea->update();
    }
    emit breakpointCommandsChanged(m_currentFilePath, lineOneBased, cleaned);
}

inline void SourceCodeView::removeBreakpointQuietly(int lineOneBased) {
    if (lineOneBased <= 0 || !m_breakpoints.contains(lineOneBased)) return;
    m_breakpoints.remove(lineOneBased);
    m_breakpointSuspendScopes.remove(lineOneBased);
    m_breakpointInfo.remove(lineOneBased);
    highlightCurrentLine();
    if (m_lineNumberArea) m_lineNumberArea->update();
}

}  // namespace gridlock::ui

