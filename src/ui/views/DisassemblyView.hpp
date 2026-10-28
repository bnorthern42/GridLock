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

struct BranchArc {
    int fromLine = -1;
    int toLine = -1;
    uint64_t fromAddr = 0;
    uint64_t toAddr = 0;
    bool isConditional = false;
};

struct DisasmBasicBlock {
    uint64_t startAddress = 0;
    uint64_t endAddress = 0;
    QStringList lines;
    QList<uint64_t> successors;
    uint64_t trueTarget = 0;
    uint64_t falseTarget = 0;
};

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

class DisassemblyView : public QWidget {
    Q_OBJECT
public:
    explicit DisassemblyView(QWidget *parent = nullptr);
    ~DisassemblyView() override = default;

    int branchArcCount() const { return m_branchArcs.size(); }
    int basicBlockCount() const { return m_basicBlocks.size(); }
    bool isGraphMode() const { return m_graphMode; }
    void setGraphMode(bool enabled);
    void toggleViewMode();
    bool seekToAddress(uint64_t address);
    uint64_t currentAddress() const { return m_currentAddress; }

    // Compatibility helpers for QPlainTextEdit-like inspection
    QString toPlainText() const;
    void setPlainText(const QString& text);
    QTextDocument* document() const;
    QTextCursor textCursor() const;
    void setTextCursor(const QTextCursor& cursor);
    void centerCursor();

    const QVector<BranchArc>& branchArcs() const { return m_branchArcs; }
    const QVector<DisasmBasicBlock>& basicBlocks() const { return m_basicBlocks; }
    int lineForAddress(uint64_t address) const { return m_addrToLine.value(address, -1); }
    uint64_t addressForLine(int line) const { return m_lineToAddr.value(line, 0ULL); }
    QVector<uint64_t> xrefsToAddress(uint64_t targetAddr) const;
    QString decompiledPseudocode() const { return m_decompiledPseudocode; }
    bool isDecompilerMode() const { return m_decompilerMode; }
    void setDecompilerMode(bool enabled);

    void setDecompilerTargetLanguage(const QString& lang);
    QString decompilerTargetLanguage() const { return m_targetLang; }
    QString decompileToLanguage(const QString& lang) const;
    QString functionSignatureHeader() const { return m_functionSignature; }
    void refreshDecompilerView();

    void setSplitGraphDecompilerMode(bool enabled);
    bool isSplitGraphDecompilerMode() const { return m_splitGraphDecompiler; }

    uint64_t decompilerLineToAddress(int lineNumber) const;
    int addressToDecompilerLine(uint64_t address) const;
    int currentDecompilerLine() const { return m_currentDecompilerLine; }
    bool selectDecompilerLine(int lineNumber);
    void highlightToken(const QString& token);
    QString highlightedToken() const { return m_highlightedToken; }

    void setRankAddresses(const QMap<int, uint64_t>& rankPcs);
    QVector<int> ranksAtBlock(uint64_t blockStartAddr) const;
    void setSelectedRank(int rankId);
    int selectedRank() const { return m_selectedRank; }

    void setGraphMinimapVisible(bool visible);
    bool isGraphMinimapVisible() const { return m_graphMinimapVisible; }
    QRect minimapRect() const;

    void setAddressComment(uint64_t address, const QString& comment);
    QString addressComment(uint64_t address) const;
    bool toggleBookmark(uint64_t address);
    bool isBookmarked(uint64_t address) const;
    QVector<uint64_t> bookmarkedAddresses() const;

signals:
    void addressSelected(uint64_t address);

public slots:
    void updateDisassembly(const QString& asmCode);
    void setDisassembly(const QString& asmCode) { updateDisassembly(asmCode); }
    void reloadStyle();

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
    QString applyUserCommentsToListing(const QString& rawCode) const;
    static QString defaultStartupDisassembly();

    QLabel* m_headerBadge = nullptr;
    QLabel* m_signatureLabel = nullptr;
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
};

} // namespace gridlock::ui

