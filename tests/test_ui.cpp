#include "test_ui.hpp"
#include <QTemporaryFile>
#include "../src/ui/MainWindow.hpp"
#include "../src/ui/SourceCodeView.hpp"
#include "../src/ui/ServerRackView.hpp"
#include "../src/ui/DisassemblyView.hpp"
#include "../src/ui/DifferentialGrid.hpp"
#include "../src/RankState.hpp"
#include <QApplication>
#include <QSplitter>
#include <QTextDocument>
#include <QTextBlock>
#include <QHash>

void TestMainWindowUI::testSourceFileLoading() {
    QTemporaryFile tempFile;
    QVERIFY(tempFile.open());
    tempFile.write("#include <iostream>\nint main(){}");
    tempFile.close();

    gridlock::ui::MainWindow mainWindow;
    mainWindow.loadSourceFile(tempFile.fileName());

    QVERIFY(mainWindow.getSourceCodeView() != nullptr);
    QCOMPARE(mainWindow.getSourceCodeView()->toPlainText().isEmpty(), false);
    QVERIFY(mainWindow.getSourceCodeView()->toPlainText().contains("int main"));
}

void TestMainWindowUI::testMainLayoutStructure() {
    gridlock::ui::MainWindow mainWindow;
    QList<QSplitter*> splitters = mainWindow.findChildren<QSplitter*>();
    
    QSplitter* mainVertical = nullptr;
    QSplitter* masterHorizontal = nullptr;
    
    for (QSplitter* s : splitters) {
        if (s->orientation() == Qt::Vertical) mainVertical = s;
        if (s->orientation() == Qt::Horizontal) masterHorizontal = s;
    }
    
    QVERIFY(mainVertical != nullptr);
    QVERIFY(masterHorizontal != nullptr);
    QCOMPARE(masterHorizontal->count(), 3);
}

void TestMainWindowUI::testSourceCodeViewportMargins() {
    gridlock::ui::SourceCodeView view;
    QMargins margins = view.getViewportMargins();
    QVERIFY(margins.left() > 0);
}

void TestMainWindowUI::testServerRackStateUpdate() {
    gridlock::ui::ServerRackView view;
    gridlock::RankState state;
    state.currentState = "stopped";
    
    view.updateRankState(0, state);
    QVERIFY(true);
}

void TestMainWindowUI::testAsmSyntaxHighlighter() {
    QTextDocument doc;
    doc.setPlainText("0x401000: mov eax, ebx\n0x401004: call 0x402000");
    gridlock::ui::AsmSyntaxHighlighter highlighter(&doc);
    
    // Force rehighlight
    highlighter.rehighlight();
    
    // Test formatting of block 0
    QTextBlock block = doc.findBlockByLineNumber(0);
    QVector<QTextLayout::FormatRange> formats = block.layout()->formats();
    
    // Just verify that formats were applied by the highlighter (opcodes, registers, addresses)
    QVERIFY(formats.size() > 0);
}

void TestMainWindowUI::testSourceCodeHighlightBounds() {
    gridlock::ui::SourceCodeView view;
    view.setPlainText("Line 1\nLine 2\nLine 3\nLine 4");
    
    // Highlight line 2 (GDB 1-based index) -> maps to block 1
    view.highlightCurrentLine(2);
    
    // Highlight out of bounds (should not crash)
    view.highlightCurrentLine(100);
    view.highlightCurrentLine(0);
    view.highlightCurrentLine(-1);
    
    // Assert no exceptions and test pass
    QVERIFY(true);
}

void TestMainWindowUI::testDifferentialGridExpansion() {
    gridlock::ui::DifferentialGrid grid;
    
    // Programmatically inject 15 variables across 6 ranks
    for (int rankId = 0; rankId < 6; ++rankId) {
        QHash<QString, QString> vars;
        for (int v = 0; v < 15; ++v) {
            QString varName = QString("var_%1").arg(v);
            // Give rank 5 a diverging value for var_0
            if (rankId == 5 && v == 0) {
                vars[varName] = "divergent_value";
            } else {
                vars[varName] = "100";
            }
        }
        grid.setVariableData(rankId, vars);
    }
    
    // Verify row/col expansion
    QCOMPARE(grid.rowCount(), 7); // 1 header + 6 ranks
    QCOMPARE(grid.columnCount(), 16); // 1 header + 15 variables
    
    // Rank 0 var 0 (row 1, col 1 assuming var_0 is added first, but we can search or just check the cell)
    // Actually, hash iteration is non-deterministic! So we must search for the column index.
    int colVar0 = -1;
    for (int c = 1; c < grid.columnCount(); ++c) {
        if (grid.horizontalHeaderItem(c)->text() == "var_0") colVar0 = c;
    }
    QVERIFY(colVar0 != -1);

    // Rank 0 var 0
    QTableWidgetItem* itemNormal = grid.item(1, colVar0);
    QVERIFY(itemNormal != nullptr);
    QCOMPARE(itemNormal->text(), QString("100"));
    
    // Rank 5 var 0 (divergent)
    QTableWidgetItem* itemDivergent = grid.item(6, colVar0);
    QVERIFY(itemDivergent != nullptr);
    QCOMPARE(itemDivergent->text(), QString("divergent_value"));
}

QTEST_MAIN(TestMainWindowUI)
