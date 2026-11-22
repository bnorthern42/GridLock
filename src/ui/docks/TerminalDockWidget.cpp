/**
 * @file TerminalDockWidget.cpp
 * @brief Implementation of the embedded terminal emulator dock widget.
 *
 * Implements shell initialization, VT100 typography management, delayed font
 * application, and terminal color scheme bindings wrapping QTermWidget.
 */

#include "TerminalDockWidget.hpp"
#include "../../core/managers/ConfigManager.hpp"
#include "../../core/managers/ThemeManager.hpp"

#include <QAction>
#include <QApplication>
#include <QChildEvent>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QCursor>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontInfo>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSizePolicy>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <csignal>
#include <sys/wait.h>
#include <qtermwidget.h>

namespace gridlock::ui {

/**
 * @brief Resolves a strict fixed-pitch monospace font, falling back to guaranteed system monospace fonts.
 * @param family Candidate font family name.
 * @param pointSize Desired point size.
 * @return Validated fixed-pitch QFont instance.
 */
static QFont resolveMonospaceFont(const QString& family, int pointSize) {
    QFont font(family, pointSize);
    font.setFixedPitch(true); font.setStyleHint(QFont::Monospace);
    if (!QFontInfo(font).fixedPitch()) {
        font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        if (pointSize > 0) font.setPointSize(pointSize);
        font.setFixedPitch(true); font.setStyleHint(QFont::Monospace);
        if (!QFontInfo(font).fixedPitch()) {
            static const QStringList fallbackFamilies = {QStringLiteral("Liberation Mono"), QStringLiteral("JetBrains Mono"), QStringLiteral("Hack")};
            for (const QString& fam : fallbackFamilies) {
                QFont candidate(fam, pointSize > 0 ? pointSize : 11);
                candidate.setFixedPitch(true); candidate.setStyleHint(QFont::Monospace);
                if (QFontInfo(candidate).fixedPitch()) { font = candidate; break; }
            }
        }
    }
    return font;
}

/**
 * @brief Constructs the terminal widget, launches shell, and configures environment.
 * @param title Dock window title string.
 * @param parent Optional parent widget.
 */
TerminalDockWidget::TerminalDockWidget(const QString& title, QWidget* parent) : QWidget(parent) {
    Q_UNUSED(title);
    // Layout without interior padding for edge-to-edge terminal display.
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(0);

    // Instantiate QTermWidget backend without starting shell process yet.
    m_termWidget = new QTermWidget(0, this);
    m_termWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_termWidget->setStyleSheet(QStringLiteral(
        "QTermWidget, QTermWidget QWidget { padding: 0px; margin: 0px; border: none; font-family: monospace; }"));

    const auto ts = gridlock::core::ConfigManager::instance().getTerminalSettings();
    m_termWidget->setShellProgram(ts.shellPath);

    // Set default standard terminal environment parameters.
    setEnvironment({QStringLiteral("TERM=xterm-256color"), QStringLiteral("COLORTERM=truecolor")});
    setColorScheme(QStringLiteral("DarkPastels"));
    m_termWidget->startShellProgram();

    m_activeFont = resolveMonospaceFont(ts.fontFamily, ts.fontSize);
    setContextMenuPolicy(Qt::CustomContextMenu);
    m_termWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) { showTerminalContextMenu(mapToGlobal(pos)); });
    connect(m_termWidget, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) { showTerminalContextMenu(m_termWidget->mapToGlobal(pos)); });
    installEventFilter(this);
    layout->addWidget(m_termWidget, 1);
}

/** @brief Disconnects QTermWidget signals and terminates the PTY shell process before child destruction. */
TerminalDockWidget::~TerminalDockWidget() {
    removeEventFilter(this);
    shutdownTerminalSession();
    delete m_termWidget; m_termWidget = nullptr;
}

int TerminalDockWidget::shellPid() const {
    if (!m_termWidget) return 0;
    const int pid = m_termWidget->getShellPID();
    if (pid <= 0 || ::kill(pid, 0) != 0) return 0;
    QFile statusFile(QStringLiteral("/proc/%1/status").arg(pid));
    if (statusFile.open(QIODevice::ReadOnly) && statusFile.readAll().contains("State:\tZ")) return 0;
    return pid;
}

