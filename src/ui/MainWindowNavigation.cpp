/**
 * @file MainWindowNavigation.cpp
 * @brief Source/session file I/O, dialog launchers, workspace snapshot capture, and tutorial launch flows.
 *
 * Implements PNG workspace snapshot capturing, source file opening and ProjectExplorer
 * CRUD synchronization, Preferences/Search Everywhere/Programmer Calculator dialog
 * triggers, TOML session and recent-workspace persistence, and tutorial compilation/launching.
 */

#include "MainWindow.hpp"

#include <algorithm>
#include <optional>

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLayout>
#include <QMenu>
#include <QPixmap>
#include <QProcess>
#include <QToolTip>

#include "../core/hpc/dap/DapCoordinator.hpp"
#include "../core/hpc/gdb/GdbRankCoordinator.hpp"
#include "../core/hpc/lsp/LspCoordinator.hpp"
#include "../core/managers/ConfigManager.hpp"
#include "../core/managers/SessionManager.hpp"
#include "../core/managers/ThemeManager.hpp"
#include "../core/utils/ClangdLspClient.hpp"
#include "EditorTabManager.hpp"
#include "dialogs/calculator/ProgrammerCalculatorDialog.hpp"
#include "dialogs/preferences/PreferencesDialog.hpp"
#include "dialogs/search/CommandPaletteDialog.hpp"
#include "docks/TerminalDockWidget.hpp"
#include "tutorial/TutorialDialog.hpp"
#include "views/DisassemblyView.hpp"
#include "views/RegisterView.hpp"
#include "views/SourceCodeView.hpp"
#include "widgets/DifferentialGrid.hpp"
#include "widgets/MpiNetworkLogWidget.hpp"
#include "widgets/ProjectExplorerWidget.hpp"

namespace gridlock::ui {

/**
 * @brief Renders the entire workspace offscreen to a high-resolution PNG snapshot.
 * @param outputPath Target file system path for output image.
 * @return True on clean image grab and save, false on failure.
 */
bool MainWindow::captureWorkspaceSnapshot(const QString& outputPath) {
    QToolTip::hideText();
    hideHoverTooltipPopup();
    for (QWidget* topLevel : QApplication::topLevelWidgets()) {
        if (topLevel != this && (topLevel->windowFlags() & Qt::ToolTip)) {
            topLevel->hide();
        }
    }
    for (QLabel* lbl : findChildren<QLabel*>()) {
        if (lbl->text().startsWith("Size:")) {
            lbl->hide();
        }
    }
    ensurePolished();
    if (layout()) {
        layout()->activate();
    }
    QCoreApplication::processEvents();
    const QPixmap pixmap = grab();
    if (pixmap.isNull()) {
        return false;
    }
    return pixmap.save(outputPath, "PNG");
}

/** @brief Prompts user for a source file via file dialog and opens it. */
void MainWindow::openFile() {
    auto& cfg = gridlock::core::ConfigManager::instance();
    const QString startDir = cfg.getLastOpenDir();

    const QString fileName = QFileDialog::getOpenFileName(
        this, tr("Open Source File"), startDir, tr("C/C++ Files (*.cpp *.hpp *.h *.c)"),
        nullptr, QFileDialog::DontUseNativeDialog);

    if (!fileName.isEmpty()) {
        cfg.setLastOpenDir(QFileInfo(fileName).absolutePath());
        loadSourceFile(fileName);
    }
}

/**
 * @brief Loads a source file into EditorTabManager and activates Clangd LSP.
 * @param filePath Path to file on disk.
 */
void MainWindow::loadSourceFile(const QString& filePath) {
    // Ensure ProjectExplorerWidget CRUD signals (fileCreated, fileRenamed, fileDeleted) are connected once.
    if (m_projectExplorerWidget && !m_projectExplorerWidget->property("crudWired").toBool()) {
        m_projectExplorerWidget->setProperty("crudWired", true);
        connect(m_projectExplorerWidget, &ProjectExplorerWidget::fileCreated, this,
                [this](const QString& path) { loadSourceFile(path); });
        connect(m_projectExplorerWidget, &ProjectExplorerWidget::fileRenamed, this,
                [this](const QString& oldPath, const QString& newPath) {
                    if (m_editorTabManager) {
                        if (auto* v = m_editorTabManager->getSourceCodeViewForFile(oldPath)) {
                            const int idx = m_editorTabManager->indexOf(v);
                            if (idx >= 0) emit m_editorTabManager->tabCloseRequested(idx);
                            loadSourceFile(newPath);
                        }
                    }
                });
        connect(m_projectExplorerWidget, &ProjectExplorerWidget::fileDeleted, this,
                [this](const QString& path) {
                    if (m_editorTabManager) {
                        if (auto* v = m_editorTabManager->getSourceCodeViewForFile(path)) {
                            const int idx = m_editorTabManager->indexOf(v);
                            if (idx >= 0) emit m_editorTabManager->tabCloseRequested(idx);
                        }
                    }
                });
    }
    // Open the requested source file in EditorTabManager and synchronize live Clangd LSP state.
    if (m_editorTabManager) {
        if (auto* view = m_editorTabManager->openFile(filePath)) {
            m_currentFile = !view->currentFile().isEmpty() ? view->currentFile() : filePath;
            syncTabBreakpointMetadata(view);
            view->setActiveFocusRank(m_focusedRank);
            const QString absolutePath = QFileInfo(m_currentFile).absoluteFilePath();
            const auto cachedDiags = gridlock::core::ClangdLspClient::instance().cachedDiagnosticsForFile(absolutePath);
            if (!cachedDiags.isEmpty() && view->diagnostics().isEmpty()) {
                view->setDiagnostics(cachedDiags);
            }
            if (m_lspCoordinator) m_lspCoordinator->stop();
            syncDisassemblyDecompilerVisibility(false);
        } else if (m_editorTabManager->primaryTabWidget() && m_editorTabManager->primaryTabWidget()->currentWidget() &&
                   !m_editorTabManager->primaryTabWidget()->currentWidget()->property("absolutePath").toString().isEmpty()) {
            m_currentFile = m_editorTabManager->primaryTabWidget()->currentWidget()->property("absolutePath").toString();
        } else if (networkLog()) {
            networkLog()->appendError("CRITICAL: Failed to load " + filePath + "\n");
        }
    }
}

/** @brief Spawns modal Preferences dialog and hooks live theme/font reloading. */
void MainWindow::openPreferences() {
    auto* dlg = new PreferencesDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);

