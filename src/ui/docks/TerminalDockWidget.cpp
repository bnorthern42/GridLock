/**
 * @file TerminalDockWidget.cpp
 * @brief Implementation of the embedded terminal emulator dock widget.
 *
 * Implements shell initialization, VT100 typography management, delayed font
 * application, and terminal color scheme bindings wrapping QTermWidget.
 */

#include "TerminalDockWidget.hpp"
#include "../../core/managers/ConfigManager.hpp"

#include <QApplication>
#include <QChildEvent>
#include <QEvent>
#include <QFontDatabase>
#include <QFontInfo>
#include <QProcessEnvironment>
#include <QSizePolicy>
#include <QTimer>
#include <QVBoxLayout>
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
    font.setFixedPitch(true);
    font.setStyleHint(QFont::Monospace);
    if (!QFontInfo(font).fixedPitch()) {
        font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        if (pointSize > 0) {
            font.setPointSize(pointSize);
        }
        font.setFixedPitch(true);
        font.setStyleHint(QFont::Monospace);
        if (!QFontInfo(font).fixedPitch()) {
            static const QStringList fallbackFamilies = {
                QStringLiteral("Liberation Mono"),
                QStringLiteral("JetBrains Mono"),
                QStringLiteral("Hack")
            };
            for (const QString& fam : fallbackFamilies) {
                QFont candidate(fam, pointSize > 0 ? pointSize : 11);
                candidate.setFixedPitch(true);
                candidate.setStyleHint(QFont::Monospace);
                if (QFontInfo(candidate).fixedPitch()) {
                    font = candidate;
                    break;
                }
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
TerminalDockWidget::TerminalDockWidget(const QString& title, QWidget* parent)
    : QWidget(parent) {
    Q_UNUSED(title);

    // Layout without interior padding for edge-to-edge terminal display.
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Instantiate QTermWidget backend.
    m_termWidget = new QTermWidget(0, this);
    m_termWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    // Isolate terminal canvas from external stylesheet cascading padding/margins.
    m_termWidget->setStyleSheet(QStringLiteral(
        "QTermWidget, QTermWidget QWidget { padding: 0px; margin: 0px; border: none; font-family: monospace; }"));

    const auto ts = gridlock::core::ConfigManager::instance().getTerminalSettings();
    m_termWidget->setShellProgram(ts.shellPath);

    // Set default standard terminal environment parameters.
    QStringList env;
    env << "TERM=xterm-256color" << "COLORTERM=truecolor";
    setEnvironment(env);

    // Apply default dark theme color scheme.
    setColorScheme("DarkPastels");

    // Launch underlying shell process.
    m_termWidget->startShellProgram();

    // Enforce fixed-pitch monospace typography at startup and install recursive event filters.
    m_activeFont = resolveMonospaceFont(ts.fontFamily, ts.fontSize);
    installEventFilter(this);
    enforceMonospaceOnChildren();

    // Delay second pass to next event loop iteration after VT100 TerminalDisplay is constructed.
    QTimer::singleShot(0, this, [this]() {
        enforceMonospaceOnChildren();
    });

    layout->addWidget(m_termWidget, 1);
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
    if (!m_termWidget) {
        return;
    }

    m_termWidget->installEventFilter(this);
    m_termWidget->setFont(m_activeFont);
    m_termWidget->setTerminalFont(m_activeFont);
    m_termWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    const int parentW = width() > 24 ? width() : m_termWidget->width();
    const int parentH = height() > 24 ? height() : m_termWidget->height();
    if (parentW > 24 && m_termWidget->width() < parentW) {
        m_termWidget->setGeometry(0, 0, parentW, parentH);
    }

    // Lock fixed-pitch font and full-width geometry on all internal QTermWidget child widgets (e.g. TerminalDisplay).
    for (QWidget* child : findChildren<QWidget*>()) {
        child->installEventFilter(this);
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
 * @brief Intercepts FontChange, StyleChange, and ChildAdded events to lock fixed-pitch font and full-width geometry.
 * @param watched Target QObject receiving the event.
 * @param event Event payload.
 * @return Base QWidget::eventFilter result.
 */
bool TerminalDockWidget::eventFilter(QObject* watched, QEvent* event) {
    if (!m_applyingFontGuard && event != nullptr) {
        const QEvent::Type t = event->type();
        if (t == QEvent::ChildAdded) {
            if (auto* childWidget = qobject_cast<QWidget*>(static_cast<QChildEvent*>(event)->child())) {
                childWidget->installEventFilter(this);
                m_applyingFontGuard = true;
                enforceMonospaceOnChildren();
                m_applyingFontGuard = false;
            }
        } else if (t == QEvent::FontChange || t == QEvent::ApplicationFontChange ||
                   t == QEvent::StyleChange || t == QEvent::Polish ||
                   t == QEvent::Show || t == QEvent::Resize) {
            m_applyingFontGuard = true;
            enforceMonospaceOnChildren();
            m_applyingFontGuard = false;
        }
    }
    return QWidget::eventFilter(watched, event);
}

/**
 * @brief Verifies that QTermWidget and all internal display child widgets retain a fixed-pitch font.
 * @return True if every child widget inside TerminalDockWidget reports fixed-pitch typography.
 */
bool TerminalDockWidget::areAllChildWidgetsFixedPitch() const {
    if (!m_termWidget) {
        return false;
    }
    for (const QWidget* child : findChildren<const QWidget*>()) {
        if (!child->font().fixedPitch() && !QFontInfo(child->font()).fixedPitch()) {
            return false;
        }
    }
    return m_termWidget->getTerminalFont().fixedPitch() ||
           QFontInfo(m_termWidget->getTerminalFont()).fixedPitch();
}

/**
 * @brief Configures terminal font family and point size with monospace fallback.
 * @param family Font family name.
 * @param pointSize Point size in points.
 */
void TerminalDockWidget::setTerminalFont(const QString& family, int pointSize) {
    QFont font(family, pointSize);
    font.setFixedPitch(true);
    font.setStyleHint(QFont::Monospace);
    m_requestedFont = font;
    m_activeFont = resolveMonospaceFont(family, pointSize);
    m_applyingFontGuard = true;
    enforceMonospaceOnChildren();
    m_applyingFontGuard = false;
}

/**
 * @brief Custom font setter validating fixed-pitch metrics before applying.
 * @param family Candidate font family.
 * @param pointSize Font size.
 */
void TerminalDockWidget::setCustomFont(const QString& family, int pointSize) {
    setTerminalFont(family, pointSize);
}

/**
 * @brief Retrieves the active validated fixed-pitch terminal font.
 * @return Current fixed-pitch QFont instance.
 */
QFont TerminalDockWidget::activeTerminalFont() const {
    if (m_activeFont.fixedPitch() || QFontInfo(m_activeFont).fixedPitch()) {
        return m_activeFont;
    }
    return m_termWidget->getTerminalFont();
}

/**
 * @brief Retrieves the active terminal emulator font.
 * @return Current QFont instance.
 */
QFont TerminalDockWidget::terminalFont() const {
    if (!m_requestedFont.family().isEmpty()) {
        return m_requestedFont;
    }
    return activeTerminalFont();
}

/**
 * @brief Sets the active QTermWidget color scheme.
 * @param scheme Name of color scheme.
 */
void TerminalDockWidget::setColorScheme(const QString& scheme) {
    m_colorScheme = scheme;
    m_termWidget->setColorScheme(scheme);
}

/**
 * @brief Retrieves the current color scheme identifier.
 * @return Color scheme name.
 */
QString TerminalDockWidget::colorScheme() const {
    return m_colorScheme;
}

/**
 * @brief Sets the maximum scrollback buffer history size.
 * @param lines Number of history lines.
 */
void TerminalDockWidget::setScrollbackLines(int lines) {
    m_termWidget->setHistorySize(lines);
}

/**
 * @brief Retrieves the active scrollback history buffer capacity.
 * @return Line count.
 */
int TerminalDockWidget::scrollbackLines() const {
    return m_termWidget->historySize();
}

/**
 * @brief Injects custom environment variable strings into the terminal shell.
 * @param env Array of environment definitions.
 */
void TerminalDockWidget::setEnvironment(const QStringList& env) {
    m_environment = env;
    m_termWidget->setEnvironment(env);
}

/**
 * @brief Retrieves the environment array configured on the terminal.
 * @return List of environment variable strings.
 */
QStringList TerminalDockWidget::environment() const {
    return m_environment;
}

/**
 * @brief Dispatches a shell command to the embedded QTermWidget session and appends a newline.
 * @param command Shell command string to execute in the terminal.
 */
void TerminalDockWidget::sendCommand(const QString& command) {
    m_lastSentCommand = command.trimmed();
    if (m_termWidget != nullptr && !m_lastSentCommand.isEmpty()) {
        // Dispatch command text followed by newline to trigger interactive shell execution.
        m_termWidget->sendText(m_lastSentCommand + QStringLiteral("\n"));
    }
}

}  // namespace gridlock::ui