void TerminalDockWidget::shutdownTerminalSession() {
    if (!m_termWidget) return;
    m_termWidget->disconnect(this);
    for (QObject* child : m_termWidget->findChildren<QObject*>()) if (child) child->disconnect(this);
    const int pid = m_termWidget->getShellPID();
    const auto ptyProcs = m_termWidget->findChildren<QProcess*>();
    bool anyRunning = (pid > 0 && ::kill(pid, 0) == 0);
    for (QProcess* proc : ptyProcs) if (proc && proc->state() != QProcess::NotRunning) anyRunning = true;
    if (anyRunning) {
        m_termWidget->sendText(QStringLiteral("\x03\x04\nexit\n"));
        if (pid > 0) { ::kill(-pid, SIGTERM); ::kill(pid, SIGTERM); }
        for (QProcess* proc : ptyProcs) if (proc && proc->state() != QProcess::NotRunning) proc->waitForFinished(120);
        if (ptyProcs.isEmpty() && pid > 0) for (int i = 0; i < 12 && ::kill(pid, 0) == 0; ++i) QThread::msleep(10);
        if (pid > 0 && ::kill(pid, 0) == 0) { ::kill(-pid, SIGKILL); ::kill(pid, SIGKILL); }
        for (QProcess* proc : ptyProcs) if (proc && proc->state() != QProcess::NotRunning) { proc->kill(); proc->waitForFinished(150); }
        if (pid > 0) { int st = 0; ::waitpid(pid, &st, WNOHANG); }
    }
    m_termWidget->blockSignals(true);
}

bool TerminalDockWidget::hasSelection() const {
    return m_termWidget && !m_termWidget->selectedText(true).isEmpty();
}

void TerminalDockWidget::copySelection() {
    if (!m_termWidget) return;
    const QString sel = m_termWidget->selectedText(true);
    if (sel.isEmpty()) return;
    m_termWidget->copyClipboard();
    if (QClipboard* cb = QGuiApplication::clipboard()) {
        if (cb->text() != sel) cb->setText(sel);
    }
}

void TerminalDockWidget::pasteFromClipboard() {
    if (!m_termWidget) return;
    QClipboard* cb = QGuiApplication::clipboard();
    const QString clip = cb ? cb->text() : QString();
    if (clip.isEmpty()) return;
    m_lastPastedText = clip;
    ensureShellStarted();
    m_termWidget->pasteClipboard();
}

void TerminalDockWidget::selectAllText() {
    if (!m_termWidget) return;
    const int totalRows = std::max(0, m_termWidget->historyLinesCount() + m_termWidget->screenLinesCount() - 1);
    const int cols = std::max(0, m_termWidget->screenColumnsCount());
    m_termWidget->setSelectionStart(0, 0);
    m_termWidget->setSelectionEnd(totalRows, cols);
}

void TerminalDockWidget::clearTerminal() {
    if (!m_termWidget) return;
    m_termWidget->clear();
    ensureShellStarted();
    m_lastClearSentFormFeed = false;
    if (shellPid() > 0) {
        // Send a clean newline (never Ctrl+L / \x0c) so zsh/bash prints a fresh prompt without triggering ZLE expansion widgets.
        m_termWidget->sendText(QStringLiteral("\n"));
    }
    m_termWidget->setFocus(Qt::OtherFocusReason);
}

