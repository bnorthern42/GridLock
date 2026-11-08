/**
 * @file MainWindowDocks.cpp
 * @brief Dock management, floating/redocking, bottom tool rail, and workstation theme propagation.
 *
 * Implements panel detachment into floating tool windows and redocking back into home
 * splitters or dock areas, bottom tool-window group selection, monitor screen routing,
 * and full-workstation dynamic theme propagation across chrome toolbars and dock headers.
 */

#include "MainWindow.hpp"
#include "../core/hpc/GdbRankCoordinator.hpp"
#include "../core/managers/SpackManager.hpp"
#include "../core/managers/ThemeManager.hpp"
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
#include "GdbConsoleWidget.hpp"

#include <QApplication>
#include <QContextMenuEvent>
#include <QDockWidget>
#include <QEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QSplitter>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QWindow>
#include <algorithm>
#include <cmath>

namespace gridlock::ui {

/**
 * @brief Creates and mounts a custom title bar widget on the specified dock widget.
 * @param dock Pointer to the target dock widget.
 * @param titleText Header display title string.
 * @return Constructed custom title bar header widget.
 */
QWidget* MainWindow::setupDockTitleBar(QDockWidget* dock, const QString& titleText) {
  if (!dock) return nullptr;
  auto* header = new QWidget(dock);
  header->setObjectName(QStringLiteral("customDockHeader"));
  header->setProperty("associatedDock", QVariant::fromValue(static_cast<void*>(dock)));
  header->setFixedHeight(26);
  header->setMinimumWidth(0);

  const auto& tm = gridlock::core::ThemeManager::instance();
  const auto pal = tm.paletteForTheme(tm.activeThemeId());
  header->setStyleSheet(QStringLiteral(
      "#customDockHeader { background: %1; border-bottom: 1px solid %2; } "
      "#customDockHeader QLabel { color: %3; font-size: 12px; font-weight: 700; padding-left: 6px; } "
      "#dockFloatBtn, #dockCloseBtn { background: transparent; color: %3; border: none; font-size: 12px; "
      "font-weight: 700; min-width: 20px; max-width: 20px; min-height: 20px; max-height: 20px; } "
      "#dockFloatBtn:hover, #dockCloseBtn:hover { background: %2; color: %3; border-radius: 2px; }")
      .arg(pal.headerHex, pal.borderHex, pal.textHex));

  auto* hLayout = new QHBoxLayout(header);
  hLayout->setContentsMargins(4, 2, 4, 2);
  hLayout->setSpacing(4);

  auto* titleLabel = new QLabel(titleText, header);
  titleLabel->setMinimumWidth(0);
  titleLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  // Make the title label transparent to mouse events and install the event filter so
  // clicking/dragging the text label itself triggers header drag and double-click float.
  titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  titleLabel->installEventFilter(this);
  hLayout->addWidget(titleLabel, 1);

  auto* floatBtn = new QToolButton(header);
  floatBtn->setObjectName(QStringLiteral("dockFloatBtn"));
  floatBtn->setText(QStringLiteral("⧉"));
  floatBtn->setToolTip(QStringLiteral("Pop Out / Float Panel"));
  connect(floatBtn, &QToolButton::clicked, this, [this, dock]() { toggleDockFloating(dock); });
  hLayout->addWidget(floatBtn);

  auto* closeBtn = new QToolButton(header);
  closeBtn->setObjectName(QStringLiteral("dockCloseBtn"));
  closeBtn->setText(QStringLiteral("✕"));
  closeBtn->setToolTip(QStringLiteral("Close Panel"));
  connect(closeBtn, &QToolButton::clicked, dock, &QDockWidget::close);
  hLayout->addWidget(closeBtn);

  header->installEventFilter(this);
  dock->setTitleBarWidget(header);
  dock->setMinimumWidth(150);
  return header;
}

/**
 * @brief Checks if a dock widget is currently detached in a top-level floating window.
 * @param dock Pointer to the dock widget to inspect.
 * @return True if dock is floating or detached as top-level window.
 */
bool MainWindow::isDockFloating(const QDockWidget* dock) const {
  return dock != nullptr && (dock->isFloating() || dock->isWindow() || (dock->windowFlags() & Qt::Window));
}

/**
 * @brief Toggles a dock widget between floating top-level state and docked workspace slot.
 * @param dock Pointer to the dock widget to float or redock.
 */
void MainWindow::toggleDockFloating(QDockWidget* dock) {
  if (!dock) return;
  if (isDockFloating(dock)) { redockWidget(dock); return; }
  if (!m_dockHomeSlots.contains(dock)) {
    DockHomeSlot slot;
    if (auto* spl = qobject_cast<QSplitter*>(dock->parentWidget())) {
      slot.splitter = spl; slot.splitterIndex = spl->indexOf(dock);
    } else {
      slot.splitter = nullptr; slot.area = dockWidgetArea(dock);
      if (slot.area == Qt::NoDockWidgetArea) slot.area = Qt::RightDockWidgetArea;
    }
    m_dockHomeSlots.insert(dock, slot);
  }
  // Detach into a top-level frameless tool window owned by MainWindow so our customDockHeader
  // drives both X11/offscreen coordinate dragging and Wayland QWindow::startSystemMove() cleanly
  // without triggering QDockWidget's internal grabMouse() warning on non-popup windows.
  dock->setFloating(false);
  dock->setParent(this, Qt::Tool | Qt::Window | Qt::FramelessWindowHint);
  dock->resize((std::max)(280, dock->width()), (std::max)(220, dock->height()));
  dock->move(mapToGlobal(rect().center()) - QPoint(dock->width() / 2, dock->height() / 2));
  dock->show(); dock->raise();
  if (auto* floatBtn = dock->findChild<QToolButton*>(QStringLiteral("dockFloatBtn"))) {
    floatBtn->setToolTip(QStringLiteral("Redock Panel to Workspace (Double-Click Title Bar)"));
  }
}

/**
 * @brief Re-docks a floating dock widget into its recorded home splitter or dock area.
 * @param dock Pointer to the dock widget to re-dock.
 */
void MainWindow::redockWidget(QDockWidget* dock) {
  if (!dock) return;
  if (m_dockHomeSlots.contains(dock)) {
    const auto slot = m_dockHomeSlots.value(dock);
    dock->setWindowFlags(Qt::Widget);
    if (slot.splitter) {
      slot.splitter->insertWidget(std::clamp(slot.splitterIndex, 0, slot.splitter->count()), dock);
      dock->setFloating(false); dock->show();
      QList<int> sizes = slot.splitter->sizes();
      int idx = slot.splitter->indexOf(dock);
      if (idx >= 0 && idx < sizes.size() && sizes[idx] < 120) {
        sizes[idx] = 160; slot.splitter->setSizes(sizes);
      }
    } else {
      addDockWidget(slot.area, dock); dock->setFloating(false); dock->show();
      // Preserve 2-zone right-sidebar hierarchy (tabbed Variables/Registers/Watch over ServerRack) on redock.
      if (slot.area == Qt::RightDockWidgetArea) {
        auto* vars = findChild<QDockWidget*>(QStringLiteral("VariablesDock"));
        auto* regs = findChild<QDockWidget*>(QStringLiteral("RegisterViewDock"));
        auto* watch = findChild<QDockWidget*>(QStringLiteral("WatchExpressionsDock"));
        auto* rack = findChild<QDockWidget*>(QStringLiteral("ServerRackDock"));
        if (dock == rack && vars && !isDockFloating(vars)) {
          splitDockWidget(vars, rack, Qt::Vertical);
          resizeDocks({vars, rack}, {520, 380}, Qt::Vertical);
        } else if ((dock == regs || dock == watch) && vars && !isDockFloating(vars)) {
          tabifyDockWidget(vars, dock);
        } else if (dock == vars && regs && !isDockFloating(regs)) {
          tabifyDockWidget(regs, vars);
        }
      }
    }
  } else {
    dock->setWindowFlags(Qt::Widget); addDockWidget(Qt::RightDockWidgetArea, dock);
    dock->setFloating(false); dock->show();
  }
  if (auto* floatBtn = dock->findChild<QToolButton*>(QStringLiteral("dockFloatBtn"))) {
    floatBtn->setToolTip(QStringLiteral("Pop Out / Float Panel"));
  }
}

/**
 * @brief Calculates the global screen rectangle of a dock widget's home drop zone.
 * @param dock Pointer to the dock widget.
 * @return Global screen rectangle of the home dock or splitter region.
 */
QRect MainWindow::dockHomeGlobalRect(const QDockWidget* dock) const {
  if (m_dockHomeSlots.contains(const_cast<QDockWidget*>(dock))) {
    const auto& slot = m_dockHomeSlots[const_cast<QDockWidget*>(dock)];
    if (slot.splitter && slot.splitter->isVisible()) return QRect(slot.splitter->mapToGlobal(QPoint(0, 0)), slot.splitter->size());
    if (slot.area == Qt::LeftDockWidgetArea) return QRect(mapToGlobal(QPoint(0, 0)), QSize(280, height()));
    if (slot.area == Qt::RightDockWidgetArea) return QRect(mapToGlobal(QPoint(width() - 280, 0)), QSize(280, height()));
    if (slot.area == Qt::BottomDockWidgetArea) return QRect(mapToGlobal(QPoint(0, height() - 260)), QSize(width(), 260));
  }
  return QRect(mapToGlobal(QPoint(0, 0)), size());
}

/**
 * @brief Evaluates mouse release position during dock drag and redocks if dropped in home zone.
 * @param dock Pointer to the dock widget being dragged.
 * @param globalReleasePos Global cursor release position.
 * @return True if redocked, false otherwise.
 */
bool MainWindow::handleDockDragRelease(QDockWidget* dock, const QPoint& globalReleasePos) {
  if (!dock) return false;
  const QRect homeRect = dockHomeGlobalRect(dock).adjusted(-60, -60, 60, 60);
  const QRect mainRect = QRect(mapToGlobal(QPoint(0, 0)), size()).adjusted(-40, -40, 40, 40);
  if (homeRect.contains(globalReleasePos) || mainRect.contains(globalReleasePos)) {
    redockWidget(dock);
    return true;
  }
  return false;
}

/**
 * @brief Restores all floating or detached panels back into their home workstation slots.
 */
void MainWindow::redockAllFloatingPanels() {
  for (auto* dock : m_dockHomeSlots.keys()) {
    if (dock && isDockFloating(dock)) redockWidget(dock);
  }
  for (auto* dock : findChildren<QDockWidget*>()) {
    if (dock && isDockFloating(dock)) redockWidget(dock);
  }
}

/**
 * @brief Intercepts and handles mouse/context-menu events on custom dock headers.
 * @param watched Target QObject emitting the event.
 * @param event QEvent payload to inspect.
 * @return True if event was handled and should be consumed.
 */
bool MainWindow::handleDockHeaderEvent(QObject* watched, QEvent* event) {
  QWidget* w = qobject_cast<QWidget*>(watched);
  if (!w) return false;
  // Resolve customDockHeader whether the event targeted the header directly or its child QLabel.
  QWidget* header = (w->objectName() == QStringLiteral("customDockHeader"))
                        ? w
                        : ((w->parentWidget() && w->parentWidget()->objectName() == QStringLiteral("customDockHeader"))
                               ? w->parentWidget()
                               : nullptr);
  if (!header || qobject_cast<QToolButton*>(w)) return false;

  auto* dock = qobject_cast<QDockWidget*>(header->parentWidget());
  if (!dock) {
    dock = reinterpret_cast<QDockWidget*>(header->property("associatedDock").value<void*>());
  }
  if (!dock) return false;

  // Double-clicking anywhere on custom title bar toggles floating vs docked state.
  if (event->type() == QEvent::MouseButtonDblClick) {
    toggleDockFloating(dock);
    event->accept();
    return true;
  }
  // Consume left-button press on the custom header so QDockWidgetPrivate::mousePressEvent
  // never invokes grabMouse() (which fails on Wayland non-popup windows).
  if (event->type() == QEvent::MouseButtonPress) {
    auto* me = static_cast<QMouseEvent*>(event);
    if (me->button() == Qt::LeftButton) {
      m_draggingDock = dock;
      m_dragOffset = me->globalPosition().toPoint() - dock->pos();
      event->accept();
      return true;
    }
  } else if (event->type() == QEvent::MouseMove) {
    auto* me = static_cast<QMouseEvent*>(event);
    if ((me->buttons() & Qt::LeftButton) && m_draggingDock == dock) {
      // If the user drags a docked panel header by > 10px, automatically pop it out so it can be dragged.
      if (!isDockFloating(dock) &&
          (me->globalPosition().toPoint() - (dock->pos() + m_dragOffset)).manhattanLength() > 10) {
        toggleDockFloating(dock);
        m_dragOffset = QPoint(dock->width() / 2, 12);
      }
      if (isDockFloating(dock)) {
        // On Wayland, delegate interactive top-level window movement to the compositor via startSystemMove().
        if (QGuiApplication::platformName().contains(QStringLiteral("wayland"), Qt::CaseInsensitive)) {
          if (QWindow* win = dock->windowHandle()) {
            if (win->startSystemMove()) {
              m_draggingDock = nullptr;
              event->accept();
              return true;
            }
          }
        }
        dock->move(me->globalPosition().toPoint() - m_dragOffset);
        event->accept();
        return true;
      }
    }
  } else if (event->type() == QEvent::MouseButtonRelease) {
    auto* me = static_cast<QMouseEvent*>(event);
    // Release mouse: check if dropped in home dock zone and redock if so.
    if (me->button() == Qt::LeftButton && m_draggingDock == dock) {
      m_draggingDock = nullptr;
      if (isDockFloating(dock)) {
        handleDockDragRelease(dock, me->globalPosition().toPoint());
      }
      event->accept();
      return true;
    }
  } else if (event->type() == QEvent::ContextMenu) {
    // Show context menu with Redock / Pop Out options and Redock All.
    auto* ce = static_cast<QContextMenuEvent*>(event);
    QMenu menu(header);
    const bool floating = isDockFloating(dock);
    menu.addAction(floating ? QStringLiteral("Redock to Workspace") : QStringLiteral("Pop Out / Float Panel"),
                   this, [this, dock]() { toggleDockFloating(dock); });
    menu.addAction(QStringLiteral("Redock All Floating Panels"), this, &MainWindow::redockAllFloatingPanels);
    menu.exec(ce->globalPos());
    return true;
  }
  return false;
}

/**
 * @brief Constructs status bar telemetry layout and labels.
 */
void MainWindow::setupStatusBar() {
  QStatusBar *bar = statusBar();
  bar->setObjectName(QStringLiteral("cutterStatusBar"));
  bar->setSizeGripEnabled(false);
  bar->setContentsMargins(6, 0, 6, 0);

  if (m_currentSeekOrPc == QStringLiteral("0x00000000") || m_currentSeekOrPc.isEmpty()) {
    m_currentSeekOrPc = QStringLiteral("0x00401120");
  }

  m_statusTelemetryLabel = new QLabel(bar);
  m_statusTelemetryLabel->setObjectName(QStringLiteral("cutterStatusTelemetry"));
  m_statusTelemetryLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
  bar->addPermanentWidget(m_statusTelemetryLabel, 1);
  updateStatusTelemetry(m_currentSeekOrPc, m_currentTargetState);
}

/**
 * @brief Updates the status bar telemetry banner with active rank, PC, and status.
 * @param pcOrSeek Hexadecimal address string or symbol.
 * @param targetState Execution state string (e.g. "RUNNING", "PAUSED", "IDLE").
 */
void MainWindow::updateStatusTelemetry(const QString &pcOrSeek, const QString &targetState) {
  if (!pcOrSeek.isEmpty()) {
    m_currentSeekOrPc = pcOrSeek;
  }
  if (!targetState.isEmpty()) {
    m_currentTargetState = targetState;
  }
  if (m_statusTelemetryLabel) {
    if (m_debugSessionActive) {
      const int rank = activeSteppingRank();
      const QString role = (rank == 0) ? QStringLiteral("COORDINATOR") : QStringLiteral("WORKER");
      m_statusTelemetryLabel->setText(
          QStringLiteral("ARCH: x86_64 | [STEPPING: RANK %1] | RANK %1 [%2] | sym.main @ %3 | STATUS: %4")
              .arg(rank).arg(role).arg(m_currentSeekOrPc).arg(m_currentTargetState));
    } else {
      m_statusTelemetryLabel->setText(
          QStringLiteral("ARCH: x86_64 | MPI RANK: %1 | PC/SEEK: %2 | STATUS: %3")
              .arg(m_focusedRank).arg(m_currentSeekOrPc).arg(m_currentTargetState));
    }
  }
}

/**
 * @brief Checks if backward history traversal is possible.
 * @return True if previous address entries exist in the history stack.
 */
bool MainWindow::canNavigateBack() const { return m_seekHistoryIndex > 0; }

/**
 * @brief Checks if forward history traversal is possible.
 * @return True if forward address entries exist beyond current index.
 */
bool MainWindow::canNavigateForward() const {
  return m_seekHistoryIndex >= 0 && m_seekHistoryIndex + 1 < m_seekHistory.size();
}

/**
 * @brief Synchronizes enabled/disabled state of toolbar navigation buttons.
 */
void MainWindow::updateNavigationButtons() {
  if (m_navBackBtn) m_navBackBtn->setEnabled(canNavigateBack());
  if (m_navForwardBtn) m_navForwardBtn->setEnabled(canNavigateForward());
}

/**
 * @brief Navigates backward in the address seek history stack.
 * @return True if previous address was reached and synchronized.
 */
bool MainWindow::navigateBack() {
  if (!canNavigateBack()) return false;
  --m_seekHistoryIndex;
  m_isNavigatingHistory = true;
  const bool ok = seekToAddressOrExpression(m_seekHistory.at(m_seekHistoryIndex));
  m_isNavigatingHistory = false;
  updateNavigationButtons();
  return ok;
}

/**
 * @brief Navigates forward in the address seek history stack.
 * @return True if next address was reached and synchronized.
 */
bool MainWindow::navigateForward() {
  if (!canNavigateForward()) return false;
  ++m_seekHistoryIndex;
  m_isNavigatingHistory = true;
  const bool ok = seekToAddressOrExpression(m_seekHistory.at(m_seekHistoryIndex));
  m_isNavigatingHistory = false;
  updateNavigationButtons();
  return ok;
}

/**
 * @brief Returns the ordered list of bottom tool-window icon rail group identifiers.
 * @return List containing {"Debug", "MPI", "Terminal", "Docs"}.
 */
QStringList MainWindow::bottomToolGroups() const {
  return {QStringLiteral("Debug"), QStringLiteral("MPI"),
          QStringLiteral("Terminal"), QStringLiteral("Docs")};
}

/**
 * @brief Maps a BottomConsoleTabs index to its parent tool-window rail group name.
 * @param tabIndex Zero-based tab index inside m_bottomTabs.
 * @return Group identifier ("Debug", "MPI", "Terminal", or "Docs"), or empty if out of range.
 */
QString MainWindow::bottomToolGroupForTabIndex(int tabIndex) const {
  if (!m_bottomTabs || tabIndex < 0 || tabIndex >= m_bottomTabs->count()) return {};
  const QWidget* page = m_bottomTabs->widget(tabIndex);
  if (page == m_terminalDock) return QStringLiteral("Terminal");
  if (page == m_referenceManualWidget) return QStringLiteral("Docs");
  if (page == m_mpiDiagnosticsWidget || page == m_spackManager) return QStringLiteral("MPI");
  if (page == m_gdbConsoleWidget || page == m_memView || page == m_expressionEvaluatorWidget) {
    return QStringLiteral("Debug");
  }
  const QString title = m_bottomTabs->tabText(tabIndex);
  if (title.contains(QStringLiteral("Terminal"), Qt::CaseInsensitive)) return QStringLiteral("Terminal");
  if (title.contains(QStringLiteral("Reference"), Qt::CaseInsensitive)) return QStringLiteral("Docs");
  if (title.contains(QStringLiteral("MPI"), Qt::CaseInsensitive) ||
      title.contains(QStringLiteral("HPC"), Qt::CaseInsensitive) ||
      title.contains(QStringLiteral("Package"), Qt::CaseInsensitive)) return QStringLiteral("MPI");
  return QStringLiteral("Debug");
}

/**
 * @brief Activates a bottom tool-window group, filtering visible tabs strictly to its linked views.
 * @param groupName Target group name ("Debug", "MPI", "Terminal", or "Docs").
 */
void MainWindow::selectBottomToolGroup(const QString& groupName) {
  if (!m_bottomTabs || m_syncingBottomToolGroup) return;
  QString resolvedGroup;
  for (const QString& g : bottomToolGroups()) {
    if (g.compare(groupName.trimmed(), Qt::CaseInsensitive) == 0) { resolvedGroup = g; break; }
  }
  if (resolvedGroup.isEmpty()) return;

  m_syncingBottomToolGroup = true;
  m_activeBottomToolGroup = resolvedGroup;

  for (auto it = m_bottomRailButtons.cbegin(); it != m_bottomRailButtons.cend(); ++it) {
    if (it.value()) it.value()->setChecked(it.key() == resolvedGroup);
  }

  int firstTabInGroup = -1;
  for (int i = 0; i < m_bottomTabs->count(); ++i) {
    const bool match = (bottomToolGroupForTabIndex(i) == resolvedGroup);
    m_bottomTabs->setTabVisible(i, match);
    if (match && firstTabInGroup < 0) firstTabInGroup = i;
  }
  const int curIdx = m_bottomTabs->currentIndex();
  if (firstTabInGroup >= 0 && bottomToolGroupForTabIndex(curIdx) != resolvedGroup) {
    m_bottomTabs->setCurrentIndex(firstTabInGroup);
  }
  m_syncingBottomToolGroup = false;
}

/**
 * @brief Synchronizes the bottom tool rail button state with the specified tab index.
 * @param tabIndex Selected tab index in m_bottomTabs.
 */
void MainWindow::syncBottomToolRailToTab(int tabIndex) {
  const QString grp = bottomToolGroupForTabIndex(tabIndex);
  if (!grp.isEmpty() && grp != m_activeBottomToolGroup) {
    selectBottomToolGroup(grp);
  }
}

/**
 * @brief Resolves a monitor hint string to an active QScreen display.
 * @param monitorHint Substring match for screen name, manufacturer, or geometry.
 * @return Matched QScreen pointer or primaryScreen() fallback.
 */
QScreen* MainWindow::resolvePreferredScreen(const QString& monitorHint) const {
  const QList<QScreen*> screens = QGuiApplication::screens();
  if (screens.isEmpty()) {
    return nullptr;
  }
  const QString hint = monitorHint.trimmed();
  if (hint.isEmpty()) {
    return QGuiApplication::primaryScreen();
  }
  for (QScreen* scr : screens) {
    if (!scr) continue;
    const QRect geom = scr->geometry();
    const QSizeF phys = scr->physicalSize();
    const int diagInches = qRound(std::hypot(phys.width(), phys.height()) / 25.4);
    const QString meta = QString("%1 %2 %3 %4x%5 %6\" %6in")
                             .arg(scr->name(), scr->model(), scr->manufacturer())
                             .arg(geom.width()).arg(geom.height()).arg(diagInches);
    if (meta.contains(hint, Qt::CaseInsensitive)) {
      return scr;
    }
  }
  return QGuiApplication::primaryScreen();
}

/**
 * @brief Moves and routes window placement to a preferred secondary monitor.
 * @param monitorHint Display descriptor query string.
 * @param openUnfocused Whether window should remain unfocused on launch.
 */
void MainWindow::applyPreferredScreenRouting(const QString& monitorHint, bool openUnfocused) {
  m_preferredMonitorHint = monitorHint.trimmed();
  if (m_preferredMonitorHint.isEmpty()) {
    setAttribute(Qt::WA_ShowWithoutActivating, false);
    return;
  }
  setAttribute(Qt::WA_ShowWithoutActivating, openUnfocused);
  if (QScreen* target = resolvePreferredScreen(m_preferredMonitorHint)) {
    if (target != QGuiApplication::primaryScreen()) {
      setScreen(target);
      if (windowHandle()) {
        windowHandle()->setScreen(target);
      }
      move(target->availableGeometry().topLeft());
    }
  }
}

namespace {

/**
 * @brief Paint event filter rendering the ELF section ribbon (#addressNavBar) with theme-aware palettes.
 *
 * Replaces hardcoded dark #141619 segment fills in light themes with pal.headerHex/pal.canvasHex
 * surfaces and high-contrast semantic inks (#b42318 .plt, #1d4ed8 .text, #0f766e .rodata,
 * #15803d .data, #b45309 stack) while preserving native click-to-seek behavior.
 */
class AddressRibbonThemeFilter final : public QObject {
 public:
  // Construct event filter attached to the ribbon widget lifetime.
  explicit AddressRibbonThemeFilter(QObject* parent = nullptr) : QObject(parent) {}

