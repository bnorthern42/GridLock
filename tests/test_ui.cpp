#include "test_ui.hpp"
#include <QTemporaryFile>
#include "../src/ui/MainWindow.hpp"
#include "../src/ui/views/SourceCodeView.hpp"
#include "../src/ui/views/ServerRackView.hpp"
#include "../src/ui/views/DisassemblyView.hpp"
#include "../src/ui/widgets/DifferentialGrid.hpp"
#include "../src/RankState.hpp"
#include <QApplication>
#include <QSplitter>
#include <QTextDocument>
#include <QTextBlock>
#include <QHash>
#include <QComboBox>
#include <QLineEdit>
#include "../src/core/managers/ConfigManager.hpp"
#include "../src/core/managers/ThemeManager.hpp"
#include "../src/ui/GdbConsoleWidget.hpp"
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
    QCOMPARE(masterHorizontal->count(), 2);
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
    doc.setPlainText("0x00555: movl $0x2, %eax");
    gridlock::ui::AsmSyntaxHighlighter highlighter(&doc);
    
    // Force rehighlight
    highlighter.rehighlight();
    
    // Test formatting of block 0
    QTextBlock block = doc.findBlockByLineNumber(0);
    QVector<QTextLayout::FormatRange> formats = block.layout()->formats();
    
    QString opcodeColor = gridlock::core::ConfigManager::instance().getAssemblyOpcode();
    QString registerColor = gridlock::core::ConfigManager::instance().getAssemblyRegister();
    QString addressColor = gridlock::core::ConfigManager::instance().getAssemblyAddress();

    bool foundOpcode = false;
    bool foundRegister = false;
    bool foundAddress = false;

    for (const auto& fmt : formats) {
        QString fmtColor = fmt.format.foreground().color().name(QColor::HexRgb);
        if (fmtColor == QColor(opcodeColor).name(QColor::HexRgb)) foundOpcode = true;
        if (fmtColor == QColor(registerColor).name(QColor::HexRgb)) foundRegister = true;
        if (fmtColor == QColor(addressColor).name(QColor::HexRgb)) foundAddress = true;
    }
    
    QVERIFY(foundOpcode);
    QVERIFY(foundRegister);
    QVERIFY(foundAddress);
}

void TestMainWindowUI::testSourceCodeHighlightBounds() {
    gridlock::ui::SourceCodeView view;
    QString code;
    for (int i = 1; i <= 100; ++i) {
        code += QString("int line_%1 = 0;\n").arg(i);
    }
    view.setSourceCode(code);
    
    // Highlight line 83 (GDB 1-based index) -> maps to block 82
    view.highlightCurrentLine(83);
    
    QTextBlock block = view.textCursor().block();
    QCOMPARE(block.blockNumber(), 82);
    
    // Highlight out of bounds (should not crash)
    view.highlightCurrentLine(200);
    view.highlightCurrentLine(0);
    view.highlightCurrentLine(-1);
    
    // Assert no exceptions
    QVERIFY(true);
}

void TestMainWindowUI::testDifferentialGridExpansion() {
    gridlock::ui::DifferentialGrid grid;
    
    // Programmatically inject 15 variables across 6 ranks via updateVariableDisplay
    for (int rankId = 0; rankId < 6; ++rankId) {
        for (int v = 0; v < 15; ++v) {
            QString varName = QString("var_%1").arg(v);
            QString value = (rankId == 5 && v == 0) ? "divergent_value" : "100";
            grid.updateVariableDisplay(rankId, varName, value);
        }
    }
    
    // Verify dynamic row/col expansion
    QCOMPARE(grid.rowCount(), 7); // 1 header + 6 ranks
    QCOMPARE(grid.columnCount(), 16); // 1 header + 15 variables
    
    // Find var_0 column
    int colVar0 = -1;
    for (int c = 1; c < grid.columnCount(); ++c) {
        if (grid.horizontalHeaderItem(c)->text() == "var_0") {
            colVar0 = c;
            break;
        }
    }
    QVERIFY(colVar0 != -1);

    // Assert majority-highlighting on Rank 0 (Normal)
    QTableWidgetItem* itemNormal = grid.item(1, colVar0);
    QVERIFY(itemNormal != nullptr);
    QCOMPARE(itemNormal->text(), QString("100"));
    QCOMPARE(itemNormal->background(), QBrush());
    
    // Assert majority-highlighting on Rank 5 (Divergent)
    QTableWidgetItem* itemDivergent = grid.item(6, colVar0);
    QVERIFY(itemDivergent != nullptr);
    QCOMPARE(itemDivergent->text(), QString("divergent_value"));
    QCOMPARE(itemDivergent->background().color(), QColor(255, 200, 200));
}

