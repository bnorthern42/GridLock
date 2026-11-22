/**
 * @file MainWindowInternal.hpp
 * @brief Internal forward declarations, supporting value types, and cross-TU helpers for MainWindow.
 *
 * Keeps MainWindow.hpp concise and focused on the public, slot, and signal interface while
 * exposing shared helper declarations used across the modular MainWindow*.cpp translation units.
 */

#pragma once

#include <cstdint>

#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>
#include <Qt>

class QCheckBox;
class QDockWidget;
class QMenu;
class QPlainTextEdit;
class QSplitter;
class QTimer;
class QVulkanInstance;
class QWidget;

namespace gridlock {
class GdbRankCoordinator;
class VariablesDockWidget;
class PlainJaneTooltip;
}  // namespace gridlock

namespace gridlock::core {
class LspCoordinator;
class HpcBackend;
class DeadlockAnalyzer;
}  // namespace gridlock::core

namespace gridlock::core::commands {
class IDebugCommand;
}  // namespace gridlock::core::commands

namespace gridlock::ui {

class MainWindow;
class CallStackDockWidget;
class ServerRackView;
class SourceCodeView;
class EditorTabManager;
class DisassemblyView;
class ReferenceManualWidget;
class DifferentialGrid;
class TerminalDockWidget;
class MpiNetworkLogWidget;
class ProjectExplorerWidget;
class GdbConsoleWidget;
class MemView;
class RegisterView;
class SpackManager;
class ExpressionEvaluatorWidget;
class MpiDiagnosticsWidget;
class ArrayPlotDialog;
class CheckpointDialog;
class CommandPaletteDialog;
class PointerGraphDialog;
class ProgrammerCalculatorDialog;
class SignalCatchpointDialog;

/** @brief Unified workspace search hit record across symbols, strings, disassembly, and decompiler. */
struct WorkspaceSearchResult {
    QString category;       ///< Domain classification ("Symbol", "String", "Disassembly", "Decompiler").
    uint64_t address = 0;   ///< Associated 64-bit virtual memory address.
    QString label;          ///< Symbol or identifier label.
    QString contextSnippet; ///< Surrounding line or contextual snippet.
};

/** @brief Records the original splitter or QMainWindow dock area home container for a dock panel. */
struct DockHomeSlot {
    QSplitter* splitter = nullptr;
    int splitterIndex = -1;
    Qt::DockWidgetArea area = Qt::RightDockWidgetArea;
};

/** @brief Cached per-line rank filter and conditional expression metadata for breakpoints. */
struct BreakpointRankMeta {
    int rankFilter = -1;
    QString condition;
};

// Cross-translation-unit internal helpers shared across MainWindow*.cpp files.
QWidget* createAddressSpaceNavBar(MainWindow* window, QWidget* parent);
QString resolveTutorialTarget(const QString& target);
QString ensureWritableTutorialBinaryPath(const QString& sourcePath);
QStringList collectExpressionCandidates(MainWindow* mw);
void refreshDialogCompleters(MainWindow* mw);
void graphStructTypeFromSource(MainWindow* mw, const QString& typeName);
void bindSourceViewGraphSignals(MainWindow* mw, SourceCodeView* sv);
bool isRunnableElfExecutable(const QString& filePath);
void discoverWorkspaceTargets(const QString& rootDir, QStringList* outElfs, QStringList* outMainSources);
void clearAllInspectionDocks(VariablesDockWidget* varsDock, RegisterView* regView, CallStackDockWidget* csDock,
                             MpiDiagnosticsWidget* mpiDiag, MpiNetworkLogWidget* netLog, MemView* memView);

namespace detail {

/**
 * @brief Analyzes whether jumping from @p currentLine to @p targetLine in @p sourceText crosses
 *        enclosing function, lexical block, or local variable initialization boundaries.
 * @return Empty string when safe, or a human-readable hazard description for confirmation dialogs.
 */
inline QString checkJumpLexicalScopeHazardImpl(const QString& sourceText, int currentLine,
                                               int targetLine, const QString& currentFunction = QString()) {
    if (sourceText.isEmpty() || targetLine <= 0) return QString();
    const QStringList rawLines = sourceText.split(QLatin1Char('\n'));
    if (targetLine > rawLines.size()) return QString();

    // Strip comments and string literals line-by-line while tracking brace depth and scope stacks.
    bool inBlockComment = false;
    QStringList cleanLines;
    cleanLines.reserve(rawLines.size());
    for (const QString& rl : rawLines) {
        QString out;
        bool inStr = false;
        for (int i = 0; i < rl.size(); ++i) {
            if (inBlockComment) {
                if (i + 1 < rl.size() && rl[i] == QLatin1Char('*') && rl[i + 1] == QLatin1Char('/')) {
                    inBlockComment = false;
                    ++i;
                }
                continue;
            }
            if (inStr) {
                if (rl[i] == QLatin1Char('\\') && i + 1 < rl.size()) { ++i; continue; }
                if (rl[i] == QLatin1Char('"')) inStr = false;
                continue;
            }
            if (i + 1 < rl.size() && rl[i] == QLatin1Char('/') && rl[i + 1] == QLatin1Char('/')) break;
            if (i + 1 < rl.size() && rl[i] == QLatin1Char('/') && rl[i + 1] == QLatin1Char('*')) {
                inBlockComment = true;
                ++i;
                continue;
            }
            if (rl[i] == QLatin1Char('"')) { inStr = true; out.append(QLatin1Char(' ')); continue; }
            out.append(rl[i]);
        }
        cleanLines.append(out.trimmed());
    }

    struct FuncRange { QString name; int startLine = 0; int endLine = 0; int bodyDepth = 1; };
    QVector<FuncRange> funcs;
    QVector<int> braceStack, curScopeStack, tgtScopeStack;
    QString pendingFuncName;
    int pendingFuncLine = -1, activeFuncIdx = -1;
    static const QRegularExpression kFuncHdrRe(
        QStringLiteral("(?:^|[\\s*&>])([~A-Za-z_][A-Za-z0-9_:]*)\\s*\\([^;]*\\)\\s*(?:const|noexcept|override|final|\\s)*\\{?$"));
    static const QSet<QString> kCtrlWords = {
        QStringLiteral("if"), QStringLiteral("else"), QStringLiteral("for"), QStringLiteral("while"),
        QStringLiteral("switch"), QStringLiteral("catch"), QStringLiteral("return"), QStringLiteral("sizeof")};

    for (int i = 0; i < cleanLines.size(); ++i) {
        const int ln = i + 1;
        const QString& cl = cleanLines.at(i);
        if (ln == currentLine) curScopeStack = braceStack;
        if (ln == targetLine) tgtScopeStack = braceStack;

        if (activeFuncIdx < 0 && !cl.isEmpty() && !cl.endsWith(QLatin1Char(';'))) {
            if (const auto fm = kFuncHdrRe.match(cl); fm.hasMatch() && !kCtrlWords.contains(fm.captured(1))) {
                pendingFuncName = fm.captured(1);
                pendingFuncLine = ln;
            }
        }
        for (QChar ch : cl) {
            if (ch == QLatin1Char('{')) {
                braceStack.append(ln);
                if (activeFuncIdx < 0 && pendingFuncLine > 0 && ln - pendingFuncLine <= 3) {
                    funcs.append({pendingFuncName, pendingFuncLine, static_cast<int>(rawLines.size()),
                                  static_cast<int>(braceStack.size())});
                    activeFuncIdx = funcs.size() - 1;
                    pendingFuncLine = -1;
                }
            } else if (ch == QLatin1Char('}') && !braceStack.isEmpty()) {
                if (activeFuncIdx >= 0 && static_cast<int>(braceStack.size()) == funcs[activeFuncIdx].bodyDepth) {
                    funcs[activeFuncIdx].endLine = ln;
                    activeFuncIdx = -1;
                }
                braceStack.removeLast();
            }
        }
    }

    // 1. Check whether targetLine leaves the enclosing function frame.
    const FuncRange* curFunc = nullptr;
    for (const auto& fr : funcs) {
        if (currentLine > 0 && currentLine >= fr.startLine && currentLine <= fr.endLine) { curFunc = &fr; break; }
        if (currentLine <= 0 && !currentFunction.isEmpty() && fr.name.endsWith(currentFunction)) { curFunc = &fr; break; }
    }
    if (curFunc && (targetLine < curFunc->startLine || targetLine > curFunc->endLine)) {
        return QStringLiteral("Target line %1 lies outside the current function '%2' (lines %3-%4). Jumping across function frames can corrupt the call stack, saved registers, and C++ destructors.")
            .arg(targetLine).arg(curFunc->name).arg(curFunc->startLine).arg(curFunc->endLine);
    }
    if (currentLine <= 0 || targetLine == currentLine) return QString();

    // 2. Check whether jumping crosses nested lexical { ... } block boundaries.
    if (curScopeStack != tgtScopeStack) {
        return QStringLiteral("Jumping from line %1 to line %2 crosses a lexical block scope boundary. Skipping block entry/exit may bypass C++ constructors or RAII destructors.")
            .arg(currentLine).arg(targetLine);
    }

    // 3. Check whether jumping crosses local variable declarations/initializations in the same block.
    static const QRegularExpression kVarDeclRe(
        QStringLiteral("^(?:(?:const|constexpr|static|volatile|mutable|unsigned|signed|long|short|struct|class)\\s+)*"
                       "(auto|bool|char|short|int|long|float|double|size_t|int\\d+_t|uint\\d+_t|[A-Za-z_][A-Za-z0-9_:<>]*)\\s*"
                       "(?:[*&]+\\s*)*([A-Za-z_][A-Za-z0-9_]*)\\s*(?:=|\\{|\\(|\\[|;)"));
    static const QSet<QString> kNonTypeWords = {
        QStringLiteral("return"), QStringLiteral("if"), QStringLiteral("else"), QStringLiteral("for"),
        QStringLiteral("while"), QStringLiteral("do"), QStringLiteral("switch"), QStringLiteral("case"),
        QStringLiteral("break"), QStringLiteral("continue"), QStringLiteral("goto"), QStringLiteral("throw"),
        QStringLiteral("delete"), QStringLiteral("typedef"), QStringLiteral("using"), QStringLiteral("co_return")};
    const int scanStart = qMin(currentLine, targetLine);
    const int scanEnd = qMax(currentLine, targetLine) - 1;
    QStringList crossedDecls;
    for (int ln = scanStart; ln <= scanEnd && ln <= cleanLines.size(); ++ln) {
        const QString& cl = cleanLines.at(ln - 1);
        if (cl.isEmpty() || cl.startsWith(QLatin1Char('#'))) continue;
        if (const auto dm = kVarDeclRe.match(cl); dm.hasMatch() && !kNonTypeWords.contains(dm.captured(1))) {
            crossedDecls.append(QString::number(ln));
        }
    }
    if (!crossedDecls.isEmpty()) {
        return QStringLiteral("Jumping from line %1 to line %2 crosses local variable initialization(s) on line(s) %3. Uninitialized stack variables or skipped C++ destructors may cause undefined behavior.")
            .arg(currentLine).arg(targetLine).arg(crossedDecls.join(QStringLiteral(", ")));
    }
    return QString();
}

}  // namespace detail
}  // namespace gridlock::ui
