#include "MainWindow.hpp"
#include "../core/commands/DebugCommands.hpp"
#include "../core/hpc/DapCoordinator.hpp"
#include "../core/hpc/GdbRankCoordinator.hpp"
#include "../core/hpc/MemoryDiffer.hpp"
#include "../core/managers/ConfigManager.hpp"
#include "../core/managers/SessionManager.hpp"
#include "../core/managers/SpackManager.hpp"
#include "EditorTabManager.hpp"
#include "GdbConsoleWidget.hpp"
#include "docks/TerminalDockWidget.hpp"
#include "docks/VariablesDockWidget.hpp"
#include "views/DisassemblyView.hpp"
#include "views/MemView.hpp"
#include "views/RegisterView.hpp"
#include "views/ServerRackView.hpp"
#include "views/SourceCodeView.hpp"
#include "widgets/DifferentialGrid.hpp"
#include "widgets/ExpressionEvaluatorWidget.hpp"
#include "widgets/MpiDiagnosticsWidget.hpp"
#include "widgets/ProjectExplorerWidget.hpp"
#include "widgets/ReferenceManualWidget.hpp"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDockWidget>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QGuiApplication>
#include <QScreen>
#include <QShortcut>
#include <QSplitter>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTextCursor>
#include <QToolBar>
#include <QToolTip>
#include <QVBoxLayout>
#include <QWindow>
#include <QtConcurrent/QtConcurrent>
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace gridlock::ui {

namespace {

class AddressSpaceNavBar : public QWidget {
public:
  struct Segment {
    QString name;
    uint64_t baseAddr;
    uint64_t endAddr;
    int weight;
    QColor fillColor;
    QColor textColor;
  };

  explicit AddressSpaceNavBar(std::function<void(uint64_t)> onSeek,
                              QWidget *parent = nullptr)
      : QWidget(parent), m_onSeek(std::move(onSeek)) {
    setObjectName("addressNavBar");
    setFixedHeight(20);
    setCursor(Qt::PointingHandCursor);
    m_segments = {
        {QStringLiteral(".plt 0x401000"), 0x00401000ULL, 0x0040111fULL, 14,
         QColor("#2c2238"), QColor("#c586c0")},
        {QStringLiteral(".text 0x401120 (sym.main)"), 0x00401120ULL,
         0x00401fffULL, 34, QColor("#1e3038"), QColor("#4ec9b0")},
        {QStringLiteral(".rodata 0x402000"), 0x00402000ULL, 0x00403fffULL, 18,
         QColor("#1e2836"), QColor("#569cd6")},
        {QStringLiteral(".data 0x404000"), 0x00404000ULL, 0x00405fffULL, 16,
         QColor("#2f291e"), QColor("#dcdcaa")},
        {QStringLiteral("stack 0x7fffffffe000"), 0x7fffffffe000ULL,
         0x7fffffffffffULL, 18, QColor("#332222"), QColor("#ce9178")},
    };
  }

  void setCurrentAddress(uint64_t addr) {
    if (addr != 0ULL) {
      m_currentAddr = addr;
      update();
    }
  }

protected:
  void paintEvent(QPaintEvent * /*event*/) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.fillRect(rect(), QColor("#141619"));

    int totalWeight = 0;
    for (const auto &seg : m_segments) {
      totalWeight += seg.weight;
    }
    if (totalWeight <= 0) {
      return;
    }

    const int w = width();
    const int h = height();
    int x = 0;
    int activeCursorX = -1;

    QFont segFont("JetBrains Mono", 8, QFont::DemiBold);
    segFont.setStyleHint(QFont::Monospace);
    p.setFont(segFont);

    for (size_t i = 0; i < m_segments.size(); ++i) {
      const auto &seg = m_segments[i];
      const int segW = (i + 1 == m_segments.size())
                           ? (w - x)
                           : (w * seg.weight) / totalWeight;
      QRect segRect(x, 1, segW - 1, h - 2);
      p.fillRect(segRect, seg.fillColor);
      p.setPen(QColor("#2b2b2b"));
      p.drawRect(segRect.adjusted(0, 0, -1, -1));

      p.setPen(seg.textColor);
      p.drawText(segRect.adjusted(6, 0, -6, 0),
                 Qt::AlignVCenter | Qt::AlignLeft, seg.name);

      if (m_currentAddr >= seg.baseAddr && m_currentAddr <= seg.endAddr) {
        double ratio = 0.25;
        if (seg.endAddr > seg.baseAddr) {
          ratio = std::clamp(
              static_cast<double>(m_currentAddr - seg.baseAddr) /
                  static_cast<double>(std::min<uint64_t>(
                      0x200ULL, seg.endAddr - seg.baseAddr)),
              0.08, 0.92);
        }
        activeCursorX = x + static_cast<int>(segW * ratio);
      }
      x += segW;
    }

    if (activeCursorX < 0 && !m_segments.empty()) {
      activeCursorX = (w * m_segments[0].weight) / totalWeight + 18;
    }

    if (activeCursorX >= 0) {
      p.setPen(QPen(QColor("#ffcc00"), 1));
      p.drawLine(activeCursorX, 0, activeCursorX, 4);
      p.drawLine(activeCursorX, h - 4, activeCursorX, h);
      QPolygon topMarker;
      topMarker << QPoint(activeCursorX - 4, 0) << QPoint(activeCursorX + 4, 0)
                << QPoint(activeCursorX, 4);
      QPolygon botMarker;
      botMarker << QPoint(activeCursorX - 4, h) << QPoint(activeCursorX + 4, h)
                << QPoint(activeCursorX, h - 4);
      p.setBrush(QColor("#ffcc00"));
      p.setPen(Qt::NoPen);
      p.drawPolygon(topMarker);
      p.drawPolygon(botMarker);
    }
  }

  void mousePressEvent(QMouseEvent *event) override {
    int totalWeight = 0;
    for (const auto &seg : m_segments) {
      totalWeight += seg.weight;
    }
    const int w = std::max(1, width());
    int x = 0;
    for (size_t i = 0; i < m_segments.size(); ++i) {
      const auto &seg = m_segments[i];
      const int segW = (i + 1 == m_segments.size())
                           ? (w - x)
                           : (w * seg.weight) / totalWeight;
      if (event->position().x() >= x && event->position().x() <= x + segW) {
        setCurrentAddress(seg.baseAddr);
        if (m_onSeek) {
          m_onSeek(seg.baseAddr);
        }
        break;
      }
      x += segW;
    }
  }

private:
  std::vector<Segment> m_segments;
  uint64_t m_currentAddr = 0x00401120ULL;
  std::function<void(uint64_t)> m_onSeek;
};

} // namespace