QMenu* TerminalDockWidget::createContextMenu(QWidget* parent) {
    auto* menu = new QMenu(parent ? parent : this);
    menu->setObjectName(QStringLiteral("terminalContextMenu"));
    menu->setWindowFlags(menu->windowFlags() | Qt::Popup);
    QFont menuFont = QApplication::font("QMenu");
    if (menuFont.pointSize() <= 0) menuFont.setPointSize(10);
    menu->setFont(menuFont);
    const auto pal = gridlock::core::managers::ThemeManager::paletteForTheme(
        gridlock::core::managers::ThemeManager::instance().activeThemeId());
    const QString disabledHex = pal.isDark ? QStringLiteral("#768390") : QStringLiteral("#8c959f");
    menu->setStyleSheet(QStringLiteral(
        "QMenu#terminalContextMenu {"
        " background-color: %1; color: %2; border: 1px solid %3;"
        " padding: 4px 0px; margin: 0px; min-width: 0px; min-height: 0px; font-size: 13px;"
        "}"
        "QMenu#terminalContextMenu::item {"
        " padding: 5px 20px; margin: 0px; min-height: 0px; background: transparent; color: %2;"
        "}"
        "QMenu#terminalContextMenu::item:selected { background-color: %4; color: #ffffff; }"
        "QMenu#terminalContextMenu::item:disabled { color: %5; background: transparent; }"
        "QMenu#terminalContextMenu::separator { height: 1px; background: %3; margin: 4px 8px; }")
                            .arg(pal.headerHex, pal.textHex, pal.borderHex, pal.accentHex, disabledHex));

    QAction* copyAct = menu->addAction(tr("Copy"));
    copyAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C));
    copyAct->setShortcutVisibleInContextMenu(true);
    copyAct->setEnabled(hasSelection());
    connect(copyAct, &QAction::triggered, this, &TerminalDockWidget::copySelection);

    QAction* pasteAct = menu->addAction(tr("Paste"));
    pasteAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V));
    pasteAct->setShortcutVisibleInContextMenu(true);
    const QClipboard* cb = QGuiApplication::clipboard();
    pasteAct->setEnabled(cb != nullptr && (!cb->text().isEmpty() || cb->mimeData() != nullptr));
    connect(pasteAct, &QAction::triggered, this, &TerminalDockWidget::pasteFromClipboard);

    QAction* selectAllAct = menu->addAction(tr("Select All"));
    connect(selectAllAct, &QAction::triggered, this, &TerminalDockWidget::selectAllText);
    menu->addSeparator();
    QAction* clearAct = menu->addAction(tr("Clear Terminal"));
    connect(clearAct, &QAction::triggered, this, &TerminalDockWidget::clearTerminal);

    menu->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    menu->setMinimumSize(0, 0);
    menu->setMaximumSize(320, 260);
    menu->adjustSize();
    return menu;
}

void TerminalDockWidget::showTerminalContextMenu(const QPoint& globalPos) {
    if (m_showingContextMenu) return;
    m_showingContextMenu = true;
    QMenu* menu = createContextMenu(this);
    menu->adjustSize();
    menu->exec(globalPos.isNull() ? QCursor::pos() : globalPos);
    menu->deleteLater();
    m_showingContextMenu = false;
}

void TerminalDockWidget::ensureShellStarted() {
    if (m_shellStarted || !m_termWidget) return;
    m_shellStarted = true;
    enforceMonospaceOnChildren();
}

void TerminalDockWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    ensureShellStarted();
}

/**
 * @brief Re-applies validated monospace typography and full-width geometry across QTermWidget and its children.
 */
void TerminalDockWidget::enforceMonospaceOnChildren() {
    if (!m_activeFont.fixedPitch() && !QFontInfo(m_activeFont).fixedPitch()) {
        m_activeFont = resolveMonospaceFont(QStringLiteral("JetBrains Mono"), 11);
    }
    m_activeFont.setFixedPitch(true);
    m_activeFont.setStyleHint(QFont::Monospace);
    setFont(m_activeFont);
    if (!m_termWidget) return;

    m_termWidget->installEventFilter(this);
    m_termWidget->setFont(m_activeFont);
    m_termWidget->setTerminalFont(m_activeFont);
    m_termWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    const int parentW = width() > 24 ? width() : m_termWidget->width();
    const int parentH = height() > 24 ? height() : m_termWidget->height();
    if (parentW > 24 && m_termWidget->width() < parentW) m_termWidget->setGeometry(0, 0, parentW, parentH);

    // Lock fixed-pitch font, context-menu policy, and full-width geometry strictly on non-window QTermWidget child widgets.
    for (QWidget* child : m_termWidget->findChildren<QWidget*>()) {
        if (!child || child->isWindow() || qobject_cast<QMenu*>(child)) continue;
        child->installEventFilter(this);
        child->setContextMenuPolicy(Qt::CustomContextMenu);
        child->setFont(m_activeFont);
        const QString cls = QString::fromLatin1(child->metaObject()->className());
        if ((cls.contains(QStringLiteral("TerminalDisplay"), Qt::CaseInsensitive) || child->width() > 80) &&
            parentW > 80 && child->width() < (parentW * 95) / 100) {
            child->setGeometry(0, 0, parentW, parentH);
        }
    }
    m_termWidget->updateGeometry();
    m_termWidget->update();
}

/**
 * @brief Intercepts keyboard copy/paste shortcuts, ShortcutOverride, ContextMenu, and font/geometry events.
 * @param watched Target QObject receiving the event.
 * @param event Event payload.
 * @return True if the event was handled and consumed; otherwise base QWidget::eventFilter result.
 */