    connect(dlg, &PreferencesDialog::preferencesChanged, this, [this]() {
        if (m_projectExplorerWidget) m_projectExplorerWidget->reloadStyle();
        gridlock::core::managers::ThemeManager::instance().applyThemeAndFonts();
        if (m_terminalDock) {
            const auto ts = gridlock::core::ConfigManager::instance().getTerminalSettings();
            m_terminalDock->setCustomFont(ts.fontFamily, ts.fontSize);
        }
        if (m_editorTabManager) {
            for (int i = 0; i < m_editorTabManager->count(); ++i) {
                if (auto* view = qobject_cast<SourceCodeView*>(m_editorTabManager->widget(i))) {
                    view->reloadStyle();
                }
            }
        }
        if (m_registerView) m_registerView->update();
        if (m_disassemblyView) m_disassemblyView->reloadStyle();
    });

    dlg->exec();
}

/** @brief Opens the unified Command Palette / Search Everywhere dialog with an optional prefix. */
void MainWindow::openCommandPalette(const QString& initialPrefix) {
    if (!m_commandPaletteDialog) m_commandPaletteDialog = new CommandPaletteDialog(this);
    m_commandPaletteDialog->populateFromMainWindow(this);
    m_commandPaletteDialog->openWithPrefix(initialPrefix);
}

/** @brief Opens Search Everywhere directly in the requested scope tab (`All`, `Files`, `Symbols`, `Text`, `Actions`, `Ranks`). */
void MainWindow::openSearchEverywhere(int scope, const QString& initialQuery) {
    if (!m_commandPaletteDialog) m_commandPaletteDialog = new CommandPaletteDialog(this);
    m_commandPaletteDialog->populateFromMainWindow(this);
    m_commandPaletteDialog->openWithScope(static_cast<CommandPaletteDialog::Scope>(std::clamp(scope, 0, 5)), initialQuery);
}

/** @brief Opens and focuses the modeless Programmer's Calculator dialog. */
void MainWindow::openProgrammerCalculator() {
    if (!m_programmerCalculatorDialog) m_programmerCalculatorDialog = new ProgrammerCalculatorDialog(this);
    m_programmerCalculatorDialog->reloadStyle();
    m_programmerCalculatorDialog->show();
    m_programmerCalculatorDialog->raise();
    m_programmerCalculatorDialog->activateWindow();
}