void TestMainWindowUI::testDifferentialGridEmits() {
    gridlock::ui::DifferentialGrid grid;
    int emitCount = 0;
    QObject::connect(&grid, &gridlock::ui::DifferentialGrid::watchVariableAdded, [&emitCount](const QString& /*name*/) {
        emitCount++;
    });

    QHash<QString, QString> vars;
    vars["offset"] = "123";
    grid.setVariableData(0, vars);
    
    // Should NOT emit watchVariableAdded for the placeholder "-" or anything else during setVariableData
    QCOMPARE(emitCount, 0);

    // Editing row 0 col 0 SHOULD emit
    grid.item(0, 0)->setText("mtype");
    QCOMPARE(emitCount, 1);
}

void TestMainWindowUI::testGdbConsoleFiltering() {
    gridlock::ui::GdbConsoleWidget console;
    console.resetRanks(4); // Creates Rank 0..3

    console.appendGdbOutput(0, "~\"Breakpoint hit at main\\n\"");
    console.appendGdbOutput(1, "~\"Segfault encountered\\n\"");
    console.appendGdbOutput(0, "~\"Variable x = 5\\n\"");

    QPlainTextEdit* consoleEdit = console.findChild<QPlainTextEdit*>();
    QComboBox* rankCombo = console.findChild<QComboBox*>();
    QLineEdit* filterEdit = console.findChild<QLineEdit*>();

    QVERIFY(consoleEdit != nullptr);
    QVERIFY(rankCombo != nullptr);
    QVERIFY(filterEdit != nullptr);

    // Verify Rank Filtering (Index 2 -> Rank 1, since Index 0 is "All Ranks", Index 1 is "Rank 0")
    rankCombo->setCurrentIndex(2);
    QCOMPARE(consoleEdit->toPlainText(), QString("Segfault encountered"));

    // Verify Text Filtering
    rankCombo->setCurrentIndex(0); // Reset to All Ranks
    filterEdit->setText("Variable");
    QCOMPARE(consoleEdit->toPlainText(), QString("Variable x = 5"));
}

void TestMainWindowUI::testGutterBreakpointPropagation() {
    gridlock::ui::SourceCodeView view;
    QString code;
    for (int i = 1; i <= 10; ++i) {
        code += QString("Line %1\n").arg(i);
    }
    view.setSourceCode(code);
    view.setCurrentFile("/test/dummy.cpp");
    
    // Headless layout initialization
    view.resize(800, 600);
    QApplication::processEvents();

    struct ViewSpy : public gridlock::ui::SourceCodeView {
        int getY(int line) {
            QTextBlock b = document()->findBlockByLineNumber(line - 1);
            return qRound(blockBoundingGeometry(b).translated(contentOffset()).top()) + 2;
        }
    };
    int yPos = static_cast<ViewSpy*>(&view)->getY(5);

    int signalFired = 0;
    int signaledLine = -1;
    QObject::connect(&view, &gridlock::ui::SourceCodeView::breakpointToggled, [&](const QString& /*file*/, int line, bool /*isSet*/, const QString& /*condition*/) {
        signalFired++;
        signaledLine = line;
    });

    // Use the modern QMouseEvent constructor (device-aware, replaces the deprecated 5-arg form)
    QMouseEvent event(QEvent::MouseButtonPress, QPointF(15, yPos), QPointF(15, yPos),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    view.lineNumberAreaMousePressEvent(&event);

    QCOMPARE(signalFired, 1);
    QCOMPARE(signaledLine, 5);
}

QTEST_MAIN(TestMainWindowUI)