 protected:
  // Intercept QEvent::Paint on #addressNavBar to render light/dark theme-aligned ELF segments.
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() != QEvent::Paint) return QObject::eventFilter(watched, event);
    auto* bar = qobject_cast<QWidget*>(watched);
    if (!bar) return false;

    // Resolve active ThemePalette and current seek address property.
    const auto& tm = gridlock::core::ThemeManager::instance();
    const auto pal = tm.paletteForTheme(tm.activeThemeId());
    uint64_t curAddr = 0x00401120ULL;
    const QVariant prop = bar->property("currentAddress");
    if (prop.isValid() && prop.toULongLong() != 0ULL) curAddr = prop.toULongLong();

    // Standard x86_64 ELF section descriptors with dark and light semantic accent inks.
    struct SegSpec {
      const char* label;
      uint64_t base;
      uint64_t end;
      int weight;
      const char* darkFill;
      const char* darkFg;
      const char* lightFg;
    };
    // Configure .plt, .text, .rodata, .data, and stack section bounds and contrast-verified inks.
    static constexpr SegSpec kSegs[] = {
        {".plt 0x401000", 0x00401000ULL, 0x0040111fULL, 14, "#2c2238", "#c586c0", "#b42318"},
        {".text 0x401120 (sym.main)", 0x00401120ULL, 0x00401fffULL, 34, "#1e3038", "#4ec9b0", "#1d4ed8"},
        {".rodata 0x402000", 0x00402000ULL, 0x00403fffULL, 18, "#1e2836", "#569cd6", "#0f766e"},
        {".data 0x404000", 0x00404000ULL, 0x00405fffULL, 16, "#2f291e", "#dcdcaa", "#15803d"},
        {"stack 0x7fffffffe000", 0x7fffffffe000ULL, 0x7fffffffffffULL, 18, "#332222", "#ce9178", "#b45309"},
    };

