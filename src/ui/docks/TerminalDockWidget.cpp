/**
 * @file TerminalDockWidget.cpp
 * @brief Implementation of the embedded terminal emulator dock widget.
 *
 * Implements shell initialization, monospace typography management, color scheme
 * bindings, and copy/paste/clear ergonomics using QTermWidget when HAVE_QTERMWIDGET6 == 1
 * or a built-in Qt6 QPlainTextEdit + QProcess fallback terminal when HAVE_QTERMWIDGET6 == 0.
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
#include <QPlainTextEdit>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSizePolicy>
#include <QTextCursor>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <csignal>
#include <sys/wait.h>
#if defined(HAVE_QTERMWIDGET6) && HAVE_QTERMWIDGET6
#include <qtermwidget.h>
#endif

namespace gridlock::ui {

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

#if !(defined(HAVE_QTERMWIDGET6) && HAVE_QTERMWIDGET6)
static QString stripAnsiEscapeSequences(QString text) {
    static const QRegularExpression oscRe(QStringLiteral("\\x1b\\][^\\x07\\x1b]*(?:\\x07|\\x1b\\\\)"));
    static const QRegularExpression csiRe(QStringLiteral("\\x1b\\[[0-9;?]*[ -/]*[@-~]"));
    static const QRegularExpression singleEscRe(QStringLiteral("\\x1b[@-Z\\\\-_]"));
    text.remove(oscRe);
    text.remove(csiRe);
    text.remove(singleEscRe);
    text.remove(QLatin1Char('\r'));
    return text;
}
#endif

TerminalDockWidget::TerminalDockWidget(const QString& title, QWidget* parent) : QWidget(parent) {
    Q_UNUSED(title);
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(0);

    const auto ts = gridlock::core::ConfigManager::instance().getTerminalSettings();
    m_activeFont = resolveMonospaceFont(ts.fontFamily, ts.fontSize);

#if defined(HAVE_QTERMWIDGET6) && HAVE_QTERMWIDGET6
    m_termWidget = new QTermWidget(0, this);
    m_termWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_termWidget->setStyleSheet(QStringLiteral(
        "QTermWidget, QTermWidget QWidget { padding: 0px; margin: 0px; border: none; font-family: monospace; }"));
    m_termWidget->setShellProgram(ts.shellPath);
    setEnvironment({QStringLiteral("TERM=xterm-256color"), QStringLiteral("COLORTERM=truecolor")});
    setColorScheme(QStringLiteral("DarkPastels"));
    m_termWidget->startShellProgram();

    setContextMenuPolicy(Qt::CustomContextMenu);
    m_termWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) { showTerminalContextMenu(mapToGlobal(pos)); });
    connect(m_termWidget, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) { showTerminalContextMenu(m_termWidget->mapToGlobal(pos)); });
    installEventFilter(this);
    layout->addWidget(m_termWidget, 1);
#else
    m_shellProgram = ts.shellPath.trimmed().isEmpty() ? qEnvironmentVariable("SHELL", QStringLiteral("/bin/bash")) : ts.shellPath.trimmed();
    m_fallbackView = new QPlainTextEdit(this);
    m_fallbackView->setObjectName(QStringLiteral("fallbackTerminalView"));
    m_fallbackView->setFrameShape(QFrame::NoFrame);
    m_fallbackView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_fallbackView->setMaximumBlockCount(m_scrollbackLines);
    m_fallbackView->setUndoRedoEnabled(false);
    m_fallbackView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_fallbackView->installEventFilter(this);
    if (m_fallbackView->viewport()) m_fallbackView->viewport()->installEventFilter(this);

    setEnvironment({QStringLiteral("TERM=dumb"), QStringLiteral("PAGER=cat")});
    setColorScheme(QStringLiteral("DarkPastels"));
    startFallbackShell();

    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) { showTerminalContextMenu(mapToGlobal(pos)); });
    connect(m_fallbackView, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) { showTerminalContextMenu(m_fallbackView->mapToGlobal(pos)); });
    installEventFilter(this);
    layout->addWidget(m_fallbackView, 1);
    enforceMonospaceOnChildren();
#endif
}

TerminalDockWidget::~TerminalDockWidget() {
    removeEventFilter(this);
    shutdownTerminalSession();
#if defined(HAVE_QTERMWIDGET6) && HAVE_QTERMWIDGET6
    delete m_termWidget; m_termWidget = nullptr;
#else
    delete m_fallbackShell; m_fallbackShell = nullptr;
    delete m_fallbackView; m_fallbackView = nullptr;
#endif
}

#if !(defined(HAVE_QTERMWIDGET6) && HAVE_QTERMWIDGET6)
void TerminalDockWidget::startFallbackShell() {
    if (m_fallbackShell && m_fallbackShell->state() != QProcess::NotRunning) return;
    delete m_fallbackShell;
    m_fallbackShell = new QProcess(this);
    m_fallbackShell->setProcessChannelMode(QProcess::MergedChannels);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const QString& pair : m_environment) {
        if (const int eq = pair.indexOf(QLatin1Char('=')); eq > 0) {
            env.insert(pair.left(eq), pair.mid(eq + 1));
        }
    }
    m_fallbackShell->setProcessEnvironment(env);
    if (!m_currentWorkingDir.isEmpty() && QDir(m_currentWorkingDir).exists()) {
        m_fallbackShell->setWorkingDirectory(m_currentWorkingDir);
    }
    connect(m_fallbackShell, &QProcess::readyReadStandardOutput, this, [this]() {
        if (m_fallbackShell) appendFallbackOutput(m_fallbackShell->readAllStandardOutput());
    });
    const QString exe = (!m_shellProgram.isEmpty() && QFileInfo::exists(m_shellProgram))
                            ? m_shellProgram
                            : QStringLiteral("/bin/bash");
    m_fallbackShell->start(exe, {});
    m_fallbackShell->waitForStarted(1500);
}

void TerminalDockWidget::appendFallbackOutput(const QByteArray& rawBytes) {
    if (!m_fallbackView || rawBytes.isEmpty()) return;
    const QString cleaned = stripAnsiEscapeSequences(QString::fromUtf8(rawBytes));
    if (cleaned.isEmpty()) return;
    QTextCursor cur = m_fallbackView->textCursor();
    cur.movePosition(QTextCursor::End);
    cur.insertText(cleaned);
    m_inputStartPos = cur.position();
    m_fallbackView->setTextCursor(cur);
    if (QScrollBar* sb = m_fallbackView->verticalScrollBar()) sb->setValue(sb->maximum());
}
#endif

int TerminalDockWidget::shellPid() const {
#if defined(HAVE_QTERMWIDGET6) && HAVE_QTERMWIDGET6
    if (!m_termWidget) return 0;
    const int pid = m_termWidget->getShellPID();
#else
    if (!m_fallbackShell || m_fallbackShell->state() == QProcess::NotRunning) return 0;
    const int pid = static_cast<int>(m_fallbackShell->processId());
#endif
    if (pid <= 0 || ::kill(pid, 0) != 0) return 0;
    QFile statusFile(QStringLiteral("/proc/%1/status").arg(pid));
    if (statusFile.open(QIODevice::ReadOnly) && statusFile.readAll().contains("State:\tZ")) return 0;
    return pid;
}

void TerminalDockWidget::shutdownTerminalSession() {
#if defined(HAVE_QTERMWIDGET6) && HAVE_QTERMWIDGET6
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
#else
    if (!m_fallbackShell) return;
    m_fallbackShell->disconnect(this);
    if (m_fallbackShell->state() != QProcess::NotRunning) {
        m_fallbackShell->write("exit\n");
        m_fallbackShell->closeWriteChannel();
        if (!m_fallbackShell->waitForFinished(150)) {
            m_fallbackShell->terminate();
            if (!m_fallbackShell->waitForFinished(150)) {
                m_fallbackShell->kill();
                m_fallbackShell->waitForFinished(150);
            }
        }
    }
    m_fallbackShell->blockSignals(true);
#endif
}

bool TerminalDockWidget::hasSelection() const {
#if defined(HAVE_QTERMWIDGET6) && HAVE_QTERMWIDGET6
    return m_termWidget && !m_termWidget->selectedText(true).isEmpty();
#else
    return m_fallbackView && m_fallbackView->textCursor().hasSelection();
#endif
}

void TerminalDockWidget::copySelection() {
#if defined(HAVE_QTERMWIDGET6) && HAVE_QTERMWIDGET6
    if (!m_termWidget) return;
    const QString sel = m_termWidget->selectedText(true);
    if (sel.isEmpty()) return;
    m_termWidget->copyClipboard();
    if (QClipboard* cb = QGuiApplication::clipboard()) {
        if (cb->text() != sel) cb->setText(sel);
    }
#else
    if (!m_fallbackView || !m_fallbackView->textCursor().hasSelection()) return;
    m_fallbackView->copy();
#endif
}

void TerminalDockWidget::pasteFromClipboard() {
    QClipboard* cb = QGuiApplication::clipboard();
    const QString clip = cb ? cb->text() : QString();
    if (clip.isEmpty()) return;
    m_lastPastedText = clip;
    ensureShellStarted();
#if defined(HAVE_QTERMWIDGET6) && HAVE_QTERMWIDGET6
    if (m_termWidget) m_termWidget->pasteClipboard();
#else
    if (m_fallbackView) {
        QTextCursor cur = m_fallbackView->textCursor();
        cur.movePosition(QTextCursor::End);
        cur.insertText(clip);
        m_fallbackView->setTextCursor(cur);
    }
#endif
}

void TerminalDockWidget::selectAllText() {
#if defined(HAVE_QTERMWIDGET6) && HAVE_QTERMWIDGET6
    if (!m_termWidget) return;
    const int totalRows = std::max(0, m_termWidget->historyLinesCount() + m_termWidget->screenLinesCount() - 1);
    const int cols = std::max(0, m_termWidget->screenColumnsCount());
    m_termWidget->setSelectionStart(0, 0);
    m_termWidget->setSelectionEnd(totalRows, cols);
#else
    if (m_fallbackView) m_fallbackView->selectAll();
#endif
}

void TerminalDockWidget::clearTerminal() {
    m_lastClearSentFormFeed = false;
#if defined(HAVE_QTERMWIDGET6) && HAVE_QTERMWIDGET6
    if (!m_termWidget) return;
    m_termWidget->clear();
    ensureShellStarted();
    if (shellPid() > 0) m_termWidget->sendText(QStringLiteral("\n"));
    m_termWidget->setFocus(Qt::OtherFocusReason);
#else
    if (!m_fallbackView) return;
    m_fallbackView->clear();
    m_inputStartPos = 0;
    ensureShellStarted();
    if (m_fallbackShell && m_fallbackShell->state() == QProcess::Running) {
        m_fallbackShell->write("\n");
    }
    m_fallbackView->setFocus(Qt::OtherFocusReason);
#endif
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
#if defined(HAVE_QTERMWIDGET6) && HAVE_QTERMWIDGET6
    if (m_shellStarted || !m_termWidget) return;
    m_shellStarted = true;
    enforceMonospaceOnChildren();
#else
    if (!m_fallbackShell || m_fallbackShell->state() == QProcess::NotRunning) startFallbackShell();
    m_shellStarted = true;
    enforceMonospaceOnChildren();
#endif
}

void TerminalDockWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    ensureShellStarted();
}

void TerminalDockWidget::enforceMonospaceOnChildren() {
    if (!m_activeFont.fixedPitch() && !QFontInfo(m_activeFont).fixedPitch()) {
        m_activeFont = resolveMonospaceFont(QStringLiteral("JetBrains Mono"), 11);
    }
    m_activeFont.setFixedPitch(true);
    m_activeFont.setStyleHint(QFont::Monospace);
    setFont(m_activeFont);
#if defined(HAVE_QTERMWIDGET6) && HAVE_QTERMWIDGET6
    if (!m_termWidget) return;
    m_termWidget->installEventFilter(this);
    m_termWidget->setFont(m_activeFont);
    m_termWidget->setTerminalFont(m_activeFont);
    m_termWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    const int parentW = width() > 24 ? width() : m_termWidget->width();
    const int parentH = height() > 24 ? height() : m_termWidget->height();
    if (parentW > 24 && m_termWidget->width() < parentW) m_termWidget->setGeometry(0, 0, parentW, parentH);

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
#else
    if (!m_fallbackView) return;
    m_fallbackView->setFont(m_activeFont);
    if (m_fallbackView->viewport()) m_fallbackView->viewport()->setFont(m_activeFont);
    for (QWidget* child : m_fallbackView->findChildren<QWidget*>()) {
        if (!child || child->isWindow() || qobject_cast<QMenu*>(child)) continue;
        child->setFont(m_activeFont);
    }
#endif
}

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
#if !(defined(HAVE_QTERMWIDGET6) && HAVE_QTERMWIDGET6)
                if (isCtrlC && !hasSelection() && m_fallbackShell && m_fallbackShell->state() == QProcess::Running) {
                    const int pid = shellPid();
                    if (pid > 0) ::kill(pid, SIGINT);
                    ke->accept();
                    return true;
                }
                if (watched == m_fallbackView && (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) && mods == Qt::NoModifier) {
                    const QString allText = m_fallbackView->toPlainText();
                    const int start = qBound(0, m_inputStartPos, allText.size());
                    const QString cmdLine = allText.mid(start);
                    QTextCursor cur = m_fallbackView->textCursor();
                    cur.movePosition(QTextCursor::End);
                    cur.insertText(QStringLiteral("\n"));
                    m_inputStartPos = cur.position();
                    m_fallbackView->setTextCursor(cur);
                    sendCommand(cmdLine);
                    ke->accept();
                    return true;
                }
#endif
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

bool TerminalDockWidget::areAllChildWidgetsFixedPitch() const {
    const_cast<TerminalDockWidget*>(this)->enforceMonospaceOnChildren();
#if defined(HAVE_QTERMWIDGET6) && HAVE_QTERMWIDGET6
    if (!m_termWidget) return m_activeFont.fixedPitch() || QFontInfo(m_activeFont).fixedPitch();
    for (const QWidget* child : m_termWidget->findChildren<const QWidget*>()) {
        if (!child || child->isWindow() || qobject_cast<const QMenu*>(child)) continue;
        if (!child->font().fixedPitch() && !QFontInfo(child->font()).fixedPitch()) return false;
    }
    return m_termWidget->getTerminalFont().fixedPitch() || QFontInfo(m_termWidget->getTerminalFont()).fixedPitch();
#else
    if (!m_fallbackView) return m_activeFont.fixedPitch() || QFontInfo(m_activeFont).fixedPitch();
    return m_fallbackView->font().fixedPitch() || QFontInfo(m_fallbackView->font()).fixedPitch();
#endif
}

void TerminalDockWidget::setTerminalFont(const QString& family, int pointSize) {
    QFont font(family, pointSize);
    font.setFixedPitch(true); font.setStyleHint(QFont::Monospace);
    m_requestedFont = font; m_activeFont = resolveMonospaceFont(family, pointSize);
    m_applyingFontGuard = true; enforceMonospaceOnChildren(); m_applyingFontGuard = false;
}

void TerminalDockWidget::setCustomFont(const QString& family, int pointSize) { setTerminalFont(family, pointSize); }

QFont TerminalDockWidget::activeTerminalFont() const {
    if (m_activeFont.fixedPitch() || QFontInfo(m_activeFont).fixedPitch()) return m_activeFont;
#if defined(HAVE_QTERMWIDGET6) && HAVE_QTERMWIDGET6
    return m_termWidget ? m_termWidget->getTerminalFont() : m_activeFont;
#else
    return m_fallbackView ? m_fallbackView->font() : m_activeFont;
#endif
}

QFont TerminalDockWidget::terminalFont() const { return !m_requestedFont.family().isEmpty() ? m_requestedFont : activeTerminalFont(); }

void TerminalDockWidget::setColorScheme(const QString& scheme) {
    m_colorScheme = scheme;
#if defined(HAVE_QTERMWIDGET6) && HAVE_QTERMWIDGET6
    if (m_termWidget) m_termWidget->setColorScheme(scheme);
#else
    if (m_fallbackView) {
        const auto pal = gridlock::core::managers::ThemeManager::paletteForTheme(
            gridlock::core::managers::ThemeManager::instance().activeThemeId());
        m_fallbackView->setStyleSheet(QStringLiteral(
            "QPlainTextEdit#fallbackTerminalView { background-color: %1; color: %2; border: none; padding: 4px; }")
                                          .arg(pal.canvasHex, pal.textHex));
    }
#endif
}

QString TerminalDockWidget::colorScheme() const { return m_colorScheme; }

void TerminalDockWidget::setScrollbackLines(int lines) {
    m_scrollbackLines = lines;
#if defined(HAVE_QTERMWIDGET6) && HAVE_QTERMWIDGET6
    if (m_termWidget) m_termWidget->setHistorySize(lines);
#else
    if (m_fallbackView) m_fallbackView->setMaximumBlockCount(qMax(100, lines));
#endif
}

int TerminalDockWidget::scrollbackLines() const {
#if defined(HAVE_QTERMWIDGET6) && HAVE_QTERMWIDGET6
    return m_termWidget ? m_termWidget->historySize() : m_scrollbackLines;
#else
    return m_scrollbackLines;
#endif
}

void TerminalDockWidget::setEnvironment(const QStringList& env) {
    m_environment = env;
#if defined(HAVE_QTERMWIDGET6) && HAVE_QTERMWIDGET6
    if (m_termWidget) m_termWidget->setEnvironment(env);
#endif
}

QStringList TerminalDockWidget::environment() const { return m_environment; }

void TerminalDockWidget::sendCommand(const QString& command) {
    m_lastSentCommand = command.trimmed();
    ensureShellStarted();
    if (m_lastSentCommand.isEmpty()) return;
    QString payload;
    if (m_pendingCdSync && !m_currentWorkingDir.isEmpty()) {
        m_pendingCdSync = false;
        QString escaped = m_currentWorkingDir;
        escaped.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
        payload = QStringLiteral("cd -- '%1' && %2\n").arg(escaped, m_lastSentCommand);
    } else {
        payload = m_lastSentCommand + QStringLiteral("\n");
    }
#if defined(HAVE_QTERMWIDGET6) && HAVE_QTERMWIDGET6
    if (m_termWidget != nullptr && m_termWidget->getShellPID() > 0) m_termWidget->sendText(payload);
#else
    if (m_fallbackShell && m_fallbackShell->state() == QProcess::Running) {
        m_fallbackShell->write(payload.toUtf8());
    }
#endif
}

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
    QString escaped = m_currentWorkingDir;
    escaped.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
#if defined(HAVE_QTERMWIDGET6) && HAVE_QTERMWIDGET6
    if (m_termWidget != nullptr) {
        m_termWidget->setWorkingDirectory(m_currentWorkingDir);
        if (m_termWidget->getShellPID() > 0) m_termWidget->sendText(QStringLiteral("cd -- '%1'\n").arg(escaped));
    }
#else
    if (m_fallbackShell && m_fallbackShell->state() == QProcess::Running) {
        m_fallbackShell->write(QStringLiteral("cd -- '%1'\n").arg(escaped).toUtf8());
    }
#endif
}

}  // namespace gridlock::ui
