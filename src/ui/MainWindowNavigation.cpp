#include "MainWindow.hpp"
#include "../core/hpc/DapCoordinator.hpp"
#include "../core/hpc/GdbRankCoordinator.hpp"
#include "../core/managers/ConfigManager.hpp"
#include "../core/managers/SessionManager.hpp"
#include "EditorTabManager.hpp"
#include "tutorial/TutorialDialog.hpp"
#include "views/DisassemblyView.hpp"
#include "views/MemView.hpp"
#include "views/SourceCodeView.hpp"
#include "widgets/DifferentialGrid.hpp"
#include "widgets/MpiDiagnosticsWidget.hpp"
#include "widgets/MpiNetworkLogWidget.hpp"
#include "widgets/ProjectExplorerWidget.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QPixmap>
#include <QProcess>
#include <QPushButton>
#include <QScreen>
#include <QTextCursor>
#include <QTextDocument>
#include <QToolTip>
#include <QWindow>
#include <cmath>

namespace gridlock::ui {

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
  if (m_preferredMonitorHint.isEmpty()) {
    setAttribute(Qt::WA_ShowWithoutActivating, false);
    return;
  }
  setAttribute(Qt::WA_ShowWithoutActivating, openUnfocused);
  if (QScreen *target = resolvePreferredScreen(m_preferredMonitorHint)) {
    if (target != QGuiApplication::primaryScreen()) {
      setScreen(target);
      if (windowHandle()) {
        windowHandle()->setScreen(target);
      }
      move(target->availableGeometry().topLeft());
    }
  }
}

bool MainWindow::canNavigateBack() const { return m_seekHistoryIndex > 0; }

bool MainWindow::canNavigateForward() const {
  return m_seekHistoryIndex >= 0 &&
         m_seekHistoryIndex + 1 < m_seekHistory.size();
}

void MainWindow::updateNavigationButtons() {
  if (m_navBackBtn) {
    m_navBackBtn->setEnabled(canNavigateBack());
  }
  if (m_navForwardBtn) {
    m_navForwardBtn->setEnabled(canNavigateForward());
  }
}

bool MainWindow::navigateBack() {
  if (!canNavigateBack()) {
    return false;
  }
  --m_seekHistoryIndex;
  m_isNavigatingHistory = true;
  const bool ok =
      seekToAddressOrExpression(m_seekHistory.at(m_seekHistoryIndex));
  m_isNavigatingHistory = false;
  updateNavigationButtons();
  return ok;
}