    // Initialize crisp non-antialiased pixel painter and monospace label font.
    QPainter p(bar);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.fillRect(bar->rect(), QColor(pal.isDark ? pal.canvasHex : pal.headerHex));
    QFont segFont(QStringLiteral("JetBrains Mono"), 8, QFont::DemiBold);
    segFont.setStyleHint(QFont::Monospace);
    p.setFont(segFont);

    // Compute proportional horizontal layout across the 100-unit section weight sum.
    const int w = bar->width(), h = bar->height(), totalWeight = 100;
    int x = 0, activeCursorX = -1;
    // Draw each proportional section cell, semantic accent underline, and label text.
    for (size_t i = 0; i < 5; ++i) {
      const auto& seg = kSegs[i];
      const int segW = (i == 4) ? (w - x) : (w * seg.weight) / totalWeight;
      const bool inSeg = (curAddr >= seg.base && curAddr <= seg.end);
      const QRect segRect(x, 1, std::max(1, segW - 1), std::max(2, h - 2));
      // Use pal.headerHex / pal.canvasHex in light themes so no black ribbon bar remains.
      const QColor fill = pal.isDark ? QColor(QLatin1String(seg.darkFill))
                                     : QColor(inSeg ? pal.canvasHex : pal.headerHex);
      const QColor fg = QColor(QLatin1String(pal.isDark ? seg.darkFg : seg.lightFg));
      p.fillRect(segRect, fill);
      // Paint a 2px semantic bottom accent bar in light mode for section boundary legibility.
      if (!pal.isDark) p.fillRect(QRect(segRect.left(), segRect.bottom() - 1, segRect.width(), 2), fg);
      p.setPen(QColor(pal.borderHex));
      p.drawRect(segRect.adjusted(0, 0, -1, -1));
      p.setPen(fg);
      p.drawText(segRect.adjusted(6, 0, -6, -1), Qt::AlignVCenter | Qt::AlignLeft, QLatin1String(seg.label));
      // Track active instruction pointer horizontal coordinate within matching ELF segment.
      if (inSeg && seg.end > seg.base) {
        const double ratio = std::clamp(static_cast<double>(curAddr - seg.base) /
                                            static_cast<double>(std::min<uint64_t>(0x200ULL, seg.end - seg.base)),
                                        0.08, 0.92);
        activeCursorX = x + static_cast<int>(segW * ratio);
      }
      x += segW;
    }
    // Fall back to .text entry offset when curAddr lies outside preconfigured ranges.
    if (activeCursorX < 0) activeCursorX = (w * kSegs[0].weight) / totalWeight + 18;

