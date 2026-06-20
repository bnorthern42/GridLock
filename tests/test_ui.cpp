#include "test_ui.hpp"
#include <QTemporaryFile>
#include "../src/ui/MainWindow.hpp"
#include "../src/ui/SourceCodeView.hpp"
#include <QApplication>

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

QTEST_MAIN(TestMainWindowUI)
