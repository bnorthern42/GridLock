/** @file test_ui_debugger.cpp @brief Integration tests for stepping, breakpoints, checkpoints, and multi-rank controls. */
#include "test_ui.hpp"
#include <QDir>
#include <QtWidgets>
#include <QtTest/QtTest>
#include <cmath>
#include <csignal>
#include <unistd.h>
#include "../../src/core/hpc/gdb/GdbRankCoordinator.hpp"
#include "../../src/core/hpc/IBackendCoordinator.hpp"
#include "../../src/core/managers/ConfigManager.hpp"
#include "../../src/core/managers/ThemeManager.hpp"
#include "../../src/core/utils/CliParser.hpp"
#include "../../src/ui/EditorTabManager.hpp"
#include "../../src/ui/GdbConsoleWidget.hpp"
#include "../../src/ui/MainWindow.hpp"
#include "../../src/ui/dialogs/plotter/ArrayPlotDialog.hpp"
#include "../../src/ui/dialogs/project/ProjectSettingsDialog.hpp"
#include "../../src/ui/dialogs/project/ProjectWizard.hpp"
#include "../../src/ui/dialogs/debugger/AttachProcessDialog.hpp"
#include "../../src/ui/dialogs/debugger/CheckpointDialog.hpp"
#include "../../src/ui/dialogs/search/CommandPaletteDialog.hpp"
#include "../../src/ui/dialogs/debugger/ConditionalBreakpointDialog.hpp"
#include "../../src/ui/dialogs/graph/PointerGraphDialog.hpp"
#include "../../src/ui/dialogs/preferences/PreferencesDialog.hpp"
#include "../../src/ui/dialogs/debugger/SignalCatchpointDialog.hpp"
#include "../../src/ui/docks/TerminalDockWidget.hpp"
#include "../../src/ui/docks/VariablesDockWidget.hpp"
#include "../../src/ui/views/DisassemblyView.hpp"
#include "../../src/ui/views/MemView.hpp"
#include "../../src/ui/views/RegisterView.hpp"
#include "../../src/ui/views/ServerRackView.hpp"
#include "../../src/ui/views/SourceCodeView.hpp"
#include "../../src/ui/widgets/DifferentialGrid.hpp"
#include "../../src/ui/widgets/ExpressionEvaluatorWidget.hpp"
#include "../../src/ui/widgets/MpiDiagnosticsWidget.hpp"
void TestMainWindowUI::testDynamicSourceMappingLoopBranchSteppingAndRankContext() {
    gridlock::ui::MainWindow mainWindow; mainWindow.resize(1440, 900); mainWindow.loadPreviewFixture();
    if (!mainWindow.findChild<gridlock::ui::SourceCodeView*>()) mainWindow.loadSourceFile(QFileInfo::exists(QStringLiteral("tutorial/inspection_demo.cpp")) ? QStringLiteral("tutorial/inspection_demo.cpp") : QStringLiteral("../tutorial/inspection_demo.cpp"));
    mainWindow.show(); QVERIFY(QTest::qWaitForWindowExposed(&mainWindow)); QApplication::processEvents();
    auto* sourceView = mainWindow.findChild<gridlock::ui::SourceCodeView*>(); auto* disasmView = mainWindow.findChild<gridlock::ui::DisassemblyView*>(); auto* serverRack = mainWindow.findChild<gridlock::ui::ServerRackView*>();
    QVERIFY(sourceView != nullptr && disasmView != nullptr && serverRack != nullptr);
    sourceView->setPlainText(QStringLiteral("// 1\n// 2\n// 3\n// 4\n// 5\n") + sourceView->toPlainText()); mainWindow.rebuildDynamicSourceLineMap();
    const int initLine = mainWindow.findSourceLineForStatement(QStringLiteral("MPI_Init")), finalizeLine = mainWindow.findSourceLineForStatement(QStringLiteral("MPI_Finalize")), returnLine = mainWindow.findSourceLineForStatement(QStringLiteral("return 0"));
    QVERIFY2(initLine > 5 && finalizeLine > initLine && returnLine >= finalizeLine, "Dynamic scanner must locate shifted statements");
    auto checkLine = [&](int l, uint64_t a) { QCOMPARE(mainWindow.sourceLineToAddress(l), a); QCOMPARE(mainWindow.addressToSourceLine(a), l); };
    checkLine(initLine, 0x0040113dULL); checkLine(finalizeLine, 0x00401178ULL); checkLine(returnLine, 0x00401183ULL);
    QCOMPARE(mainWindow.activeSteppingRank(), 0); serverRack->selectRank(2); QApplication::processEvents(); QCOMPARE(mainWindow.activeSteppingRank(), 2); serverRack->selectRank(0); QApplication::processEvents(); QCOMPARE(mainWindow.activeSteppingRank(), 0);
    sourceView->setBreakpoints(QSet<int>()); mainWindow.stopTargetWorkflow(); mainWindow.startTargetWorkflow(); QApplication::processEvents(); QCOMPARE(disasmView->currentAddress(), 0x00401120ULL);
    while (disasmView->currentAddress() < 0x00401149ULL) { mainWindow.stepInstructionWorkflow(); QApplication::processEvents(); }
    QCOMPARE(disasmView->currentAddress(), 0x00401149ULL);
    auto stepTo = [&](uint64_t addr) { mainWindow.stepInstructionWorkflow(); QCOMPARE(disasmView->currentAddress(), addr); };
    stepTo(0x0040114dULL); stepTo(0x0040114fULL); stepTo(0x00401152ULL); stepTo(0x00401155ULL); stepTo(0x00401168ULL); stepTo(0x0040116aULL); stepTo(0x0040116eULL); stepTo(0x00401149ULL);
    for (int iter = 1; iter <= 2; ++iter) { for (int s = 0; s < 7; ++s) mainWindow.stepInstructionWorkflow(); QCOMPARE(disasmView->currentAddress(), 0x0040116eULL); stepTo(0x00401149ULL); }
    stepTo(0x0040114dULL); stepTo(0x00401178ULL); for (int s = 0; s < 3; ++s) mainWindow.stepInstructionWorkflow();
    QCOMPARE(disasmView->currentAddress(), 0x00401183ULL); QCOMPARE(sourceView->executionLine(), returnLine);
    mainWindow.stopTargetWorkflow(); serverRack->selectRank(2); mainWindow.startTargetWorkflow(); QApplication::processEvents();
    while (disasmView->currentAddress() < 0x00401155ULL) { mainWindow.stepInstructionWorkflow(); QApplication::processEvents(); }
    QCOMPARE(disasmView->currentAddress(), 0x00401155ULL); stepTo(0x00401157ULL); mainWindow.stepInstructionWorkflow(); stepTo(0x00401163ULL); stepTo(0x00401149ULL);
    auto& cfg = gridlock::core::ConfigManager::instance(); QCOMPARE(cfg.defaultDisassemblyMode(), QStringLiteral("cfg"));
    const QString liveGdbAsm = QStringLiteral("0x00400550 <main+0x0>: push rbp ; line 12\n0x00400554 <main+0x4>: mov rbp, rsp ; line 12\n0x00400560 <main+0x10>xcall 00400420 <MPI_Init@plt> ; line 14\n0x0040056b <main+0x1b>: cmp dword [rbp-0x4], 0x2 ; line 16\n0x0040056f <main+0x1f>: jg 00400658 ; line 16\n0x00400575 <main+0x25>: cmp dword [rbp-0x8], 0x0 ; line 17\n0x00400579 <main+0x29>: je 00400600 ; line 17\n0x0040057f <main+0x2f>: add dword [rbp-0x4], 0x1 ; line 18\n0x00400585 <main+0xc5>xjmp 0040056b ; line 18\n0x00400600 <main+0xb0>: xor eax, eax ; line 19\n0x00400602 <main+0x1aa>xjmp 00400650 ; line 16\n0x00400650 <main+0x100>: add dword [rbp-0x4], 0x1 ; line 20\n0x00400654 <main+0x104>: jmp 0040056b ; line 20\n0x00400658 <main+0x108>: leave ; line 22\n0x00400659 <main+0x109>: ret ; line 22");
    disasmView->setGraphMode(true); disasmView->updateDisassembly(liveGdbAsm); QApplication::processEvents();
    QVERIFY2(disasmView->basicBlockCount() >= 4 && disasmView->cfgEdgeCount() >= 3, qPrintable(QStringLiteral("CFG collapsed: BB=%1 E=%2").arg(disasmView->basicBlockCount()).arg(disasmView->cfgEdgeCount())));
    QVERIFY(mainWindow.grab().save(QStringLiteral("/tmp/cfg_live_stepping_verify.png"))); QFile::remove(QStringLiteral("/tmp/cfg_live_stepping_verify.png"));
    disasmView->seekToAddress(0x0040056bULL); mainWindow.stopTargetWorkflow(); QApplication::processEvents();
    QVERIFY2(disasmView->isGraphMode() && disasmView->basicBlockCount() >= 4, "DisassemblyView must remain in multi-block CFG Map mode across seek and stop");
    gridlock::ui::PreferencesDialog prefsCfgDlg(&mainWindow); auto* defModeCombo = prefsCfgDlg.findChild<QComboBox*>(QStringLiteral("defaultDisassemblyModeCombo"));
    QVERIFY2(defModeCombo != nullptr && defModeCombo->count() >= 3, "PreferencesDialog Debugger page must expose defaultDisassemblyModeCombo");
    gridlock::ui::DisassemblyView cfgPane; cfgPane.resize(600, 500); cfgPane.setGraphMode(true); cfgPane.show(); QVERIFY(QTest::qWaitForWindowExposed(&cfgPane)); QApplication::processEvents();
    const QString longTmpl = QStringLiteral("gridlock::Solver::Solve(std::vector<std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >, std::allocator<std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > > > const&, std::map<int, std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > > const&, double)");
    QVERIFY(longTmpl.size() >= 320);
    cfgPane.updateDisassembly(QStringLiteral("0x003ce970 <%1+0x0>: push rbp ; line 340\n0x003ce971 <%1+0x1>: mov rbp, rsp ; line 340\n0x003ce975 <std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&)>call 0x003d2d3c ; line 345\n0x003ce97a <%1+0xa>: test al, al ; line 346\n0x003ce97c <%1+0xc>: je 0x003ce990 ; line 346\n0x003ce97e <%1+0xe>: call 0x003d2e00 <%1> ; line 348\n0x003ce983 <%1+0x13>: jmp 0x003ce998 ; line 349\n0x003ce990 <%1+0x20>: nop ; line 352\n0x003ce998 <%1+0x28>: pop rbp ; line 355\n0x003ce999 <%1+0x29>: ret ; line 356").arg(longTmpl));
    cfgPane.seekToAddress(0x003ce975ULL); QApplication::processEvents();
    auto* cfgView = cfgPane.findChild<QGraphicsView*>(QStringLiteral("cutterCfgGraphView")); QVERIFY(cfgView && cfgView->scene() && cfgPane.basicBlockCount() >= 3);
    QRectF targetRect, leftmostRect;
    for (QGraphicsItem* item : cfgView->scene()->items()) {
        if (qAbs(item->zValue() - 1.0) < 0.05) { const QRectF br = item->sceneBoundingRect(); QVERIFY2(br.width() <= 960.0, qPrintable(QStringLiteral("CFG block width %1 > 960").arg(br.width()))); if (item->data(Qt::UserRole).toULongLong() == 0x003ce970ULL) targetRect = br; if (leftmostRect.isNull() || br.left() < leftmostRect.left()) leftmostRect = br; }
        if (auto* txt = dynamic_cast<QGraphicsSimpleTextItem*>(item)) QVERIFY2(!txt->text().contains(QStringLiteral(")>call")), qPrintable(txt->text()));
    }
    QVERIFY(!targetRect.isNull() && !leftmostRect.isNull() && cfgView->mapFromScene(targetRect.topLeft()).x() >= 0);
    auto* hbar = cfgView->horizontalScrollBar(); QVERIFY(hbar && hbar->maximum() > hbar->minimum()); hbar->setValue(hbar->minimum()); QApplication::processEvents();
    QVERIFY(cfgView->mapFromScene(leftmostRect.topLeft()).x() >= 0);
    QTest::keyClick(cfgView, Qt::Key_Right); QTest::keyClick(cfgView, Qt::Key_Home); QApplication::processEvents(); QVERIFY(cfgView->mapFromScene(targetRect.topLeft()).x() >= 0);
    QCOMPARE(cfgView->cursor().shape(), Qt::ArrowCursor); QCOMPARE(cfgView->viewport()->cursor().shape(), Qt::ArrowCursor);
    const QPoint midPt = cfgView->viewport()->rect().center();
    QTest::mousePress(cfgView->viewport(), Qt::MiddleButton, Qt::NoModifier, midPt); QTest::mouseMove(cfgView->viewport(), midPt + QPoint(25, 10)); QTest::mouseRelease(cfgView->viewport(), Qt::MiddleButton, Qt::NoModifier, midPt + QPoint(25, 10));
    QApplication::processEvents(); QCOMPARE(cfgView->cursor().shape(), Qt::ArrowCursor); QCOMPARE(cfgView->viewport()->cursor().shape(), Qt::ArrowCursor);
}
void TestMainWindowUI::testRealCompiledTargetDwarfMappingBreakpointRunAndLatchedToolbar() {
    auto& cfg = gridlock::core::ConfigManager::instance(); cfg.setDebuggerEngine(QStringLiteral("gdb")); cfg.setGlobalMpiArgs(QStringLiteral("--oversubscribe"));
    gridlock::ui::MainWindow mainWindow; mainWindow.resize(1440, 900); mainWindow.show(); QApplication::processEvents();
    QVERIFY(mainWindow.coordinator() && QString::fromLatin1(mainWindow.coordinator()->metaObject()->className()).contains(QStringLiteral("GdbRankCoordinator")));
    cfg.setDebuggerEngine(QStringLiteral("lldb-dap")); QApplication::processEvents(); QVERIFY(mainWindow.coordinator() && QString::fromLatin1(mainWindow.coordinator()->metaObject()->className()).contains(QStringLiteral("DapCoordinator")));
    cfg.setDebuggerEngine(QStringLiteral("gdb")); QApplication::processEvents(); QVERIFY(mainWindow.coordinator() && QString::fromLatin1(mainWindow.coordinator()->metaObject()->className()).contains(QStringLiteral("GdbRankCoordinator")));
    auto* attachAct = mainWindow.findChild<QAction*>(QStringLiteral("actionAttachProcess")); QVERIFY2(attachAct && attachAct->shortcut() == QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_P), "MainWindow must expose #actionAttachProcess with Ctrl+Alt+P shortcut");
    QProcess helperProc; struct ProcGuard { QProcess* p; ~ProcGuard() { if (p && p->state() != QProcess::NotRunning) { p->kill(); p->waitForFinished(1000); } } } guard{&helperProc};
    helperProc.start(QStringLiteral("sleep"), {QStringLiteral("30")}); QVERIFY(helperProc.waitForStarted(3000));
    const qint64 helperPid = helperProc.processId(); gridlock::ui::AttachProcessDialog attachDlg(&mainWindow);
    auto* procTable = attachDlg.findChild<QTableWidget*>(QStringLiteral("attachProcessTable")); auto* procTree = attachDlg.findChild<QTreeWidget*>(QStringLiteral("attachProcessTree"));
    auto* filterEd = attachDlg.findChild<QLineEdit*>(QStringLiteral("attachProcessFilterEdit")); auto* myProcChk = attachDlg.findChild<QCheckBox*>(QStringLiteral("attachMyProcessesOnlyCheck"));
    auto* ptraceBanner = attachDlg.findChild<QWidget*>(QStringLiteral("attachPtraceWarningBanner")); auto* attachBtn = attachDlg.findChild<QPushButton*>(QStringLiteral("attachSelectedBtn"));
    QVERIFY(procTable && procTable->rowCount() > 0 && procTree && filterEd && myProcChk && myProcChk->isChecked() && ptraceBanner && attachBtn);
    int helperRow = -1;
    for (int r = 0; r < procTable->rowCount(); ++r) { for (int c = 0; c < procTable->columnCount(); ++c) QVERIFY2(!(procTable->item(r, c) && procTable->item(r, c)->text().contains(QStringLiteral("mpi_navier_stokes"), Qt::CaseInsensitive)), "Hardcoded mpi_navier_stokes row still present in AttachProcessDialog"); if (procTable->item(r, 0) && procTable->item(r, 0)->text().toLongLong() == helperPid) helperRow = r; }
    QVERIFY2(helperRow >= 0, "AttachProcessDialog must include live child helper PID from /proc"); procTable->selectRow(helperRow); QCOMPARE(attachDlg.selectedPid(), helperPid);
    QTemporaryDir fakeProcDir; QVERIFY(fakeProcDir.isValid()); const uint curUid = static_cast<uint>(::geteuid());
    auto mkFakePid = [&](int pid, int rank) { const QString d = fakeProcDir.filePath(QString::number(pid)); QVERIFY(QDir().mkpath(d)); { QFile f(d + "/comm"); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("deadlock_ring\n"); } { QFile f(d + "/status"); QVERIFY(f.open(QIODevice::WriteOnly)); f.write(QStringLiteral("Name:\tdeadlock_ring\nPPid:\t4000\nUid:\t%1\t%1\t%1\t%1\n").arg(curUid).toUtf8()); } { QFile f(d + "/cmdline"); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("./deadlock_ring"); } if (rank >= 0) { QFile f(d + "/environ"); QVERIFY(f.open(QIODevice::WriteOnly)); f.write(QStringLiteral("OMPI_COMM_WORLD_RANK=%1\nOMPI_COMM_WORLD_SIZE=2\nOMPI_MCA_ess_base_jobid=job42\n").arg(rank).toUtf8()); } };
    mkFakePid(4102, 1); mkFakePid(4101, 0); mkFakePid(4999, -1);
    const QString fakeSysctl = fakeProcDir.filePath(QStringLiteral("ptrace_scope")); { QFile f(fakeSysctl); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("1\n"); }
    attachDlg.setProcPaths(fakeProcDir.path(), fakeSysctl); QCOMPARE(attachDlg.mpiJobGroupCount(), 1); QCOMPARE(attachDlg.ptraceScope(), 1); QVERIFY(!ptraceBanner->isHidden());
    procTree->setCurrentItem(procTree->topLevelItem(0)); QCOMPARE(attachDlg.selectedPids(), (QList<qint64>{4101, 4102}));
    filterEd->setText(QStringLiteral("4999")); QVERIFY(procTree->topLevelItem(0)->isHidden() && !procTree->topLevelItem(1)->isHidden());
    auto* tabW = attachDlg.findChild<QTabWidget*>(QStringLiteral("attachModeTabWidget")); auto* remEpEd = attachDlg.findChild<QLineEdit*>(QStringLiteral("remoteEndpointEdit"));
    auto* remSymEd = attachDlg.findChild<QLineEdit*>(QStringLiteral("remoteSymbolBinaryEdit")); auto* remSymBtn = attachDlg.findChild<QPushButton*>(QStringLiteral("remoteSymbolBrowseBtn"));
    auto* remGdbCombo = attachDlg.findChild<QComboBox*>(QStringLiteral("remoteGdbBinaryCombo")); auto* remProtoCombo = attachDlg.findChild<QComboBox*>(QStringLiteral("remoteProtocolModeCombo"));
    QVERIFY(tabW && tabW->count() == 2 && remEpEd && remSymEd && remSymBtn && remGdbCombo && remProtoCombo && attachDlg.findChild<QLabel*>(QStringLiteral("remoteCommandHintLabel")));
    attachDlg.setRemoteTargetTabActive(true); QVERIFY(attachDlg.isRemoteTargetTabActive());
    remEpEd->setText(QStringLiteral("localhost:1234..1237")); remSymEd->setText(QStringLiteral("/tmp/qemu_guest.elf")); remProtoCombo->setCurrentIndex(1);
    const auto remTargets = attachDlg.selectedRemoteTargets(); QVERIFY(remTargets.size() == 4 && remTargets[0].endpoint == QStringLiteral("localhost:1234") && remTargets[3].endpoint == QStringLiteral("localhost:1237") && remTargets[0].extendedRemote && attachBtn->isEnabled());
    remEpEd->setText(QStringLiteral("localhost:99999")); QVERIFY(attachDlg.selectedRemoteTargets().isEmpty() && !attachBtn->isEnabled()); QVERIFY(mainWindow.attachToRemoteTargets({remTargets[0]})); mainWindow.stopTargetWorkflow();
    QVERIFY2(mainWindow.selectTargetByName(QStringLiteral("deadlock_ring")), "selectTargetByName must succeed");
    if (!mainWindow.ensureTargetBinaryCompiled()) { QFile::remove(mainWindow.activeTargetBinaryPath()); QTest::qWait(400); }
    QVERIFY2(mainWindow.ensureTargetBinaryCompiled(), "ensureTargetBinaryCompiled must compile ELF binary");
    auto* editorTabs = mainWindow.findChild<gridlock::ui::EditorTabManager*>();
    auto* disasmView = mainWindow.findChild<gridlock::ui::DisassemblyView*>(); auto* targetCombo = mainWindow.findChild<QComboBox*>(QStringLiteral("toolbarTargetSelector"));
    auto* rankSpin = mainWindow.findChild<QSpinBox*>(QStringLiteral("toolbarRankSpin")); auto* regTable = mainWindow.findChild<QTableWidget*>(QStringLiteral("cutterRegisterTable")); auto* memView = mainWindow.findChild<gridlock::ui::MemView*>();
    QVERIFY(editorTabs && disasmView && targetCombo && rankSpin && regTable && memView);
    const int sendLine = mainWindow.findSourceLineForStatement(QStringLiteral("MPI_Send(")), recvLine = mainWindow.findSourceLineForStatement(QStringLiteral("MPI_Recv("));
    QVERIFY(sendLine > 0 && recvLine > sendLine && mainWindow.sourceLineToAddress(sendLine) != 0ULL && mainWindow.sourceLineToAddress(recvLine) != 0ULL);
    rankSpin->setValue(2); QVERIFY2(mainWindow.selectTargetByName(QStringLiteral("inspection_demo")) && mainWindow.ensureTargetBinaryCompiled(), "inspection_demo must select and compile");
    auto* srcView = editorTabs->currentSourceCodeView(); QVERIFY(srcView != nullptr && QFileInfo::exists(mainWindow.activeTargetBinaryPath()));
    for (int bp : srcView->breakpoints().values()) srcView->toggleBreakpoint(bp);
    const int bpLine1 = mainWindow.findSourceLineForStatement(QStringLiteral("MPI_Comm_rank(")), bpLine2 = mainWindow.findSourceLineForStatement(QStringLiteral("printf("));
    QVERIFY(bpLine1 > 0 && bpLine2 > bpLine1); srcView->toggleBreakpoint(bpLine1); srcView->toggleBreakpoint(bpLine2);
    mainWindow.startTargetWorkflow();
    QVERIFY(mainWindow.isDebugSessionActive() && mainWindow.isRunButtonLatched() && mainWindow.coordinator()->getProcessCount() == 2);
    QTRY_COMPARE_WITH_TIMEOUT(srcView->executionLine(), bpLine1, 8000);
    QTRY_VERIFY_WITH_TIMEOUT(mainWindow.coordinator()->getPidForRank(0) > 0 && mainWindow.coordinator()->getPidForRank(1) > 0, 5000);
    const qint64 pid0 = mainWindow.coordinator()->getPidForRank(0), pid1 = mainWindow.coordinator()->getPidForRank(1);
    QVERIFY(pid0 > 0 && pid1 > 0 && pid0 != pid1 && pid0 != QCoreApplication::applicationPid() && pid1 != QCoreApplication::applicationPid());
    QVERIFY(QFileInfo::exists(QStringLiteral("/proc/%1/status").arg(pid0)) && QFileInfo::exists(QStringLiteral("/proc/%1/status").arg(pid1)));
    auto* mpiDiag = mainWindow.findChild<gridlock::ui::MpiDiagnosticsWidget*>();
    auto* waitTable = mpiDiag ? mpiDiag->findChild<QTableWidget*>(QStringLiteral("mpiRankWaitTable")) : nullptr;
    auto* dlList = mpiDiag ? mpiDiag->findChild<QListWidget*>(QStringLiteral("mpiDeadlockList")) : nullptr; auto* dlBadge = mpiDiag ? mpiDiag->findChild<QLabel*>(QStringLiteral("mpiDeadlockStatusBadge")) : nullptr;
    QVERIFY(mpiDiag && waitTable && dlList && dlBadge && mpiDiag->rankWaitTableRowCount() == 2);
    QTRY_VERIFY_WITH_TIMEOUT(waitTable->item(0, 0) && waitTable->item(0, 0)->text().contains(QString::number(pid0)), 4000);
    emit mainWindow.coordinator()->stackTraceUpdated(0, {StackFrameEntry{0, 0, 0x401140ULL, QStringLiteral("MPI_Bcast"), mainWindow.activeTargetSourcePath(), bpLine1}});
    emit mainWindow.coordinator()->stackTraceUpdated(1, {StackFrameEntry{0, 0, 0x401180ULL, QStringLiteral("MPI_Barrier"), mainWindow.activeTargetSourcePath(), bpLine1}});
    QTRY_VERIFY_WITH_TIMEOUT(waitTable->item(0, 2) && waitTable->item(0, 2)->text().contains(QStringLiteral("MPI_Bcast")) && waitTable->item(1, 2) && waitTable->item(1, 2)->text().contains(QStringLiteral("MPI_Barrier")) && dlList->count() >= 1 && !dlBadge->text().contains(QStringLiteral("NO CYCLE")), 3000);
    auto readReg = [&](const QString& n) { for (int r = 0; r < regTable->rowCount(); ++r) if (regTable->item(r, 0) && regTable->item(r, 0)->text() == n && regTable->item(r, 1)) return regTable->item(r, 1)->text(); return QString(); };
    QTRY_VERIFY_WITH_TIMEOUT(!readReg(QStringLiteral("rsp")).isEmpty() && readReg(QStringLiteral("rsp")) != QStringLiteral("0x7fffffffe480"), 5000);
    bool ripOk = false; const uint64_t liveRip1 = readReg(QStringLiteral("rip")).section(QLatin1Char(' '), 0, 0).mid(2).toULongLong(&ripOk, 16);
    QVERIFY(ripOk && liveRip1 != 0ULL && disasmView->currentAddress() == liveRip1);
    QVERIFY(mainWindow.stepInstructionWorkflow()); QTRY_VERIFY_WITH_TIMEOUT(disasmView->currentAddress() != 0ULL && disasmView->currentAddress() != liveRip1, 5000);
    QVERIFY(mainWindow.continueExecutionWorkflow()); QTRY_COMPARE_WITH_TIMEOUT(srcView->executionLine(), bpLine2, 5000);
    mainWindow.onRankSelected(1); QTRY_COMPARE_WITH_TIMEOUT(srcView->executionLine(), bpLine2, 5000); mainWindow.onRankSelected(0);
    auto* addrEdit = memView->findChild<QLineEdit*>(QStringLiteral("cutterMemAddressEdit")); QPushButton *readBtn = nullptr, *diffBtn = nullptr;
    for (auto* b : memView->findChildren<QPushButton*>()) { if (b->text().compare(QStringLiteral("Read"), Qt::CaseInsensitive) == 0) readBtn = b; if (b->text().contains(QStringLiteral("Diff"), Qt::CaseInsensitive)) diffBtn = b; }
    const auto memSpins = memView->findChildren<QSpinBox*>(); QVERIFY(addrEdit && readBtn && diffBtn && memSpins.size() >= 3);
    addrEdit->setText(QStringLiteral("$rsp")); readBtn->click(); QTRY_VERIFY_WITH_TIMEOUT(memView->currentBaseAddress() > 0x1000000ULL, 5000);
    memSpins[1]->setValue(0); memSpins[2]->setValue(1); diffBtn->click(); QTRY_VERIFY_WITH_TIMEOUT(memView->changedByteCount() > 0, 5000);
    mainWindow.stopTargetWorkflow();
    QVERIFY(!mainWindow.isDebugSessionActive() && !mainWindow.isRunButtonLatched() && mainWindow.coordinator()->getProcessCount() == 0 && mainWindow.coordinator()->getPidForRank(0) == 0);
    auto* callStackDock = mainWindow.findChild<QDockWidget*>(QStringLiteral("CallStackDock")); QVERIFY2(callStackDock != nullptr, "MainWindow must expose docked CallStackDock");
    auto* threadCombo = callStackDock->findChild<QComboBox*>(QStringLiteral("threadSelectorCombo")); auto* callStackTable = callStackDock->findChild<QTableWidget*>(QStringLiteral("callStackTable"));
    auto* varsDock = mainWindow.findChild<QDockWidget*>(QStringLiteral("VariablesDock")); auto* gdbConsole = mainWindow.findChild<gridlock::ui::GdbConsoleWidget*>();
    auto* consoleCmdEdit = gdbConsole ? gdbConsole->findChild<QLineEdit*>(QStringLiteral("gdbConsoleCommandEdit")) : nullptr; auto* consoleExecBtn = gdbConsole ? gdbConsole->findChild<QPushButton*>(QStringLiteral("gdbConsoleExecuteBtn")) : nullptr; auto* consoleViewport = gdbConsole ? gdbConsole->findChild<QPlainTextEdit*>(QStringLiteral("gdbConsoleViewport")) : nullptr;
    QVERIFY(threadCombo && callStackTable && varsDock && gdbConsole && consoleCmdEdit && consoleExecBtn && consoleViewport);
    QTemporaryDir nestedDir; QVERIFY(nestedDir.isValid());
    const QString nestedCpp = nestedDir.filePath(QStringLiteral("nested_stack.cpp"));
    { QFile nf(nestedCpp); QVERIFY(nf.open(QIODevice::WriteOnly));
      nf.write("#include <mpi.h>\nint watchedCounter = 10;\nclass CellMesh { protected: int protDim = 9; public: int dims = 3; };\nstruct SubdomainBlock { CellMesh mesh; double tol = 1e-6; };\n__attribute__((noinline)) int innerCallee(int innerVal) {\n    int innerLocal = innerVal + 7;\n    int stateVal = 111;\n    stateVal = 999;\n    return innerLocal + stateVal;\n}\n__attribute__((noinline)) int outerCaller(int outerSecret) {\n    SubdomainBlock domain_cfg{};\n    int outerLocal = outerSecret + (domain_cfg.mesh.dims > 0 ? 1 : 0) + (domain_cfg.tol > 0.0 ? 0 : 1);\n    int res = innerCallee(outerLocal);\n    return res;\n}\n__attribute__((noinline)) int loopProbe(int step) {\n    int v = step * 10;\n    return v;\n}\nint main(int argc, char** argv) {\n    MPI_Init(&argc, &argv);\n    int total = outerCaller(4242);\n    for (int step = 1; step <= 4; ++step) total += loopProbe(step);\n    watchedCounter = 77;\n    MPI_Finalize();\n    return (total + watchedCounter) == 0 ? 1 : 0;\n}\n"); }
    rankSpin->setValue(1); targetCombo->insertItem(0, QStringLiteral("nested_stack"), nestedCpp);
    QVERIFY(mainWindow.selectTargetByName(QStringLiteral("nested_stack")) && mainWindow.ensureTargetBinaryCompiled());
    auto* nestedSrcView = editorTabs->currentSourceCodeView(); QVERIFY(nestedSrcView != nullptr);
    const int revMutLine = mainWindow.findSourceLineForStatement(QStringLiteral("stateVal = 999")), innerBpLine = mainWindow.findSourceLineForStatement(QStringLiteral("return innerLocal")), outerCallLine = mainWindow.findSourceLineForStatement(QStringLiteral("innerCallee(outerLocal)")), cfgLine = mainWindow.findSourceLineForStatement(QStringLiteral("domain_cfg.mesh.dims")), logLine = mainWindow.findSourceLineForStatement(QStringLiteral("int v = step * 10")), ignLine = mainWindow.findSourceLineForStatement(QStringLiteral("return v"));
    QVERIFY(revMutLine > 0 && innerBpLine > revMutLine && outerCallLine > innerBpLine && cfgLine > 0 && logLine > 0 && ignLine > logLine); nestedSrcView->toggleBreakpoint(revMutLine);
    mainWindow.startTargetWorkflow(); QTRY_COMPARE_WITH_TIMEOUT(nestedSrcView->executionLine(), revMutLine, 8000);
    auto* varsTree = varsDock->findChild<QTreeView*>(); auto* varSearch = varsDock->findChild<QLineEdit*>(QStringLiteral("variableSearchEdit"));
    QVERIFY2(varsTree != nullptr && varsTree->model() != nullptr && varSearch != nullptr && varsTree->isSortingEnabled() && varsTree->header()->sectionResizeMode(0) == QHeaderView::Interactive, "VariablesDockWidget must expose #variableSearchEdit, sortable headers, and interactive column resizing");
    auto findVarIdx = [&](const QModelIndex& parent, const QString& name) { for (int r = 0; r < varsTree->model()->rowCount(parent); ++r) { const QModelIndex i = varsTree->model()->index(r, 0, parent); if (varsTree->model()->data(i).toString() == name) return i; } return QModelIndex(); };
    auto* recBtn = mainWindow.findChild<QPushButton*>(QStringLiteral("toolbarRecordHistoryBtn")), *stepBackBtn = mainWindow.findChild<QPushButton*>(QStringLiteral("toolbarStepBackBtn")), *revContBtn = mainWindow.findChild<QPushButton*>(QStringLiteral("toolbarReverseContinueBtn"));
    QVERIFY(recBtn && stepBackBtn && revContBtn); QTRY_VERIFY_WITH_TIMEOUT(findVarIdx({}, QStringLiteral("stateVal")).isValid() && varsTree->model()->data(findVarIdx({}, QStringLiteral("stateVal")).siblingAtColumn(1)).toString().contains(QStringLiteral("111")), 5000);
    recBtn->click(); QVERIFY(mainWindow.coordinator()->isReverseRecordingEnabled(0)); QTest::qWait(150);
    QVERIFY(mainWindow.stepOverWorkflow()); QTRY_COMPARE_WITH_TIMEOUT(nestedSrcView->executionLine(), innerBpLine, 5000); QTRY_VERIFY_WITH_TIMEOUT(findVarIdx({}, QStringLiteral("stateVal")).isValid() && varsTree->model()->data(findVarIdx({}, QStringLiteral("stateVal")).siblingAtColumn(1)).toString().contains(QStringLiteral("999")), 5000);
    stepBackBtn->click(); QTRY_COMPARE_WITH_TIMEOUT(nestedSrcView->executionLine(), revMutLine, 5000); QTRY_VERIFY_WITH_TIMEOUT(findVarIdx({}, QStringLiteral("stateVal")).isValid() && varsTree->model()->data(findVarIdx({}, QStringLiteral("stateVal")).siblingAtColumn(1)).toString().contains(QStringLiteral("111")), 5000);
    mainWindow.coordinator()->setReverseRecordingEnabled(0, false); QTest::qWait(100); QVERIFY(mainWindow.stepOverWorkflow()); QTRY_COMPARE_WITH_TIMEOUT(nestedSrcView->executionLine(), innerBpLine, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(callStackTable->rowCount() >= 3 && threadCombo->count() >= 1, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(nestedSrcView->inlineGhostTextForLine(innerBpLine).contains(QStringLiteral("// innerLocal = ")), 5000);
    cfg.setInlineVariableValuesEnabled(false); QVERIFY(nestedSrcView->inlineGhostTextForLine(innerBpLine).isEmpty());
    cfg.setInlineVariableValuesEnabled(true); QVERIFY(!nestedSrcView->inlineGhostTextForLine(innerBpLine).isEmpty());
    { gridlock::ui::PreferencesDialog pd(&mainWindow); auto* ivChk = pd.findChild<QCheckBox*>(QStringLiteral("inlineVarValuesCheck")); QVERIFY(ivChk && ivChk->isChecked());
      gridlock::ui::ConditionalBreakpointDialog cbd(QString(), &mainWindow); QVERIFY(cbd.findChild<QSpinBox*>(QStringLiteral("bpIgnoreCountSpin")) && cbd.findChild<QCheckBox*>(QStringLiteral("bpLogpointCheck")) && cbd.findChild<QLineEdit*>(QStringLiteral("bpLogMessageEdit"))); }
    nestedSrcView->toggleBreakpoint(outerCallLine); nestedSrcView->setBreakpointOptions(outerCallLine, QString(), 3, QStringLiteral("iter={i}"));
    QVERIFY(nestedSrcView->isLogpointForLine(outerCallLine) && nestedSrcView->breakpointIgnoreCountForLine(outerCallLine) == 3 && nestedSrcView->breakpointTooltipForLine(outerCallLine).contains(QStringLiteral("Logpoint")) && nestedSrcView->breakpointTooltipForLine(outerCallLine).contains(QLatin1Char('3')));
    nestedSrcView->toggleBreakpoint(outerCallLine); nestedSrcView->toggleBreakpoint(revMutLine);
    auto verifyFrameRow = [&](int row, const QString& exp) { const QString fn = callStackTable->item(row, 1) ? callStackTable->item(row, 1)->text() : QString(), loc = callStackTable->item(row, 2) ? callStackTable->item(row, 2)->text() : QString(), addr = callStackTable->item(row, 3) ? callStackTable->item(row, 3)->text() : QString(); bool hexOk = false; const quint64 pa = addr.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive) ? addr.mid(2).toULongLong(&hexOk, 16) : 0ULL; QVERIFY2(fn.contains(exp) && loc.contains(QLatin1Char(':')) && hexOk && pa != 0ULL, qPrintable(fn)); };
    verifyFrameRow(0, QStringLiteral("innerCallee")); verifyFrameRow(1, QStringLiteral("outerCaller")); verifyFrameRow(2, QStringLiteral("main"));
    callStackTable->selectRow(1); QMetaObject::invokeMethod(callStackTable, "cellClicked", Q_ARG(int, 1), Q_ARG(int, 1)); QTRY_COMPARE_WITH_TIMEOUT(editorTabs->currentSourceCodeView()->executionLine(), outerCallLine, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(([&]() { const QModelIndex ic = findVarIdx({}, QStringLiteral("domain_cfg")), os = findVarIdx({}, QStringLiteral("outerSecret")); return ic.isValid() && varsTree->model()->hasChildren(ic) && varsTree->model()->data(ic.siblingAtColumn(1)).toString().contains(QLatin1Char('{')) && os.isValid() && varsTree->model()->data(os.siblingAtColumn(1)).toString().contains(QStringLiteral("4242")); })(), 5000);
    { QSet<QString> seenTop; for (int r = 0; r < varsTree->model()->rowCount(); ++r) { const QString vn = varsTree->model()->data(varsTree->model()->index(r, 0)).toString(); QVERIFY2(!vn.endsWith(QStringLiteral("@entry")) && !seenTop.contains(vn), qPrintable(vn)); seenTop.insert(vn); } }
    varsTree->expand(findVarIdx({}, QStringLiteral("domain_cfg"))); QTRY_VERIFY_WITH_TIMEOUT(([&]() { const QModelIndex ic = findVarIdx({}, QStringLiteral("domain_cfg")); const QModelIndex m = findVarIdx(ic, QStringLiteral("mesh")), t = findVarIdx(ic, QStringLiteral("tol")); const QString tv = varsTree->model()->data(t.siblingAtColumn(1)).toString(); return m.isValid() && varsTree->model()->hasChildren(m) && t.isValid() && (tv.contains(QStringLiteral("1e")) || tv.contains(QStringLiteral("e-07"))); })(), 5000);
    varsTree->expand(findVarIdx(findVarIdx({}, QStringLiteral("domain_cfg")), QStringLiteral("mesh"))); QTRY_VERIFY_WITH_TIMEOUT(([&]() { const QModelIndex m = findVarIdx(findVarIdx({}, QStringLiteral("domain_cfg")), QStringLiteral("mesh")); const QModelIndex dm = findVarIdx(m, QStringLiteral("dims")), pm = findVarIdx(m, QStringLiteral("protDim")); return dm.isValid() && varsTree->model()->data(dm.siblingAtColumn(1)).toString().contains(QLatin1Char('3')) && pm.isValid() && !findVarIdx(m, QStringLiteral("protected")).isValid() && !findVarIdx(m, QStringLiteral("public")).isValid() && !findVarIdx(m, QStringLiteral("private")).isValid(); })(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(([&]() { const QString g = nestedSrcView->inlineGhostTextForLine(cfgLine); return !g.contains(QStringLiteral("{...}")) && g.contains(QStringLiteral("domain_cfg.mesh.dims = 3")) && g.contains(QStringLiteral("domain_cfg.tol = ")); })(), 5000);
    varSearch->setText(QStringLiteral("tol")); QApplication::processEvents(); QTRY_VERIFY_WITH_TIMEOUT(findVarIdx({}, QStringLiteral("domain_cfg")).isValid() && findVarIdx(findVarIdx({}, QStringLiteral("domain_cfg")), QStringLiteral("tol")).isValid() && !findVarIdx({}, QStringLiteral("outerSecret")).isValid(), 3000);
    varSearch->clear(); QApplication::processEvents(); QTRY_VERIFY_WITH_TIMEOUT(findVarIdx({}, QStringLiteral("outerSecret")).isValid(), 3000);
    varsTree->sortByColumn(0, Qt::AscendingOrder); QApplication::processEvents(); const int icAsc = findVarIdx({}, QStringLiteral("domain_cfg")).row(), osAsc = findVarIdx({}, QStringLiteral("outerSecret")).row();
    varsTree->sortByColumn(0, Qt::DescendingOrder); QApplication::processEvents(); const int icDesc = findVarIdx({}, QStringLiteral("domain_cfg")).row(), osDesc = findVarIdx({}, QStringLiteral("outerSecret")).row();
    QVERIFY2(icAsc >= 0 && osAsc >= 0 && icAsc < osAsc && osDesc < icDesc, "Sorting NAME column asc/desc must deterministically invert top-level order");
    mainWindow.openPointerGraphVisualizer(QStringLiteral("outerLocal"), QStringLiteral("4243"), QStringLiteral("int"));
    auto* livePg = mainWindow.findChild<gridlock::ui::PointerGraphDialog*>(); auto* pgCloseBtn = livePg ? livePg->findChild<QPushButton*>(QStringLiteral("pointerGraphCloseBtn")) : nullptr; QVERIFY2(livePg && pgCloseBtn, "PointerGraphDialog must expose #pointerGraphCloseBtn"); QTRY_VERIFY_WITH_TIMEOUT(livePg->nodeCount() == 1, 3000);
    mainWindow.openPointerGraphVisualizer(QStringLiteral("domain_cfg"), QStringLiteral("{...}"), QStringLiteral("SubdomainBlock"));
    QTRY_VERIFY_WITH_TIMEOUT(livePg->nodeCount() >= 3 && livePg->pinnedRootExpressions().contains(QStringLiteral("outerLocal")) && livePg->pinnedRootExpressions().contains(QStringLiteral("domain_cfg")) && livePg->edgeCount() >= 1 && livePg->nodeHasField(QStringLiteral("domain_cfg"), QStringLiteral("mesh")) && livePg->nodeHasField(QStringLiteral("domain_cfg.mesh"), QStringLiteral("dims")), 5000);
    pgCloseBtn->click(); QApplication::processEvents(); QVERIFY(!livePg->isVisible());
    gridlock::ui::PointerGraphDialog pgDlg(mainWindow.coordinator(), &mainWindow); QVERIFY(pgDlg.findChild<QLineEdit*>(QStringLiteral("pointerGraphRootEdit")) && pgDlg.findChild<QPushButton*>(QStringLiteral("pointerGraphAddBtn")) && pgDlg.findChild<QPushButton*>(QStringLiteral("pointerGraphLayoutBtn")) && pgDlg.findChild<QPushButton*>(QStringLiteral("pointerGraphExpandHopBtn")) && pgDlg.findChild<QPushButton*>(QStringLiteral("pointerGraphClearBtn")) && pgDlg.findChild<QPushButton*>(QStringLiteral("pointerGraphCloseBtn")) && pgDlg.findChild<QGraphicsView*>(QStringLiteral("pointerGraphView")));
    pgDlg.upsertStructNode(QStringLiteral("n1"), QStringLiteral("Node"), QStringLiteral("0x7fff1000"), {{QStringLiteral("val"), QStringLiteral("10"), QStringLiteral("int")}, {QStringLiteral("next"), QStringLiteral("0x7fff2000"), QStringLiteral("Node*")}, {QStringLiteral("ghost_guard"), QStringLiteral("0x0"), QStringLiteral("HaloExchangeBuf *")}});
    pgDlg.upsertStructNode(QStringLiteral("n2"), QStringLiteral("Node"), QStringLiteral("0x7fff2000"), {{QStringLiteral("val"), QStringLiteral("20"), QStringLiteral("int")}, {QStringLiteral("next"), QStringLiteral("0x7fff1000"), QStringLiteral("Node*")}}, QStringLiteral("n1"), QStringLiteral("next")); pgDlg.connectPointerField(QStringLiteral("n2"), QStringLiteral("next"), QStringLiteral("0x7fff1000")); pgDlg.expandAllPointersOneHop(); QVERIFY(pgDlg.nodeCount() == 2 && pgDlg.edgeCount() == 2 && pgDlg.hasCycleOrSharedAlias() && !pgDlg.nodeHasField(QStringLiteral("n1.ghost_guard"), QStringLiteral("val")));
    { auto* srcVm = qobject_cast<QAbstractProxyModel*>(varsTree->model())->sourceModel(); QVERIFY(srcVm != nullptr); const QJsonArray initArr{QJsonObject{{"name", "A"}, {"value", "{...}"}, {"type", "HaloExchangeBuf"}}, QJsonObject{{"name", "assemblyTimeSec"}, {"value", "1.114"}, {"type", "double"}}, QJsonObject{{"name", "assemblyTimeSec@entry"}, {"value", "1.114"}, {"type", "double"}}};
      QMetaObject::invokeMethod(srcVm, "onLocalsUpdated", Q_ARG(int, 0), Q_ARG(int, 0), Q_ARG(QJsonArray, initArr)); QMetaObject::invokeMethod(srcVm, "handleGdbOutput", Q_ARG(int, 0), Q_ARG(QString, QStringLiteral("^done,name=\"var_99_A\",numchild=\"1\",value=\"{...} @ 0x7fffffffabd0\",type=\"HaloExchangeBuf &\""))); QMetaObject::invokeMethod(srcVm, "handleGdbOutput", Q_ARG(int, 0), Q_ARG(QString, QStringLiteral("^done,name=\"var_99_assemblyTimeSec\",numchild=\"0\",value=\"1.114\",type=\"double\""))); QMetaObject::invokeMethod(srcVm, "handleGdbOutput", Q_ARG(int, 0), Q_ARG(QString, QStringLiteral("^done,name=\"var_99_assemblyTimeSec@entry\",numchild=\"0\",value=\"1.114\",type=\"double\""))); QCOMPARE(srcVm->rowCount(), 2); QVERIFY(srcVm->data(srcVm->index(0, 1)).toString().contains(QStringLiteral("0x7fffffffabd0"))); }
    consoleCmdEdit->setText(QStringLiteral("print outerSecret")); consoleExecBtn->click(); QTRY_VERIFY_WITH_TIMEOUT(consoleViewport->toPlainText().contains(QStringLiteral("4242")), 5000); consoleCmdEdit->setText(QStringLiteral("-data-evaluate-expression 21 + 21")); consoleExecBtn->click(); QTRY_VERIFY_WITH_TIMEOUT(consoleViewport->toPlainText().contains(QStringLiteral("42")), 5000); consoleCmdEdit->setText(QStringLiteral("print outerSecret; echo PWNED")); consoleExecBtn->click(); QApplication::processEvents(); QVERIFY(!consoleViewport->toPlainText().contains(QStringLiteral("PWNED")));
    const QString solverCpp = nestedDir.filePath(QStringLiteral("SolverDemo.cpp"));
    { QFile mf(solverCpp); QVERIFY(mf.open(QIODevice::WriteOnly)); for (int ln = 1; ln <= 90; ++ln) mf.write(QStringLiteral("// line %1\nint dummy_%1 = %1;\n").arg(ln).toUtf8()); }
    mainWindow.loadSourceFile(solverCpp); QApplication::processEvents(); auto* solverView = editorTabs->currentSourceCodeView();
    QVERIFY2(solverView != nullptr && solverView->currentFile().endsWith(QStringLiteral("SolverDemo.cpp")), "Secondary file tab SolverDemo.cpp must become active");
    solverView->toggleBreakpoint(82); QTest::qWait(300); QApplication::processEvents();
    QVERIFY2(editorTabs->currentSourceCodeView() == solverView && editorTabs->currentSourceCodeView()->currentFile().endsWith(QStringLiteral("SolverDemo.cpp")), "Toggling breakpoint in SolverDemo.cpp must NEVER switch active tab");
    nestedSrcView->toggleBreakpoint(logLine); nestedSrcView->setBreakpointOptions(logLine, QString(), 0, QStringLiteral("TRACE_STEP_HIT"));
    nestedSrcView->toggleBreakpoint(ignLine); nestedSrcView->setBreakpointOptions(ignLine, QString(), 2, QString());
    auto* logProgOut = mainWindow.findChild<QPlainTextEdit*>(QStringLiteral("programOutputView")); QVERIFY(logProgOut != nullptr);
    QVERIFY(mainWindow.continueExecutionWorkflow()); QTRY_COMPARE_WITH_TIMEOUT(nestedSrcView->executionLine(), ignLine, 8000);
    QTRY_VERIFY_WITH_TIMEOUT(logProgOut->toPlainText().contains(QStringLiteral("TRACE_STEP_HIT")), 5000); QTRY_VERIFY_WITH_TIMEOUT(findVarIdx({}, QStringLiteral("v")).isValid() && varsTree->model()->data(findVarIdx({}, QStringLiteral("v")).siblingAtColumn(1)).toString().contains(QStringLiteral("30")), 5000);
    nestedSrcView->toggleBreakpoint(logLine); nestedSrcView->toggleBreakpoint(ignLine);
    QSignalSpy wpHitSpy(mainWindow.coordinator(), SIGNAL(watchpointHit(int,QString,QString,QString))); QVERIFY2(QMetaObject::invokeMethod(mainWindow.coordinator(), "setDataWatchpoint", Q_ARG(QString, QStringLiteral("watchedCounter")), Q_ARG(int, 0), Q_ARG(int, -1)), "Coordinator must expose setDataWatchpoint"); QTest::qWait(150);
    QVERIFY(mainWindow.continueExecutionWorkflow()); QTRY_VERIFY_WITH_TIMEOUT(wpHitSpy.count() >= 1, 6000); QVERIFY(wpHitSpy.first().at(1).toString() == QStringLiteral("watchedCounter") && wpHitSpy.first().at(2).toString() == QStringLiteral("10") && wpHitSpy.first().at(3).toString() == QStringLiteral("77"));
    solverView->toggleBreakpoint(82); mainWindow.stopTargetWorkflow(); QCOMPARE(callStackTable->rowCount(), 0); QVERIFY(nestedSrcView->inlineGhostTextForLine(innerBpLine).isEmpty());
    auto* memMapBtn = memView->findChild<QPushButton*>(QStringLiteral("memMapModeBtn")); auto* memProf = memView->findChild<QWidget*>(QStringLiteral("memoryProfilerWidget"));
    auto* treemapCanvas = memView->findChild<QWidget*>(QStringLiteral("memoryTreemapCanvas")); auto* breakdownTable = memView->findChild<QTableWidget*>(QStringLiteral("memoryBreakdownTable"));
    auto* profSearch = memView->findChild<QLineEdit*>(QStringLiteral("memProfileSearchEdit")); auto* preloadCheck = memView->findChild<QCheckBox*>(QStringLiteral("memTrackPreloadCheck"));
    QVERIFY2(memMapBtn && memProf && treemapCanvas && breakdownTable && profSearch && preloadCheck && preloadCheck->isChecked() && breakdownTable->isSortingEnabled() && breakdownTable->horizontalHeader()->sectionResizeMode(0) == QHeaderView::Interactive, "MemView must expose #memMapModeBtn, MemoryProfilerWidget controls, and sortable Interactive columns");
    const QString barrierCpp = nestedDir.filePath(QStringLiteral("barrier_hex.cpp"));
    { QFile bf(barrierCpp); QVERIFY(bf.open(QIODevice::WriteOnly));
      bf.write("#include <mpi.h>\n#include <cstdio>\n#include <cstdlib>\n#include <string>\n#include <thread>\n#include <unistd.h>\n#include <vector>\n__attribute__((noinline)) std::vector<char> allocateSolverArena() { std::vector<char> b(4 * 1024 * 1024, 1); b[0] = 7; b.back() = 9; return b; }\n__attribute__((noinline)) int solveStage(int r, int it, int sum) {\n  int res = sum + r + it;\n  return res;\n}\nint main(int argc, char** argv) {\n  MPI_Init(&argc, &argv);\n  if (argc < 2 || std::string(argv[1]) != \"HEX\") { MPI_Finalize(); return 2; }\n  int r = -1, s = 0; MPI_Comm_rank(MPI_COMM_WORLD, &r); MPI_Comm_size(MPI_COMM_WORLD, &s); auto solverArena = allocateSolverArena();\n  const char* omp = std::getenv(\"OMP_NUM_THREADS\"); const char* pol = std::getenv(\"OMP_WAIT_POLICY\"); int nt = (omp && std::atoi(omp) > 0) ? std::atoi(omp) : 8; std::vector<std::thread> ws;\n  for (int t = 0; t < nt; ++t) ws.emplace_back([r, t]() { volatile int acc = r + t; (void)acc; });\n  for (auto& th : ws) th.join();\n  std::printf(\"PRE_BARRIER_R%d OMP=%s POL=%s\\n\", r, omp ? omp : \"NONE\", pol ? pol : \"NONE\"); std::fflush(stdout); usleep(250000); int v = r + 1; MPI_Barrier(MPI_COMM_WORLD);\n  int g = 0; MPI_Allreduce(&v, &g, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD); MPI_Barrier(MPI_COMM_WORLD);\n  int out = solveStage(r, 3, g + solverArena[0]); MPI_Finalize(); return (out > 0 && s == 4) ? 0 : 1;\n}\n"); }
    auto projCfg = gridlock::core::ConfigManager::instance().loadProjectSettings(); projCfg.targetArguments = QStringLiteral("HEX"); projCfg.programArguments = "HEX";
    gridlock::core::ConfigManager::instance().saveProjectSettings(projCfg); qunsetenv("OMP_NUM_THREADS"); qunsetenv("OMP_WAIT_POLICY");
    rankSpin->setValue(4); targetCombo->insertItem(0, QStringLiteral("barrier_hex"), barrierCpp); QVERIFY(mainWindow.selectTargetByName(QStringLiteral("barrier_hex")) && mainWindow.ensureTargetBinaryCompiled());
    auto* barrierView = editorTabs->currentSourceCodeView(); const int solveBp = mainWindow.findSourceLineForStatement(QStringLiteral("return res")); QVERIFY(barrierView != nullptr && solveBp > 0);
    barrierView->toggleBreakpoint(solveBp); mainWindow.startTargetWorkflow();
    auto* rackView = mainWindow.findChild<gridlock::ui::ServerRackView*>(); QVERIFY(rackView != nullptr);
    for (int r = 0; r < rackView->count(); ++r) { const QString pc = rackView->item(r)->data(Qt::UserRole + 1).value<gridlock::RankState>().registers.value(16); QVERIFY2(!pc.contains(QStringLiteral("401120")) && !pc.contains(QStringLiteral("401155")) && !pc.contains(QStringLiteral("00000000")), qPrintable(pc)); }
    QSignalSpy cmdSpy(mainWindow.coordinator(), SIGNAL(commandSentToGdb(int,QString)));
    mainWindow.coordinator()->requestStackAndThreads(0); mainWindow.coordinator()->selectStackFrame(0, 1, 0); mainWindow.coordinator()->evaluateExpression(0, QStringLiteral("r")); mainWindow.coordinator()->sendDebuggerCommand(0, QStringLiteral("bt"));
    mainWindow.onRankSelected(1); mainWindow.onRankSelected(0); QCOMPARE(cmdSpy.count(), 0);
    const QString launchSummary = mainWindow.coordinator()->lastLaunchCommandSummary(); QVERIFY2(launchSummary.contains(QStringLiteral("OMP_NUM_THREADS=")) && launchSummary.contains(QStringLiteral("OMP_WAIT_POLICY=PASSIVE")), qPrintable(launchSummary));
    auto* progOut = mainWindow.findChild<QPlainTextEdit*>(QStringLiteral("programOutputView")); QVERIFY(progOut != nullptr); QTRY_COMPARE_WITH_TIMEOUT(barrierView->executionLine(), solveBp, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(([&]() { const QString o = progOut->toPlainText(); return o.contains(QStringLiteral("PRE_BARRIER_R0")) && o.contains(QStringLiteral("PRE_BARRIER_R1")) && o.contains(QStringLiteral("PRE_BARRIER_R2")) && o.contains(QStringLiteral("PRE_BARRIER_R3")) && o.contains(QStringLiteral("POL=PASSIVE")) && !o.contains(QStringLiteral("OMP=NONE")); })(), 5000);
    memMapBtn->click(); treemapCanvas->resize(720, 260); treemapCanvas->show(); QApplication::processEvents();
    const qint64 liveRankPid = mainWindow.coordinator()->getPidForRank(0); int rectCount = 0; QVERIFY(liveRankPid > 0 && QMetaObject::invokeMethod(memProf, "refreshForPid", Q_ARG(qint64, liveRankPid)) && QMetaObject::invokeMethod(memProf, "treemapRectCount", Q_RETURN_ARG(int, rectCount)) && breakdownTable->rowCount() >= 3 && rectCount >= 2);
    bool sawSharedLib = false, sawArena = false;
    for (int r = 0; r < breakdownTable->rowCount(); ++r) {
        const QString lib = breakdownTable->item(r, 0) ? breakdownTable->item(r, 0)->text() : QString(), cat = breakdownTable->item(r, 1) ? breakdownTable->item(r, 1)->text() : QString(), sym = breakdownTable->item(r, 5) ? breakdownTable->item(r, 5)->text() : QString();
        const double mb = breakdownTable->item(r, 2) ? breakdownTable->item(r, 2)->text().section(QLatin1Char(' '), 0, 0).toDouble() : 0.0;
        if ((lib.contains(QStringLiteral("libc")) || lib.contains(QStringLiteral("libstdc++")) || lib.contains(QStringLiteral("libmpi"))) && mb > 0.0) sawSharedLib = true;
        if ((lib.contains(QStringLiteral("barrier_hex")) || sym.contains(QStringLiteral("allocateSolverArena")) || cat.contains(QStringLiteral("Heap"))) && mb >= 3.8) sawArena = true;
    }
    QVERIFY2(sawSharedLib && sawArena, "Breakdown table must include live shared libraries and >=3.8 MB arena attribution");
    breakdownTable->sortByColumn(2, Qt::DescendingOrder); QApplication::processEvents();
    QVERIFY(breakdownTable->item(0, 2)->text().section(' ', 0, 0).toDouble() >= breakdownTable->item(breakdownTable->rowCount() - 1, 2)->text().section(' ', 0, 0).toDouble());
    auto visProfCount = [&]() { int c = 0; for (int r = 0; r < breakdownTable->rowCount(); ++r) if (!breakdownTable->isRowHidden(r)) ++c; return c; };
    const int fullProfCount = visProfCount(); profSearch->setText(QStringLiteral("libc")); QApplication::processEvents();
    QVERIFY(visProfCount() > 0 && visProfCount() < fullProfCount);
    profSearch->clear(); QApplication::processEvents(); QCOMPARE(visProfCount(), fullProfCount);
    mainWindow.stopTargetWorkflow();
}
/** @brief Verifies GdbRankCoordinator signal disposition/catchpoint MI commands and SignalCatchpointDialog UI integration. */
void TestMainWindowUI::testSignalDispositionAndCatchpoints() {
    gridlock::GdbRankCoordinator coord; coord.initializeMockSession(2);
    for (int r = 0; r < 2; ++r) coord.processGdbOutput(r, QStringLiteral("*stopped,reason=\"breakpoint-hit\",frame={addr=\"0x401000\",func=\"main\",fullname=\"/tmp/main.cpp\",line=\"10\",args=[]}\n"));
    QStringList sentCmds; connect(&coord, &gridlock::GdbRankCoordinator::commandSentToGdb, [&](int r, const QString& cmd) { if (r == 0) sentCmds.append(cmd.trimmed()); });
    coord.setSignalDisposition(QStringLiteral("SIGUSR1"), false, false, true); QVERIFY(sentCmds.contains(QStringLiteral("-interpreter-exec console \"handle SIGUSR1 nostop noprint pass\"")));
    coord.insertCatchpoint({CatchpointSpec::Kind::CppThrow, QStringLiteral("std::runtime_error"), -1, true}); coord.insertCatchpoint({CatchpointSpec::Kind::Syscall, QStringLiteral("mmap, openat, clone3, ioctl"), -1, true}); coord.insertCatchpoint({CatchpointSpec::Kind::LoadLib, QStringLiteral("libmpi"), -1, true});
    QVERIFY(sentCmds.contains(QStringLiteral("-catch-throw -r std::runtime_error")) && sentCmds.contains(QStringLiteral("-interpreter-exec console \"catch syscall mmap openat clone3 ioctl\"")) && sentCmds.contains(QStringLiteral("-catch-load -r libmpi")));
    gridlock::ui::SignalCatchpointDialog dlg(&coord); QCOMPARE(dlg.catchpoints().size(), 3); dlg.setSignalDispositionRow(QStringLiteral("SIGFPE"), true, true, false); QVERIFY(dlg.dispositionForSignal(QStringLiteral("SIGFPE")).stop && !dlg.dispositionForSignal(QStringLiteral("SIGFPE")).pass);
    dlg.removeCatchpointById(dlg.catchpoints().first().gdbId); QVERIFY(dlg.catchpoints().size() == 2 && sentCmds.contains(QStringLiteral("-break-delete 1")));
    gridlock::ui::MainWindow mw; mw.show(); QApplication::processEvents(); auto* sigAct = mw.findChild<QAction*>(QStringLiteral("actionSignalsAndCatchpoints")); QVERIFY(sigAct && sigAct->shortcut() == QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_S));
    sigAct->trigger(); QApplication::processEvents(); auto* mwSigDlg = mw.findChild<gridlock::ui::SignalCatchpointDialog*>(QStringLiteral("SignalCatchpointDialog")); QVERIFY(mwSigDlg && mwSigDlg->isVisible()); mwSigDlg->close();
}
/** @brief Verifies nexti, Ctrl+F10 Run to Cursor, draggable gutter PC jump, lexical scope guard, Force Early Return, and shortcut uniqueness. */
void TestMainWindowUI::testNonLinearExecutionControl() {
    gridlock::GdbRankCoordinator coord; coord.initializeMockSession(2);
    for (int r = 0; r < 2; ++r) coord.processGdbOutput(r, QStringLiteral("*stopped,reason=\"breakpoint-hit\",frame={addr=\"0x401120\",func=\"solve\",fullname=\"/tmp/solver.cpp\",line=\"4\",args=[]}\n"));
    QStringList sent; connect(&coord, &gridlock::GdbRankCoordinator::commandSentToGdb, [&](int r, const QString& c) { if (r == 0) sent.append(c.trimmed()); });
    coord.nextInstruction(0); coord.runToCursor(QStringLiteral("/tmp/solver.cpp"), 12, 0); coord.runToAddress(0x401180ULL, 0); coord.jumpToLocation(QStringLiteral("/tmp/solver.cpp"), 15, 0); coord.jumpToAddress(0x401190ULL, 0); coord.forceEarlyReturn(QString(), 0); coord.forceEarlyReturn(QStringLiteral("42"), 0);
    QVERIFY(sent.contains(QStringLiteral("-exec-next-instruction")) && sent.contains(QStringLiteral("-exec-until \"/tmp/solver.cpp:12\"")) && sent.contains(QStringLiteral("-exec-until *0x401180")) && sent.contains(QStringLiteral("-exec-jump \"/tmp/solver.cpp:15\"")) && sent.contains(QStringLiteral("-exec-jump *0x401190")) && sent.contains(QStringLiteral("-exec-return")) && sent.contains(QStringLiteral("-exec-return 42")));
    const QString sampleCpp = QStringLiteral("int solve(int x) {\n  int a = 1;\n  a += x;\n  a *= 2;\n  int b = 7;\n  if (a > 0) {\n    a += b;\n  }\n  return a;\n}\nint helper() {\n  return 0;\n}\n");
    QVERIFY(gridlock::ui::MainWindow::checkJumpLexicalScopeHazard(sampleCpp, 3, 4, QStringLiteral("solve")).isEmpty() && !gridlock::ui::MainWindow::checkJumpLexicalScopeHazard(sampleCpp, 4, 6, QStringLiteral("solve")).isEmpty() && !gridlock::ui::MainWindow::checkJumpLexicalScopeHazard(sampleCpp, 3, 7, QStringLiteral("solve")).isEmpty() && !gridlock::ui::MainWindow::checkJumpLexicalScopeHazard(sampleCpp, 3, 12, QStringLiteral("solve")).isEmpty());
    static QStringList s_execWarnings; s_execWarnings.clear();
    const QtMessageHandler prevH = qInstallMessageHandler([](QtMsgType t, const QMessageLogContext&, const QString& m) { if (t == QtWarningMsg) s_execWarnings.append(m); });
    struct WarnGuard { QtMessageHandler p; ~WarnGuard() { qInstallMessageHandler(p); } } wg{prevH};
    gridlock::ui::MainWindow mw; mw.resize(1440, 900); mw.show(); QApplication::processEvents(); mw.setCoordinator(&coord); sent.clear();
    auto* sv = mw.sourceCodeView(); auto* dv = mw.disassemblyView(); QVERIFY(sv && dv); sv->setCurrentFile(QStringLiteral("/tmp/solver.cpp")); sv->setSourceCode(sampleCpp); sv->highlightExecutionLine(3); QApplication::processEvents();
    QWidget* gutter = nullptr; for (QWidget* w : sv->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly)) if (w != sv->viewport() && w->width() == sv->lineNumberAreaWidth()) { gutter = w; break; }
    QVERIFY(gutter != nullptr); QSignalSpy jumpSpy(sv, &gridlock::ui::SourceCodeView::jumpToLineRequested);
    const int rowH = qMax(14, sv->fontMetrics().lineSpacing()); const QPoint pStart(10, 2 * rowH + rowH / 2), pEnd(10, 3 * rowH + rowH / 2);
    QTest::mousePress(gutter, Qt::LeftButton, Qt::NoModifier, pStart); QTest::mouseMove(gutter, pEnd); QApplication::processEvents(); QVERIFY(sv->isDraggingExecutionPc() && sv->dragTargetLine() == 4);
    QTest::mouseRelease(gutter, Qt::LeftButton, Qt::NoModifier, pEnd); QApplication::processEvents(); QVERIFY(jumpSpy.count() == 1 && jumpSpy.first().at(1).toInt() == 4 && sent.contains(QStringLiteral("-exec-jump \"/tmp/solver.cpp:4\"")));
    sv->setTextCursor(QTextCursor(sv->document()->findBlockByNumber(8))); sv->setFocus(); QTest::keyClick(&mw, Qt::Key_F10, Qt::ControlModifier); QApplication::processEvents(); QVERIFY(sent.contains(QStringLiteral("-exec-until \"/tmp/solver.cpp:9\"")));
    QTest::keyClick(&mw, Qt::Key_F10, Qt::ControlModifier | Qt::ShiftModifier); QApplication::processEvents(); QVERIFY(sent.contains(QStringLiteral("-exec-next-instruction")));
    auto* nextiBtn = dv->findChild<QPushButton*>(QStringLiteral("disasmNextiBtn")); QVERIFY(nextiBtn && dv->findChild<QPushButton*>(QStringLiteral("disasmStepiBtn"))); sent.clear(); nextiBtn->click(); QVERIFY(sent.contains(QStringLiteral("-exec-next-instruction")));
    emit dv->runToAddressRequested(0x401180ULL); emit dv->jumpToAddressRequested(0x401190ULL); QVERIFY(sent.contains(QStringLiteral("-exec-until *0x401180")) && sent.contains(QStringLiteral("-exec-jump *0x401190")) && mw.forceEarlyReturnWorkflow(QStringLiteral("7"), false) && sent.contains(QStringLiteral("-exec-return 7")));
    QVERIFY(mw.findChild<QAction*>(QStringLiteral("actionNextInstruction")) && mw.findChild<QAction*>(QStringLiteral("actionRunToCursor")) && mw.findChild<QAction*>(QStringLiteral("actionJumpToLine")) && mw.findChild<QAction*>(QStringLiteral("actionForceEarlyReturn")));
    for (const QString& w : s_execWarnings) QVERIFY2(!w.contains(QStringLiteral("Ambiguous shortcut overload"), Qt::CaseInsensitive), qPrintable(w));
    mw.setCoordinator(nullptr);
}
/** @brief Verifies non-destructive breakpoint enable/disable hollow markers, tbreak, rbreak, and GDB breakpoint command lists. */
void TestMainWindowUI::testBreakpointLifecycleAndCommands() {
    gridlock::GdbRankCoordinator coord; coord.initializeMockSession(2);
    for (int r = 0; r < 2; ++r) coord.processGdbOutput(r, QStringLiteral("*stopped,reason=\"breakpoint-hit\",frame={addr=\"0x401120\",func=\"solve\",fullname=\"/tmp/solver.cpp\",line=\"4\",args=[]}\n"));
    QStringList sent; connect(&coord, &gridlock::GdbRankCoordinator::commandSentToGdb, [&](int r, const QString& c) { if (r == 0) sent.append(c.trimmed()); });
    QSignalSpy createdSpy(&coord, &IBackendCoordinator::breakpointRecordCreated); QSignalSpy tempHitSpy(&coord, &IBackendCoordinator::temporaryBreakpointHit);
    coord.setBreakpoint(QStringLiteral("/tmp/solver.cpp"), 10, QStringLiteral("rank == 0"), -1);
    coord.processGdbOutput(0, QStringLiteral("^done,bkpt={number=\"3\",type=\"breakpoint\",disp=\"keep\",enabled=\"y\",fullname=\"/tmp/solver.cpp\",line=\"10\"}\n"));
    coord.setBreakpointEnabled(QStringLiteral("/tmp/solver.cpp"), 10, false); coord.setBreakpointEnabled(QStringLiteral("/tmp/solver.cpp"), 10, true);
    coord.setBreakpointCommands(QStringLiteral("/tmp/solver.cpp"), 10, {QStringLiteral("silent"), QStringLiteral("printf \"rank=%d\\n\", rank"), QStringLiteral("continue")});
    coord.setBreakpointCommands(QStringLiteral("/tmp/solver.cpp"), 10, {});
    coord.insertTemporaryBreakpoint(QStringLiteral("/tmp/solver.cpp"), 14);
    coord.processGdbOutput(0, QStringLiteral("^done,bkpt={number=\"4\",type=\"breakpoint\",disp=\"del\",enabled=\"y\",fullname=\"/tmp/solver.cpp\",line=\"14\"}\n=breakpoint-deleted,id=\"4\"\n"));
    coord.insertRegexBreakpoint(QStringLiteral("MPI_.*|Solver::step.*"));
    coord.processGdbOutput(0, QStringLiteral("=breakpoint-created,bkpt={number=\"5\",type=\"breakpoint\",disp=\"keep\",enabled=\"y\",func=\"MPI_Send\",fullname=\"/tmp/solver.cpp\",line=\"20\"}\n"));
    QVERIFY(sent.contains(QStringLiteral("-break-disable 3")) && sent.contains(QStringLiteral("-break-enable 3")) && sent.contains(QStringLiteral("-break-commands 3")));
    QVERIFY(sent.contains(QStringLiteral("-break-commands 3 \"silent\" \"printf \\\"rank=%d\\\\n\\\", rank\" \"continue\"")) && sent.contains(QStringLiteral("-break-insert -t -f /tmp/solver.cpp:14")) && sent.contains(QStringLiteral("-interpreter-exec console \"rbreak MPI_.*|Solver::step.*\"")));
    QVERIFY(tempHitSpy.count() == 1 && tempHitSpy.first().at(2).toInt() == 14 && createdSpy.count() >= 1 && !coord.regexCreatedBreakpoints().isEmpty() && coord.regexCreatedBreakpoints().last().gdbId == 5 && coord.regexCreatedBreakpoints().last().line == 20);
    gridlock::ui::SourceCodeView srcView; QString sampleCode; for (int i = 1; i <= 25; ++i) sampleCode += QStringLiteral("    int step_%1 = %1;\n").arg(i);
    srcView.setCurrentFile(QStringLiteral("/tmp/solver.cpp")); srcView.setSourceCode(sampleCode); srcView.resize(640, 420); srcView.show(); QApplication::processEvents();
    srcView.toggleBreakpoint(10); srcView.setBreakpointOptions(10, QStringLiteral("rank == 0"), 2, QString()); srcView.setBreakpointCommands(10, {QStringLiteral("silent"), QStringLiteral("continue")});
    srcView.handleBreakpointPopoverResult(10, true, true, false, QStringLiteral("rank == 0"), QStringLiteral("All"), false);
    QVERIFY(srcView.hasBreakpoint(10) && !srcView.isBreakpointEnabledForLine(10) && srcView.gutterMarkerStateForLine(10) == QStringLiteral("DisabledBreakpoint") && srcView.breakpointCondition(10) == QStringLiteral("rank == 0") && srcView.breakpointIgnoreCountForLine(10) == 2 && srcView.breakpointCommandsForLine(10) == (QStringList{QStringLiteral("silent"), QStringLiteral("continue")}) && srcView.breakpointTooltipForLine(10).contains(QStringLiteral("[DISABLED]")) && srcView.breakpointTooltipForLine(10).contains(QStringLiteral("GDB Commands: 2")));
    srcView.setBreakpointEnabled(10, true); QVERIFY(srcView.isBreakpointEnabledForLine(10) && srcView.gutterMarkerStateForLine(10) == QStringLiteral("ConditionalBreakpoint"));
    srcView.setTemporaryBreakpoint(14, true); QVERIFY(srcView.hasBreakpoint(14) && srcView.isTemporaryBreakpointForLine(14) && srcView.gutterMarkerStateForLine(14) == QStringLiteral("TemporaryBreakpoint")); srcView.removeBreakpointQuietly(14); QVERIFY(!srcView.hasBreakpoint(14));
    gridlock::ui::ConditionalBreakpointDialog bpDlg(QString(), &srcView); bpDlg.setSourceDocumentContext(QStringLiteral("/tmp/solver.cpp"), sampleCode, 10); bpDlg.setTargetLineContext(10, QStringLiteral("int step_10 = 10;"));
    QSignalSpy enSpy(&bpDlg, &gridlock::ui::ConditionalBreakpointDialog::breakpointEnabledToggled);
    auto* enChk = bpDlg.findChild<QCheckBox*>(QStringLiteral("bpEnabledCheck")); auto* tmpChk = bpDlg.findChild<QCheckBox*>(QStringLiteral("bpTemporaryCheck")); auto* cmdEd = bpDlg.findChild<QPlainTextEdit*>(QStringLiteral("bpCommandsEdit"));
    QVERIFY(enChk && tmpChk && cmdEd); enChk->setChecked(false); tmpChk->setChecked(true); cmdEd->setPlainText(QStringLiteral("silent\nbt 2\ncontinue"));
    QVERIFY(enSpy.count() == 1 && !enSpy.first().at(2).toBool() && !bpDlg.isRemoveRequested() && bpDlg.isTemporary() && bpDlg.commands() == (QStringList{QStringLiteral("silent"), QStringLiteral("bt 2"), QStringLiteral("continue")}));
    gridlock::ui::MainWindow mw; mw.resize(1440, 900); mw.show(); QApplication::processEvents(); mw.setCoordinator(&coord); sent.clear();
    QVERIFY(mw.findChild<QAction*>(QStringLiteral("actionRegexBreakpoint")) != nullptr && mw.insertRegexBreakpointWorkflow(QStringLiteral("Solver::.*"), false) && sent.contains(QStringLiteral("-interpreter-exec console \"rbreak Solver::.*\"")));
    auto* mwSv = mw.sourceCodeView(); QVERIFY(mwSv != nullptr); mwSv->setCurrentFile(QStringLiteral("/tmp/solver.cpp")); mwSv->setSourceCode(sampleCode);
    mwSv->setTemporaryBreakpoint(14, true); QVERIFY(mwSv->hasBreakpoint(14)); emit coord.temporaryBreakpointHit(0, QStringLiteral("/tmp/solver.cpp"), 14); QApplication::processEvents(); QVERIFY(!mwSv->hasBreakpoint(14));
    QTest::keyClick(&mw, Qt::Key_P, Qt::ControlModifier | Qt::ShiftModifier); QApplication::processEvents();
    auto* pal = mw.findChild<gridlock::ui::CommandPaletteDialog*>(QStringLiteral("CommandPaletteDialog")); QVERIFY(pal && pal->isVisible());
    pal->setFilterText(QStringLiteral("> rbreak")); QVERIFY(pal->resultCount() > 0 && pal->resultItemAt(0).title.contains(QStringLiteral("rbreak"), Qt::CaseInsensitive)); QTest::keyClick(pal->inputEdit(), Qt::Key_Escape); mw.setCoordinator(nullptr);
}
static void testCleanExitWhileTargetRunning() {
    static QStringList s_exitWarnings; s_exitWarnings.clear();
    const QtMessageHandler prevH = qInstallMessageHandler([](QtMsgType t, const QMessageLogContext&, const QString& m) { if (t == QtWarningMsg) s_exitWarnings.append(m); });
    struct ExitWarnGuard { QtMessageHandler p; ~ExitWarnGuard() { qInstallMessageHandler(p); } } wg{prevH};
    QTemporaryDir spinDir; QVERIFY(spinDir.isValid()); const QString spinCpp = spinDir.filePath(QStringLiteral("spin_exit.cpp"));
    { QFile sf(spinCpp); QVERIFY(sf.open(QIODevice::WriteOnly)); sf.write("#include <unistd.h>\nint main() { while (true) { usleep(20000); } return 0; }\n"); }
    auto& cfg = gridlock::core::ConfigManager::instance(); cfg.setDebuggerEngine(QStringLiteral("gdb")); cfg.setGlobalMpiArgs(QStringLiteral("--oversubscribe"));
    auto isGone = [](qint64 pid) { if (pid <= 0 || ::kill(static_cast<pid_t>(pid), 0) != 0) return true; QFile f(QStringLiteral("/proc/%1/status").arg(pid)); return !f.open(QIODevice::ReadOnly) || f.readAll().contains("State:\tZ"); };
    const int ranks = (!QStandardPaths::findExecutable(QStringLiteral("mpirun")).isEmpty() || !QStandardPaths::findExecutable(QStringLiteral("mpiexec")).isEmpty()) ? 2 : 1;
    auto mw = std::make_unique<gridlock::ui::MainWindow>(); mw->resize(1440, 900); mw->show(); QApplication::processEvents();
    auto* td = mw->findChild<gridlock::ui::TerminalDockWidget*>(); QVERIFY(td != nullptr); td->ensureShellStarted(); td->sendCommand(QStringLiteral("trap '' HUP")); const int ptyPid = td->shellPid(); QVERIFY(ptyPid > 0 && !isGone(ptyPid));
    auto* tc = mw->findChild<QComboBox*>(QStringLiteral("toolbarTargetSelector")); auto* rs = mw->findChild<QSpinBox*>(QStringLiteral("toolbarRankSpin")); QVERIFY(tc && rs); rs->setValue(ranks); tc->insertItem(0, QStringLiteral("spin_exit"), spinCpp); QVERIFY(mw->selectTargetByName(QStringLiteral("spin_exit")) && mw->ensureTargetBinaryCompiled());
    if (auto* sv = mw->sourceCodeView()) for (int bp : sv->breakpoints().values()) sv->toggleBreakpoint(bp);
    mw->startTargetWorkflow(); QVERIFY(mw->isDebugSessionActive() && mw->coordinator() != nullptr); QTRY_VERIFY_WITH_TIMEOUT(mw->coordinator()->getPidForRank(0) > 0 && (ranks < 2 || mw->coordinator()->getPidForRank(1) > 0), 8000);
    const qint64 pid0 = mw->coordinator()->getPidForRank(0), pid1 = (ranks >= 2) ? mw->coordinator()->getPidForRank(1) : 0; QVERIFY(::kill(static_cast<pid_t>(pid0), 0) == 0 && (ranks < 2 || ::kill(static_cast<pid_t>(pid1), 0) == 0));
    QVERIFY(mw->close()); QApplication::processEvents(); QVERIFY(mw->isShuttingDown() && !mw->isDebugSessionActive() && td->shellPid() == 0 && isGone(ptyPid)); mw.reset(); QApplication::processEvents(); QTRY_VERIFY_WITH_TIMEOUT(isGone(pid0) && isGone(pid1) && isGone(ptyPid), 2500);
    auto mw2 = std::make_unique<gridlock::ui::MainWindow>(); mw2->resize(1280, 800); mw2->show(); QApplication::processEvents();
    auto* tc2 = mw2->findChild<QComboBox*>(QStringLiteral("toolbarTargetSelector")); auto* rs2 = mw2->findChild<QSpinBox*>(QStringLiteral("toolbarRankSpin")); QVERIFY(tc2 && rs2); rs2->setValue(1); tc2->insertItem(0, QStringLiteral("spin_exit"), spinCpp); QVERIFY(mw2->selectTargetByName(QStringLiteral("spin_exit")) && mw2->ensureTargetBinaryCompiled());
    mw2->startTargetWorkflow(); QTRY_VERIFY_WITH_TIMEOUT(mw2->isDebugSessionActive() && mw2->coordinator() && mw2->coordinator()->getPidForRank(0) > 0, 8000);
    const qint64 pidDirect = mw2->coordinator()->getPidForRank(0), ptyPid2 = mw2->findChild<gridlock::ui::TerminalDockWidget*>() ? mw2->findChild<gridlock::ui::TerminalDockWidget*>()->shellPid() : 0;
    QVERIFY(::kill(static_cast<pid_t>(pidDirect), 0) == 0 && ptyPid2 > 0); mw2.reset(); QApplication::processEvents(); QTRY_VERIFY_WITH_TIMEOUT(isGone(pidDirect) && isGone(ptyPid2), 2500);
    for (const QString& w : s_exitWarnings) QVERIFY2(!w.contains(QStringLiteral("did not die with SIGHUP"), Qt::CaseInsensitive), qPrintable(w));
}
static void testMultiRankScopePauseAllAndRackFilter() {
    gridlock::ui::MainWindow mw; mw.resize(1440, 900); mw.show(); QApplication::processEvents();
    auto* rs = mw.findChild<QSpinBox*>(QStringLiteral("toolbarRankSpin")); auto* sc = mw.findChild<QComboBox*>(QStringLiteral("toolbarExecutionScopeCombo"));
    auto* pa = mw.findChild<QAction*>(QStringLiteral("actionPauseAllRanks")); auto* ca = mw.findChild<QAction*>(QStringLiteral("debug.continue")); auto* oa = mw.findChild<QAction*>(QStringLiteral("debug.step_over"));
    QVERIFY(rs && sc && pa && ca && oa && pa->shortcut() == QKeySequence(Qt::Key_F6) && !sc->isVisible());
    bool inDbg = false; for (QAction* ma : mw.menuBar()->actions()) if (QMenu* m = ma ? ma->menu() : nullptr; m && m->title().remove(QLatin1Char('&')) == QLatin1String("Debug")) inDbg = m->actions().contains(pa);
    QVERIFY(inDbg);
    gridlock::GdbRankCoordinator coord1; coord1.initializeMockSession(1); mw.setCoordinator(&coord1); QApplication::processEvents();
    QVERIFY2(!sc->isVisible(), "Scope pill must stay hidden when active ranks == 1");
    gridlock::GdbRankCoordinator coord; coord.initializeMockSession(3);
    auto stopAll3 = [&]() { for (int r = 0; r < 3; ++r) coord.processGdbOutput(r, QStringLiteral("*stopped,reason=\"breakpoint-hit\",frame={addr=\"0x401120\",func=\"solve\",fullname=\"/tmp/solver.cpp\",line=\"4\",args=[]}\n")); };
    stopAll3(); mw.setCoordinator(&coord); mw.onRankSelected(1); QApplication::processEvents();
    QVERIFY2(sc->isVisible() && mw.findChild<QToolButton*>(QStringLiteral("toolbarPauseAllRanksBtn"))->isVisible(), "Scope pill and Pause All button must be visible when active ranks > 1");
    QMap<int, QStringList> sent; QObject::connect(&coord, &gridlock::GdbRankCoordinator::commandSentToGdb, [&](int r, const QString& c) { sent[r].append(c.trimmed()); });
    mw.setExecutionScopeAllRanks(true); QApplication::processEvents();
    QVERIFY(mw.isAllRanksExecutionScope() && mw.executionTargetRank() == -1 && oa->toolTip().contains(QStringLiteral("All Ranks")) && ca->toolTip().contains(QStringLiteral("All Ranks")));
    sent.clear(); QVERIFY(mw.stepOverWorkflow() && mw.stepIntoWorkflow() && mw.stepOutWorkflow() && mw.continueExecutionWorkflow());
    for (int r = 0; r < 3; ++r) QVERIFY(sent[r].contains(QStringLiteral("-exec-next")) && sent[r].contains(QStringLiteral("-exec-step")) && sent[r].contains(QStringLiteral("-exec-finish")) && sent[r].contains(QStringLiteral("-exec-continue")));
    stopAll3(); mw.setExecutionScopeAllRanks(false); QApplication::processEvents();
    QVERIFY(!mw.isAllRanksExecutionScope() && mw.executionTargetRank() == 1 && sc->itemText(1) == QStringLiteral("Rank 1") && oa->toolTip().contains(QStringLiteral("Rank 1")) && ca->toolTip().contains(QStringLiteral("Rank 1")));
    sent.clear(); QVERIFY(mw.stepOverWorkflow() && mw.stepIntoWorkflow() && mw.stepOutWorkflow() && mw.continueExecutionWorkflow());
    QVERIFY(sent[1].contains(QStringLiteral("-exec-next")) && sent[1].contains(QStringLiteral("-exec-step")) && sent[1].contains(QStringLiteral("-exec-finish")) && sent[1].contains(QStringLiteral("-exec-continue")) && !sent[0].contains(QStringLiteral("-exec-next")) && !sent[2].contains(QStringLiteral("-exec-continue")));
    sent.clear(); QTest::keyClick(&mw, Qt::Key_F6); QApplication::processEvents(); for (int r = 0; r < 3; ++r) QVERIFY(sent[r].contains(QStringLiteral("-exec-interrupt")));
    QTest::keyClick(&mw, Qt::Key_P, Qt::ControlModifier | Qt::ShiftModifier); QApplication::processEvents();
    auto* pal = mw.findChild<gridlock::ui::CommandPaletteDialog*>(QStringLiteral("CommandPaletteDialog")); QVERIFY(pal && pal->isVisible());
    pal->setFilterText(QStringLiteral("> Pause All Ranks")); QVERIFY(pal->resultCount() > 0 && pal->resultItemAt(0).badge == QStringLiteral("F6"));
    pal->setFilterText(QStringLiteral("> Step Over")); QVERIFY(pal->resultCount() > 0 && pal->resultItemAt(0).subtitle.contains(QStringLiteral("Rank 1"))); QTest::keyClick(pal->inputEdit(), Qt::Key_Escape);
    auto* rack = mw.findChild<gridlock::ui::ServerRackView*>(); auto* fAll = mw.findChild<QToolButton*>(QStringLiteral("serverRackFilterAllBtn")); auto* fDead = mw.findChild<QToolButton*>(QStringLiteral("serverRackFilterDeadlockBtn")); auto* fStop = mw.findChild<QToolButton*>(QStringLiteral("serverRackFilterStoppedBtn")); auto* fRun = mw.findChild<QToolButton*>(QStringLiteral("serverRackFilterRunningBtn"));
    QVERIFY(rack && fAll && fDead && fStop && fRun && gridlock::ui::ServerRackView::badgeTextForState(QStringLiteral("deadlock")) == QStringLiteral("DLOCK") && gridlock::ui::ServerRackView::badgeColorForState(QStringLiteral("deadlock")) == QColor(QStringLiteral("#f44747")) && gridlock::ui::ServerRackView::badgeTextForState(QStringLiteral("exited")) == QStringLiteral("EXIT") && gridlock::ui::ServerRackView::badgeColorForState(QStringLiteral("exited")) == QColor(QStringLiteral("#64748b")));
    QList<gridlock::RankState> st(4); st[0].rankId = 0; st[0].currentState = QStringLiteral("deadlock"); st[0].registers[16] = QStringLiteral("0x00401120"); st[1].rankId = 1; st[1].currentState = QStringLiteral("stopped"); st[1].registers[16] = QStringLiteral("0x00401140"); st[2].rankId = 2; st[2].currentState = QStringLiteral("running"); st[2].registers[16] = QStringLiteral("0x00401155"); st[3].rankId = 3; st[3].currentState = QStringLiteral("exited");
    rack->updateRanks(st); QApplication::processEvents(); QCOMPARE(rack->visibleRankCount(), 4);
    fDead->click(); QVERIFY(rack->visibleRankCount() == 1 && !rack->item(0)->isHidden() && rack->item(1)->isHidden());
    fStop->click(); QVERIFY(rack->visibleRankCount() == 1 && !rack->item(1)->isHidden() && rack->item(0)->isHidden());
    fRun->click(); QVERIFY(rack->visibleRankCount() == 1 && !rack->item(2)->isHidden() && rack->item(3)->isHidden());
    fAll->click(); QVERIFY(rack->visibleRankCount() == 4 && !rack->grab().isNull());
    gridlock::GdbRankCoordinator coord16; coord16.initializeMockSession(16); QList<gridlock::RankState> st16(16); for (int r = 0; r < 16; ++r) { st16[r].rankId = r; st16[r].currentState = QStringLiteral("stopped"); }
    rack->updateRanks(st16); mw.setCoordinator(&coord16); mw.openArrayPlotter(); mw.openProcessCheckpoints(); QApplication::processEvents();
    auto* varsDock = mw.variablesDockWidget(); auto* memView = mw.memView(); auto* apDlg = mw.findChild<gridlock::ui::ArrayPlotDialog*>(QStringLiteral("ArrayPlotDialog")); auto* cpDlg = mw.checkpointDialog();
    auto* apCombo = apDlg ? apDlg->findChild<QComboBox*>(QStringLiteral("arrayPlotRankCombo")) : nullptr; auto* cpSpin = cpDlg ? cpDlg->findChild<QSpinBox*>(QStringLiteral("checkpointRankSpin")) : nullptr;
    QSignalSpy focusSpy(&mw, &gridlock::ui::MainWindow::focusedRankChanged); mw.setFocusedRank(3); QApplication::processEvents();
    QVERIFY(mw.focusedRank() == 3 && mw.activeSteppingRank() == 3 && focusSpy.count() == 1 && rack->selectedRank() == 3 && varsDock->focusedRank() == 3 && memView->focusedRank() == 3 && apCombo && apCombo->count() == 16 && apCombo->currentData().toInt() == 3 && cpSpin && cpSpin->value() == 3);
    mw.setFocusedRank(11); QApplication::processEvents(); QVERIFY(mw.focusedRank() == 11 && rack->selectedRank() == 11 && varsDock->focusedRank() == 11 && memView->focusedRank() == 11 && apCombo->currentData().toInt() == 11 && cpSpin->value() == 11);
    memView->setRankPinned(true); mw.setFocusedRank(2); QApplication::processEvents(); QVERIFY(mw.focusedRank() == 2 && varsDock->focusedRank() == 2 && memView->focusedRank() == 11);
    memView->setRankPinned(false); mw.setFocusedRank(4); QApplication::processEvents(); QVERIFY(memView->focusedRank() == 4);
    QVERIFY(varsDock->applyLocalsForRank(4, {QJsonObject{{QStringLiteral("name"), QStringLiteral("tag")}, {QStringLiteral("value"), QStringLiteral("400")}, {QStringLiteral("type"), QStringLiteral("int")}}}) && !varsDock->applyLocalsForRank(2, {QJsonObject{{QStringLiteral("name"), QStringLiteral("tag")}, {QStringLiteral("value"), QStringLiteral("200")}, {QStringLiteral("type"), QStringLiteral("int")}}}));
    if (auto* mpiW = mw.findChild<gridlock::ui::MpiDiagnosticsWidget*>()) { mpiW->beginLiveSession(8); if (auto* wt = mpiW->findChild<QTableWidget*>(QStringLiteral("mpiRankWaitTable"))) { emit wt->cellClicked(5, 0); QApplication::processEvents(); QCOMPARE(mw.focusedRank(), 5); } }
    auto* evalW = mw.findChild<gridlock::ui::ExpressionEvaluatorWidget*>(QStringLiteral("ExpressionEvaluatorDock")); auto* evalHist = evalW ? evalW->findChild<QPlainTextEdit*>(QStringLiteral("evaluatorHistoryArea")) : nullptr;
    QVERIFY(evalW && evalHist); mw.setCoordinator(&coord); stopAll3(); evalW->setSelectedTargetRank(-1); sent.clear(); mw.evaluateExpressionFromWidget(QStringLiteral("my_val")); for (int r = 0; r < 3; ++r) QVERIFY(sent[r].join(QLatin1Char('\n')).contains(QStringLiteral("-data-evaluate-expression \"my_val\"")));
    emit coord.expressionEvaluated(0, QStringLiteral("my_val"), QStringLiteral("42")); emit coord.expressionEvaluated(1, QStringLiteral("my_val"), QStringLiteral("43")); emit coord.expressionEvaluated(2, QStringLiteral("my_val"), QStringLiteral("42")); QApplication::processEvents();
    const QString hTxt = evalHist->toPlainText(); QVERIFY(hTxt.contains(QStringLiteral("[Rank 0]: 42")) && hTxt.contains(QStringLiteral("[Rank 1]: 43")) && hTxt.contains(QStringLiteral("[Rank 2]: 42")) && hTxt.contains(QStringLiteral("[DIVERGENT]"))); apDlg->close(); cpDlg->close(); mw.setCoordinator(nullptr);
}
static void testLaunchOptionsCliArgsAndUnifiedSessionStart() {
    gridlock::core::utils::CliParser cli;
    QVERIFY(cli.parse({QStringLiteral("gridlock"), QStringLiteral("./solver"), QStringLiteral("--"), QStringLiteral("--nx"), QStringLiteral("64"), QStringLiteral("--tol"), QStringLiteral("1e-6")}) && cli.targetPath() == QStringLiteral("./solver") && cli.getTestFilePath() == QStringLiteral("./solver") && cli.options().programArgs == (QStringList{QStringLiteral("--nx"), QStringLiteral("64"), QStringLiteral("--tol"), QStringLiteral("1e-6")}) && cli.programArgsString() == QStringLiteral("--nx 64 --tol 1e-6"));
    QTemporaryDir lDir; QVERIFY(lDir.isValid()); const QString qCpp = lDir.filePath(QStringLiteral("quick_main.cpp"));
    { QFile qf(qCpp); QVERIFY(qf.open(QIODevice::WriteOnly)); qf.write("#include <cstdio>\nint main(int argc, char** argv) {\n  (void)argv;\n  printf(\"ARGC=%d\\n\", argc);\n  return 0;\n}\n"); }
    auto& cfg = gridlock::core::ConfigManager::instance(); const QString savedRepo = QFileInfo::exists(QStringLiteral("meson.build")) ? QDir::currentPath() : QDir(QStringLiteral("..")).absolutePath();
    struct WsRestore { QString p; ~WsRestore() { gridlock::core::ConfigManager::instance().setWorkspace(p); } } restore{savedRepo};
    gridlock::ui::MainWindow mw; mw.resize(1440, 900); mw.show(); QApplication::processEvents();
    QVERIFY(mw.applyCliLaunchOptions(qCpp, {QStringLiteral("--nx"), QStringLiteral("64"), QStringLiteral("--tol"), QStringLiteral("1e-6")}) && cfg.loadProjectSettings().programArguments == "--nx 64 --tol 1e-6");
    auto* loBtn = mw.findChild<QToolButton*>(QStringLiteral("toolbarLaunchOptionsBtn")); auto* argsEd = mw.findChild<QLineEdit*>(QStringLiteral("launchOptionsArgsEdit")); auto* wdEd = mw.findChild<QLineEdit*>(QStringLiteral("launchOptionsWorkDirEdit")); auto* stopChk = mw.findChild<QCheckBox*>(QStringLiteral("launchOptionsStopAtMainCheck"));
    QVERIFY(loBtn && loBtn->menu() && argsEd && wdEd && stopChk && mw.stopAtMainOnLaunch()); emit loBtn->menu()->aboutToShow(); QCOMPARE(argsEd->text(), QStringLiteral("--nx 64 --tol 1e-6")); QVERIFY(stopChk->isChecked());
    argsEd->setText(QStringLiteral("--nx 128 --ny 64")); emit argsEd->textEdited(argsEd->text()); wdEd->setText(lDir.path()); emit wdEd->textEdited(wdEd->text());
    QVERIFY(cfg.loadProjectSettings().programArguments == "--nx 128 --ny 64" && QString::fromStdString(cfg.loadProjectSettings().workingDirectory) == lDir.path());
    if (auto* sv = mw.sourceCodeView()) sv->setBreakpoints({});
    if (auto* rs = mw.findChild<QSpinBox*>(QStringLiteral("toolbarRankSpin"))) rs->setValue(1);
    auto* runAct = mw.findChild<QAction*>(QStringLiteral("actionRunTarget")); QVERIFY(runAct != nullptr); runAct->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(mw.isDebugSessionActive() && mw.latestRankStates().count(0) > 0 && mw.latestRankStates().at(0).currentState == QStringLiteral("stopped") && mw.latestRankStates().at(0).currentFunction == QStringLiteral("main"), 8000);
    mw.stopTargetWorkflow(); QApplication::processEvents();
    gridlock::ui::dialogs::ProjectWizard wiz; wiz.findChild<QLineEdit*>(QStringLiteral("wizardWorkspaceDirEdit"))->setText(lDir.path()); wiz.findChild<QLineEdit*>(QStringLiteral("wizardTargetExecEdit"))->setText(mw.activeTargetBinaryPath());
    wiz.findChild<QLineEdit*>(QStringLiteral("wizardExecArgsEdit"))->setText(QStringLiteral("--mesh fine.msh --dt 0.01")); wiz.findChild<QLineEdit*>(QStringLiteral("wizardCustomMpiArgsEdit"))->setText(QStringLiteral("--bind-to core")); wiz.accept();
    QVERIFY(!wiz.getMpiLaunchCommand().contains(QStringLiteral("--mesh")) && cfg.loadProjectSettings().programArguments == "--mesh fine.msh --dt 0.01" && !cfg.loadProjectSettings().mpiArgs.contains(QStringLiteral("--mesh")));
    mw.loadPreviewFixture(); mw.stopTargetWorkflow(); QVERIFY(!mw.isDebugSessionActive()); mw.startDebuggingSession(QString(), 2); QVERIFY(mw.isDebugSessionActive() && mw.isRunButtonLatched());
    mw.stopTargetWorkflow(); QTest::keyClick(&mw, Qt::Key_R, Qt::AltModifier); QApplication::processEvents(); QVERIFY(mw.isDebugSessionActive()); mw.stopTargetWorkflow();
}
static void testLiveVariableRegisterEditingAndFullValueInspection() {
    gridlock::GdbRankCoordinator coord; coord.initializeMockSession(2);
    for (int r = 0; r < 2; ++r) coord.processGdbOutput(r, QStringLiteral("*stopped,reason=\"breakpoint-hit\",frame={addr=\"0x401120\",func=\"solve\",fullname=\"/tmp/solver.cpp\",line=\"4\",args=[]}\n"));
    QMap<int, QStringList> sent; QObject::connect(&coord, &gridlock::GdbRankCoordinator::commandSentToGdb, [&](int r, const QString& c) { sent[r].append(c.trimmed()); });
    gridlock::ui::MainWindow mw; mw.resize(1440, 900); mw.show(); QApplication::processEvents(); mw.setCoordinator(&coord); mw.setFocusedRank(1);
    auto* varsDock = mw.variablesDockWidget(); auto* varModel = varsDock ? varsDock->variableModel() : nullptr; auto* regView = mw.registerView(); auto* diffGrid = mw.differentialGrid();
    QVERIFY(varsDock && varModel && regView && diffGrid);
    const QString longStr = QStringLiteral("std::string(\"mpi_divergence_payload_segment_0123456789_abcdefghijklmnopqrstuvwxyz_ABCDEFGHIJKLMNOPQRSTUVWXYZ_tail_marker\")");
    QJsonArray locals;
    locals.append(QJsonObject{{QStringLiteral("name"), QStringLiteral("data")}, {QStringLiteral("value"), QStringLiteral("{...}")}, {QStringLiteral("type"), QStringLiteral("Particle[1]")}});
    locals.append(QJsonObject{{QStringLiteral("name"), QStringLiteral("payload")}, {QStringLiteral("value"), longStr}, {QStringLiteral("type"), QStringLiteral("std::string")}});
    QVERIFY(varsDock->applyLocalsForRank(1, locals));
    varModel->onVariableChildrenUpdated(1, QStringLiteral("data"), {QVariantMap{{QStringLiteral("name"), QStringLiteral("[0]")}, {QStringLiteral("value"), QStringLiteral("{...}")}, {QStringLiteral("type"), QStringLiteral("Particle")}, {QStringLiteral("hasChildren"), true}}});
    varModel->onVariableChildrenUpdated(1, QStringLiteral("data[0]"), {QVariantMap{{QStringLiteral("name"), QStringLiteral("coords")}, {QStringLiteral("value"), QStringLiteral("{x = 1.5, y = 2.5}")}, {QStringLiteral("type"), QStringLiteral("Vec2")}, {QStringLiteral("hasChildren"), true}}});
    varModel->onVariableChildrenUpdated(1, QStringLiteral("data[0].coords"), {QVariantMap{{QStringLiteral("name"), QStringLiteral("x")}, {QStringLiteral("value"), QStringLiteral("1.5")}, {QStringLiteral("type"), QStringLiteral("double")}, {QStringLiteral("hasChildren"), false}}});
    const QModelIndex dataIdx = varModel->index(0, 0), elem0Idx = varModel->index(0, 0, dataIdx), coordsIdx = varModel->index(0, 0, elem0Idx), xValIdx = varModel->index(0, 1, coordsIdx);
    QVERIFY(dataIdx.isValid() && elem0Idx.isValid() && coordsIdx.isValid() && xValIdx.isValid() && (varModel->flags(xValIdx) & Qt::ItemIsEditable));
    QSignalSpy varEditSpy(varModel, &gridlock::VariableTreeModel::variableEditRequested); sent.clear();
    QVERIFY(varModel->setData(xValIdx, QStringLiteral("9.25"), Qt::EditRole));
    QVERIFY(varEditSpy.count() == 1 && varEditSpy.first().at(0).toInt() == 1 && varEditSpy.first().at(1).toString() == QStringLiteral("data[0].coords.x") && varEditSpy.first().at(2).toString() == QStringLiteral("9.25"));
    QVERIFY(sent[1].contains(QStringLiteral("-gdb-set var data[0].coords.x=9.25")));
    gridlock::RankState rState; rState.rankId = 1; rState.currentState = QStringLiteral("stopped"); rState.registers[0] = QStringLiteral("0x0000000000000010");
    regView->updateRegisters(rState); QSignalSpy regEditSpy(regView, &gridlock::ui::RegisterView::registerEditRequested); sent.clear();
    QVERIFY(regView->setRegisterValue(QStringLiteral("rax"), QStringLiteral("0x1234")) && regEditSpy.count() == 1 && regEditSpy.first().at(1).toString() == QStringLiteral("rax") && regEditSpy.first().at(2).toString() == QStringLiteral("0x1234"));
    QVERIFY(sent[1].contains(QStringLiteral("-gdb-set $rax=0x1234")));
    varsDock->inspectFullValue(QStringLiteral("payload"), longStr);
    auto* vEdit = varsDock->findChild<QPlainTextEdit*>(QStringLiteral("fullValueInspectorTextEdit")); auto* vCopy = varsDock->findChild<QPushButton*>(QStringLiteral("fullValueInspectorCopyBtn"));
    QVERIFY(vEdit && vCopy && vEdit->toPlainText() == longStr && varsDock->lastInspectedFullValue() == longStr); vCopy->click(); QCOMPARE(QApplication::clipboard()->text(), longStr);
    regView->inspectFullValue(QStringLiteral("rax"), QStringLiteral("0x1234 (4660)")); QCOMPARE(regView->lastInspectedFullValue(), QStringLiteral("0x1234 (4660)"));
    diffGrid->updateVariableDisplay(0, QStringLiteral("payload"), longStr); emit diffGrid->cellDoubleClicked(1, 1);
    auto* gEdit = diffGrid->findChild<QPlainTextEdit*>(QStringLiteral("fullValueInspectorTextEdit")); QVERIFY(gEdit && gEdit->toPlainText() == longStr && diffGrid->property("lastInspectedFullValue").toString() == longStr);
    mw.setCoordinator(nullptr);
}
static void testKeybindingsLightThemeLegibility() {
    auto& tm = gridlock::core::managers::ThemeManager::instance();
    const QString origTheme = tm.activeThemeId(); QString lightThemeId;
    for (const QString& id : tm.availableThemes()) if (!gridlock::core::managers::ThemeManager::paletteForTheme(id).isDark) { lightThemeId = id; break; }
    QVERIFY2(!lightThemeId.isEmpty(), "At least one Light theme must be registered in ThemeManager");
    struct ThemeGuard { QString orig; ~ThemeGuard() { gridlock::core::managers::ThemeManager::instance().applyTheme(orig, false); } } guard{origTheme};
    tm.applyTheme(lightThemeId, false);
    gridlock::ui::PreferencesDialog dlg; dlg.resize(960, 680);
    if (auto* side = dlg.findChild<QListWidget*>(QStringLiteral("preferencesCategoryList")))
        for (int i = 0; i < side->count(); ++i) if (side->item(i)->text().contains(QStringLiteral("Keybindings"), Qt::CaseInsensitive)) { side->setCurrentRow(i); break; }
    dlg.show(); QApplication::processEvents();
    auto* tbl = dlg.findChild<QTableWidget*>(QStringLiteral("keybindingsTable"));
    QVERIFY2(tbl != nullptr && tbl->rowCount() >= 6, "Keybindings table must have at least 6 rows");
    QVERIFY2(tbl->palette().color(QPalette::Base).lightness() > 180 && tbl->palette().color(QPalette::Text).lightness() < 110, "Light theme table contrast check");
    const QString outPng = qEnvironmentVariable("GRIDLOCK_VERIFY_KEYBINDINGS_PNG");
    if (!outPng.isEmpty()) dlg.grab().toImage().save(outPng);
    const QImage vpImg = tbl->viewport()->grab().toImage().convertToFormat(QImage::Format_RGB32);
    for (int r = 0; r < qMin(6, tbl->rowCount()); ++r) {
        for (int c = 0; c < 3; ++c) {
            QTableWidgetItem* it = tbl->item(r, c); QVERIFY(it != nullptr);
            const QVariant fg = it->data(Qt::ForegroundRole);
            if (fg.isValid() && fg.canConvert<QBrush>() && fg.value<QBrush>().style() != Qt::NoBrush)
                QVERIFY2(fg.value<QBrush>().color().lightness() < 110, qPrintable(QStringLiteral("Row %1 col %2 has light foreground").arg(r).arg(c)));
        }
        const QRect rRect = tbl->visualItemRect(tbl->item(r, 1)).intersected(vpImg.rect()); int darkInkPx = 0;
        for (int y = rRect.top(); y <= rRect.bottom(); ++y) for (int x = rRect.left(); x <= rRect.right(); ++x) if (qGray(vpImg.pixel(x, y)) < 120) ++darkInkPx;
        QVERIFY2(darkInkPx > 10, qPrintable(QStringLiteral("Row %1 action label rendered invisible (darkInkPx=%2)").arg(r).arg(darkInkPx)));
    }
    dlg.close();
}

static void testPreLaunchExecutableGuardAndExtensionlessSiblingCompile() {
    QTemporaryDir tDir; QVERIFY(tDir.isValid());
    const QString nonExecFile = tDir.filePath(QStringLiteral("raw_unexecutable")), csvFile = tDir.filePath(QStringLiteral("metrics.csv")), subDir = tDir.filePath(QStringLiteral("target_dir"));
    { QFile f(nonExecFile); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("not an executable binary\n"); }
    QFile::setPermissions(nonExecFile, QFile::ReadOwner | QFile::WriteOwner);
    { QFile cf(csvFile); QVERIFY(cf.open(QIODevice::WriteOnly)); cf.write("rank,latency_us\n0,12.4\n"); }
    QVERIFY(QDir().mkpath(subDir));

    gridlock::ui::MainWindow mw; mw.resize(1280, 800); mw.show(); QApplication::processEvents();
    auto* targetCombo = mw.findChild<QComboBox*>(QStringLiteral("toolbarTargetSelector"));
    auto* progOut = mw.findChild<QPlainTextEdit*>(QStringLiteral("programOutputView"));
    auto* runAct = mw.findChild<QAction*>(QStringLiteral("actionRunTarget"));
    QVERIFY(targetCombo && progOut && runAct);

    // 1. Non-executable file, non-code .csv data file, and directory must fail pre-launch validation cleanly with 0 spawned ranks.
    targetCombo->insertItem(0, QStringLiteral("raw_unexecutable"), nonExecFile);
    QVERIFY(mw.selectTargetByName(QStringLiteral("raw_unexecutable")));
    runAct->trigger(); QApplication::processEvents();
    QVERIFY2(!mw.isDebugSessionActive() && !mw.isRunButtonLatched() && (!mw.coordinator() || mw.coordinator()->getProcessCount() == 0),
             "Non-executable target must abort pre-launch without spawning MPI ranks");
    QVERIFY2(progOut->toPlainText().contains(QStringLiteral("Target is not executable")) && !progOut->toPlainText().contains(QStringLiteral("permission denied")),
             qPrintable(progOut->toPlainText()));

    targetCombo->insertItem(0, QStringLiteral("metrics_csv"), csvFile);
    QVERIFY(mw.selectTargetByName(QStringLiteral("metrics_csv")) && !QFileInfo::exists(tDir.filePath(QStringLiteral("metrics"))));
    mw.startTargetWorkflow(); QApplication::processEvents();
    QVERIFY(!mw.isDebugSessionActive() && (!mw.coordinator() || mw.coordinator()->getProcessCount() == 0) && progOut->toPlainText().contains(csvFile));

    targetCombo->insertItem(0, QStringLiteral("dir_target"), subDir);
    QVERIFY(mw.selectTargetByName(QStringLiteral("dir_target")));
    mw.startTargetWorkflow(); QApplication::processEvents();
    QVERIFY2(!mw.isDebugSessionActive() && progOut->toPlainText().contains(QStringLiteral("Target is a directory")), qPrintable(progOut->toPlainText()));

    // 2. Extensionless target path with sibling .cpp source resolves, compiles, and enforces +x permissions cleanly.
    const QString extlessBin = tDir.filePath(QStringLiteral("extless_demo")), extlessCpp = tDir.filePath(QStringLiteral("extless_demo.cpp"));
    { QFile sf(extlessCpp); QVERIFY(sf.open(QIODevice::WriteOnly)); sf.write("int main() { return 0; }\n"); }
    QVERIFY(!QFileInfo::exists(extlessBin));
    targetCombo->insertItem(0, QStringLiteral("extless_demo"), extlessBin);
    QVERIFY2(mw.selectTargetByName(QStringLiteral("extless_demo")) && mw.ensureTargetBinaryCompiled(),
             "Extensionless target with sibling .cpp must resolve and compile cleanly");
    QCOMPARE(QFileInfo(mw.activeTargetSourcePath()).canonicalFilePath(), QFileInfo(extlessCpp).canonicalFilePath());
    QCOMPARE(QFileInfo(mw.activeTargetBinaryPath()).canonicalFilePath(), QFileInfo(extlessBin).canonicalFilePath());
    QVERIFY2(QFileInfo(extlessBin).isFile() && QFileInfo(extlessBin).isExecutable(), "Compiled extensionless target binary must have execute permissions");

    // Stripping +x on a binary with a sibling .cpp must trigger rebuild/chmod restoration on ensureTargetBinaryCompiled().
    QVERIFY(QFile::setPermissions(extlessBin, QFile::ReadOwner | QFile::WriteOwner) && !QFileInfo(extlessBin).isExecutable());
    QVERIFY2(mw.ensureTargetBinaryCompiled() && QFileInfo(extlessBin).isExecutable(), "Missing +x on target with sibling .cpp must restore executable binary");
}

void TestMainWindowUI::testCowCheckpointsAndProcSafety() {
    using gridlock::ui::CheckpointDialog; using gridlock::ui::SafetyLevel; QTemporaryDir fakeProc; QVERIFY(fakeProc.isValid());
    auto mkProc = [&](int pid, const QMap<QString, QString>& fds, const QByteArray& maps) {
        const QString pDir = fakeProc.filePath(QString::number(pid)), fdDir = pDir + QStringLiteral("/fd"); QVERIFY(QDir().mkpath(fdDir));
        for (auto it = fds.constBegin(); it != fds.constEnd(); ++it) QCOMPARE(::symlink(it.value().toUtf8().constData(), (fdDir + QLatin1Char('/') + it.key()).toUtf8().constData()), 0);
        QFile mf(pDir + QStringLiteral("/maps")); QVERIFY(mf.open(QIODevice::WriteOnly)); mf.write(maps);
    };
    mkProc(1001, {{QStringLiteral("0"), QStringLiteral("/dev/pts/0")}, {QStringLiteral("1"), QStringLiteral("/dev/pts/0")}}, "555555554000-555555558000 r-xp 00000000 08:01 1 /usr/bin/solver\n7ffff7a00000-7ffff7b00000 rw-p 00000000 00:00 0 [heap]\n");
    mkProc(1002, {{QStringLiteral("3"), QStringLiteral("socket:[12345]")}, {QStringLiteral("4"), QStringLiteral("pipe:[67890]")}}, "7ffff6000000-7ffff6400000 rw-s 00000000 00:18 2 /dev/shm/openmpi_vader_segment\n");
    mkProc(1003, {{QStringLiteral("5"), QStringLiteral("/dev/infiniband/uverbs0")}}, "7ffff5000000-7ffff5100000 rw-s 00000000 00:06 3 /dev/infiniband/uverbs0\n");
    const auto repSafe = gridlock::ui::inspectProcessCheckpointSafety(1001, fakeProc.path()), repWarn = gridlock::ui::inspectProcessCheckpointSafety(1002, fakeProc.path()), repUnsafe = gridlock::ui::inspectProcessCheckpointSafety(1003, fakeProc.path());
    QVERIFY(repSafe.level == SafetyLevel::Safe && repSafe.warnings.isEmpty() && repSafe.blockers.isEmpty() && repSafe.badgeText == QStringLiteral("SAFE") && repWarn.level == SafetyLevel::Warning && repWarn.blockers.isEmpty() && repWarn.warnings.size() >= 3 && repUnsafe.level == SafetyLevel::BlockedOrUnsafe && !repUnsafe.blockers.isEmpty());
    gridlock::GdbRankCoordinator coord; coord.initializeMockSession(2); coord.processGdbOutput(0, QStringLiteral("*stopped,reason=\"breakpoint-hit\",frame={addr=\"0x401120\",func=\"solve\",fullname=\"/tmp/solver.cpp\",line=\"28\",args=[]}\n"));
    QStringList sent; connect(&coord, &gridlock::GdbRankCoordinator::commandSentToGdb, [&](int r, const QString& c) { if (r == 0) sent.append(c.trimmed()); });
    QSignalSpy cpSpy(&coord, &IBackendCoordinator::checkpointsReady); coord.createCheckpoint(0, QStringLiteral("before halo")); QVERIFY(sent.contains(QStringLiteral("212-interpreter-exec console \"checkpoint\"")));
    coord.processGdbOutput(0, QStringLiteral("~\"checkpoint 1: fork returned pid 4242.\\n\"\n212^done\n"));
    QVERIFY(cpSpy.count() >= 1 && coord.checkpointsForRank(0).size() == 1 && coord.checkpointsForRank(0).first().id == 1 && coord.checkpointsForRank(0).first().forkPid == 4242 && coord.checkpointsForRank(0).first().note == QStringLiteral("before halo") && sent.contains(QStringLiteral("213-interpreter-exec console \"info checkpoints\"")));
    coord.processGdbOutput(0, QStringLiteral("~\"  1 process 4242 at 0x401120, file solver.cpp, line 28\\n\"\n~\"  0 process 4200 (main process) at 0x401100\\n\"\n213^done\n"));
    QVERIFY(coord.checkpointsForRank(0).size() == 2 && coord.checkpointsForRank(0).first().note == QStringLiteral("before halo") && coord.checkpointsForRank(0).first().location.contains(QStringLiteral("solver.cpp")));
    coord.restoreCheckpoint(0, 1); coord.deleteCheckpoint(0, 1); QVERIFY(sent.contains(QStringLiteral("-interpreter-exec console \"restart 1\"")) && sent.contains(QStringLiteral("-interpreter-exec console \"delete checkpoint 1\"")) && coord.checkpointsForRank(0).size() == 1);
    CheckpointDialog cpDlg(&coord); cpDlg.setProcRootForTesting(fakeProc.path()); cpDlg.show(); QApplication::processEvents(); sent.clear();
    QVERIFY(cpDlg.findChild<QPushButton*>(QStringLiteral("checkpointCloseBtn")) && cpDlg.findChild<QPushButton*>(QStringLiteral("createCheckpointBtn")) && cpDlg.checkpointTable() && cpDlg.checkpointTable()->rowCount() == 1 && cpDlg.createCheckpointNow(true) && sent.contains(QStringLiteral("212-interpreter-exec console \"checkpoint\""))); cpDlg.close();
    gridlock::ui::MainWindow mw; mw.resize(1440, 900); mw.show(); QApplication::processEvents(); auto* cpAct = mw.findChild<QAction*>(QStringLiteral("actionProcessCheckpoints")); QVERIFY(cpAct && cpAct->shortcut() == QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_K));
    QTest::keyClick(&mw, Qt::Key_K, Qt::ControlModifier | Qt::AltModifier); QApplication::processEvents(); QVERIFY(mw.checkpointDialog() && mw.checkpointDialog()->isVisible()); mw.checkpointDialog()->close();
    QTest::keyClick(&mw, Qt::Key_P, Qt::ControlModifier | Qt::ShiftModifier); QApplication::processEvents(); auto* pal = mw.findChild<gridlock::ui::CommandPaletteDialog*>(QStringLiteral("CommandPaletteDialog"));
    QVERIFY(pal && pal->isVisible()); pal->setFilterText(QStringLiteral("> Process Checkpoints")); QVERIFY(pal->resultCount() > 0 && pal->resultItemAt(0).title.contains(QStringLiteral("Process Checkpoints"))); QTest::keyClick(pal->inputEdit(), Qt::Key_Escape);
    testCleanExitWhileTargetRunning(); testMultiRankScopePauseAllAndRackFilter(); testLaunchOptionsCliArgsAndUnifiedSessionStart(); testLiveVariableRegisterEditingAndFullValueInspection(); testKeybindingsLightThemeLegibility(); testPreLaunchExecutableGuardAndExtensionlessSiblingCompile();
}