void MainWindow::setupUi() {
  setWindowTitle("GridLock - MPI Graphical Debugger");
  setWindowIcon(QIcon());
  resize(1440, 900);
  applyCutterWorkspaceLayout();
  setupToolbar();
}

void MainWindow::applyCutterWorkspaceLayout() {
  setDockOptions(QMainWindow::AllowNestedDocks | QMainWindow::AllowTabbedDocks);
  setDockNestingEnabled(true);
  setDocumentMode(true);
  setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);
  setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
  setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
  setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
  setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
}

bool MainWindow::captureWorkspaceSnapshot(const QString &outputPath) {
  QToolTip::hideText();
  hideHoverTooltipPopup();
  for (QWidget *topLevel : QApplication::topLevelWidgets()) {
    if (topLevel != this && (topLevel->windowFlags() & Qt::ToolTip)) {
      topLevel->hide();
    }
  }
  for (QLabel *lbl : findChildren<QLabel *>()) {
    if (lbl->text().startsWith("Size:")) {
      lbl->hide();
    }
  }
  this->ensurePolished();
  if (this->layout()) {
    this->layout()->activate();
  }
  QCoreApplication::processEvents();
  QPixmap pixmap = this->grab();
  if (pixmap.isNull()) {
    return false;
  }
  return pixmap.save(outputPath, "PNG");
}

QScreen *MainWindow::resolvePreferredScreen(const QString &monitorHint) const {
  const QList<QScreen *> screens = QGuiApplication::screens();
  if (screens.isEmpty()) {
    return nullptr;
  }
  const QString hint = monitorHint.trimmed();
  if (hint.isEmpty()) {
    return QGuiApplication::primaryScreen();
  }
  for (QScreen *scr : screens) {
    if (!scr) {
      continue;
    }
    const QRect geom = scr->geometry();
    const QSizeF phys = scr->physicalSize();
    const int diagInches =
        qRound(std::hypot(phys.width(), phys.height()) / 25.4);
    const QString meta = QString("%1 %2 %3 %4x%5 %6\" %6in")
                             .arg(scr->name(), scr->model(),
                                  scr->manufacturer())
                             .arg(geom.width())
                             .arg(geom.height())
                             .arg(diagInches);
    if (meta.contains(hint, Qt::CaseInsensitive)) {
      return scr;
    }
  }
  return QGuiApplication::primaryScreen();
}

