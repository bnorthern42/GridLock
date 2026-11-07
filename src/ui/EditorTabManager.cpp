/**
 * @file EditorTabManager.cpp
 * @brief Implementation of the multi-document source tab manager widget.
 *
 * Implements tab lifecycle management, file loading with CRLF normalization,
 * persistent breakpoint restoration via ConfigManager, and signal forwarding.
 */

#include "EditorTabManager.hpp"
#include "../core/managers/ConfigManager.hpp"
#include "views/SourceCodeView.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTabBar>

namespace gridlock::ui {

/**
 * @brief Constructs the tab manager and configures closable/movable tabs.
 * @param parent Optional parent widget.
 */
EditorTabManager::EditorTabManager(QWidget* parent)
    : QTabWidget(parent) {
    setObjectName(QStringLiteral("EditorTabManager"));
    setDocumentMode(true);
    setTabsClosable(true);
    setMovable(true);
    setContentsMargins(0, 0, 0, 0);
    connect(this, &QTabWidget::tabCloseRequested, this,
            &EditorTabManager::onTabCloseRequested);
}

/**
 * @brief Loads a source file into a new or existing editor tab.
 * @param filePath Path to the target source file.
 * @return Active SourceCodeView pointer, or nullptr on I/O error.
 */
SourceCodeView* EditorTabManager::openFile(const QString& filePath) {
    // Resolve relative paths when launched from builddir/ or workspace root.
    QString resolvedPath = filePath;
    if (!QFileInfo::exists(resolvedPath) && QFileInfo::exists(QStringLiteral("../") + filePath)) {
        resolvedPath = QStringLiteral("../") + filePath;
    }
    const QFileInfo resolvedInfo(resolvedPath);
    const QString absolutePath = !resolvedInfo.canonicalFilePath().isEmpty()
                                     ? resolvedInfo.canonicalFilePath()
                                     : resolvedInfo.absoluteFilePath();

    // If file is already open, activate its tab and return existing view.
    if (m_openFiles.contains(absolutePath)) {
        setCurrentWidget(m_openFiles[absolutePath]);
        return m_openFiles[absolutePath];
    }

    // Attempt to open the resolved file from disk.
    QFile file(resolvedPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return nullptr;
    }

    // Read full file text and normalize CRLF line endings to standard Unix LF.
    QString code = file.readAll();
    code.replace("\r\n", "\n");
    file.close();

    // Construct fresh SourceCodeView and initialize file properties.
    auto* view = new SourceCodeView(this);
    view->setProperty("absolutePath", absolutePath);
    view->setSourceCode(code);
    view->setCurrentFile(absolutePath);

    // Restore persistent breakpoints associated with this file path.
    const auto breakpoints = core::ConfigManager::instance().getBreakpoints();
    if (breakpoints.contains(absolutePath)) {
        view->setBreakpoints(breakpoints[absolutePath]);
    } else {
        view->setBreakpoints(QSet<int>());
    }

    // Forward view inspection and breakpoint signals through EditorTabManager.
    connect(view, &SourceCodeView::toggleBreakpointRequested, this,
            &EditorTabManager::toggleBreakpointRequested);
    connect(view, &SourceCodeView::breakpointToggled, this,
            &EditorTabManager::breakpointToggled);
    connect(view, &SourceCodeView::hoverVariableRequested, this,
            &EditorTabManager::hoverVariableRequested);
    connect(view, &SourceCodeView::hideHoverTooltip, this,
            &EditorTabManager::hideHoverTooltip);
    connect(view, &SourceCodeView::pinVariableRequested, this,
            &EditorTabManager::pinVariableRequested);

    // Append new tab with base file name and full path tooltip.
    const QFileInfo fi(absolutePath);
    const QString baseName = fi.fileName();
    const int index = addTab(view, baseName);
    setTabToolTip(index, absolutePath);

    // Update tab title with dirty indicator ("● ") whenever the document modification state changes.
    connect(view, &SourceCodeView::modificationStateChanged, this, [this, view, baseName](bool modified) {
        const int tabIdx = indexOf(view);
        if (tabIdx >= 0) {
            setTabText(tabIdx, modified ? (QStringLiteral("● ") + baseName) : baseName);
        }
    });

    m_openFiles[absolutePath] = view;
    setCurrentIndex(index);
    if (tabBar() && tabBar()->isHidden()) {
        tabBar()->show();
    }

    return view;
}

/**
 * @brief Retrieves the active SourceCodeView widget.
 * @return SourceCodeView pointer or nullptr.
 */
SourceCodeView* EditorTabManager::currentSourceCodeView() const {
    return qobject_cast<SourceCodeView*>(currentWidget());
}

/**
 * @brief Finds the open SourceCodeView corresponding to a canonical file path.
 * @param filePath The file path to query.
 * @return Matching view pointer or nullptr if file is not currently open.
 */
SourceCodeView* EditorTabManager::getSourceCodeViewForFile(
    const QString& filePath) const {
    QString resolvedPath = filePath;
    if (!QFileInfo::exists(resolvedPath) && QFileInfo::exists(QStringLiteral("../") + filePath)) {
        resolvedPath = QStringLiteral("../") + filePath;
    }
    const QFileInfo resolvedInfo(resolvedPath);
    const QString absolutePath = !resolvedInfo.canonicalFilePath().isEmpty()
                                     ? resolvedInfo.canonicalFilePath()
                                     : resolvedInfo.absoluteFilePath();
    if (m_openFiles.contains(absolutePath)) {
        return m_openFiles[absolutePath];
    }
    return nullptr;
}

/**
 * @brief Handles tab closure by user request.
 * @param index Index of the tab being closed.
 */
void EditorTabManager::onTabCloseRequested(int index) {
    QWidget* widget = this->widget(index);
    if (auto* view = qobject_cast<SourceCodeView*>(widget)) {
        const QString absolutePath = view->property("absolutePath").toString();
        m_openFiles.remove(absolutePath);
    }
    removeTab(index);
    widget->deleteLater();
}

/**
 * @brief Iteratively removes and deallocates all tabs.
 */
void EditorTabManager::clearAllTabs() {
    while (count() > 0) {
        onTabCloseRequested(0);
    }
}

/**
 * @brief Returns whether the editor tab at index has unsaved document modifications.
 * @param index Zero-based tab index.
 * @return True if the underlying SourceCodeView document is modified.
 */
bool EditorTabManager::isTabModified(int index) const {
    auto* view = qobject_cast<SourceCodeView*>(widget(index));
    return view && view->document() && view->document()->isModified();
}

/**
 * @brief Saves all currently modified editor tabs to disk and clears their dirty indicators.
 * @return Number of modified tabs written to disk.
 */
int EditorTabManager::saveAllModifiedTabs() {
    int savedCount = 0;
    for (int i = 0; i < count(); ++i) {
        if (auto* view = qobject_cast<SourceCodeView*>(widget(i))) {
            if (view->document() && view->document()->isModified() && view->saveToFile()) {
                const QString baseName = QFileInfo(view->property("absolutePath").toString()).fileName();
                setTabText(i, baseName);
                ++savedCount;
            }
        }
    }
    return savedCount;
}

/**
 * @brief Toggles the active editor tab between corresponding C/C++ header (.hpp/.h) and source (.cpp/.c) files.
 * @return True if a matching counterpart file was found and activated.
 */
bool EditorTabManager::switchHeaderSource() {
    auto* view = currentSourceCodeView();
    if (!view) return false;
    const QFileInfo fi(view->property("absolutePath").toString());
    if (!fi.exists()) return false;
    const QString ext = fi.suffix().toLower();
    const bool isHeader = (ext == QStringLiteral("hpp") || ext == QStringLiteral("hxx") ||
                           ext == QStringLiteral("hh") || ext == QStringLiteral("h"));
    const QStringList candidates = isHeader
                                       ? QStringList{QStringLiteral("cpp"), QStringLiteral("cxx"), QStringLiteral("cc"), QStringLiteral("c")}
                                       : QStringList{QStringLiteral("hpp"), QStringLiteral("h"), QStringLiteral("hxx"), QStringLiteral("hh")};
    // Search same directory first for matching stem + counterpart extension.
    for (const QString& targetExt : candidates) {
        const QString candidatePath = fi.dir().filePath(fi.completeBaseName() + QLatin1Char('.') + targetExt);
        if (QFileInfo::exists(candidatePath)) {
            return openFile(candidatePath) != nullptr;
        }
    }
    return false;
}

}  // namespace gridlock::ui
