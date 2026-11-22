/**
 * @file MainWindowPerspectives.cpp
 * @brief Workspace perspectives, typography/theme propagation, status telemetry, and doc screenshots.
 *
 * Implements MainWindow::setWorkspacePerspective, toggleMaximizeBottomConsole,
 * applyTypographySettings, propagateThemeToAllWidgets, areAllChromeToolbarsAndDockHeadersThemeAligned,
 * syncRankCountChrome, updateStatusTelemetry, and captureDocumentationScreenshots.
 */

#include "MainWindow.hpp"

#include <algorithm>
#include <utility>
#include <vector>

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QFont>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QTextCursor>
#include <QToolBar>
#include <QToolButton>

#include "../core/managers/ConfigManager.hpp"
#include "../core/managers/ThemeManager.hpp"
#include "EditorTabManager.hpp"
#include "dialogs/AttachProcessDialog.hpp"
#include "dialogs/ConditionalBreakpointDialog.hpp"
#include "dialogs/PointerGraphDialog.hpp"
#include "dialogs/PreferencesDialog.hpp"
#include "docks/CallStackDockWidget.hpp"
#include "docks/VariablesDockWidget.hpp"
#include "views/DisassemblyView.hpp"
#include "views/MemView.hpp"
#include "views/MemoryProfilerWidget.hpp"
#include "views/ServerRackView.hpp"
#include "views/SourceCodeView.hpp"
#include "widgets/DifferentialGrid.hpp"
#include "widgets/ExpressionEvaluatorWidget.hpp"
#include "widgets/HpcConsoleWidget.hpp"
#include "widgets/MpiDiagnosticsWidget.hpp"
#include "widgets/ProjectExplorerWidget.hpp"

namespace gridlock::ui {

namespace {

/**
 * @brief Paint event filter rendering the ELF section ribbon (#addressNavBar) with theme-aware palettes.
 */
class AddressRibbonThemeFilter final : public QObject {
 public:
  explicit AddressRibbonThemeFilter(QObject* parent = nullptr) : QObject(parent) {}

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() != QEvent::Paint) return QObject::eventFilter(watched, event);
    auto* bar = qobject_cast<QWidget*>(watched);
    if (!bar) return false;

    const auto& tm = gridlock::core::ThemeManager::instance();
    const auto pal = tm.paletteForTheme(tm.activeThemeId());
    uint64_t curAddr = 0x00401120ULL;
    const QVariant prop = bar->property("currentAddress");
    if (prop.isValid() && prop.toULongLong() != 0ULL) curAddr = prop.toULongLong();

    struct SegSpec {
      const char* label; uint64_t base; uint64_t end; int weight;
      const char* darkFill; const char* darkFg; const char* lightFg;
    };
    static constexpr SegSpec kSegs[] = {
        {".plt 0x401000", 0x00401000ULL, 0x0040111fULL, 14, "#2c2238", "#c586c0", "#b42318"},
        {".text 0x401120 (sym.main)", 0x00401120ULL, 0x00401fffULL, 34, "#1e3038", "#4ec9b0", "#1d4ed8"},
        {".rodata 0x402000", 0x00402000ULL, 0x00403fffULL, 18, "#1e2836", "#569cd6", "#0f766e"},
        {".data 0x404000", 0x00404000ULL, 0x00405fffULL, 16, "#2f291e", "#dcdcaa", "#15803d"},
        {"stack 0x7fffffffe000", 0x7fffffffe000ULL, 0x7fffffffffffULL, 18, "#332222", "#ce9178", "#b45309"},
    };

    QPainter p(bar);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.fillRect(bar->rect(), QColor(pal.isDark ? pal.canvasHex : pal.headerHex));
    QFont segFont(QStringLiteral("JetBrains Mono"), 8, QFont::DemiBold);
    segFont.setStyleHint(QFont::Monospace);
    p.setFont(segFont);

    const int w = bar->width(), h = bar->height(), totalWeight = 100;
    int x = 0, activeCursorX = -1;
    for (size_t i = 0; i < 5; ++i) {
      const auto& seg = kSegs[i];
      const int segW = (i == 4) ? (w - x) : (w * seg.weight) / totalWeight;
      const bool inSeg = (curAddr >= seg.base && curAddr <= seg.end);
      const QRect segRect(x, 1, std::max(1, segW - 1), std::max(2, h - 2));
      const QColor fill = pal.isDark ? QColor(QLatin1String(seg.darkFill))
                                     : QColor(inSeg ? pal.canvasHex : pal.headerHex);
      const QColor fg = QColor(QLatin1String(pal.isDark ? seg.darkFg : seg.lightFg));
      p.fillRect(segRect, fill);
      if (!pal.isDark) p.fillRect(QRect(segRect.left(), segRect.bottom() - 1, segRect.width(), 2), fg);
      p.setPen(QColor(pal.borderHex)); p.drawRect(segRect.adjusted(0, 0, -1, -1));
      p.setPen(fg); p.drawText(segRect.adjusted(6, 0, -6, -1), Qt::AlignVCenter | Qt::AlignLeft, QLatin1String(seg.label));
      if (inSeg && seg.end > seg.base) {
        const double ratio = std::clamp(static_cast<double>(curAddr - seg.base) /
                                            static_cast<double>(std::min<uint64_t>(0x200ULL, seg.end - seg.base)),
                                        0.08, 0.92);
        activeCursorX = x + static_cast<int>(segW * ratio);
      }
      x += segW;
    }
    if (activeCursorX < 0) activeCursorX = (w * kSegs[0].weight) / totalWeight + 18;

