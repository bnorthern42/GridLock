/**
 * @file DisassemblyView.hpp
 * @brief Interactive disassembly, control-flow graph, and decompiler view widget.
 *
 * Provides linear assembly browsing with branch flow arcs and bookmark gutter,
 * an interactive 2D Control Flow Graph (CFG) canvas with Manhattan edge routing,
 * MPI rank divergence indicators, and multi-language structured decompilation.
 */

#pragma once
#include <QCheckBox>
#include <QComboBox>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QLabel>
#include <QList>
#include <QMap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QSplitter>
#include <QStackedWidget>
#include <QString>
#include <QStringList>
#include <QSyntaxHighlighter>
#include <QTextCursor>
#include <QTextDocument>
#include <QVector>
#include <QWidget>
#include <cstdint>

namespace gridlock::ui {

/**
 * @brief Represents a directed control-flow branch arc connecting two assembly lines.
 */
struct BranchArc {
    int fromLine = -1;       ///< 0-based source instruction line index.
    int toLine = -1;         ///< 0-based destination instruction line index.
    uint64_t fromAddr = 0;   ///< Originating instruction pointer address.
    uint64_t toAddr = 0;     ///< Target instruction pointer address.
    bool isConditional = false; ///< True if branch is conditional; false if unconditional.
};

/**
 * @brief Structured cross-reference record between two instruction addresses.
 */
struct XrefRecord {
    uint64_t fromAddress = 0;   ///< Calling or branching originating address.
    uint64_t toAddress = 0;     ///< Target destination address.
    QString type;               ///< Reference classification ("CALL", "JMP", "CJMP", "DATA").
    QString mnemonic;           ///< Instruction mnemonic (e.g. call, jmp, je, movsd).
    QString instructionText;    ///< Full disassembly instruction string.
    QString targetSymbol;       ///< Symbolic target or label annotation if resolved.
};

class XrefInspectorDialog;

/**
 * @brief Represents an isolated basic block of linear instructions for CFG analysis.
 */
struct DisasmBasicBlock {
    uint64_t startAddress = 0;  ///< Entry instruction pointer address.
    uint64_t endAddress = 0;    ///< Terminating instruction pointer address.
    QStringList lines;          ///< Raw formatted instruction text lines.
    QList<uint64_t> successors; ///< List of outgoing successor basic block addresses.
    uint64_t trueTarget = 0;    ///< Branch target address taken when condition evaluates true.
    uint64_t falseTarget = 0;   ///< Fallthrough branch target address.
};

/**
 * @brief Syntax highlighter for x86_64 and generic assembly listings.
 */
class AsmSyntaxHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit AsmSyntaxHighlighter(QTextDocument *parent = nullptr);
    void reloadRules();
protected:
    void highlightBlock(const QString &text) override;
private:
    struct HighlightingRule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };
    QVector<HighlightingRule> highlightingRules;
};

class DisassemblyTextEdit;
class BranchFlowGutter;

/**
 * @brief Interactive hybrid assembly listing, CFG graph, and decompiler widget.
 */
class DisassemblyView : public QWidget {
    Q_OBJECT
public:
    explicit DisassemblyView(QWidget *parent = nullptr);
    ~DisassemblyView() override = default;

    /// Number of detected branch flow arcs.
    int branchArcCount() const { return m_branchArcs.size(); }
    /// Number of partitioned basic blocks.
    int basicBlockCount() const { return m_basicBlocks.size(); }
    /// True if displaying the 2D CFG scene; false if linear assembly text.
    bool isGraphMode() const { return m_graphMode; }
    void setGraphMode(bool enabled);
    /// Toggles view mode between linear assembly and 2D CFG graph.
    void toggleViewMode();
    /// Seeks disassembly, graph, and decompiler views to an instruction address.
    bool seekToAddress(uint64_t address);
    /// Returns the currently active 64-bit instruction pointer address.
    uint64_t currentAddress() const { return m_currentAddress; }

    // Compatibility helpers for QPlainTextEdit-like inspection
    QString toPlainText() const;
    void setPlainText(const QString& text);
    QTextDocument* document() const;
    QTextCursor textCursor() const;
    void setTextCursor(const QTextCursor& cursor);
    void centerCursor();