void MainWindow::applyPreferredScreenRouting(const QString &monitorHint,
                                             bool openUnfocused) {
  m_preferredMonitorHint = monitorHint.trimmed();
  setAttribute(Qt::WA_ShowWithoutActivating, openUnfocused);
  if (QScreen *target = resolvePreferredScreen(m_preferredMonitorHint)) {
    setScreen(target);
    if (windowHandle()) {
      windowHandle()->setScreen(target);
    }
    move(target->availableGeometry().topLeft());
  }
}

void MainWindow::setupStatusBar() {
  QStatusBar *bar = statusBar();
  bar->setObjectName("cutterStatusBar");
  bar->setSizeGripEnabled(false);
  bar->setContentsMargins(6, 0, 6, 0);

  if (m_currentSeekOrPc == QStringLiteral("0x00000000") ||
      m_currentSeekOrPc.isEmpty()) {
    m_currentSeekOrPc = QStringLiteral("0x00401120");
  }

  m_statusTelemetryLabel = new QLabel(bar);
  m_statusTelemetryLabel->setObjectName("cutterStatusTelemetry");
  m_statusTelemetryLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
  bar->addPermanentWidget(m_statusTelemetryLabel, 1);
  updateStatusTelemetry(m_currentSeekOrPc, m_currentTargetState);
}

void MainWindow::updateStatusTelemetry(const QString &pcOrSeek,
                                       const QString &targetState) {
  if (!pcOrSeek.isEmpty()) {
    m_currentSeekOrPc = pcOrSeek;
  }
  if (!targetState.isEmpty()) {
    m_currentTargetState = targetState;
  }
  if (m_statusTelemetryLabel) {
    m_statusTelemetryLabel->setText(
        QStringLiteral("ARCH: x86_64 | MPI RANK: %1 | PC/SEEK: %2 | STATUS: %3")
            .arg(m_focusedRank)
            .arg(m_currentSeekOrPc)
            .arg(m_currentTargetState));
  }
}

bool MainWindow::seekToAddressOrExpression(const QString &expr) {
  const QString cleaned = expr.trimmed();
  if (cleaned.isEmpty()) {
    return false;
  }

  QString normalized = cleaned;
  bool isHexWithoutPrefix = false;
  if (!normalized.startsWith("0x", Qt::CaseInsensitive)) {
    (void)normalized.toULongLong(&isHexWithoutPrefix, 16);
    if (isHexWithoutPrefix && normalized.length() >= 4) {
      normalized = QStringLiteral("0x") + normalized;
    }
  }

  // Parse hex address if applicable
  bool hexOk = false;
  QString hexPart = normalized;
  if (hexPart.startsWith("0x", Qt::CaseInsensitive)) {
    hexPart = hexPart.mid(2);
  }
  const uint64_t parsedAddr = hexPart.toULongLong(&hexOk, 16);

  // If expr is a symbol name (not hex), check if ProjectExplorerWidget can resolve and activate it
  if ((!hexOk || parsedAddr == 0ULL) && m_projectExplorerWidget) {
    if (m_projectExplorerWidget->activateSymbol(cleaned)) {
      return true;
    }
  }

  if (m_seekBar && m_seekBar->text() != normalized) {
    m_seekBar->setText(normalized);
  }

  updateStatusTelemetry(normalized);

  if (auto *navBar = dynamic_cast<AddressSpaceNavBar *>(m_addressNavBar)) {
    if (hexOk && parsedAddr != 0ULL) {
      navBar->setCurrentAddress(parsedAddr);
    }
  }

  // Synchronize with Memory View address bar and request memory if coordinator is active
  if (m_memView) {
    if (hexOk && parsedAddr != 0ULL) {
      m_memView->seekToAddress(parsedAddr);
    }
    if (auto *addrEdit = m_memView->findChild<QLineEdit *>()) {
      addrEdit->setText(normalized);
    }
    if (m_coordinator && m_currentTargetState != QStringLiteral("IDLE")) {
      m_coordinator->readMemory(m_focusedRank, normalized, 256);
    }
  }

  // Synchronize with DisassemblyView by forwarding hex addresses and highlighting symbols
  if (m_disassemblyView) {
    if (hexOk && parsedAddr != 0ULL) {
      m_disassemblyView->seekToAddress(parsedAddr);
    } else if (!m_disassemblyView->toPlainText().isEmpty()) {
      QTextDocument *doc = m_disassemblyView->document();
      if (doc) {
        QTextCursor found = doc->find(normalized);
        if (!found.isNull()) {
          m_disassemblyView->setTextCursor(found);
          m_disassemblyView->centerCursor();
        }
      }
    }
  }

  return true;
}