/** @brief Opens the Programmer's Calculator and seeds it with @p text. */
void MainWindow::openProgrammerCalculatorWithText(const QString& text) {
    openProgrammerCalculator();
    if (m_programmerCalculatorDialog) m_programmerCalculatorDialog->setInitialTextOrValue(text);
}

/** @brief Saves current session breakpoints, watch variables, and active ranks to TOML. */
void MainWindow::saveSessionAs() {
    const QString filePath = QFileDialog::getSaveFileName(
        this, "Save Session", QDir::currentPath(), "TOML Files (*.toml)");
    if (filePath.isEmpty()) return;

    gridlock::core::managers::SessionState state;
    if (m_differentialGrid) {
        for (const auto& w : m_differentialGrid->getWatchExpressions()) {
            state.watchedVariables.push_back(QString::fromStdString(w));
        }
    }
    for (auto it = m_persistentBreakpoints.constBegin(); it != m_persistentBreakpoints.constEnd(); ++it) {
        for (int line : *it) {
            state.activeBreakpoints.push_back(QString("%1:%2").arg(it.key()).arg(line));
        }
    }
    state.selectedRanks.push_back(m_focusedRank);
    gridlock::core::managers::SessionManager::instance().saveSession(filePath, state);
}

/** @brief Loads a persisted TOML debug session file and synchronizes views. */
void MainWindow::loadSession() {
    const QString filePath = QFileDialog::getOpenFileName(
        this, "Load Session", QDir::currentPath(), "TOML Files (*.toml)");
    if (filePath.isEmpty()) return;

    const auto optState = gridlock::core::managers::SessionManager::instance().loadSession(filePath);
    if (!optState) return;
    const auto& state = *optState;

    if (m_differentialGrid) {
        m_differentialGrid->clearWatches();
        for (const auto& w : state.watchedVariables) {
            m_differentialGrid->addVariableColumn(w);
        }
    }

    m_persistentBreakpoints.clear();
    for (const auto& bp : state.activeBreakpoints) {
        const auto parts = bp.split(':');
        if (parts.size() == 2) {
            const QString& file = parts[0];
            const int line = parts[1].toInt();
            m_persistentBreakpoints[file].insert(line);
            if (m_editorTabManager) {
                if (auto* view = m_editorTabManager->getSourceCodeViewForFile(file)) {
                    view->setBreakpoints(m_persistentBreakpoints[file]);
                }
            }
            if (auto* gdbCoord = dynamic_cast<gridlock::GdbRankCoordinator*>(m_coordinator)) {
                gdbCoord->broadcastBreakpoint(file, line, true, QString());
            } else if (auto* dapCoord = dynamic_cast<DapCoordinator*>(m_coordinator)) {
                dapCoord->toggleBreakpoint(file, line);
            }
        }
    }

    if (!state.selectedRanks.empty()) {
        m_focusedRank = state.selectedRanks.front();
        onRankSelected(m_focusedRank);
    }
}

/** @brief Records a workspace or TOML session path into recent-session history and refreshes File -> Recent Sessions. */
void MainWindow::recordRecentWorkspace(const QString& path) {
    if (!path.isEmpty()) {
        gridlock::core::ConfigManager::instance().recordRecentSession(path);
        gridlock::core::managers::SessionManager::instance().addRecentSession(path);
    }
    rebuildRecentSessionsMenu();
}