    /// Returns list of detected control flow branch arcs.
    const QVector<BranchArc>& branchArcs() const { return m_branchArcs; }
    /// Returns partitioned basic blocks.
    const QVector<DisasmBasicBlock>& basicBlocks() const { return m_basicBlocks; }
    /// Maps 64-bit instruction address to linear line number.
    int lineForAddress(uint64_t address) const { return m_addrToLine.value(address, -1); }
    /// Maps linear line number to 64-bit instruction address.
    uint64_t addressForLine(int line) const { return m_lineToAddr.value(line, 0ULL); }
    /// Returns callers jumping or branching to targetAddr.
    QVector<uint64_t> xrefsToAddress(uint64_t targetAddr) const;
    /// Returns incoming branch, call, and loop edges targeting targetAddr.
    QVector<XrefRecord> incomingXrefs(uint64_t targetAddr) const;
    /// Returns outgoing calls, branches, and data references from sourceAddr or function.
    QVector<XrefRecord> outgoingXrefs(uint64_t sourceAddr) const;
    /// Opens or refreshes the interactive Xref and call-graph inspector dialog.
    XrefInspectorDialog* openXrefInspector(uint64_t targetAddr);
    /// Returns currently generated decompiled source text.
    QString decompiledPseudocode() const { return m_decompiledPseudocode; }
    /// Checks whether standalone decompiler mode is enabled.
    bool isDecompilerMode() const { return m_decompilerMode; }
    /// Sets standalone decompiler mode active or inactive.
    void setDecompilerMode(bool enabled);

    /// Sets active decompiler target language ("C", "C++", "Pseudocode").
    void setDecompilerTargetLanguage(const QString& lang);
    /// Returns active decompiler target language.
    QString decompilerTargetLanguage() const { return m_targetLang; }
    /// Translates active function instructions into target language.
    QString decompileToLanguage(const QString& lang) const;
    /// Returns reconstructed high-level function signature.
    QString functionSignatureHeader() const { return m_functionSignature; }
    /// Re-decompiles and refreshes the decompiler view.
    void refreshDecompilerView();

    /// Updates the sub-header banner to prominently display the active stepping rank and current PC.
    void setActiveSteppingRank(int rank, uint64_t pc);
    /// Returns the sub-header banner label widget.
    QLabel* functionBannerLabel() const { return m_functionBannerLabel; }

    /// Sets side-by-side CFG + Decompiler split mode.
    void setSplitGraphDecompilerMode(bool enabled);
    /// Checks whether side-by-side split mode is active.
    bool isSplitGraphDecompilerMode() const { return m_splitGraphDecompiler; }

    /// Maps 1-based decompiler source line to assembly address.
    uint64_t decompilerLineToAddress(int lineNumber) const;
    /// Maps assembly address to 1-based decompiler line.
    int addressToDecompilerLine(uint64_t address) const;
    /// Returns currently active 1-based decompiler line.
    int currentDecompilerLine() const { return m_currentDecompilerLine; }
    /// Selects decompiler line and seeks matching assembly address.
    bool selectDecompilerLine(int lineNumber);
    /// Highlights token across disassembly and decompiler views.
    void highlightToken(const QString& token);
    /// Returns currently highlighted search token.
    QString highlightedToken() const { return m_highlightedToken; }

    /// Updates program counter locations for all active MPI ranks.
    void setRankAddresses(const QMap<int, uint64_t>& rankPcs);
    /// Returns list of MPI ranks currently within the given basic block.
    QVector<int> ranksAtBlock(uint64_t blockStartAddr) const;
    /// Sets the focused MPI rank ID.
    void setSelectedRank(int rankId);
    /// Returns the currently focused MPI rank ID.
    int selectedRank() const { return m_selectedRank; }

    /// Sets the list of MPI ranks participating in a detected deadlock wait-for cycle.
    void setDeadlockCycleRanks(const QVector<int>& cycleRanks);
    /// Returns the list of MPI ranks participating in a deadlock wait-for cycle.
    QVector<int> deadlockCycleRanks() const { return m_deadlockCycleRanks; }

    /// Applies live assembly instruction patch at instruction pointer address.
    bool patchInstruction(uint64_t address, const QString& newInstruction);
    /// Returns total number of actively patched assembly instructions.
    int patchedInstructionCount() const { return m_patchedInstructions.size(); }
    /// Reverts all actively applied instruction patches.
    void revertAllPatches();

    /// Sets visibility of the CFG minimap overlay.
    void setGraphMinimapVisible(bool visible);
    /// Checks whether the CFG minimap overlay is visible.
    bool isGraphMinimapVisible() const { return m_graphMinimapVisible; }
    /// Computes viewport coordinates for the CFG minimap overlay.
    QRect minimapRect() const;

    /// Attaches user comment annotation to an instruction address.
    void setAddressComment(uint64_t address, const QString& comment);
    /// Retrieves user comment annotation for an instruction address.
    QString addressComment(uint64_t address) const;
    /// Toggles user bookmark at an instruction address.
    bool toggleBookmark(uint64_t address);
    /// Checks whether an address has an active user bookmark.
    bool isBookmarked(uint64_t address) const;
    /// Returns sorted list of all active user bookmark addresses.
    QVector<uint64_t> bookmarkedAddresses() const;