void MainWindow::setupToolbar() {
  QToolBar *toolbar = addToolBar("Main Toolbar");
  toolbar->setObjectName("MainToolbar");
  toolbar->setMovable(false);

  m_runAction = new QAction("▶ Run Target", this);
  connect(m_runAction, &QAction::triggered, this, [this]() {
    auto settings =
        gridlock::core::ConfigManager::instance().loadProjectSettings();
    QString binary =
        m_coordinator && !m_coordinator->getCurrentBinaryPath().isEmpty()
            ? m_coordinator->getCurrentBinaryPath()
            : QString::fromStdString(settings.targetBinary);
    auto cmd = std::make_unique<gridlock::core::commands::LaunchCommand>(
        this, binary,
        gridlock::core::ConfigManager::instance().getDefaultRanks());
    executeCommand(std::move(cmd));
  });
  toolbar->addAction(m_runAction);

  QAction *continueAction = new QAction("⏩ Continue", this);
  connect(continueAction, &QAction::triggered, this, [this]() {
    auto cmd = std::make_unique<gridlock::core::commands::ContinueCommand>(
        m_coordinator, m_focusedRank + 1);
    executeCommand(std::move(cmd));
  });
  toolbar->addAction(continueAction);

  QAction *stepAction = new QAction("↷ Step Inst", this);
  connect(stepAction, &QAction::triggered, this, [this]() {
    auto cmd = std::make_unique<gridlock::core::commands::StepCommand>(
        m_coordinator, m_focusedRank + 1, false);
    executeCommand(std::move(cmd));
  });
  toolbar->addAction(stepAction);

  QAction *pauseAction = new QAction("Pause Rank", this);
  connect(pauseAction, &QAction::triggered, this, [this]() {
    auto cmd = std::make_unique<gridlock::core::commands::PauseCommand>(
        m_coordinator, m_focusedRank + 1);
    executeCommand(std::move(cmd));
  });
  toolbar->addAction(pauseAction);

  QAction *stopAction = new QAction("⏹ Stop", this);
  connect(stopAction, &QAction::triggered, this, [this]() {
    if (m_coordinator) {
      m_coordinator->terminateSession();
    }
  });
  toolbar->addAction(stopAction);

  QAction *exitAction = new QAction("Terminate Session", this);
  connect(exitAction, &QAction::triggered, this, [this]() {
    auto cmd =
        std::make_unique<gridlock::core::commands::TerminateCommand>(this);
    executeCommand(std::move(cmd));
  });
  toolbar->addAction(exitAction);

  for (QAction *action : toolbar->actions()) {
    action->setToolTip(QString());
  }

  QWidget *spacer = new QWidget(toolbar);
  spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  toolbar->addWidget(spacer);

  m_seekBar = new QLineEdit(toolbar);
  m_seekBar->setObjectName("cutterSeekBar");
  m_seekBar->setPlaceholderText("Go to address / symbol (Ctrl+G)...");
  m_seekBar->setMinimumWidth(300);
  m_seekBar->setFixedWidth(320);
  m_seekBar->setMaximumWidth(320);
  QFont seekFont("JetBrains Mono");
  seekFont.setStyleHint(QFont::Monospace);
  seekFont.setPointSize(9);
  m_seekBar->setFont(seekFont);
  toolbar->addWidget(m_seekBar);

  connect(m_seekBar, &QLineEdit::returnPressed, this, [this]() {
    seekToAddressOrExpression(m_seekBar->text());
  });

  QShortcut *seekShortcut =
      new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_G), this);
  connect(seekShortcut, &QShortcut::activated, this, [this]() {
    if (m_seekBar) {
      m_seekBar->setFocus(Qt::ShortcutFocusReason);
      m_seekBar->selectAll();
    }
  });

  addToolBarBreak(Qt::TopToolBarArea);
  QToolBar *navToolbar = addToolBar("Address Space Navigation");
  navToolbar->setObjectName("AddressNavToolbar");
  navToolbar->setMovable(false);
  navToolbar->setFloatable(false);
  navToolbar->setContentsMargins(0, 0, 0, 0);

  m_addressNavBar = new AddressSpaceNavBar(
      [this](uint64_t addr) {
        seekToAddressOrExpression(
            QStringLiteral("0x%1").arg(addr, 8, 16, QChar('0')));
      },
      navToolbar);
  m_addressNavBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  navToolbar->addWidget(m_addressNavBar);
}