    const QColor needle = pal.isDark ? QColor(QStringLiteral("#ffcc00")) : QColor(pal.accentHex);
    p.setPen(QPen(needle, 1));
    p.drawLine(activeCursorX, 0, activeCursorX, 4); p.drawLine(activeCursorX, h - 4, activeCursorX, h);
    p.setBrush(needle); p.setPen(Qt::NoPen);
    p.drawPolygon(QPolygon({QPoint(activeCursorX - 4, 0), QPoint(activeCursorX + 4, 0), QPoint(activeCursorX, 4)}));
    p.drawPolygon(QPolygon({QPoint(activeCursorX - 4, h), QPoint(activeCursorX + 4, h), QPoint(activeCursorX, h - 4)}));
    return true;
  }
};

}  // namespace

/** @brief Switches workstation between "Source", "Debug", "Binary", and "MPI" without force-redocking floating panels. */
void MainWindow::setWorkspacePerspective(const QString& perspective) {
  auto* masterSplitter = findChild<QSplitter*>(QStringLiteral("MasterHorizontalSplitter"));
  auto* mainVertSplitter = findChild<QSplitter*>(QStringLiteral("MainVerticalSplitter"));
  auto* navTb = findChild<QToolBar*>(QStringLiteral("AddressNavToolbar"));
  auto* varsDock = findChild<QDockWidget*>(QStringLiteral("VariablesDock"));
  auto* regsDock = findChild<QDockWidget*>(QStringLiteral("RegisterViewDock"));
  auto* watchDock = findChild<QDockWidget*>(QStringLiteral("WatchExpressionsDock"));
  auto* cstackDock = findChild<QDockWidget*>(QStringLiteral("CallStackDock"));
  if (varsDock && regsDock && watchDock && !isDockFloating(varsDock) && !isDockFloating(regsDock) && !isDockFloating(watchDock)) {
    if (!tabifiedDockWidgets(varsDock).contains(regsDock)) tabifyDockWidget(varsDock, regsDock);
    if (!tabifiedDockWidgets(varsDock).contains(watchDock)) tabifyDockWidget(regsDock, watchDock);
  }
  if (m_disassemblyView) m_disassemblyView->setMinimumWidth(120);

  const bool wasBin = (m_workspacePerspective == QStringLiteral("Binary"));
  const bool isBin = (perspective.compare(QStringLiteral("Binary"), Qt::CaseInsensitive) == 0 ||
                      perspective.compare(QStringLiteral("Reverse"), Qt::CaseInsensitive) == 0);
  const bool isMpi = (perspective.compare(QStringLiteral("MPI"), Qt::CaseInsensitive) == 0);
  const bool isDbg = (perspective.compare(QStringLiteral("Debug"), Qt::CaseInsensitive) == 0 ||
                      perspective.compare(QStringLiteral("Code"), Qt::CaseInsensitive) == 0);
  m_workspacePerspective = isBin ? QStringLiteral("Binary") : (isMpi ? QStringLiteral("MPI") : (isDbg ? QStringLiteral("Debug") : QStringLiteral("Source")));

  const bool hideIdleRight = (m_workspacePerspective == QStringLiteral("Source") && !m_debugSessionActive);
  for (QDockWidget* d : {varsDock, regsDock, watchDock, cstackDock}) {
    if (d && !isDockFloating(d)) d->setVisible(!hideIdleRight);
  }
  if (!hideIdleRight && varsDock && !isDockFloating(varsDock)) { varsDock->raise(); resizeDocks({varsDock}, {320}, Qt::Horizontal); }

  const bool showDis = isBin || ((isMpi || isDbg) && core::ConfigManager::instance().showDecompilerWithSource()) ||
                       (isDbg && perspective.compare(QStringLiteral("Code"), Qt::CaseInsensitive) == 0);
  if (navTb) navTb->setVisible(showDis);
  for (const QString& actId : {QStringLiteral("actionToolbarNavBack"), QStringLiteral("actionToolbarNavForward"),
                               QStringLiteral("actionToolbarSeekBar")}) {
    if (auto* a = findChild<QAction*>(actId)) a->setVisible(showDis);
  }

  if (isBin) {
    if (m_editorTabManager) m_editorTabManager->setVisible(false);
    if (m_disassemblyView) { m_disassemblyView->setVisible(true); m_disassemblyView->setSplitGraphDecompilerMode(true); }
    if (masterSplitter) masterSplitter->setSizes({0, (std::max)(width() - 360, 640)});
  } else if (isMpi) {
    if (m_editorTabManager) m_editorTabManager->setVisible(true);
    if (m_disassemblyView) m_disassemblyView->setVisible(showDis);
    if (masterSplitter) masterSplitter->setSizes(showDis ? QList<int>{420, 500} : QList<int>{920, 0});
    selectBottomToolGroup(QStringLiteral("MPI"));
    if (m_bottomTabs && m_mpiDiagnosticsWidget) m_bottomTabs->setCurrentWidget(m_mpiDiagnosticsWidget);
    if (mainVertSplitter) mainVertSplitter->setSizes({430, 410});
  } else if (isDbg) {
    if (m_editorTabManager) m_editorTabManager->setVisible(true);
    if (m_disassemblyView) m_disassemblyView->setVisible(showDis);
    if (masterSplitter) masterSplitter->setSizes(showDis ? QList<int>{440, 520} : QList<int>{940, 0});
    if (mainVertSplitter) mainVertSplitter->setSizes({600, 260});
    m_bottomConsoleMaximized = false;
  } else {
    if (m_editorTabManager) m_editorTabManager->setVisible(true);
    if (m_disassemblyView) m_disassemblyView->setVisible(false);
    if (masterSplitter) masterSplitter->setSizes({1020, 0});
    if (mainVertSplitter) mainVertSplitter->setSizes({600, 260});
    m_bottomConsoleMaximized = false;
  }

  if (m_perspSourceBtn) m_perspSourceBtn->setChecked(m_workspacePerspective == QStringLiteral("Source"));
  if (m_perspDebugBtn) m_perspDebugBtn->setChecked(m_workspacePerspective == QStringLiteral("Debug"));
  if (m_perspBinaryBtn) m_perspBinaryBtn->setChecked(m_workspacePerspective == QStringLiteral("Binary"));
  if (m_perspMpiBtn) m_perspMpiBtn->setChecked(m_workspacePerspective == QStringLiteral("MPI"));
  syncRankCountChrome();
  syncDisassemblyDecompilerVisibility(isBin || wasBin);
}