bool TerminalDockWidget::eventFilter(QObject* watched, QEvent* event) {
    if (auto* w = qobject_cast<QWidget*>(watched); w && (qobject_cast<QMenu*>(w) || (w != this && w->isWindow()))) {
        return QWidget::eventFilter(watched, event);
    }
    if (event != nullptr) {
        const QEvent::Type t = event->type();
        if (t == QEvent::ShortcutOverride || t == QEvent::KeyPress) {
            auto* ke = static_cast<QKeyEvent*>(event);
            const Qt::KeyboardModifiers mods = ke->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier);
            const bool isCtrlShiftC = (mods == (Qt::ControlModifier | Qt::ShiftModifier) && ke->key() == Qt::Key_C);
            const bool isCtrlShiftV = (mods == (Qt::ControlModifier | Qt::ShiftModifier) && ke->key() == Qt::Key_V);
            const bool isShiftInsert = (mods == Qt::ShiftModifier && ke->key() == Qt::Key_Insert);
            const bool isCtrlC = (mods == Qt::ControlModifier && ke->key() == Qt::Key_C);
            const bool isCtrlV = (mods == Qt::ControlModifier && ke->key() == Qt::Key_V);
            if (t == QEvent::ShortcutOverride) {
                if (isCtrlShiftC || isCtrlShiftV || isShiftInsert || isCtrlC || isCtrlV) { ke->accept(); return true; }
            } else {
                if (isCtrlShiftC) { copySelection(); ke->accept(); return true; }
                if (isCtrlShiftV || isShiftInsert || isCtrlV) { pasteFromClipboard(); ke->accept(); return true; }
                if (isCtrlC && hasSelection()) { copySelection(); ke->accept(); return true; }
            }
        } else if (t == QEvent::ContextMenu) {
            auto* ce = static_cast<QContextMenuEvent*>(event);
            showTerminalContextMenu(ce->globalPos());
            ce->accept();
            return true;
        }
    }

    if (!m_applyingFontGuard && event != nullptr) {
        const QEvent::Type t = event->type();
        if (t == QEvent::ChildAdded) {
            if (auto* childWidget = qobject_cast<QWidget*>(static_cast<QChildEvent*>(event)->child());
                childWidget && !childWidget->isWindow() && !qobject_cast<QMenu*>(childWidget)) {
                childWidget->installEventFilter(this);
                m_applyingFontGuard = true; enforceMonospaceOnChildren(); m_applyingFontGuard = false;
            }
        } else if (t == QEvent::FontChange || t == QEvent::ApplicationFontChange || t == QEvent::StyleChange ||
                   t == QEvent::Polish || t == QEvent::Show || t == QEvent::Resize) {
            m_applyingFontGuard = true; enforceMonospaceOnChildren(); m_applyingFontGuard = false;
        }
    }
    return QWidget::eventFilter(watched, event);
}

/**
 * @brief Verifies that QTermWidget and all internal display child widgets retain a fixed-pitch font.
 * @return True if every child widget inside TerminalDockWidget reports fixed-pitch typography.
 */
bool TerminalDockWidget::areAllChildWidgetsFixedPitch() const {
    if (!m_termWidget) return m_activeFont.fixedPitch() || QFontInfo(m_activeFont).fixedPitch();
    const_cast<TerminalDockWidget*>(this)->enforceMonospaceOnChildren();
    for (const QWidget* child : m_termWidget->findChildren<const QWidget*>()) {
        if (!child || child->isWindow() || qobject_cast<const QMenu*>(child)) continue;
        if (!child->font().fixedPitch() && !QFontInfo(child->font()).fixedPitch()) return false;
    }
    return m_termWidget->getTerminalFont().fixedPitch() || QFontInfo(m_termWidget->getTerminalFont()).fixedPitch();
}

/**
 * @brief Configures terminal font family and point size with monospace fallback.
 * @param family Font family name.
 * @param pointSize Point size in points.
 */
void TerminalDockWidget::setTerminalFont(const QString& family, int pointSize) {
    QFont font(family, pointSize);
    font.setFixedPitch(true); font.setStyleHint(QFont::Monospace);
    m_requestedFont = font; m_activeFont = resolveMonospaceFont(family, pointSize);
    m_applyingFontGuard = true; enforceMonospaceOnChildren(); m_applyingFontGuard = false;
}

/**
 * @brief Custom font setter validating fixed-pitch metrics before applying.
 * @param family Candidate font family.
 * @param pointSize Font size.
 */
void TerminalDockWidget::setCustomFont(const QString& family, int pointSize) { setTerminalFont(family, pointSize); }

