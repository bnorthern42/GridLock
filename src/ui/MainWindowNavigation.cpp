/**
 * @file MainWindowNavigation.cpp
 * @brief Implementation of navigation history, address routing, and workspace state I/O.
 *
 * Implements address seek history traversal (back/forward navigation stack),
 * address space ribbon bar visualization, multi-screen monitor routing,
 * workspace snapshot capturing, source/session file opening, and tutorial launching.
 */

#include "MainWindow.hpp"
#include "../core/hpc/DapCoordinator.hpp"
#include "../core/hpc/GdbRankCoordinator.hpp"
#include "../core/hpc/LspCoordinator.hpp"
#include "../core/managers/ConfigManager.hpp"
#include "../core/managers/SessionManager.hpp"
#include "../core/managers/ThemeManager.hpp"
#include "EditorTabManager.hpp"
#include "dialogs/PreferencesDialog.hpp"
#include "docks/TerminalDockWidget.hpp"
#include "tutorial/TutorialDialog.hpp"
#include "views/DisassemblyView.hpp"
#include "views/MemView.hpp"
#include "views/RegisterView.hpp"
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
#include <QPainter>
#include <QPixmap>
#include <QProcess>
#include <QPushButton>
#include <QScreen>
#include <QTextCursor>
#include <QTextDocument>
#include <QToolTip>
#include <QVariant>
#include <QWindow>
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace gridlock::ui {

namespace {

/**
 * @class AddressSpaceNavBar
 * @brief Interactive visual ribbon depicting memory and binary section segments.
 *
 * Visualizes .plt, .text, .rodata, .data, and stack sections with proportional
 * weights, highlighting the active instruction pointer / seek address marker.
 */
class AddressSpaceNavBar : public QWidget {
public:
    /**
     * @struct Segment
     * @brief Describes a discrete memory region with address bounds and color accents.
     */
    struct Segment {
        QString name;
        uint64_t baseAddr;
        uint64_t endAddr;
        int weight;
        QColor fillColor;
        QColor textColor;
    };

    /**
     * @brief Constructs the navigation ribbon with standard ELF sections.
     * @param onSeek Callback invoked when user clicks a memory segment.
     * @param parent Optional parent QWidget.
     */
    explicit AddressSpaceNavBar(std::function<void(uint64_t)> onSeek,
                                QWidget* parent = nullptr)
        : QWidget(parent), m_onSeek(std::move(onSeek)) {
        setObjectName("addressNavBar");
        setFixedHeight(20);
        setCursor(Qt::PointingHandCursor);
        // Pre-configure standard binary section topology for x86_64 executables.
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

    /**
     * @brief Updates the active instruction pointer / seek address indicator.
     * @param addr Memory address in hex.
     */
    void setCurrentAddress(uint64_t addr) {
        if (addr != 0ULL) {
            m_currentAddr = addr;
            setProperty("currentAddress", QVariant::fromValue<qulonglong>(addr));
            update();
        }
    }

protected:
    /**
     * @brief Renders proportional segment blocks and gold position needles.
     * @param event Paint event payload.
     */
    void paintEvent(QPaintEvent* /*event*/) override {
        // Synchronize local current address with dynamic Qt property if modified externally.
        const QVariant prop = property("currentAddress");
        if (prop.isValid()) {
            const uint64_t propAddr = prop.toULongLong();
            if (propAddr != 0ULL) {
                m_currentAddr = propAddr;
            }
        }
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, false);
        p.fillRect(rect(), QColor("#141619"));

        // Sum relative weights to distribute ribbon horizontal width proportionally.
        int totalWeight = 0;
        for (const auto& seg : m_segments) {
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

        // Iterate over memory segments and draw tinted rectangular bounding boxes.
        for (size_t i = 0; i < m_segments.size(); ++i) {
            const auto& seg = m_segments[i];
            const int segW = (i + 1 == m_segments.size())
                                 ? (w - x)
                                 : (w * seg.weight) / totalWeight;
            QRect segRect(x, 1, segW - 1, h - 2);
            p.fillRect(segRect, seg.fillColor);
            p.setPen(QColor("#2b2b2b"));
            p.drawRect(segRect.adjusted(0, 0, -1, -1));

            // Render section label text within segment bounds.
            p.setPen(seg.textColor);
            p.drawText(segRect.adjusted(6, 0, -6, 0),
                       Qt::AlignVCenter | Qt::AlignLeft, seg.name);

            // Calculate precise cursor coordinate if active address falls inside segment.
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

        // Fallback needle placement if address does not match known segments.
        if (activeCursorX < 0 && !m_segments.empty()) {
            activeCursorX = (w * m_segments[0].weight) / totalWeight + 18;
        }

        // Render yellow indicator line and dual arrow heads at active cursor position.
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

    /**
     * @brief Translates user mouse clicks on ribbon segments into address seek events.
     * @param event Mouse event containing click coordinates.
     */
    void mousePressEvent(QMouseEvent* event) override {
        // Calculate cumulative segment weights to match horizontal layout.
        int totalWeight = 0;
        for (const auto& seg : m_segments) {
            totalWeight += seg.weight;
        }
        const int w = std::max(1, width());
        int x = 0;
        // Identify which segment was clicked based on local X position.
        for (size_t i = 0; i < m_segments.size(); ++i) {
            const auto& seg = m_segments[i];
            const int segW = (i + 1 == m_segments.size())
                                 ? (w - x)
                                 : (w * seg.weight) / totalWeight;
            if (event->position().x() >= x && event->position().x() <= x + segW) {
                // Update internal seek marker and notify callback of newly selected base address.
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

}  // namespace

/**
 * @brief Constructs and configures the interactive address-space ribbon widget.
 * @param window The hosting MainWindow instance.
 * @param parent Optional parent widget (e.g. toolbar).
 * @return Fully configured QWidget pointer.
 */
QWidget* createAddressSpaceNavBar(MainWindow* window, QWidget* parent) {
    return new AddressSpaceNavBar(
        [window](uint64_t addr) {
            // Seek to hex representation of chosen memory segment base address.
            window->seekToAddressOrExpression(
                QStringLiteral("0x%1").arg(addr, 8, 16, QChar('0')));
        },
        parent);
}

/**
 * @brief Renders the entire workspace offscreen to a high-resolution PNG snapshot.
 * @param outputPath Target file system path for output image.
 * @return True on clean image grab and save, false on failure.
 */
bool MainWindow::captureWorkspaceSnapshot(const QString& outputPath) {
    // Suppress ephemeral tooltips and hover popovers before capturing pixels.
    QToolTip::hideText();
    hideHoverTooltipPopup();
    for (QWidget* topLevel : QApplication::topLevelWidgets()) {
        if (topLevel != this && (topLevel->windowFlags() & Qt::ToolTip)) {
            topLevel->hide();
        }
    }
    // Remove internal debug size labels from snapshot output.
    for (QLabel* lbl : findChildren<QLabel*>()) {
        if (lbl->text().startsWith("Size:")) {
            lbl->hide();
        }
    }
    // Force layout activation and flush pending paint events.
    ensurePolished();
    if (layout()) {
        layout()->activate();
    }
    QCoreApplication::processEvents();
    // Grab entire widget hierarchy backing store pixmap.
    const QPixmap pixmap = grab();
    if (pixmap.isNull()) {
        return false;
    }
    // Write rasterized pixmap directly to PNG file on disk.
    return pixmap.save(outputPath, "PNG");
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
        if (!scr) {
            continue;
        }
        const QRect geom = scr->geometry();
        const QSizeF phys = scr->physicalSize();
        const int diagInches =
            qRound(std::hypot(phys.width(), phys.height()) / 25.4);
        const QString meta = QString("%1 %2 %3 %4x%5 %6\" %6in")
                                 .arg(scr->name(), scr->model(), scr->manufacturer())
                                 .arg(geom.width())
                                 .arg(geom.height())
                                 .arg(diagInches);
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
void MainWindow::applyPreferredScreenRouting(const QString& monitorHint,
                                             bool openUnfocused) {
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
    return m_seekHistoryIndex >= 0 &&
           m_seekHistoryIndex + 1 < m_seekHistory.size();
}

/**
 * @brief Synchronizes enabled/disabled state of toolbar navigation buttons.
 */
void MainWindow::updateNavigationButtons() {
    if (m_navBackBtn) {
        m_navBackBtn->setEnabled(canNavigateBack());
    }
    if (m_navForwardBtn) {
        m_navForwardBtn->setEnabled(canNavigateForward());
    }
}

/**
 * @brief Navigates backward in the address seek history stack.
 * @return True if previous address was reached and synchronized.
 */
bool MainWindow::navigateBack() {
    if (!canNavigateBack()) {
        return false;
    }
    // Decrement history cursor and seek without appending duplicate entries.
    --m_seekHistoryIndex;
    m_isNavigatingHistory = true;
    const bool ok =
        seekToAddressOrExpression(m_seekHistory.at(m_seekHistoryIndex));
    m_isNavigatingHistory = false;
    updateNavigationButtons();
    return ok;
}

/**
 * @brief Navigates forward in the address seek history stack.
 * @return True if next address was reached and synchronized.
 */
bool MainWindow::navigateForward() {
    if (!canNavigateForward()) {
        return false;
    }
    // Increment history cursor and seek without appending duplicate entries.
    ++m_seekHistoryIndex;
    m_isNavigatingHistory = true;
    const bool ok =
        seekToAddressOrExpression(m_seekHistory.at(m_seekHistoryIndex));
    m_isNavigatingHistory = false;
    updateNavigationButtons();
    return ok;
}

/**
 * @brief Synchronizes all inspection views to an address or symbol expression.
 * @param expr Hexadecimal address, symbol, or string query.
 * @return True if target resolved and views updated.
 */
bool MainWindow::seekToAddressOrExpression(const QString& expr) {
    const QString cleaned = expr.trimmed();
    if (cleaned.isEmpty()) {
        return false;
    }

    // Auto-prefix hexadecimal values missing the "0x" notation.
    QString normalized = cleaned;
    bool isHexWithoutPrefix = false;
    if (!normalized.startsWith("0x", Qt::CaseInsensitive)) {
        (void)normalized.toULongLong(&isHexWithoutPrefix, 16);
        if (isHexWithoutPrefix && normalized.length() >= 4) {
            normalized = QStringLiteral("0x") + normalized;
        }
    }

    // Parse unsigned 64-bit integer address representation.
    bool hexOk = false;
    QString hexPart = normalized;
    if (hexPart.startsWith("0x", Qt::CaseInsensitive)) {
        hexPart = hexPart.mid(2);
    }
    const uint64_t parsedAddr = hexPart.toULongLong(&hexOk, 16);

    // Try symbol and string table lookups first if expression is non-numeric.
    if ((!hexOk || parsedAddr == 0ULL) && m_projectExplorerWidget) {
        if (m_projectExplorerWidget->activateSymbol(cleaned)) {
            return true;
        }
        if (m_projectExplorerWidget->activateString(cleaned)) {
            return true;
        }
    }

    // Synchronize global seek input field text.
    if (m_seekBar && m_seekBar->text() != normalized) {
        m_seekBar->setText(normalized);
    }

    // Update status bar breadcrumb with active seek address.
    updateStatusTelemetry(normalized);

    // Update address ribbon marker position.
    if (m_addressNavBar && hexOk && parsedAddr != 0ULL) {
        m_addressNavBar->setProperty("currentAddress",
                                     QVariant::fromValue<qulonglong>(parsedAddr));
        m_addressNavBar->update();
    }

    // Synchronize Hexdump memory viewport.
    if (m_memView) {
        if (hexOk && parsedAddr != 0ULL) {
            m_memView->seekToAddress(parsedAddr);
        }
        if (auto* addrEdit = m_memView->findChild<QLineEdit*>()) {
            addrEdit->setText(normalized);
        }
        // Trigger memory read request over coordinator if target is running/paused.
        if (m_coordinator && m_currentTargetState != QStringLiteral("IDLE")) {
            m_coordinator->readMemory(m_focusedRank, normalized, 256);
        }
    }

    // Synchronize Disassembly and Decompiler graph/linear viewports.
    if (m_disassemblyView) {
        if (hexOk && parsedAddr != 0ULL) {
            // Determine if source code mapping exists or if fallback decompiler is required.
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
            // Find text token within disassembly buffer when not numeric address.
            QTextDocument* doc = m_disassemblyView->document();
            if (doc) {
                QTextCursor found = doc->find(normalized);
                if (!found.isNull()) {
                    m_disassemblyView->setTextCursor(found);
                    m_disassemblyView->centerCursor();
                }
            }
        }
    }

    // Push address to navigation history stack if not navigating history.
    if (!m_isNavigatingHistory) {
        // Truncate forward history if seeking from middle of history stack.
        if (m_seekHistoryIndex >= 0 &&
            m_seekHistoryIndex + 1 < m_seekHistory.size()) {
            m_seekHistory.resize(m_seekHistoryIndex + 1);
        }
        // Append new unique seek target.
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

/**
 * @brief Prompts user for source file via file dialog and opens it.
 */
void MainWindow::openFile() {
    auto& cfg = gridlock::core::ConfigManager::instance();
    const QString startDir = cfg.getLastOpenDir();

    const QString fileName =
        QFileDialog::getOpenFileName(this, tr("Open Source File"), startDir,
                                     tr("C/C++ Files (*.cpp *.hpp *.h *.c)"),
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
    if (m_editorTabManager) {
        if (auto* view = m_editorTabManager->openFile(filePath)) {
            m_currentFile = filePath;
            const QString absolutePath = QFileInfo(m_currentFile).absoluteFilePath();
            if (m_lspCoordinator) {
                m_lspCoordinator->stop();
                m_lspCoordinator->start(absolutePath);
                m_lspCoordinator->didOpen(m_currentFile, view->getPlainText());
            }
            if (networkLog()) {
                networkLog()->appendText("Successfully loaded: " + m_currentFile +
                                         "\n");
            }
        } else {
            if (networkLog()) {
                networkLog()->appendError("CRITICAL: Failed to load " + filePath +
                                          "\n");
            }
        }
    }
}

/**
 * @brief Spawns modal Preferences dialog and hooks live theme/font reloading.
 */
void MainWindow::openPreferences() {
    // Construct preferences dialog instance configured to self-delete upon dialog closure.
    auto* dlg = new PreferencesDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);

    // Reapply application themes, code editor styling, and terminal fonts when preferences change.
    connect(dlg, &PreferencesDialog::preferencesChanged, this, [this]() {
        // Refresh project explorer file system tree styling.
        if (m_projectExplorerWidget) {
            m_projectExplorerWidget->reloadStyle();
        }

        // Apply global theme color palettes and typography rules.
        gridlock::core::managers::ThemeManager::instance().applyThemeAndFonts();

        // Update embedded terminal font family and size metrics.
        if (m_terminalDock) {
            const auto ts =
                gridlock::core::ConfigManager::instance().getTerminalSettings();
            m_terminalDock->setTerminalFont(ts.fontFamily, ts.fontSize);
        }

        // Propagate updated font and color configurations across all open source editor tabs.
        if (m_editorTabManager) {
            for (int i = 0; i < m_editorTabManager->count(); ++i) {
                if (auto* view =
                        qobject_cast<SourceCodeView*>(m_editorTabManager->widget(i))) {
                    view->reloadStyle();
                }
            }
        }
        // Force repaint on hardware register and disassembly viewports.
        if (m_registerView) {
            m_registerView->update();
        }
        if (m_disassemblyView) {
            m_disassemblyView->reloadStyle();
        }
    });

    dlg->exec();
}

/**
 * @brief Saves current session breakpoints, watch variables, and active ranks to TOML.
 */
void MainWindow::saveSessionAs() {
    // Prompt user for output TOML session destination path.
    const QString filePath = QFileDialog::getSaveFileName(
        this, "Save Session", QDir::currentPath(), "TOML Files (*.toml)");
    if (filePath.isEmpty()) {
        return;
    }

    gridlock::core::managers::SessionState state;

    // Serialize watched variable expressions currently present in differential grid.
    if (m_differentialGrid) {
        for (const auto& w : m_differentialGrid->getWatchExpressions()) {
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

/**
 * @brief Loads a persisted TOML debug session file and synchronizes views.
 */
void MainWindow::loadSession() {
    const QString filePath = QFileDialog::getOpenFileName(
        this, "Load Session", QDir::currentPath(), "TOML Files (*.toml)");
    if (filePath.isEmpty()) {
        return;
    }

    const auto optState =
        gridlock::core::managers::SessionManager::instance().loadSession(filePath);
    if (!optState) {
        return;
    }

    const auto& state = *optState;

    if (m_differentialGrid) {
        m_differentialGrid->clearWatches();
    }
    for (const auto& w : state.watchedVariables) {
        if (m_differentialGrid) {
            m_differentialGrid->addVariableColumn(w);
        }
    }

    m_persistentBreakpoints.clear();
    for (const auto& bp : state.activeBreakpoints) {
        const auto parts = bp.split(':');
        if (parts.size() == 2) {
            const QString file = parts[0];
            const int line = parts[1].toInt();
            m_persistentBreakpoints[file].insert(line);

            if (m_editorTabManager) {
                if (auto* view =
                        m_editorTabManager->getSourceCodeViewForFile(file)) {
                    view->setBreakpoints(m_persistentBreakpoints[file]);
                }
            }

            if (auto* gdbCoord =
                    dynamic_cast<gridlock::GdbRankCoordinator*>(m_coordinator)) {
                gdbCoord->broadcastBreakpoint(file, line, true, QString());
            } else if (auto* dapCoord =
                           dynamic_cast<DapCoordinator*>(m_coordinator)) {
                dapCoord->toggleBreakpoint(file, line);
            }
        }
    }

    if (!state.selectedRanks.empty()) {
        m_focusedRank = state.selectedRanks.front();
        onRankSelected(m_focusedRank);
    }
}

/**
 * @brief Compiles tutorial demo target and initiates multi-rank debug session.
 * @param absoluteFilePath Path to the tutorial C/C++ source file.
 */
void MainWindow::onTutorialLaunchRequested(const QString& absoluteFilePath) {
    loadSourceFile(absoluteFilePath);

    const QFileInfo fi(absoluteFilePath);
    if (m_projectExplorerWidget) {
        m_projectExplorerWidget->setRootPath(fi.absolutePath());
    }

    const QString compiler = fi.suffix() == "c" ? "mpicc" : "mpic++";
    const QString outDir = QDir::tempPath();
    const QString outBin = outDir + "/" + fi.baseName() + "_demo";

    QProcess proc;
    proc.start(compiler, QStringList() << absoluteFilePath << "-g" << "-O0"
                                       << "-o" << outBin);
    proc.waitForFinished();

    if (proc.exitCode() == 0) {
        m_persistentBreakpoints =
            gridlock::core::ConfigManager::instance().getBreakpoints();

        if (m_editorTabManager) {
            if (auto* view =
                    m_editorTabManager->getSourceCodeViewForFile(absoluteFilePath)) {
                view->setBreakpoints(m_persistentBreakpoints[absoluteFilePath]);
            }
        }

        if (m_coordinator) {
            if (auto* gdbCoord =
                    dynamic_cast<gridlock::GdbRankCoordinator*>(m_coordinator)) {
                for (int line : m_persistentBreakpoints[absoluteFilePath]) {
                    gdbCoord->broadcastBreakpoint(absoluteFilePath, line, true,
                                                  QString());
                }
            } else if (auto* dapCoord =
                           dynamic_cast<DapCoordinator*>(m_coordinator)) {
                for (int line : m_persistentBreakpoints[absoluteFilePath]) {
                    dapCoord->toggleBreakpoint(absoluteFilePath, line);
                }
            }
        }

        const int ranks = (fi.baseName() == "deadlock_demo") ? 2 : 3;
        startDebuggingSession(outBin, ranks);
    } else {
        if (networkLog()) {
            networkLog()->appendError("Tutorial compilation failed:\n" +
                                      proc.readAllStandardError());
        }
    }
}

/**
 * @brief Executes interactive modal tutorial dialog.
 * @return True if tutorial accepted and launched, false if rejected.
 */
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

}  // namespace gridlock::ui