/** @brief Toggles the bottom console dock between standard height (260px) and maximized height (65%). */
void MainWindow::toggleMaximizeBottomConsole() {
  auto* mainVertSplitter = findChild<QSplitter*>(QStringLiteral("MainVerticalSplitter"));
  if (!mainVertSplitter) return;
  m_bottomConsoleMaximized = !m_bottomConsoleMaximized;
  const int totalH = (std::max)(mainVertSplitter->height(), 760);
  mainVertSplitter->setSizes(m_bottomConsoleMaximized ? QList<int>{totalH * 32 / 100, totalH * 68 / 100}
                                                      : QList<int>{600, 260});
}

/** @brief Applies UI and Code/Disassembly typography settings live across the entire workstation. */
void MainWindow::applyTypographySettings(const QString& uiFamily, int uiSize,
                                         const QString& codeFamily, int codeSize) {
  QSettings s("gridlock", "debugger");
  if (!uiFamily.trimmed().isEmpty()) s.setValue("appearance/ui_font_family", uiFamily.trimmed());
  s.setValue("appearance/ui_font_size", qBound(7, uiSize, 32));
  if (!codeFamily.trimmed().isEmpty()) s.setValue("appearance/code_font_family", codeFamily.trimmed());
  s.setValue("appearance/code_font_size", qBound(7, codeSize, 48));
  s.sync();

  gridlock::core::managers::ThemeManager::instance().applyThemeAndFonts();
  if (m_editorTabManager) {
    for (int i = 0; i < m_editorTabManager->count(); ++i) {
      if (auto* view = qobject_cast<SourceCodeView*>(m_editorTabManager->widget(i))) view->reloadStyle();
    }
  }
  if (m_disassemblyView) m_disassemblyView->reloadStyle();
  if (m_projectExplorerWidget) m_projectExplorerWidget->reloadStyle();
}