bool MainWindow::navigateForward() {
  if (!canNavigateForward()) {
    return false;
  }
  ++m_seekHistoryIndex;
  m_isNavigatingHistory = true;
  const bool ok =
      seekToAddressOrExpression(m_seekHistory.at(m_seekHistoryIndex));
  m_isNavigatingHistory = false;
  updateNavigationButtons();
  return ok;
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

  bool hexOk = false;
  QString hexPart = normalized;
  if (hexPart.startsWith("0x", Qt::CaseInsensitive)) {
    hexPart = hexPart.mid(2);
  }
  const uint64_t parsedAddr = hexPart.toULongLong(&hexOk, 16);

  if ((!hexOk || parsedAddr == 0ULL) && m_projectExplorerWidget) {
    if (m_projectExplorerWidget->activateSymbol(cleaned)) {
      return true;
    }
    if (m_projectExplorerWidget->activateString(cleaned)) {
      return true;
    }
  }

  if (m_seekBar && m_seekBar->text() != normalized) {
    m_seekBar->setText(normalized);
  }

  updateStatusTelemetry(normalized);

  if (m_addressNavBar && hexOk && parsedAddr != 0ULL) {
    m_addressNavBar->setProperty("currentAddress",
                                 QVariant::fromValue<qulonglong>(parsedAddr));
    m_addressNavBar->update();
  }

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

  if (m_disassemblyView) {
    if (hexOk && parsedAddr != 0ULL) {
      const bool hasSourceMapping =
          (parsedAddr >= 0x00401120ULL && parsedAddr <= 0x004011ffULL) &&
          !m_forceDecompilerFallback;
      m_disassemblyView->seekToAddress(parsedAddr);
      if (!hasSourceMapping) {
        m_showingDecompilerFallback = true;
        m_disassemblyView->setSplitGraphDecompilerMode(true);
      } else if (m_showingDecompilerFallback) {
        m_showingDecompilerFallback = false;
        m_disassemblyView->setSplitGraphDecompilerMode(false);
        m_disassemblyView->setGraphMode(false);
      }
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

  if (!m_isNavigatingHistory) {
    if (m_seekHistoryIndex >= 0 &&
        m_seekHistoryIndex + 1 < m_seekHistory.size()) {
      m_seekHistory.resize(m_seekHistoryIndex + 1);
    }
    if (m_seekHistory.isEmpty() ||
        m_seekHistory.last().compare(normalized, Qt::CaseInsensitive) != 0) {
      m_seekHistory.append(normalized);
      m_seekHistoryIndex = m_seekHistory.size() - 1;
    }
    updateNavigationButtons();
  }

  return true;
}

void MainWindow::enableDecompilerFallback(bool enabled) {
  m_forceDecompilerFallback = enabled;
  m_showingDecompilerFallback = enabled;
  if (m_disassemblyView) {
    m_disassemblyView->setSplitGraphDecompilerMode(enabled);
  }
}

void MainWindow::saveSessionAs() {
  QString filePath = QFileDialog::getSaveFileName(
      this, "Save Session", QDir::currentPath(), "TOML Files (*.toml)");
  if (filePath.isEmpty())
    return;

  gridlock::core::managers::SessionState state;

  if (m_differentialGrid) {
    for (const auto &w : m_differentialGrid->getWatchExpressions()) {
      state.watchedVariables.push_back(QString::fromStdString(w));
    }
  }

  for (auto it = m_persistentBreakpoints.constBegin();
       it != m_persistentBreakpoints.constEnd(); ++it) {
    for (int line : *it) {
      state.activeBreakpoints.push_back(
          QString("%1:%2").arg(it.key()).arg(line));
    }
  }

  state.selectedRanks.push_back(m_focusedRank);

  gridlock::core::managers::SessionManager::instance().saveSession(filePath,
                                                                   state);
}

void MainWindow::loadSession() {
  QString filePath = QFileDialog::getOpenFileName(
      this, "Load Session", QDir::currentPath(), "TOML Files (*.toml)");
  if (filePath.isEmpty())
    return;

  auto optState =
      gridlock::core::managers::SessionManager::instance().loadSession(
          filePath);
  if (!optState)
    return;

  const auto &state = *optState;

  if (m_differentialGrid)
    m_differentialGrid->clearWatches();
  for (const auto &w : state.watchedVariables) {
    if (m_differentialGrid)
      m_differentialGrid->addVariableColumn(w);
  }

  m_persistentBreakpoints.clear();
  for (const auto &bp : state.activeBreakpoints) {
    auto parts = bp.split(':');
    if (parts.size() == 2) {
      QString file = parts[0];
      int line = parts[1].toInt();
      m_persistentBreakpoints[file].insert(line);

      if (m_editorTabManager) {
        if (auto *view = m_editorTabManager->getSourceCodeViewForFile(file)) {
          view->setBreakpoints(m_persistentBreakpoints[file]);
        }
      }

      if (auto *gdbCoord =
              dynamic_cast<gridlock::GdbRankCoordinator *>(m_coordinator)) {
        gdbCoord->broadcastBreakpoint(file, line, true, QString());
      } else if (auto *dapCoord =
                     dynamic_cast<DapCoordinator *>(m_coordinator)) {
        dapCoord->toggleBreakpoint(file, line);
      }
    }
  }

  if (!state.selectedRanks.empty()) {
    m_focusedRank = state.selectedRanks.front();
    onRankSelected(m_focusedRank);
  }
}

void MainWindow::onTutorialLaunchRequested(const QString &absoluteFilePath) {
  loadSourceFile(absoluteFilePath);

  QFileInfo fi(absoluteFilePath);
  if (m_projectExplorerWidget) {
    m_projectExplorerWidget->setRootPath(fi.absolutePath());
  }

  QString compiler = fi.suffix() == "c" ? "mpicc" : "mpic++";
  QString outDir = QDir::tempPath();
  QString outBin = outDir + "/" + fi.baseName() + "_demo";

  QProcess proc;
  proc.start(compiler,
             QStringList() << absoluteFilePath << "-g" << "-O0" << "-o" << outBin);
  proc.waitForFinished();

  if (proc.exitCode() == 0) {
    m_persistentBreakpoints =
        gridlock::core::ConfigManager::instance().getBreakpoints();

    if (m_editorTabManager) {
      if (auto *view =
              m_editorTabManager->getSourceCodeViewForFile(absoluteFilePath)) {
        view->setBreakpoints(m_persistentBreakpoints[absoluteFilePath]);
      }
    }

    if (m_coordinator) {
      if (auto *gdbCoord =
              dynamic_cast<gridlock::GdbRankCoordinator *>(m_coordinator)) {
        for (int line : m_persistentBreakpoints[absoluteFilePath]) {
          gdbCoord->broadcastBreakpoint(absoluteFilePath, line, true,
                                        QString());
        }
      } else if (auto *dapCoord =
                     dynamic_cast<DapCoordinator *>(m_coordinator)) {
        for (int line : m_persistentBreakpoints[absoluteFilePath]) {
          dapCoord->toggleBreakpoint(absoluteFilePath, line);
        }
      }
    }

    int ranks = (fi.baseName() == "deadlock_demo") ? 2 : 3;
    startDebuggingSession(outBin, ranks);
  } else {
    if (networkLog()) {
      networkLog()->appendError("Tutorial compilation failed:\n" +
                                proc.readAllStandardError());
    }
  }
}

bool MainWindow::execTutorialDialog() {
  gridlock::ui::TutorialDialog dialog(this);
  QObject::connect(&dialog,
                   &gridlock::ui::TutorialDialog::launchTutorialRequested, this,
                   &gridlock::ui::MainWindow::onTutorialLaunchRequested);
  if (dialog.exec() == QDialog::Accepted) {
    show();
    return true;
  }
  return false;
}

} // namespace gridlock::ui