void MainWindow::setupDocks() {
  // Left Zone: Project Explorer / Symbols & Functions list
  m_projectExplorerWidget = new ProjectExplorerWidget(this);
  m_projectExplorerWidget->setObjectName("ProjectExplorerDock");
  addDockWidget(Qt::LeftDockWidgetArea, m_projectExplorerWidget);

  connect(&gridlock::core::managers::SessionManager::instance(),
          &gridlock::core::managers::SessionManager::sessionLoaded,
          m_projectExplorerWidget, &ProjectExplorerWidget::setRootPath);

  connect(m_projectExplorerWidget, &ProjectExplorerWidget::fileDoubleClicked,
          this, [this](const QString &filePath) {
            if (m_editorTabManager) {
              loadSourceFile(filePath);
            }
          });
  connect(m_projectExplorerWidget,
          &ProjectExplorerWidget::symbolAddressSelected, this,
          [this](uint64_t address, const QString & /*symbolName*/) {
            seekToAddressOrExpression(
                QStringLiteral("0x%1").arg(address, 8, 16, QChar('0')));
          });

  // Center & Bottom Zone Splitter
  QSplitter *mainVerticalSplitter = new QSplitter(Qt::Vertical, this);
  mainVerticalSplitter->setObjectName("MainVerticalSplitter");
  mainVerticalSplitter->setMinimumWidth(240);
  mainVerticalSplitter->setContentsMargins(0, 0, 0, 0);
  mainVerticalSplitter->setHandleWidth(4);
  mainVerticalSplitter->setChildrenCollapsible(false);

  // Center Zone: Source Code (Left) & Disassembly View (Right) side-by-side
  QSplitter *masterHorizontalSplitter =
      new QSplitter(Qt::Horizontal, mainVerticalSplitter);
  masterHorizontalSplitter->setObjectName("MasterHorizontalSplitter");
  masterHorizontalSplitter->setContentsMargins(0, 0, 0, 0);
  masterHorizontalSplitter->setHandleWidth(4);
  masterHorizontalSplitter->setChildrenCollapsible(false);

  m_editorTabManager = new EditorTabManager(masterHorizontalSplitter);
  m_editorTabManager->setMinimumWidth(120);
  m_editorTabManager->setMaximumWidth(QWIDGETSIZE_MAX);
  m_editorTabManager->setSizePolicy(QSizePolicy::Expanding,
                                    QSizePolicy::Expanding);
  m_editorTabManager->setDocumentMode(true);
  m_editorTabManager->setContentsMargins(0, 0, 0, 0);
  if (m_editorTabManager->tabBar()) {
    m_editorTabManager->tabBar()->setUsesScrollButtons(true);
    m_editorTabManager->tabBar()->setElideMode(Qt::ElideNone);
  }
  connect(m_editorTabManager, &EditorTabManager::toggleBreakpointRequested,
          this, [this](const QString &loc) {
            if (auto *gdbCoord = dynamic_cast<gridlock::GdbRankCoordinator *>(
                    m_coordinator)) {
              gdbCoord->insertBreakpoint(loc);
            }
          });
  connect(m_editorTabManager, &EditorTabManager::breakpointToggled, this,
          [this](const QString &file, int line, bool isSet,
                 const QString &condition) {
            QString absoluteFilePath = QFileInfo(file).absoluteFilePath();

            // Update persistent cache (only tracks line numbers currently)
            if (!isSet) {
              m_persistentBreakpoints[absoluteFilePath].remove(line);
            } else {
              m_persistentBreakpoints[absoluteFilePath].insert(line);
            }
            gridlock::core::ConfigManager::instance().saveBreakpoints(
                m_persistentBreakpoints);

            if (auto *gdbCoord = dynamic_cast<gridlock::GdbRankCoordinator *>(
                    m_coordinator)) {
              gdbCoord->broadcastBreakpoint(absoluteFilePath, line, isSet,
                                            condition);
            } else if (auto *dapCoord =
                           dynamic_cast<DapCoordinator *>(m_coordinator)) {
              if (isSet) {
                dapCoord->toggleBreakpoint(absoluteFilePath, line);
              } else {
                dapCoord->toggleBreakpoint(absoluteFilePath, line);
              }
            }
          });

  connect(
      m_editorTabManager, &EditorTabManager::hoverVariableRequested, this,
      [this](const QString &varName, const QPoint &globalPos) {
        if (auto *gdbCoord =
                dynamic_cast<gridlock::GdbRankCoordinator *>(m_coordinator)) {
          gdbCoord->evaluateHoverVariable(m_focusedRank, varName, globalPos);
        }
      });

  connect(m_editorTabManager, &EditorTabManager::hideHoverTooltip, this,
          &MainWindow::hideHoverTooltipPopup);

  // Right Inspection Zone: Variables, Registers, Watch Expressions, and Server Rack
  m_variablesDockWidget = new VariablesDockWidget(this);
  m_variablesDockWidget->setObjectName("VariablesWidget");
  QDockWidget *variablesDock = new QDockWidget("Variables", this);
  variablesDock->setObjectName("VariablesDock");
  variablesDock->setWidget(m_variablesDockWidget);
  QWidget *emptyVarTitle = new QWidget(variablesDock);
  emptyVarTitle->setFixedSize(0, 0);
  variablesDock->setTitleBarWidget(emptyVarTitle);

  m_registerView = new RegisterView(this);
  m_registerView->setObjectName("RegisterViewWidget");
  connect(m_registerView, &RegisterView::addressActivated, this,
          [this](uint64_t addr) {
            seekToAddressOrExpression(
                QStringLiteral("0x%1").arg(addr, 8, 16, QChar('0')));
          });
  QDockWidget *registersDock = new QDockWidget("Registers", this);
  registersDock->setObjectName("RegisterViewDock");
  registersDock->setWidget(m_registerView);
  QWidget *emptyRegTitle = new QWidget(registersDock);
  emptyRegTitle->setFixedSize(0, 0);
  registersDock->setTitleBarWidget(emptyRegTitle);

  m_differentialGrid = new DifferentialGrid(this);
  m_differentialGrid->setObjectName("DifferentialGrid");
  m_differentialGrid->verticalHeader()->setDefaultSectionSize(20);
  m_differentialGrid->horizontalHeader()->setDefaultSectionSize(96);
  connect(m_differentialGrid, &DifferentialGrid::watchVariableAdded, this,
          [this](const QString &name) {
            if (auto *gdbCoord = dynamic_cast<gridlock::GdbRankCoordinator *>(
                    m_coordinator)) {
              gdbCoord->registerWatchVariable(name);
            }
          });

  auto ps = gridlock::core::ConfigManager::instance().loadProjectSettings();
  for (const auto &w : ps.watchExpressions) {
    if (!w.empty()) {
      m_differentialGrid->addVariableColumn(QString::fromStdString(w));
    }
  }
  m_differentialGrid->updateVariableDisplay(0, QStringLiteral("data[0].id"),
                                            QStringLiteral("0 (int)"));
  m_differentialGrid->updateVariableDisplay(
      0, QStringLiteral("data[0].coords.x"), QStringLiteral("0.0f (float)"));
  m_differentialGrid->updateVariableDisplay(0, QStringLiteral("mpi_rank"),
                                            QStringLiteral("0 (int)"));
  m_differentialGrid->updateVariableDisplay(1, QStringLiteral("data[0].id"),
                                            QStringLiteral("1 (int)"));
  m_differentialGrid->updateVariableDisplay(
      1, QStringLiteral("data[0].coords.x"), QStringLiteral("1.5f (float)"));
  m_differentialGrid->updateVariableDisplay(1, QStringLiteral("mpi_rank"),
                                            QStringLiteral("1 (int)"));
  m_differentialGrid->setColumnHidden(0, true);
  m_differentialGrid->setRowHidden(0, true);
  m_differentialGrid->horizontalHeader()->setSectionResizeMode(
      QHeaderView::ResizeToContents);
  m_differentialGrid->horizontalHeader()->setStretchLastSection(true);

  QWidget *watchContainer = new QWidget(this);
  QVBoxLayout *watchLayout = new QVBoxLayout(watchContainer);
  watchLayout->setContentsMargins(0, 0, 0, 0);
  watchLayout->setSpacing(0);

  QWidget *watchToolbar = new QWidget(watchContainer);
  watchToolbar->setObjectName("watchToolbar");
  QHBoxLayout *watchBarLayout = new QHBoxLayout(watchToolbar);
  watchBarLayout->setContentsMargins(3, 2, 3, 2);
  watchBarLayout->setSpacing(3);

  QLineEdit *watchInput = new QLineEdit(watchToolbar);
  watchInput->setObjectName("watchExpressionInput");
  watchInput->setPlaceholderText("New watch expression...");
  watchInput->setFixedHeight(22);

  QPushButton *addWatchBtn = new QPushButton("+ Watch", watchToolbar);
  addWatchBtn->setObjectName("addWatchBtn");
  addWatchBtn->setFixedHeight(22);

  auto submitWatch = [this, watchInput]() {
    const QString expr = watchInput->text().trimmed();
    if (!expr.isEmpty() && m_differentialGrid) {
      m_differentialGrid->addVariableColumn(expr);
      emit m_differentialGrid->watchVariableAdded(expr);
      watchInput->clear();
    }
  };
  connect(addWatchBtn, &QPushButton::clicked, this, submitWatch);
  connect(watchInput, &QLineEdit::returnPressed, this, submitWatch);

  watchBarLayout->addWidget(watchInput, 1);
  watchBarLayout->addWidget(addWatchBtn);
  watchLayout->addWidget(watchToolbar);
  watchLayout->addWidget(m_differentialGrid, 1);

  QDockWidget *watchDock = new QDockWidget("Watch Expressions", this);
  watchDock->setObjectName("WatchExpressionsDock");
  watchDock->setWidget(watchContainer);

  m_serverRackView = new ServerRackView(this);
  m_serverRackView->setObjectName("ServerRackViewWidget");
  connect(m_serverRackView, &ServerRackView::rankSelected, this,
          &MainWindow::onRankSelected);
  QDockWidget *serverRackDock = new QDockWidget("Server Rack", this);
  serverRackDock->setObjectName("ServerRackDock");
  serverRackDock->setWidget(m_serverRackView);

  addDockWidget(Qt::RightDockWidgetArea, variablesDock);
  addDockWidget(Qt::RightDockWidgetArea, registersDock);
  tabifyDockWidget(variablesDock, registersDock);
  addDockWidget(Qt::RightDockWidgetArea, watchDock);
  addDockWidget(Qt::RightDockWidgetArea, serverRackDock);
  registersDock->raise();

  connect(m_editorTabManager, &EditorTabManager::pinVariableRequested, this,
          [this, watchDock](const QString &varName) {
            if (auto *gdbCoord = dynamic_cast<gridlock::GdbRankCoordinator *>(
                    m_coordinator)) {
              gdbCoord->registerWatchVariable(varName);
            }
            if (m_differentialGrid) {
              m_differentialGrid->addVariableColumn(varName);
            }
            if (watchDock) {
              watchDock->show();
              watchDock->raise();
            }
          });

  m_disassemblyView = new DisassemblyView(masterHorizontalSplitter);
  m_disassemblyView->setMinimumWidth(120);
  m_disassemblyView->setMaximumWidth(QWIDGETSIZE_MAX);
  m_disassemblyView->setSizePolicy(QSizePolicy::Expanding,
                                   QSizePolicy::Expanding);
  m_disassemblyView->setContentsMargins(0, 0, 0, 0);

  masterHorizontalSplitter->addWidget(m_editorTabManager);
  masterHorizontalSplitter->addWidget(m_disassemblyView);

  masterHorizontalSplitter->setStretchFactor(0, 45);
  masterHorizontalSplitter->setStretchFactor(1, 55);
  masterHorizontalSplitter->setSizes({430, 520});

  mainVerticalSplitter->addWidget(masterHorizontalSplitter);

  // Bottom Zone: Reduced to 7 core I/O & memory inspection tabs
  m_bottomTabs = new QTabWidget(mainVerticalSplitter);
  m_bottomTabs->setObjectName("BottomConsoleTabs");
  m_bottomTabs->setMinimumWidth(200);
  m_bottomTabs->setDocumentMode(true);
  m_bottomTabs->setContentsMargins(0, 0, 0, 0);
  if (m_bottomTabs->tabBar()) {
    m_bottomTabs->tabBar()->setUsesScrollButtons(true);
    m_bottomTabs->tabBar()->setElideMode(Qt::ElideNone);
  }

  m_terminalDock = new TerminalDockWidget("Terminal", m_bottomTabs);
  m_terminalDock->setObjectName("TerminalDock");

  m_expressionEvaluatorWidget = new ExpressionEvaluatorWidget(m_bottomTabs);
  m_expressionEvaluatorWidget->setObjectName("ExpressionEvaluatorDock");
  QWidget *emptyEvalTitle = new QWidget(m_expressionEvaluatorWidget);
  emptyEvalTitle->setFixedSize(0, 0);
  m_expressionEvaluatorWidget->setTitleBarWidget(emptyEvalTitle);
  connect(m_expressionEvaluatorWidget,
          &ExpressionEvaluatorWidget::evaluateRequested, this,
          [this](const QString &expr) {
            if (m_coordinator) {
              m_coordinator->evaluateExpression(m_focusedRank, expr);
            }
          });

  m_referenceManualWidget = new ReferenceManualWidget(m_bottomTabs);
  m_referenceManualWidget->setObjectName("ReferenceManualWidget");

  m_gdbConsoleWidget = new GdbConsoleWidget(m_bottomTabs);
  m_gdbConsoleWidget->setObjectName("GdbConsoleWidget");

  m_memView = new MemView(m_bottomTabs);
  m_memView->setObjectName("MemViewDock");

  m_spackManager = new SpackManager(m_hpcBackend, m_bottomTabs);
  m_spackManager->setObjectName("HpcConsoleDock");

  connect(m_memView, &MemView::requestMemory, this,
          [this](const QString &address, int length) {
            if (m_coordinator) {
              m_coordinator->readMemory(m_focusedRank, address, length);
            }
          });

  connect(
      m_memView, &MemView::requestMemoryDiff, this,
      [this](int baseRank, int targetRank, const QString &addrStr, int length) {
        if (!m_coordinator)
          return;
        pid_t basePid = m_coordinator->getPidForRank(baseRank);
        pid_t targetPid = m_coordinator->getPidForRank(targetRank);

        bool ok;
        quint64 addrInt = addrStr.toULongLong(&ok, 16);
        if (!ok)
          return;
        void *remoteAddr = reinterpret_cast<void *>(addrInt);

        QPointer<MainWindow> weakThis(this);
        (void)QtConcurrent::run([weakThis, basePid, targetPid, remoteAddr,
                                 length]() {
          auto result = gridlock::core::hpc::MemoryDiffer::compareMemory(
              basePid, targetPid, remoteAddr, length);
          QMetaObject::invokeMethod(weakThis, [weakThis, result, remoteAddr]() {
            if (weakThis && weakThis->m_memView) {
              weakThis->m_memView->displayMemoryDiff(result, remoteAddr);
            }
          });
        });
      });

  m_mpiDiagnosticsWidget = new MpiDiagnosticsWidget(m_bottomTabs);
  connect(m_mpiDiagnosticsWidget, &MpiDiagnosticsWidget::jumpToFrameRequested,
          this, &MainWindow::onRankSelected);

  m_bottomTabs->addTab(m_terminalDock, "Terminal");
  m_bottomTabs->addTab(m_gdbConsoleWidget, "GDB Console");
  m_bottomTabs->addTab(m_memView, "Memory View");
  m_bottomTabs->addTab(m_mpiDiagnosticsWidget, "MPI Diagnostics");
  m_bottomTabs->addTab(m_spackManager, "HPC Console");
  m_bottomTabs->addTab(m_expressionEvaluatorWidget, "Evaluator");
  m_bottomTabs->addTab(m_referenceManualWidget, "Reference Manual");
  m_bottomTabs->setCurrentWidget(m_memView);

  mainVerticalSplitter->addWidget(m_bottomTabs);
  m_bottomTabs->setMinimumHeight(180);

  mainVerticalSplitter->setStretchFactor(0, 68);
  mainVerticalSplitter->setStretchFactor(1, 32);
  mainVerticalSplitter->setSizes({600, 260});

  setCentralWidget(mainVerticalSplitter);

  const QList<QDockWidget *> sideDocks = {
      m_projectExplorerWidget, variablesDock, registersDock, watchDock,
      serverRackDock};
  for (QDockWidget *dock : sideDocks) {
    if (!dock) {
      continue;
    }
    dock->setMinimumWidth(150);
    dock->setMaximumWidth(QWIDGETSIZE_MAX);
    dock->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    if (QWidget *inner = dock->widget()) {
      inner->setMinimumWidth(150);
      inner->setMaximumWidth(QWIDGETSIZE_MAX);
      inner->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }
  }

  resizeDocks({m_projectExplorerWidget, registersDock}, {215, 255},
              Qt::Horizontal);
  resizeDocks({registersDock, watchDock, serverRackDock}, {320, 210, 290},
              Qt::Vertical);

  QMenu *viewMenu = nullptr;
  for (QAction *action : menuBar()->actions()) {
    if (action->menu() && action->text() == "&View") {
      viewMenu = action->menu();
      break;
    }
  }

  for (auto *dock : findChildren<QDockWidget *>()) {
    if (viewMenu) {
      viewMenu->addAction(dock->toggleViewAction());
    }
    dock->setFeatures(QDockWidget::DockWidgetClosable |
                      QDockWidget::DockWidgetMovable |
                      QDockWidget::DockWidgetFloatable);
    dock->setContentsMargins(0, 0, 0, 0);
    if (QWidget *inner = dock->widget()) {
      inner->setContentsMargins(0, 0, 0, 0);
      if (inner->layout()) {
        inner->layout()->setContentsMargins(0, 0, 0, 0);
        inner->layout()->setSpacing(0);
      }
    }

    connect(dock, &QDockWidget::topLevelChanged, dock, [dock](bool topLevel) {
      if (topLevel) {
        dock->setWindowFlags(dock->windowFlags() & ~Qt::FramelessWindowHint);
        dock->show();
      }
    });
  }

  QSettings settings("gridlock", "debugger");
  settings.remove("MainWindow/Geometry_v8");
  settings.remove("MainWindow/State_v8");
  settings.remove("MainWindow/Splitter_v8_MasterHorizontalSplitter");
  settings.remove("MainWindow/Splitter_v8_MainVerticalSplitter");
}

} // namespace gridlock::ui