/** @brief Rebuilds the File -> Recent Sessions submenu from ConfigManager and SessionManager history. */
void MainWindow::rebuildRecentSessionsMenu() {
    if (!m_recentSessionsMenu) return;
    m_recentSessionsMenu->clear();
    QStringList entries = gridlock::core::ConfigManager::instance().recentSessions();
    for (const QString& p : gridlock::core::managers::SessionManager::instance().recentSessions()) {
        if (QFileInfo::exists(p) && !entries.contains(p) && entries.size() < 10) entries.append(p);
    }
    for (const QString& path : entries) {
        const QFileInfo fi(path);
        QString label = fi.isDir() ? QDir(path).dirName() : fi.completeBaseName();
        label.replace(QLatin1Char('_'), QLatin1Char(' '));
        if (label.trimmed().isEmpty()) label = QStringLiteral("Workspace Session");
        QAction* act = m_recentSessionsMenu->addAction(label);
        act->setData(path);
        act->setToolTip(path);
        connect(act, &QAction::triggered, this, [this, path]() {
            if (QFileInfo(path).isFile() && !path.endsWith(QStringLiteral(".toml"), Qt::CaseInsensitive)) {
                loadBinaryTarget(path);
                return;
            }
            const QString toml = QFileInfo(path).isDir() ? QDir(path).filePath(QStringLiteral(".gridlock/workspace.toml")) : path;
            const auto opt = QFileInfo::exists(toml) ? gridlock::core::managers::SessionManager::instance().loadSession(toml) : std::nullopt;
            if (!opt.has_value()) {
                switchWorkspaceDirectory(path);
                return;
            }
            if (m_differentialGrid) {
                m_differentialGrid->clearWatches();
                for (const auto& w : opt->watchedVariables) m_differentialGrid->addVariableColumn(w);
            }
            m_persistentBreakpoints.clear();
            for (const auto& bp : opt->activeBreakpoints) {
                const auto p = bp.split(':');
                if (p.size() != 2) continue;
                m_persistentBreakpoints[p[0]].insert(p[1].toInt());
                if (m_editorTabManager) {
                    if (auto* v = m_editorTabManager->getSourceCodeViewForFile(p[0])) {
                        v->setBreakpoints(m_persistentBreakpoints[p[0]]);
                    }
                }
            }
        });
    }
    if (entries.isEmpty()) {
        m_recentSessionsMenu->addAction(QStringLiteral("(No Recent Sessions)"))->setEnabled(false);
        return;
    }
    m_recentSessionsMenu->addSeparator();
    connect(m_recentSessionsMenu->addAction(QStringLiteral("Clear Recent Sessions")), &QAction::triggered, this, [this]() {
        gridlock::core::ConfigManager::instance().clearRecentSessions();
        gridlock::core::managers::SessionManager::instance().clearRecentSessions();
        rebuildRecentSessionsMenu();
    });
}

/**
 * @brief Compiles tutorial demo target and initiates multi-rank debug session.
 * @param absoluteFilePath Path to the tutorial C/C++ source file.
 */
void MainWindow::onTutorialLaunchRequested(const QString& absoluteFilePath) {
    loadSourceFile(absoluteFilePath);

    const QFileInfo fi(absoluteFilePath);
    if (m_projectExplorerWidget) m_projectExplorerWidget->setRootPath(fi.absolutePath());

    const QString compiler = fi.suffix() == "c" ? "mpicc" : "mpic++";
    const QString outBin = QDir::tempPath() + "/" + fi.baseName() + "_demo";

    QProcess proc;
    proc.start(compiler, QStringList() << absoluteFilePath << "-g" << "-O0" << "-o" << outBin);
    proc.waitForFinished();

    if (proc.exitCode() == 0) {
        m_persistentBreakpoints = gridlock::core::ConfigManager::instance().getBreakpoints();
        if (m_editorTabManager) {
            if (auto* view = m_editorTabManager->getSourceCodeViewForFile(absoluteFilePath)) {
                view->setBreakpoints(m_persistentBreakpoints[absoluteFilePath]);
            }
        }
        if (m_coordinator) {
            if (auto* gdbCoord = dynamic_cast<gridlock::GdbRankCoordinator*>(m_coordinator)) {
                for (int line : m_persistentBreakpoints[absoluteFilePath]) {
                    gdbCoord->broadcastBreakpoint(absoluteFilePath, line, true, QString());
                }
            } else if (auto* dapCoord = dynamic_cast<DapCoordinator*>(m_coordinator)) {
                for (int line : m_persistentBreakpoints[absoluteFilePath]) {
                    dapCoord->toggleBreakpoint(absoluteFilePath, line);
                }
            }
        }
        const int ranks = (fi.baseName() == "deadlock_demo") ? 2 : 3;
        startDebuggingSession(outBin, ranks);
    } else if (networkLog()) {
        networkLog()->appendError("Tutorial compilation failed:\n" + proc.readAllStandardError());
    }
}

/** @brief Executes interactive modal tutorial dialog. */
bool MainWindow::execTutorialDialog() {
    gridlock::ui::TutorialDialog dialog(this);
    QObject::connect(&dialog, &gridlock::ui::TutorialDialog::launchTutorialRequested, this,
                     &gridlock::ui::MainWindow::onTutorialLaunchRequested);
    if (dialog.exec() == QDialog::Accepted) {
        show();
        return true;
    }
    return false;
}

}  // namespace gridlock::ui