/**
 * @brief Retrieves the active validated fixed-pitch terminal font.
 * @return Current fixed-pitch QFont instance.
 */
QFont TerminalDockWidget::activeTerminalFont() const {
    if (m_activeFont.fixedPitch() || QFontInfo(m_activeFont).fixedPitch()) return m_activeFont;
    return m_termWidget ? m_termWidget->getTerminalFont() : m_activeFont;
}

/**
 * @brief Retrieves the active terminal emulator font.
 * @return Current QFont instance.
 */
QFont TerminalDockWidget::terminalFont() const { return !m_requestedFont.family().isEmpty() ? m_requestedFont : activeTerminalFont(); }

/**
 * @brief Sets the active QTermWidget color scheme.
 * @param scheme Name of color scheme.
 */
void TerminalDockWidget::setColorScheme(const QString& scheme) { m_colorScheme = scheme; if (m_termWidget) m_termWidget->setColorScheme(scheme); }

/**
 * @brief Retrieves the current color scheme identifier.
 * @return Color scheme name.
 */
QString TerminalDockWidget::colorScheme() const { return m_colorScheme; }

/**
 * @brief Sets the maximum scrollback buffer history size.
 * @param lines Number of history lines.
 */
void TerminalDockWidget::setScrollbackLines(int lines) { m_scrollbackLines = lines; if (m_termWidget) m_termWidget->setHistorySize(lines); }

/**
 * @brief Retrieves the active scrollback history buffer capacity.
 * @return Line count.
 */
int TerminalDockWidget::scrollbackLines() const { return m_termWidget ? m_termWidget->historySize() : m_scrollbackLines; }

/**
 * @brief Injects custom environment variable strings into the terminal shell.
 * @param env Array of environment definitions.
 */
void TerminalDockWidget::setEnvironment(const QStringList& env) { m_environment = env; if (m_termWidget) m_termWidget->setEnvironment(env); }

/**
 * @brief Retrieves the environment array configured on the terminal.
 * @return List of environment variable strings.
 */
QStringList TerminalDockWidget::environment() const { return m_environment; }

/**
 * @brief Dispatches a shell command to the embedded QTermWidget session and appends a newline.
 * @param command Shell command string to execute in the terminal.
 */
void TerminalDockWidget::sendCommand(const QString& command) {
    m_lastSentCommand = command.trimmed();
    ensureShellStarted();
    if (m_termWidget != nullptr && !m_lastSentCommand.isEmpty() && m_termWidget->getShellPID() > 0) {
        if (m_pendingCdSync && !m_currentWorkingDir.isEmpty()) {
            m_pendingCdSync = false;
            QString escaped = m_currentWorkingDir;
            escaped.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
            m_termWidget->sendText(QStringLiteral("cd -- '%1' && %2\n").arg(escaped, m_lastSentCommand));
        } else {
            // Dispatch command text followed by newline to trigger interactive shell execution.
            m_termWidget->sendText(m_lastSentCommand + QStringLiteral("\n"));
        }
    }
}

/**
 * @brief Updates the terminal working directory and changes the live PTY shell directory.
 * @param dirPath Target directory (or file path whose parent directory is resolved).
 */
void TerminalDockWidget::setWorkingDirectory(const QString& dirPath) {
    const QString trimmed = dirPath.trimmed();
    if (trimmed.isEmpty()) return;
    const QFileInfo rawInfo(trimmed);
    if (!rawInfo.exists()) return;
    const QFileInfo dirInfo(rawInfo.isDir() ? rawInfo.absoluteFilePath() : rawInfo.absolutePath());
    const QString canonical = !dirInfo.canonicalFilePath().isEmpty() ? dirInfo.canonicalFilePath() : QDir::cleanPath(dirInfo.absoluteFilePath());
    if (canonical.isEmpty() || !QDir(canonical).exists() || canonical == m_currentWorkingDir) return;
    m_currentWorkingDir = canonical;
    m_pendingCdSync = true;
    if (m_termWidget != nullptr) {
        m_termWidget->setWorkingDirectory(m_currentWorkingDir);
        if (m_termWidget->getShellPID() > 0) {
            // Escape single quotes for POSIX sh/bash/zsh and switch the live interactive shell directory.
            QString escaped = m_currentWorkingDir;
            escaped.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
            m_termWidget->sendText(QStringLiteral("cd -- '%1'\n").arg(escaped));
        }
    }
}

}  // namespace gridlock::ui