    /// Returns ordered list of cross-view context menu action labels for an instruction address.
    QStringList contextActionNamesForAddress(uint64_t address) const;
    /// Executes a named cross-view context menu action for the specified instruction address.
    bool triggerContextAction(uint64_t address, const QString& actionName);
    /// Returns the unclipped pixel width computed for the CFG basic block starting at blockStartAddr.
    int blockWidthForAddress(uint64_t blockStartAddr) const;
    /// Returns the address of the instruction immediately following currentAddr in the parsed listing.
    uint64_t nextInstructionAddress(uint64_t currentAddr) const;
    /// Checks whether the rendered CFG basic block containing addr displays an active instruction marker.
    bool activeCfgBlockContainsInstructionMarker(uint64_t addr) const;
    /// Returns the active point size of the linear assembly and decompiler font.
    int codeFontPointSize() const;


signals:
    /// Emitted when an instruction address is selected by click or seek.
    void addressSelected(uint64_t address);
    /// Emitted when the user selects "Seek in Memory View" on an instruction address.
    void seekInMemoryViewRequested(uint64_t address);
    /// Emitted when an assembly instruction is patched.
    void instructionPatched(uint64_t address, const QByteArray& newBytes);
    /// Emitted when the user requests Reference Manual documentation for a symbol or mnemonic.
    void documentationRequested(const QString& symbol);

public slots:
    /// Loads and analyzes new assembly code listing.
    void updateDisassembly(const QString& asmCode);
    /// Compatibility alias for updateDisassembly.
    void setDisassembly(const QString& asmCode) { updateDisassembly(asmCode); }
    /// Reloads appearance styling and fonts from configuration and ThemeManager.
    void reloadStyle();
    /// Returns the active canvas background QColor resolved from ThemeManager.
    QColor activeCanvasBackgroundColor() const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    friend class DisassemblyTextEdit;
    friend class BranchFlowGutter;

    void parseDisassembly(const QString& asmCode);
    void rebuildCfgScene();
    void updateViewModeLayout();
    void highlightActiveInstruction();
    void highlightActiveDecompilerLine();
    void setupDecompilerUi(QSplitter* splitSplitter, const QFont& font);
    QString applyUserCommentsToListing(const QString& rawCode) const;
    QString applyPatchesToListing(const QString& rawCode) const;
    static QString defaultStartupDisassembly();

    QLabel* m_headerBadge = nullptr;
    QLabel* m_signatureLabel = nullptr;
    QLabel* m_functionBannerLabel = nullptr;
    QLabel* m_statsBadge = nullptr;
    QPushButton* m_modeToggleBtn = nullptr;
    QPushButton* m_decompilerToggleBtn = nullptr;
    QPushButton* m_splitToggleBtn = nullptr;
    QStackedWidget* m_stack = nullptr;
    QWidget* m_splitViewContainer = nullptr;
    QWidget* m_decompilerPage = nullptr;
    DisassemblyTextEdit* m_linearEdit = nullptr;
    QGraphicsView* m_cfgView = nullptr;
    QGraphicsScene* m_cfgScene = nullptr;
    QPlainTextEdit* m_decompilerEdit = nullptr;
    QCheckBox* m_autoRefreshCheck = nullptr;
    QPushButton* m_refreshDecompilerBtn = nullptr;
    QComboBox* m_targetLangCombo = nullptr;
    AsmSyntaxHighlighter* m_highlighter = nullptr;

    QVector<BranchArc> m_branchArcs;
    QVector<DisasmBasicBlock> m_basicBlocks;
    QMap<uint64_t, int> m_addrToLine;
    QMap<int, uint64_t> m_lineToAddr;
    QMap<int, uint64_t> m_rankAddresses;
    mutable QMap<int, uint64_t> m_decompilerLineToAddr;
    mutable QMap<uint64_t, int> m_addrToDecompilerLine;
    QMap<uint64_t, QString> m_userComments;
    QMap<uint64_t, QString> m_patchedInstructions;
    QVector<int> m_deadlockCycleRanks;
    QSet<uint64_t> m_bookmarks;
    QString m_baseDisassembly;
    uint64_t m_currentAddress = 0x401120ULL;
    int m_selectedRank = 0;
    int m_currentDecompilerLine = 1;
    bool m_updatingDecompilerCursor = false;
    bool m_graphMode = false;
    bool m_decompilerMode = false;
    bool m_splitGraphDecompiler = false;
    bool m_graphMinimapVisible = true;
    QString m_activeSymbol = QStringLiteral("sym.main");
    QString m_functionSignature = QStringLiteral("int sym.main(int argc, char **argv)");
    QString m_targetLang = QStringLiteral("C");
    QString m_decompiledPseudocode;
    QString m_highlightedToken;
    XrefInspectorDialog* m_xrefDialog = nullptr;
    QVector<XrefRecord> m_xrefs;
};

}  // namespace gridlock::ui