    // Render active instruction / seek needle with dual triangular arrowheads.
    const QColor needle = pal.isDark ? QColor(QStringLiteral("#ffcc00")) : QColor(pal.accentHex);
    p.setPen(QPen(needle, 1));
    p.drawLine(activeCursorX, 0, activeCursorX, 4);
    p.drawLine(activeCursorX, h - 4, activeCursorX, h);
    p.setBrush(needle);
    p.setPen(Qt::NoPen);
    p.drawPolygon(QPolygon({QPoint(activeCursorX - 4, 0), QPoint(activeCursorX + 4, 0), QPoint(activeCursorX, 4)}));
    p.drawPolygon(QPolygon({QPoint(activeCursorX - 4, h), QPoint(activeCursorX + 4, h), QPoint(activeCursorX, h - 4)}));
    return true;
  }
};

}  // namespace

/**
 * @brief Propagates the active ThemeManager palette across all chrome toolbars, dock headers, and workstation views.
 *
 * Updates MainToolbar, AddressNavToolbar, #addressNavBar, history buttons, seek bar,
 * CODE | REVERSE | MPI perspective buttons, bottom tool rail, console tabs, and dock headers.
 */
void MainWindow::propagateThemeToAllWidgets() {
  // Query active theme palette tokens and polarity from ThemeManager singleton.
  auto& tm = gridlock::core::ThemeManager::instance();
  const auto pal = tm.paletteForTheme(tm.activeThemeId());
  const bool isDark = pal.isDark;

  // 1. Re-style Main Toolbar (#MainToolbar) and target selector (#toolbarTargetSelector) in 12px UI sans-serif
  if (auto* mainToolBar = findChild<QToolBar*>(QStringLiteral("MainToolbar"))) {
    mainToolBar->setStyleSheet(QStringLiteral(
        "QToolBar#MainToolbar { background-color: %1; border-bottom: 1px solid %2; "
        "spacing: 2px; padding: 1px 2px; height: 30px; min-height: 28px; max-height: 30px; }"
        "QToolBar#MainToolbar::separator { background-color: %2; width: 1px; margin: 4px 3px; }"
        "QToolBar#MainToolbar QLabel { background: transparent; color: %3; "
        "font-size: 12px; font-weight: 700; font-style: normal; padding: 0px 2px; border: none; }"
        "QToolBar#MainToolbar QComboBox, QToolBar#MainToolbar QSpinBox, #toolbarTargetSelector { "
        "background-color: %4; color: %3; border: 1px solid %2; border-radius: 3px; "
        "padding: 0px 5px; height: 22px; min-height: 20px; max-height: 22px; font-size: 12px; font-weight: 500; }"
        "QToolBar#MainToolbar QToolButton { background-color: %1; color: %3; "
        "border: 1px solid %2; border-radius: 3px; padding: 0px 5px; margin: 0px; "
        "height: 22px; min-height: 20px; max-height: 22px; font-size: 12px; font-weight: 600; }"
        "QToolBar#MainToolbar QToolButton:hover:enabled { background-color: %2; border: 1px solid %5; color: %3; }"
        "QToolBar#MainToolbar QToolButton:pressed:enabled { background-color: %4; border: 1px solid %5; }"
        "QToolBar#MainToolbar QToolButton:checked { background-color: %6; color: #ffffff; border: 1px solid %5; font-weight: 600; }"
        "QToolBar#MainToolbar QToolButton:disabled { background-color: %1; color: %7; border: 1px solid %2; }")
        .arg(pal.headerHex, pal.borderHex, pal.textHex, pal.canvasHex, pal.accentHex,
             isDark ? QStringLiteral("#19362b") : pal.accentHex,
             isDark ? QStringLiteral("#5c6370") : QStringLiteral("#93a1a1")));
    // Highlight Run Target (m_runAction) as the distinct primary call-to-action button on MainToolbar.
    if (m_runAction) {
      if (QWidget* runBtn = mainToolBar->widgetForAction(m_runAction)) {
        runBtn->setObjectName(QStringLiteral("toolbarRunTargetBtn"));
        runBtn->setStyleSheet(QStringLiteral(
            "QToolButton, QToolButton#toolbarRunTargetBtn { background-color: %1; color: #ffffff; "
            "font-size: 12px; font-weight: 600; border: 1px solid %1; border-radius: 4px; "
            "padding: 3px 12px; min-height: 18px; max-height: 18px; }"
            "QToolButton:hover:enabled { background-color: %1; color: #ffffff; border: 1px solid %2; }"
            "QToolButton:checked { background-color: %3; color: #ffffff; border: 1px solid %1; font-weight: 600; }"
            "QToolButton:disabled { background-color: %4; color: %5; border: 1px solid %4; }")
            .arg(pal.accentHex, pal.textHex, isDark ? QStringLiteral("#1f6f4a") : pal.accentHex,
                 pal.borderHex, isDark ? QStringLiteral("#5c6370") : QStringLiteral("#93a1a1")));
      }
    }
  }

  // 2. Re-style Address Space Section Ribbon (#AddressNavToolbar / #addressNavBar), Nav Arrows, Seek Bar, & CODE | REVERSE | MPI
  if (auto* navToolBar = findChild<QToolBar*>(QStringLiteral("AddressNavToolbar"))) {
    navToolBar->setStyleSheet(QStringLiteral(
        "QToolBar#AddressNavToolbar { background-color: %1; border-bottom: 1px solid %2; padding: 0px; margin: 0px; }")
        .arg(pal.headerHex, pal.borderHex));
  }
  // Attach theme-aware paint filter once and refresh #addressNavBar palette and QSS.
  if (m_addressNavBar) {
    if (!m_addressNavBar->property("gl_ribbon_filter").toBool()) {
      m_addressNavBar->installEventFilter(new AddressRibbonThemeFilter(m_addressNavBar));
      m_addressNavBar->setProperty("gl_ribbon_filter", true);
    }
    QPalette navPal = m_addressNavBar->palette();
    navPal.setColor(QPalette::Window, QColor(pal.headerHex));
    navPal.setColor(QPalette::Base, QColor(pal.canvasHex));
    navPal.setColor(QPalette::WindowText, QColor(pal.textHex));
    m_addressNavBar->setPalette(navPal);
    m_addressNavBar->setStyleSheet(QStringLiteral(
        "#addressNavBar { background-color: %1; color: %2; border-bottom: 1px solid %3; }")
        .arg(pal.headerHex, pal.textHex, pal.borderHex));
    m_addressNavBar->update();
  }

  // Style history navigation buttons (< / >) and global address seek bar dynamically in 12px UI sans-serif.
  const QString navBtnStyle = QStringLiteral(
      "QPushButton { background-color: %1; color: %2; border: 1px solid %3; border-radius: 3px; "
      "padding: 0px; margin: 0px; font-size: 12px; font-weight: 700; "
      "min-width: 22px; max-width: 24px; min-height: 22px; max-height: 24px; }"
      "QPushButton:hover:enabled { background-color: %3; border-color: %4; color: %2; }"
      "QPushButton:disabled { background-color: %1; color: %5; border: 1px solid %3; }")
      .arg(pal.headerHex, pal.textHex, pal.borderHex, pal.accentHex,
           isDark ? QStringLiteral("#5c6370") : QStringLiteral("#93a1a1"));
  for (QPushButton* btn : {m_navBackBtn, m_navForwardBtn}) {
    if (btn) btn->setStyleSheet(navBtnStyle);
  }
  if (m_seekBar) {
    m_seekBar->setStyleSheet(QStringLiteral(
        "QLineEdit#cutterSeekBar { background-color: %1; color: %2; border: 1px solid %3; "
        "border-radius: 3px; padding: 0px 6px; min-height: 22px; max-height: 24px; font-size: 12px; } "
        "QLineEdit#cutterSeekBar:focus { border: 1px solid %4; }")
        .arg(pal.canvasHex, pal.textHex, pal.borderHex, pal.accentHex));
  }

  // Style CODE | REVERSE | MPI segmented mode buttons: 24px height, 0px 10px padding, 12px font, pal.headerHex / pal.accentHex.
  const QString perspStyle = QStringLiteral(
      "QPushButton { background-color: %1; color: %2; border: 1px solid %3; border-radius: 3px; "
      "padding: 0px 10px; font-size: 12px; font-weight: 600; min-height: 24px; max-height: 24px; } "
      "QPushButton:hover { background-color: %3; color: %2; border: 1px solid %4; } "
      "QPushButton:checked { background-color: %4; color: #ffffff; border: 1px solid %4; }")
      .arg(pal.headerHex, pal.textHex, pal.borderHex, pal.accentHex);
  for (QPushButton* btn : {m_perspCodeBtn, m_perspReverseBtn, m_perspMpiBtn}) {
    if (btn) btn->setStyleSheet(perspStyle);
  }

  // 3. Re-style Bottom Tool Window Rail (#bottomToolWindowRail)
  if (m_bottomToolRail) {
    m_bottomToolRail->setStyleSheet(QStringLiteral(
        "#bottomToolWindowRail { background-color: %1; border-right: 1px solid %2; }"
        "#bottomToolWindowRail QToolButton { background-color: transparent; color: %3; "
        "border: 1px solid transparent; border-radius: 4px; min-width: 28px; max-width: 28px; "
        "min-height: 28px; max-height: 28px; padding: 2px; margin: 0px; }"
        "#bottomToolWindowRail QToolButton:hover { background-color: %4; color: %3; border: 1px solid %2; }"
        "#bottomToolWindowRail QToolButton:checked { background-color: %4; color: %5; border: 1px solid %5; border-left: 2px solid %5; }")
        .arg(pal.headerHex, pal.borderHex, pal.textHex, pal.canvasHex, pal.accentHex));
  }

  // 4. Re-style Bottom Console Tabs (m_bottomTabs) with >= 12px UI chrome and monospace console viewports
  if (m_bottomTabs) {
    m_bottomTabs->setStyleSheet(QStringLiteral(
        "QTabWidget::pane { border: 0px; margin: 0px; padding: 0px; background-color: %1; }"
        "QTabBar { background-color: %2; border-bottom: 1px solid %3; }"
        "QTabBar::tab { background-color: %2; color: %4; padding: 3px 10px; min-height: 20px; max-height: 24px; "
        "border-right: 1px solid %3; border-bottom: 1px solid %3; font-size: 12px; font-weight: 600; }"
        "QTabBar::tab:selected { background-color: %1; color: %4; border-bottom: 2px solid %5; }"
        "QTabBar::tab:hover:!selected { background-color: %3; color: %4; }"
        "QLineEdit, QComboBox, QSpinBox { background-color: %1; color: %4; border: 1px solid %3; border-radius: 3px; padding: 0px 6px; min-height: 20px; max-height: 22px; font-size: 12px; }"
        "QLineEdit:focus, QComboBox:focus, QSpinBox:focus { border: 1px solid %5; background-color: %1; }"
        "QPushButton { background-color: %2; color: %4; border: 1px solid %3; border-radius: 3px; padding: 1px 8px; min-height: 20px; max-height: 22px; font-size: 12px; font-weight: 600; }"
        "QPushButton:hover { background-color: %3; border: 1px solid %5; color: %4; }"
        "QCheckBox { color: %4; font-size: 12px; font-weight: 600; spacing: 5px; }"
        "QPlainTextEdit, QTextEdit, QTextBrowser, QListWidget { background-color: %1; color: %4; border: none; font-family: 'JetBrains Mono', monospace; font-size: 12px; }")
        .arg(pal.canvasHex, pal.headerHex, pal.borderHex, pal.textHex, pal.accentHex));
  }

  // 5. Re-style all custom dock headers (#customDockHeader) in 12px UI sans-serif
  const QString dockHeaderStyle = QStringLiteral(
      "#customDockHeader { background-color: %1; border-bottom: 1px solid %2; } "
      "#customDockHeader QLabel { color: %3; font-size: 12px; font-weight: 700; padding-left: 6px; } "
      "#dockFloatBtn, #dockCloseBtn { background-color: transparent; color: %3; border: none; font-size: 12px; "
      "font-weight: 700; min-width: 20px; max-width: 20px; min-height: 20px; max-height: 20px; } "
      "#dockFloatBtn:hover, #dockCloseBtn:hover { background: %2; color: %3; border-radius: 2px; }")
      .arg(pal.headerHex, pal.borderHex, pal.textHex);

  for (auto* dock : findChildren<QDockWidget*>()) {
    if (!dock) continue;
    if (auto* titleWidget = dock->titleBarWidget()) {
      if (titleWidget->objectName() == QStringLiteral("customDockHeader")) {
        titleWidget->setStyleSheet(dockHeaderStyle);
      }
    }
  }

  // 6. Ensure all QTabBar instances suppress scroll buttons and fit tabs compactly.
  for (auto* tb : findChildren<QTabBar*>()) {
    tb->setUsesScrollButtons(false);
    tb->setElideMode(Qt::ElideNone);
    tb->setExpanding(false);
  }

  // 7. Propagate theme to subviews and Variables scope strip
  if (m_variablesDockWidget) {
    if (auto* varHdr = m_variablesDockWidget->findChild<QWidget*>(QStringLiteral("variablesHeaderBar"))) {
      varHdr->setStyleSheet(QStringLiteral("#variablesHeaderBar { background-color: %1; border-bottom: 1px solid %2; } #variablesHeaderBar QLabel { color: %3; font-size: 12px; font-weight: 600; }").arg(pal.headerHex, pal.borderHex, pal.textHex));
    }
  }
  if (m_memView) m_memView->reloadStyle();
  if (m_differentialGrid) m_differentialGrid->reloadStyle();
  if (m_serverRackView) m_serverRackView->reloadStyle();
  if (m_disassemblyView) m_disassemblyView->reloadStyle();
  if (sourceCodeView()) sourceCodeView()->reloadStyle();
  if (m_projectExplorerWidget) m_projectExplorerWidget->reloadStyle();
}