/** @brief Updates the status bar telemetry banner with active rank, PC, and status. */
void MainWindow::updateStatusTelemetry(const QString& pcOrSeek, const QString& targetState) {
  if (!pcOrSeek.isEmpty()) m_currentSeekOrPc = pcOrSeek;
  if (!targetState.isEmpty()) m_currentTargetState = targetState;
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

/** @brief Synchronizes ServerRackDock and multi-rank selector visibility for single vs multi-rank runs. */
void MainWindow::syncRankCountChrome(int rankCount) {
  const int ranks = (rankCount > 0) ? rankCount
                    : ((m_debugSessionActive && m_coordinator && m_coordinator->getProcessCount() > 0)
                           ? m_coordinator->getProcessCount()
                           : (m_rankCountSpin ? m_rankCountSpin->value() : 1));
  const bool multi = (ranks > 1);
  const bool isMpiPersp = (m_workspacePerspective == QStringLiteral("MPI"));
  const bool showMpiChrome = multi || isMpiPersp;
  const bool hideRight = (m_workspacePerspective == QStringLiteral("Source") && !m_debugSessionActive);
  auto* varsDock = findChild<QDockWidget*>(QStringLiteral("VariablesDock"));
  auto* cstackDock = findChild<QDockWidget*>(QStringLiteral("CallStackDock"));
  auto* rackDock = findChild<QDockWidget*>(QStringLiteral("ServerRackDock"));
  if (rackDock && !isDockFloating(rackDock)) rackDock->setVisible(!hideRight && showMpiChrome);
  if (!hideRight && varsDock && cstackDock && !isDockFloating(varsDock) && !isDockFloating(cstackDock)) {
    if (showMpiChrome && rackDock && !isDockFloating(rackDock)) resizeDocks({varsDock, cstackDock, rackDock}, {320, 200, 260}, Qt::Vertical);
    else resizeDocks({varsDock, cstackDock}, {460, 320}, Qt::Vertical);
  }
  for (const QString& id : {QStringLiteral("variablesScopeLabel"), QStringLiteral("variablesRankSelector"),
                            QStringLiteral("gdbConsoleTargetLabel"), QStringLiteral("gdbConsoleRankCombo"),
                            QStringLiteral("evaluatorRankCombo"), QStringLiteral("memProfileRankCombo")}) {
    if (QWidget* w = findChild<QWidget*>(id)) w->setVisible(showMpiChrome);
  }
  if (m_programOutputRankFilter) m_programOutputRankFilter->setVisible(showMpiChrome);
  const bool active = m_debugSessionActive || (m_coordinator && m_coordinator->getProcessCount() > 0);
  const bool showTbMulti = (active && multi) || isMpiPersp;
  if (m_executionScopeAction) m_executionScopeAction->setVisible(showTbMulti);
  if (auto* paTbAct = findChild<QAction*>(QStringLiteral("actionToolbarPauseAllRanks"))) paTbAct->setVisible(showTbMulti);
  if (m_expressionEvaluatorWidget) m_expressionEvaluatorWidget->setAvailableRankCount(ranks);
}

/** @brief Propagates the active ThemeManager palette across all chrome toolbars, dock headers, and views. */
void MainWindow::propagateThemeToAllWidgets() {
  auto& tm = gridlock::core::ThemeManager::instance();
  const auto pal = tm.paletteForTheme(tm.activeThemeId());
  const bool isDark = pal.isDark;

  if (auto* mainToolBar = findChild<QToolBar*>(QStringLiteral("MainToolbar"))) {
    mainToolBar->setMovable(true);
    mainToolBar->setStyleSheet(QStringLiteral(
        "QToolBar#MainToolbar { background-color: %1; border-bottom: 1px solid %2; spacing: 1px; padding: 1px 2px; height: 28px; min-height: 28px; max-height: 30px; }"
        "QToolBar#MainToolbar::handle { background-color: %2; width: 4px; margin: 4px 1px; border-radius: 2px; }"
        "QToolBar#MainToolbar::separator { background-color: %2; width: 1px; margin: 4px 1px; }"
        "QToolBar#MainToolbar QLabel { background: transparent; color: %3; font-size: 12px; font-weight: 700; font-style: normal; padding: 0px 2px; border: none; }"
        "QToolBar#MainToolbar QComboBox { background-color: %4; color: %3; border: 1px solid %2; border-radius: 4px; padding: 0px 5px; margin: 0px; height: 24px; min-height: 24px; max-height: 24px; font-size: 11px; font-weight: 500; }"
        "QToolBar#MainToolbar QSpinBox, #toolbarRankSpin { background-color: %4; color: %3; border: 1px solid %2; border-radius: 4px; padding: 0px 16px 0px 5px; margin: 0px; height: 24px; min-height: 24px; max-height: 24px; font-size: 11px; font-weight: 500; }"
        "QToolBar#MainToolbar QSpinBox::up-button, QToolBar#MainToolbar QSpinBox::down-button { width: 13px; margin-right: 1px; }"
        "QToolBar#MainToolbar QToolButton, QToolBar#MainToolbar QPushButton { background-color: %1; color: %3; border: 1px solid %2; border-radius: 4px; padding: 0px 2px; margin: 0px; min-width: 0px; height: 24px; min-height: 24px; max-height: 24px; font-size: 11px; font-weight: 600; }"
        "QToolBar#MainToolbar QToolButton[popupMode=\"1\"] { padding-left: 5px; padding-right: 20px; }"
        "QToolBar#MainToolbar QToolButton::menu-button { width: 16px; border-left: 1px solid %2; subcontrol-origin: padding; subcontrol-position: top right; }"
        "QToolBar#MainToolbar QToolButton::menu-arrow { width: 10px; height: 10px; }"
        "QToolBar#MainToolbar QToolButton[popupMode=\"2\"] { padding-left: 5px; padding-right: 18px; }"
        "QToolBar#MainToolbar QToolButton::menu-indicator { subcontrol-origin: padding; subcontrol-position: center right; right: 5px; }"
        "QToolBar#MainToolbar QToolButton:hover:enabled, QToolBar#MainToolbar QPushButton:hover:enabled { background-color: %2; border: 1px solid %5; color: %3; }"
        "QToolBar#MainToolbar QToolButton:pressed:enabled, QToolBar#MainToolbar QPushButton:pressed:enabled { background-color: %4; border: 1px solid %5; }"
        "QToolBar#MainToolbar QToolButton:checked, QToolBar#MainToolbar QPushButton:checked { background-color: %6; color: #ffffff; border: 1px solid %5; font-weight: 600; }"
        "QToolBar#MainToolbar QToolButton:disabled, QToolBar#MainToolbar QPushButton:disabled { background-color: %1; color: %7; border: 1px solid %2; }")
        .arg(pal.headerHex, pal.borderHex, pal.textHex, pal.canvasHex, pal.accentHex,
             isDark ? QStringLiteral("#19362b") : pal.accentHex,
             isDark ? QStringLiteral("#5c6370") : QStringLiteral("#93a1a1")));
    for (QToolButton* btn : mainToolBar->findChildren<QToolButton*>()) { btn->setFixedHeight(26); btn->setAttribute(Qt::WA_AlwaysShowToolTips, true); }
    for (QPushButton* btn : mainToolBar->findChildren<QPushButton*>()) { btn->setFixedHeight(26); btn->setAttribute(Qt::WA_AlwaysShowToolTips, true); }
    if (m_rankCountSpin) { m_rankCountSpin->setMinimumWidth(84); m_rankCountSpin->setMaximumWidth(92); m_rankCountSpin->setFixedHeight(26); }
    if (m_targetSelector) { m_targetSelector->setMinimumWidth(168); m_targetSelector->setMaximumWidth(190); m_targetSelector->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed); m_targetSelector->setFixedHeight(26); }
    if (m_executionScopeCombo) { m_executionScopeCombo->setMinimumWidth(115); m_executionScopeCombo->setMaximumWidth(124); m_executionScopeCombo->setFixedHeight(26); }
    if (m_runAction) {
      if (QWidget* runBtn = mainToolBar->widgetForAction(m_runAction)) {
        runBtn->setObjectName(QStringLiteral("toolbarRunTargetBtn")); runBtn->setFixedHeight(26);
        runBtn->setStyleSheet(QStringLiteral(
            "QToolButton, QToolButton#toolbarRunTargetBtn { background-color: %1; color: #ffffff; font-size: 11px; font-weight: 600; border: 1px solid %1; border-radius: 4px; padding: 0px 4px; margin: 0px; min-width: 0px; height: 24px; min-height: 24px; max-height: 24px; }"
            "QToolButton:hover:enabled { background-color: %1; color: #ffffff; border: 1px solid %2; }"
            "QToolButton:checked { background-color: %3; color: #ffffff; border: 1px solid %1; font-weight: 600; }"
            "QToolButton:disabled { background-color: %4; color: %5; border: 1px solid %4; }")
            .arg(pal.accentHex, pal.textHex, isDark ? QStringLiteral("#1f6f4a") : pal.accentHex, pal.borderHex,
                 isDark ? QStringLiteral("#5c6370") : QStringLiteral("#93a1a1")));
      }
    }
  }

  if (auto* navToolBar = findChild<QToolBar*>(QStringLiteral("AddressNavToolbar"))) {
    navToolBar->setMovable(true);
    navToolBar->setStyleSheet(QStringLiteral("QToolBar#AddressNavToolbar { background-color: %1; border-bottom: 1px solid %2; padding: 0px; margin: 0px; } QToolBar#AddressNavToolbar::handle { background-color: %2; width: 4px; margin: 3px 2px; border-radius: 1px; }").arg(pal.headerHex, pal.borderHex));
  }
  if (m_addressNavBar) {
    if (!m_addressNavBar->property("gl_ribbon_filter").toBool()) {
      m_addressNavBar->installEventFilter(new AddressRibbonThemeFilter(m_addressNavBar));
      m_addressNavBar->setProperty("gl_ribbon_filter", true);
    }
    QPalette np = m_addressNavBar->palette();
    np.setColor(QPalette::Window, QColor(pal.headerHex));
    np.setColor(QPalette::Base, QColor(pal.canvasHex));
    np.setColor(QPalette::WindowText, QColor(pal.textHex));
    m_addressNavBar->setPalette(np);
    m_addressNavBar->setStyleSheet(QStringLiteral("#addressNavBar { background-color: %1; color: %2; border-bottom: 1px solid %3; }").arg(pal.headerHex, pal.textHex, pal.borderHex));
    m_addressNavBar->update();
  }

  const QString navBtnStyle = QStringLiteral("QPushButton { background-color: %1; color: %2; border: 1px solid %3; border-radius: 3px; padding: 0px; margin: 0px; font-size: 11px; font-weight: 700; min-width: 20px; max-width: 24px; min-height: 22px; max-height: 24px; } QPushButton:hover:enabled { background-color: %3; border-color: %4; color: %2; } QPushButton:disabled { background-color: %1; color: %5; border: 1px solid %3; }").arg(pal.headerHex, pal.textHex, pal.borderHex, pal.accentHex, isDark ? QStringLiteral("#5c6370") : QStringLiteral("#93a1a1"));
  for (QPushButton* btn : {m_navBackBtn, m_navForwardBtn}) if (btn) btn->setStyleSheet(navBtnStyle);
  if (m_seekBar) m_seekBar->setStyleSheet(QStringLiteral("QLineEdit#cutterSeekBar { background-color: %1; color: %2; border: 1px solid %3; border-radius: 3px; padding: 0px 3px; margin: 0px; min-width: 40px; max-width: 48px; min-height: 22px; max-height: 24px; font-size: 11px; } QLineEdit#cutterSeekBar:focus { border: 1px solid %4; }").arg(pal.canvasHex, pal.textHex, pal.borderHex, pal.accentHex));

  const QString perspStyle = QStringLiteral("QPushButton { background-color: %1; color: %2; border: 1px solid %3; border-radius: 3px; padding: 0px 3px; margin: 0px; min-width: 0px; font-size: 10px; font-weight: 700; min-height: 24px; max-height: 24px; } QPushButton:hover { background-color: %3; color: %2; border: 1px solid %4; } QPushButton:checked { background-color: %4; color: #ffffff; border: 1px solid %4; }").arg(pal.headerHex, pal.textHex, pal.borderHex, pal.accentHex);
  for (QPushButton* btn : {m_perspSourceBtn, m_perspDebugBtn, m_perspBinaryBtn, m_perspMpiBtn}) if (btn) btn->setStyleSheet(perspStyle);
  if (m_bottomToolRail) {
    m_bottomToolRail->setStyleSheet(QStringLiteral("#bottomToolWindowRail { background-color: %1; border-right: 1px solid %2; } #bottomToolWindowRail QToolButton { background-color: transparent; color: %3; border: 1px solid transparent; border-radius: 4px; min-width: 28px; max-width: 28px; min-height: 28px; max-height: 28px; padding: 2px; margin: 0px; } #bottomToolWindowRail QToolButton:hover { background-color: %4; color: %3; border: 1px solid %2; } #bottomToolWindowRail QToolButton:checked { background-color: %4; color: %5; border: 1px solid %5; border-left: 2px solid %5; }").arg(pal.headerHex, pal.borderHex, pal.textHex, pal.canvasHex, pal.accentHex));
  }
  if (m_bottomTabs) {
    m_bottomTabs->setStyleSheet(QStringLiteral("QTabWidget::pane { border: 0px; margin: 0px; padding: 0px; background-color: %1; } QTabBar { background-color: %2; border-bottom: 1px solid %3; } QTabBar::tab { background-color: %2; color: %4; padding: 3px 10px; min-height: 20px; max-height: 24px; border-right: 1px solid %3; border-bottom: 1px solid %3; font-size: 12px; font-weight: 600; } QTabBar::tab:selected { background-color: %1; color: %4; border-bottom: 2px solid %5; } QTabBar::tab:hover:!selected { background-color: %3; color: %4; } QLineEdit, QComboBox, QSpinBox { background-color: %1; color: %4; border: 1px solid %3; border-radius: 3px; padding: 0px 6px; min-height: 20px; max-height: 22px; font-size: 12px; } QLineEdit:focus, QComboBox:focus, QSpinBox:focus { border: 1px solid %5; background-color: %1; } QPushButton { background-color: %2; color: %4; border: 1px solid %3; border-radius: 3px; padding: 1px 8px; min-height: 20px; max-height: 22px; font-size: 12px; font-weight: 600; } QPushButton:hover { background-color: %3; border: 1px solid %5; color: %4; } QCheckBox { color: %4; font-size: 12px; font-weight: 600; spacing: 5px; } QPlainTextEdit, QTextEdit, QTextBrowser, QListWidget { background-color: %1; color: %4; border: none; font-family: 'JetBrains Mono', monospace; font-size: 12px; }").arg(pal.canvasHex, pal.headerHex, pal.borderHex, pal.textHex, pal.accentHex));
  }
  const QString dockHeaderStyle = QStringLiteral("#customDockHeader { background-color: %1; border-bottom: 1px solid %2; } #customDockHeader QLabel { color: %3; font-size: 12px; font-weight: 700; padding-left: 6px; } #dockFloatBtn, #dockCloseBtn { background-color: transparent; color: %3; border: none; font-size: 12px; font-weight: 700; min-width: 20px; max-width: 20px; min-height: 20px; max-height: 20px; } #dockFloatBtn:hover, #dockCloseBtn:hover { background: %2; color: %3; border-radius: 2px; }").arg(pal.headerHex, pal.borderHex, pal.textHex);
  for (auto* dock : findChildren<QDockWidget*>()) {
    if (dock && dock->titleBarWidget() && dock->titleBarWidget()->objectName() == QStringLiteral("customDockHeader")) {
      dock->titleBarWidget()->setStyleSheet(dockHeaderStyle);
    }
  }
  const QString dockTabStyle = QStringLiteral("QTabBar { background-color: %1; border-bottom: 1px solid %2; } QTabBar::tab { background-color: %1; color: %3; padding: 3px 4px; margin: 0px; min-height: 20px; max-height: 24px; border-right: 1px solid %2; font-size: 12px; font-weight: 600; } QTabBar::tab:selected { background-color: %4; color: %3; border-bottom: 2px solid %5; } QTabBar::tab:hover:!selected { background-color: %2; color: %3; }").arg(pal.headerHex, pal.borderHex, pal.textHex, pal.canvasHex, pal.accentHex);
  for (auto* tb : findChildren<QTabBar*>()) {
    tb->setUsesScrollButtons(false); tb->setElideMode(Qt::ElideNone); tb->setExpanding(false);
    if (tb->parentWidget() == this) tb->setStyleSheet(dockTabStyle);
  }
  if (auto* vh = m_variablesDockWidget ? m_variablesDockWidget->findChild<QWidget*>(QStringLiteral("variablesHeaderBar")) : nullptr) {
    vh->setStyleSheet(QStringLiteral("#variablesHeaderBar { background-color: %1; border-bottom: 1px solid %2; } #variablesHeaderBar QLabel { color: %3; font-size: 12px; font-weight: 600; }").arg(pal.headerHex, pal.borderHex, pal.textHex));
  }
  if (m_callStackDockWidget) m_callStackDockWidget->reloadStyle();
  if (m_memView) m_memView->reloadStyle();
  if (m_differentialGrid) m_differentialGrid->reloadStyle();
  if (m_serverRackView) m_serverRackView->reloadStyle();
  if (m_disassemblyView) m_disassemblyView->reloadStyle();
  if (sourceCodeView()) sourceCodeView()->reloadStyle();
  if (m_projectExplorerWidget) m_projectExplorerWidget->reloadStyle();
}