/**
 * @brief Verifies that all chrome toolbars, ribbons, and dock headers align with the active theme and avoid hardcoded dark styles.
 * @return True if all inspected chrome elements are theme-aligned.
 */
bool MainWindow::areAllChromeToolbarsAndDockHeadersThemeAligned() const {
  if (!gridlock::core::ThemeManager::instance().isDarkMode()) {
    if (auto* mainToolBar = findChild<QToolBar*>(QStringLiteral("MainToolbar"))) {
      const QString ss = mainToolBar->styleSheet();
      if (ss.contains(QStringLiteral("#16181d"), Qt::CaseInsensitive) || ss.contains(QStringLiteral("#1b1e24"), Qt::CaseInsensitive) || ss.contains(QStringLiteral("#1e2228"), Qt::CaseInsensitive)) return false;
    }
    if (m_bottomToolRail) {
      const QString ss = m_bottomToolRail->styleSheet();
      if (ss.contains(QStringLiteral("#181a1f"), Qt::CaseInsensitive) || ss.contains(QStringLiteral("#16181d"), Qt::CaseInsensitive)) return false;
    }
    for (auto* dock : findChildren<QDockWidget*>()) {
      if (dock && dock->titleBarWidget()) {
        const QString ss = dock->titleBarWidget()->styleSheet();
        if (ss.contains(QStringLiteral("#1e222a"), Qt::CaseInsensitive) || ss.contains(QStringLiteral("#16181d"), Qt::CaseInsensitive)) return false;
      }
    }
  }
  return true;
}

/**
 * @brief Launches a multi-rank MPI debugging session.
 * @param binaryPath Executable path to target binary.
 * @param ranks Number of parallel MPI ranks to allocate.
 */
void MainWindow::startDebuggingSession(const QString &binaryPath, int ranks) {
  if (!m_coordinator) return;
  m_coordinator->launchParallelSession(binaryPath, ranks);
  if (m_gdbConsoleWidget) m_gdbConsoleWidget->resetRanks(ranks);
  if (m_serverRackView) m_serverRackView->resetRanks(ranks);
  if (m_differentialGrid) {
    if (auto *gdbCoord = dynamic_cast<gridlock::GdbRankCoordinator *>(m_coordinator)) {
      for (const auto &w : m_differentialGrid->getWatchExpressions()) gdbCoord->registerWatchVariable(QString::fromStdString(w));
    }
  }
}

/**
 * @brief No-op Vulkan instance configuration placeholder.
 * @param inst Active Vulkan instance pointer.
 */
void MainWindow::setVulkanInstance([[maybe_unused]] QVulkanInstance *inst) {}

}  // namespace gridlock::ui