/** @brief Verifies that all chrome toolbars, ribbons, and dock headers align with the active theme. */
bool MainWindow::areAllChromeToolbarsAndDockHeadersThemeAligned() const {
  if (!gridlock::core::ThemeManager::instance().isDarkMode()) {
    if (auto* mtb = findChild<QToolBar*>(QStringLiteral("MainToolbar"))) {
      const QString ss = mtb->styleSheet();
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

/** @brief Captures 1600x960 workstation, feature, and theme documentation PNG screenshots into outputDir. */
bool MainWindow::captureDocumentationScreenshots(const QString& outputDir) {
  QDir dir(outputDir);
  if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) return false;

  resize(1600, 960);
  loadPreviewFixture();
  selectTargetByName(QStringLiteral("deadlock_ring"));
  const QString deadlockSrc = resolveWorkspacePath(QStringLiteral("tutorial/deadlock_ring.cpp"));
  if (m_editorTabManager && !deadlockSrc.isEmpty()) {
    m_editorTabManager->clearAllTabs();
    m_editorTabManager->openFile(deadlockSrc);
  }
  show();
  QApplication::processEvents();

  int bpLine = 34;
  QString bpLineText = QStringLiteral("MPI_Send(send_buf.data(), static_cast<int>(send_buf.size()), MPI_INT,");
  if (auto* sv = sourceCodeView()) {
    const QStringList srcLines = sv->toPlainText().split(QLatin1Char('\n'));
    for (int i = 0; i < srcLines.size(); ++i) {
      if (srcLines[i].contains(QStringLiteral("MPI_Send("))) { bpLine = i + 1; bpLineText = srcLines[i].trimmed(); break; }
    }
    if (!sv->hasBreakpoint(18)) sv->toggleBreakpoint(18);
    sv->setConditionalBreakpoint(bpLine, QStringLiteral("rank == 0"));
    sv->highlightExecutionLine(bpLine);
    sv->setInlineVariableValues({
        {QStringLiteral("rank"), QStringLiteral("0")}, {QStringLiteral("size"), QStringLiteral("8")},
        {QStringLiteral("next_rank"), QStringLiteral("1")}, {QStringLiteral("prev_rank"), QStringLiteral("7")},
        {QStringLiteral("tag"), QStringLiteral("100")}});
    QTextCursor cur(sv->document()->findBlockByNumber(bpLine - 1));
    sv->setTextCursor(cur);
    sv->centerCursor();
  }
  auto saveGrab = [&](const QString& fn) -> bool {
    QApplication::processEvents();
    return grab().save(dir.filePath(fn), "PNG", 88);
  };

  setWorkspacePerspective(QStringLiteral("Debug"));
  for (const auto& [presetId, fileName] : std::vector<std::pair<QString, QString>>{
           {QStringLiteral("dark_teal"), QStringLiteral("theme_obsidian_teal.png")}, {QStringLiteral("tokyo_night"), QStringLiteral("theme_tokyo_night.png")},
           {QStringLiteral("catppuccin_mocha"), QStringLiteral("theme_catppuccin_mocha.png")}, {QStringLiteral("gruvbox_dark"), QStringLiteral("theme_gruvbox_dark.png")},
           {QStringLiteral("daylight_light"), QStringLiteral("theme_daylight_light.png")}, {QStringLiteral("solarized_light"), QStringLiteral("theme_solarized_light.png")},
           {QStringLiteral("catppuccin_latte"), QStringLiteral("theme_catppuccin_latte.png")}, {QStringLiteral("gridlock_slate"), QStringLiteral("theme_gridlock_slate.png")}}) {
    core::managers::ThemeManager::instance().applyTheme(presetId, false);
    propagateThemeToAllWidgets();
    if (!saveGrab(fileName)) return false;
  }
  if (core::managers::ThemeManager::instance().activeThemeId() != QStringLiteral("gridlock_slate")) {
    core::managers::ThemeManager::instance().applyTheme(QStringLiteral("gridlock_slate"), false);
    propagateThemeToAllWidgets();
  }
  if (!saveGrab(QStringLiteral("workspace_overview.png"))) return false;
  setWorkspacePerspective(QStringLiteral("Binary"));
  if (!saveGrab(QStringLiteral("cfg_decompiler_split.png"))) return false;
  setWorkspacePerspective(QStringLiteral("Debug"));

  {
    ConditionalBreakpointDialog popDlg(QStringLiteral("rank == 0"), this);
    popDlg.setTargetLineContext(bpLine, bpLineText);
    popDlg.adjustSize();
    QPoint anchorPos(290, 260);
    if (auto* sv = sourceCodeView()) {
      QTextCursor bpCur(sv->document()->findBlockByNumber(bpLine - 1));
      sv->setTextCursor(bpCur); sv->centerCursor(); QApplication::processEvents();
      anchorPos = sv->mapTo(this, QPoint(36, sv->cursorRect(bpCur).bottom() + 4));
    }
    QPixmap basePix = grab();
    QPainter painter(&basePix); painter.drawPixmap(anchorPos, popDlg.grab()); painter.end();
    if (!basePix.save(dir.filePath(QStringLiteral("breakpoint_popover.png")), "PNG", 88)) return false;
  }

  selectBottomToolGroup(QStringLiteral("Debug"));
  if (!saveGrab(QStringLiteral("tool_rail_debug_tabs.png"))) return false;
  selectBottomToolGroup(QStringLiteral("MPI"));
  detectAndHighlightDeadlock({{0, 1}, {1, 2}, {2, 3}, {3, 0}});
  if (m_bottomTabs && m_mpiDiagnosticsWidget) m_bottomTabs->setCurrentWidget(m_mpiDiagnosticsWidget);
  if (!saveGrab(QStringLiteral("tool_rail_mpi_diagnostics.png")) || !saveGrab(QStringLiteral("mpi_diagnostics_docsets.png"))) return false;

  if (auto* pkgWidget = static_cast<HpcConsoleWidget*>(m_spackManager)) {
    if (m_bottomTabs) m_bottomTabs->setCurrentWidget(pkgWidget);
    pkgWidget->searchPackagesSynchronously(QStringLiteral("mpi"), 3000);
    pkgWidget->setViewMode(QStringLiteral("Table"));
    if (!saveGrab(QStringLiteral("package_manager_mpi_search.png"))) return false;
    pkgWidget->setViewMode(QStringLiteral("List"));
    if (!saveGrab(QStringLiteral("package_manager_list_view.png"))) return false;
  }

  {
    PreferencesDialog prefsDlg(this); prefsDlg.resize(940, 660);
    if (auto* catList = prefsDlg.findChild<QListWidget*>()) {
      for (int i = 0; i < catList->count(); ++i) if (catList->item(i)->text().contains(QStringLiteral("HPC"), Qt::CaseInsensitive)) catList->setCurrentRow(i);
    }
    prefsDlg.parseSlurmBatchScript(QStringLiteral(
        "#!/bin/bash\n#SBATCH --job-name=gridlock_ring_debug\n#SBATCH --partition=gpu-a100\n#SBATCH --account=hpc_devtools\n"
        "#SBATCH --qos=interactive\n#SBATCH --nodes=4\n#SBATCH --ntasks=16\n#SBATCH --ntasks-per-node=4\n#SBATCH --cpus-per-task=8\n"
        "#SBATCH --gres=gpu:a100:4\n#SBATCH --mem=128G\n#SBATCH --time=02:30:00\n#SBATCH --constraint=nvlink\n"
        "#SBATCH --reservation=maint_window\n#SBATCH --chdir=/scratch/hpc/gridlock\n#SBATCH --output=logs/slurm-%j.out\n"
        "#SBATCH --error=logs/slurm-%j.err\nmodule load openmpi/5.0.5 cuda/12.4\n"));
    QApplication::processEvents();
    QPixmap basePix = grab(); QPainter painter(&basePix); const QPixmap dlgPix = prefsDlg.grab();
    painter.drawPixmap((width() - dlgPix.width()) / 2, (height() - dlgPix.height()) / 2, dlgPix); painter.end();
    if (!basePix.save(dir.filePath(QStringLiteral("preferences_slurm_autodetect.png")), "PNG", 88)) return false;
  }

  {
    MemoryProfilerWidget profWidget(nullptr); profWidget.resize(1280, 800); profWidget.reloadStyle();
    profWidget.refreshForPid(QCoreApplication::applicationPid()); profWidget.show(); QApplication::processEvents();
    if (!profWidget.grab().save(dir.filePath(QStringLiteral("memory_profiler.png")), "PNG", 88)) return false;
  }

  {
    using F = PointerGraphDialog::StructFieldEntry;
    PointerGraphDialog pgDlg(nullptr, this); pgDlg.resize(1280, 800);
    pgDlg.upsertStructNode("sim", "SimConfig", "0x7fff1000", {F{"rank", "0", "int"}, F{"domain", "0x7fff2000", "MeshDomain*"}, F{"ring_head", "0x7fff3000", "RankRingNode*"}});
    pgDlg.upsertStructNode("domain", "MeshDomain", "0x7fff2000", {F{"nx", "256", "int"}, F{"ny", "256", "int"}, F{"root_field", "0x7fff4000", "FieldNode*"}}, "sim", "domain");
    pgDlg.upsertStructNode("ring_head", "RankRingNode", "0x7fff3000", {F{"rank_id", "0", "int"}, F{"next", "0x7fff3100", "RankRingNode*"}}, "sim", "ring_head");
    pgDlg.upsertStructNode("ring_peer", "RankRingNode", "0x7fff3100", {F{"rank_id", "1", "int"}, F{"next", "0x7fff3000", "RankRingNode*"}}, "ring_head", "next");
    pgDlg.upsertStructNode("root_field", "FieldNode", "0x7fff4000", {F{"norm_l2", "1.41421", "double"}, F{"size", "65536", "size_t"}}, "domain", "root_field");
    pgDlg.connectPointerField("sim", "domain", "0x7fff2000"); pgDlg.connectPointerField("sim", "ring_head", "0x7fff3000");
    pgDlg.connectPointerField("domain", "root_field", "0x7fff4000"); pgDlg.connectPointerField("ring_head", "next", "0x7fff3100");
    pgDlg.connectPointerField("ring_peer", "next", "0x7fff3000"); pgDlg.autoLayoutNodes();
    pgDlg.show(); QApplication::processEvents();
    if (!pgDlg.grab().save(dir.filePath(QStringLiteral("pointer_graph.png")), "PNG", 88)) return false;
  }

  return AttachProcessDialog::captureDocumentationPreviews(this, outputDir);
}

}  // namespace gridlock::ui
