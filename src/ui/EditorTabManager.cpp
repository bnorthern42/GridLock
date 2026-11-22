/**
 * @file EditorTabManager.cpp
 * @brief Implementation of the multi-document source tab manager and QSplitter split editor.
 *
 * Manages primary and secondary QTabWidget panes inside a layout-driven QSplitter,
 * ensuring non-overlapping geometry, visible scrollbars in both panes, active-editor
 * focus tracking, unified gutter breakpoint forwarding, and dirty-tab checks.
 */

#include "EditorTabManager.hpp"

#include <algorithm>

#include <QApplication>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMenu>
#include <QTabBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QVBoxLayout>

#include "../core/managers/ConfigManager.hpp"
#include "views/CsvTablePreviewWidget.hpp"
#include "views/SourceCodeView.hpp"

namespace gridlock::ui {

EditorTabManager::EditorTabManager(QWidget* parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("EditorTabManager"));
    setContentsMargins(0, 0, 0, 0);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setObjectName(QStringLiteral("EditorInternalSplitter"));
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(3);

    m_primaryTabWidget = new QTabWidget(m_splitter);
    m_primaryTabWidget->setObjectName(QStringLiteral("PrimaryEditorTabs"));
    m_primaryTabWidget->setDocumentMode(true);
    m_primaryTabWidget->setTabsClosable(true);
    m_primaryTabWidget->setMovable(true);

    m_secondaryTabWidget = new QTabWidget(m_splitter);
    m_secondaryTabWidget->setObjectName(QStringLiteral("SecondaryEditorTabs"));
    m_secondaryTabWidget->setDocumentMode(true);
    m_secondaryTabWidget->setTabsClosable(true);
    connect(m_secondaryTabWidget, &QTabWidget::tabCloseRequested, this, [this](int) { closeSplit(); });
    m_secondaryTabWidget->hide();

    m_splitter->addWidget(m_primaryTabWidget);
    m_splitter->addWidget(m_secondaryTabWidget);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 1);
    rootLayout->addWidget(m_splitter, 1);

    connect(m_primaryTabWidget, &QTabWidget::tabCloseRequested, this, &EditorTabManager::tabCloseRequested);
    connect(this, &EditorTabManager::tabCloseRequested, this, &EditorTabManager::onTabCloseRequested);
    connect(m_primaryTabWidget, &QTabWidget::currentChanged, this, [this](int idx) {
        m_lastActiveSecondary = false;
        if (auto* view = qobject_cast<SourceCodeView*>(m_primaryTabWidget->widget(idx))) {
            view->refreshGitDiffGutter();
        }
        emit currentChanged(idx);
    });

    // Configure right-click context menu on both tab bars for split editor actions.
    auto installTabMenu = [this](QTabBar* bar) {
        if (!bar) return;
        bar->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(bar, &QWidget::customContextMenuRequested, this, [this, bar](const QPoint& pos) {
            QMenu menu(this);
            menu.addAction(QStringLiteral("Split Editor Right"), this, [this]() { splitEditor(Qt::Horizontal); });
            menu.addAction(QStringLiteral("Split Editor Down"), this, [this]() { splitEditor(Qt::Vertical); });
            menu.addAction(QStringLiteral("Close Split"), this, &EditorTabManager::closeSplit);
            menu.addSeparator();
            menu.addAction(QStringLiteral("Switch Header/Source (F4)"), this, &EditorTabManager::switchHeaderSource);
            menu.exec(bar->mapToGlobal(pos));
        });
    };
    installTabMenu(m_primaryTabWidget->tabBar());
    installTabMenu(m_secondaryTabWidget->tabBar());
}

void EditorTabManager::wireEditorView(SourceCodeView* view) {
    if (!view) return;
    view->installEventFilter(this);
    connect(view, &SourceCodeView::toggleBreakpointRequested, this, &EditorTabManager::toggleBreakpointRequested);
    connect(view, &SourceCodeView::breakpointToggled, this, &EditorTabManager::breakpointToggled);
    connect(view, &SourceCodeView::rankBreakpointChanged, this, &EditorTabManager::rankBreakpointChanged);
    connect(view, &SourceCodeView::hoverVariableRequested, this, &EditorTabManager::hoverVariableRequested);
    connect(view, &SourceCodeView::hideHoverTooltip, this, &EditorTabManager::hideHoverTooltip);
    connect(view, &SourceCodeView::pinVariableRequested, this, &EditorTabManager::pinVariableRequested);
    connect(view, &SourceCodeView::definitionJumpRequested, this, &EditorTabManager::navigateToLocation);
}

bool EditorTabManager::eventFilter(QObject* watched, QEvent* event) {
    if (event && event->type() == QEvent::FocusIn) {
        if (auto* view = qobject_cast<SourceCodeView*>(watched)) {
            const bool nowSecondary = (m_splitActive && view == m_secondaryEditorView);
            if (m_lastActiveSecondary != nowSecondary) {
                m_lastActiveSecondary = nowSecondary;
                emit currentChanged(currentIndex());
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

SourceCodeView* EditorTabManager::openFile(const QString& filePath) {
    QString resolvedPath = filePath;
    if (!QFileInfo::exists(resolvedPath) && QFileInfo::exists(QStringLiteral("../") + filePath)) {
        resolvedPath = QStringLiteral("../") + filePath;
    }
    const QFileInfo resolvedInfo(resolvedPath);
    const QString absolutePath = !resolvedInfo.canonicalFilePath().isEmpty()
                                     ? resolvedInfo.canonicalFilePath()
                                     : resolvedInfo.absoluteFilePath();

    if (m_openFiles.contains(absolutePath)) {
        m_lastActiveSecondary = false;
        m_primaryTabWidget->setCurrentWidget(m_openFiles[absolutePath]);
        return m_openFiles[absolutePath];
    }
    if (m_openPreviewTabs.contains(absolutePath)) { m_lastActiveSecondary = false; m_primaryTabWidget->setCurrentWidget(m_openPreviewTabs[absolutePath]); return nullptr; }

    const QString ext = resolvedInfo.suffix().toLower();
    if ((ext == QStringLiteral("csv") || ext == QStringLiteral("tsv")) && resolvedInfo.exists() &&
        resolvedInfo.size() >= kLargeCsvPreviewThresholdBytes) {
        auto* preview = new CsvTablePreviewWidget(m_primaryTabWidget);
        if (!preview->loadFile(absolutePath)) { delete preview; return nullptr; }
        preview->setProperty("absolutePath", absolutePath);
        connect(preview, &CsvTablePreviewWidget::openRawTextRequested, this, [this](const QString& p) { openRawTextPreview(p); });
        const int idx = m_primaryTabWidget->addTab(preview, resolvedInfo.fileName());
        m_primaryTabWidget->setTabToolTip(idx, absolutePath);
        m_openPreviewTabs[absolutePath] = preview;
        m_lastActiveSecondary = false;
        m_primaryTabWidget->setCurrentIndex(idx);
        if (m_primaryTabWidget->tabBar() && m_primaryTabWidget->tabBar()->isHidden()) m_primaryTabWidget->tabBar()->show();
        return nullptr;
    }

    const qint64 largeLimit = SourceCodeView::isPlainDataFile(absolutePath) ? kLargeCsvPreviewThresholdBytes
                                                                            : SourceCodeView::kLargeFileThresholdBytes;
    const bool fastLargeOrData = resolvedInfo.exists() && resolvedInfo.size() >= largeLimit;
    if (fastLargeOrData && resolvedInfo.size() > 2 * 1024 * 1024) return openRawTextPreview(absolutePath, 2 * 1024 * 1024);

    QFile file(resolvedPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return nullptr;
    QString code = QString::fromUtf8(file.readAll());
    code.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    file.close();

    auto* view = new SourceCodeView(m_primaryTabWidget);
    view->setProperty("absolutePath", absolutePath);
    if (fastLargeOrData) view->setLargeFileMode(true);
    view->setCurrentFile(absolutePath);
    view->setSourceCode(code);
    view->setActiveFocusRank(m_activeFocusRank);
    const auto breakpoints = core::ConfigManager::instance().getBreakpoints();
    view->setBreakpoints(breakpoints.contains(absolutePath) ? breakpoints[absolutePath] : QSet<int>());
    wireEditorView(view);

    const QString baseName = resolvedInfo.fileName();
    const int index = m_primaryTabWidget->addTab(view, baseName);
    m_primaryTabWidget->setTabToolTip(index, absolutePath);
    connect(view, &SourceCodeView::modificationStateChanged, this, [this, view, baseName](bool modified) {
        const int tabIdx = m_primaryTabWidget->indexOf(view);
        if (tabIdx >= 0) m_primaryTabWidget->setTabText(tabIdx, modified ? (QStringLiteral("● ") + baseName) : baseName);
    });
    m_openFiles[absolutePath] = view;
    m_lastActiveSecondary = false;
    m_primaryTabWidget->setCurrentIndex(index);
    if (m_primaryTabWidget->tabBar() && m_primaryTabWidget->tabBar()->isHidden()) m_primaryTabWidget->tabBar()->show();
    return view;
}

SourceCodeView* EditorTabManager::openRawTextPreview(const QString& filePath, qint64 maxBytes) {
    const QFileInfo fi(filePath);
    const QString absolutePath = !fi.canonicalFilePath().isEmpty() ? fi.canonicalFilePath() : fi.absoluteFilePath();
    QFile file(absolutePath);
    if (!file.open(QIODevice::ReadOnly)) return nullptr;
    const qint64 totalSize = file.size();
    QByteArray raw = file.read(qMax<qint64>(1024, maxBytes));
    file.close();
    const bool truncated = totalSize > raw.size();
    if (truncated) { if (const qsizetype nl = raw.lastIndexOf('\n'); nl > 0) raw.truncate(nl + 1); }
    QString text = QString::fromUtf8(raw);
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    if (truncated) {
        text.append(QStringLiteral("\n--- [Truncated at %1 KB of %2 KB for responsive viewing] ---\n").arg(raw.size() / 1024).arg(totalSize / 1024));
    }
    auto* view = new SourceCodeView(m_primaryTabWidget);
    view->setProperty("absolutePath", absolutePath);
    view->setLargeFileMode(true);
    view->setCurrentFile(absolutePath);
    view->setSourceCode(text);
    view->setReadOnly(truncated);
    wireEditorView(view);
    const int idx = m_primaryTabWidget->addTab(view, truncated ? (fi.fileName() + QStringLiteral(" (Raw)")) : fi.fileName());
    m_primaryTabWidget->setTabToolTip(idx, absolutePath);
    m_openFiles[absolutePath] = view;
    m_lastActiveSecondary = false;
    m_primaryTabWidget->setCurrentIndex(idx);
    if (m_primaryTabWidget->tabBar() && m_primaryTabWidget->tabBar()->isHidden()) m_primaryTabWidget->tabBar()->show();
    return view;
}

SourceCodeView* EditorTabManager::currentSourceCodeView() const {
    if (m_splitActive && m_secondaryEditorView) {
        QWidget* fw = QApplication::focusWidget();
        if (fw && (fw == m_secondaryEditorView || m_secondaryEditorView->isAncestorOf(fw))) {
            return m_secondaryEditorView;
        }
        if (fw && m_primaryTabWidget && m_primaryTabWidget->isAncestorOf(fw)) {
            return qobject_cast<SourceCodeView*>(m_primaryTabWidget->currentWidget());
        }
        if (m_lastActiveSecondary) {
            return m_secondaryEditorView;
        }
    }
    return m_primaryTabWidget ? qobject_cast<SourceCodeView*>(m_primaryTabWidget->currentWidget()) : nullptr;
}

SourceCodeView* EditorTabManager::getSourceCodeViewForFile(const QString& filePath) const {
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
    if (m_splitActive && m_secondaryEditorView &&
        m_secondaryEditorView->property("absolutePath").toString() == absolutePath) {
        return m_secondaryEditorView;
    }
    return nullptr;
}

void EditorTabManager::closeTabUnchecked(int index) {
    if (!m_primaryTabWidget) return;
    QWidget* w = m_primaryTabWidget->widget(index);
    if (!w) return;
    const QString path = w->property("absolutePath").toString();
    if (qobject_cast<SourceCodeView*>(w)) {
        m_openFiles.remove(path);
    } else if (m_openPreviewTabs.value(path) == w) {
        m_openPreviewTabs.remove(path);
    }
    m_primaryTabWidget->removeTab(index);
    w->deleteLater();
}

QMessageBox::StandardButton EditorTabManager::promptUnsavedDecision(const QString& filePath) const {
    if (m_unsavedPromptHandler) return m_unsavedPromptHandler(filePath);
    if (QGuiApplication::platformName() == QStringLiteral("offscreen")) return QMessageBox::Discard;
    const QString name = filePath.isEmpty() ? QStringLiteral("Untitled") : QFileInfo(filePath).fileName();
    return QMessageBox::warning(
        const_cast<EditorTabManager*>(this), QStringLiteral("Unsaved Changes"),
        QStringLiteral("Save changes to \"%1\" before closing?").arg(name),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
}

void EditorTabManager::onTabCloseRequested(int index) {
    if (index < 0 || index >= count()) return;
    if (isTabModified(index)) {
        auto* view = qobject_cast<SourceCodeView*>(widget(index));
        const QString filePath = view ? view->property("absolutePath").toString() : QString();
        const auto choice = promptUnsavedDecision(filePath);
        if (choice == QMessageBox::Save) {
            if (!saveTab(index)) return;
        } else if (choice != QMessageBox::Discard) {
            return;
        }
    }
    closeTabUnchecked(index);
}

void EditorTabManager::clearAllTabs() {
    while (count() > 0) closeTabUnchecked(0);
    if (m_splitActive) {
        if (m_secondaryEditorView && m_secondaryEditorView->document()) {
            m_secondaryEditorView->document()->setModified(false);
        }
        closeSplit();
    }
}

bool EditorTabManager::isTabModified(int index) const {
    auto* view = qobject_cast<SourceCodeView*>(widget(index));
    return view && view->document() && view->document()->isModified();
}

bool EditorTabManager::saveTab(int index) {
    auto* view = qobject_cast<SourceCodeView*>(widget(index));
    if (!view || !view->saveToFile()) return false;
    const QString path = view->property("absolutePath").toString();
    if (!path.isEmpty()) setTabText(index, QFileInfo(path).fileName());
    return true;
}

bool EditorTabManager::hasModifiedTabs() const {
    for (int i = 0; i < count(); ++i) {
        if (isTabModified(i)) return true;
    }
    return m_splitActive && m_secondaryEditorView && m_secondaryEditorView->document() &&
           m_secondaryEditorView->document()->isModified();
}

bool EditorTabManager::confirmCloseAllModifiedTabs() {
    for (int i = 0; i < count(); ++i) {
        if (!isTabModified(i)) continue;
        setCurrentIndex(i);
        auto* view = qobject_cast<SourceCodeView*>(widget(i));
        const QString filePath = view ? view->property("absolutePath").toString() : QString();
        const auto choice = promptUnsavedDecision(filePath);
        if (choice == QMessageBox::Save) {
            if (!saveTab(i)) return false;
        } else if (choice != QMessageBox::Discard) {
            return false;
        }
    }
    if (m_splitActive && m_secondaryEditorView && m_secondaryEditorView->document() &&
        m_secondaryEditorView->document()->isModified()) {
        const QString path = m_secondaryEditorView->property("absolutePath").toString();
        const auto choice = promptUnsavedDecision(path);
        if (choice == QMessageBox::Save) {
            if (!m_secondaryEditorView->saveToFile()) return false;
        } else if (choice != QMessageBox::Discard) {
            return false;
        }
    }
    return true;
}

int EditorTabManager::saveAllModifiedTabs() {
    int savedCount = 0;
    for (int i = 0; i < count(); ++i) {
        if (isTabModified(i) && saveTab(i)) ++savedCount;
    }
    if (m_splitActive && m_secondaryEditorView && m_secondaryEditorView->document() &&
        m_secondaryEditorView->document()->isModified()) {
        if (m_secondaryEditorView->saveToFile()) {
            const QString path = m_secondaryEditorView->property("absolutePath").toString();
            if (!path.isEmpty() && m_secondaryTabWidget) {
                m_secondaryTabWidget->setTabText(0, QFileInfo(path).fileName());
            }
            ++savedCount;
        }
    }
    return savedCount;
}

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
    for (const QString& targetExt : candidates) {
        const QString candidatePath = fi.dir().filePath(fi.completeBaseName() + QLatin1Char('.') + targetExt);
        if (QFileInfo::exists(candidatePath)) {
            return openFile(candidatePath) != nullptr;
        }
    }
    return false;
}

SourceCodeView* EditorTabManager::navigateToLocation(const QString& filePath, int oneBasedLine, int zeroBasedColumn) {
    SourceCodeView* view = openFile(filePath);
    if (!view || !view->document()) return nullptr;

    const QTextBlock block = view->document()->findBlockByNumber(std::max(0, oneBasedLine - 1));
    if (block.isValid()) {
        QTextCursor cursor(block);
        const int clampedCol = std::clamp(zeroBasedColumn, 0, static_cast<int>(block.text().size()));
        cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, clampedCol);
        view->setTextCursor(cursor);
        view->ensureCursorVisible();
        view->highlightCurrentLine(std::max(1, oneBasedLine));
    }
    view->setFocus();
    emit definitionJumpRequested(view->currentFile(), std::max(1, oneBasedLine), std::max(0, zeroBasedColumn));
    return view;
}

void EditorTabManager::splitEditor(Qt::Orientation orientation) {
    if (!m_splitter || !m_secondaryTabWidget) return;
    if (!m_secondaryEditorView) {
        m_secondaryEditorView = new SourceCodeView(m_secondaryTabWidget);
        wireEditorView(m_secondaryEditorView);
        m_secondaryTabWidget->addTab(m_secondaryEditorView, QStringLiteral("split"));
        connect(m_secondaryEditorView, &SourceCodeView::modificationStateChanged, this, [this](bool modified) {
            if (!m_secondaryTabWidget || !m_secondaryEditorView) return;
            const QString path = m_secondaryEditorView->property("absolutePath").toString();
            const QString base = QFileInfo(path).fileName().isEmpty() ? QStringLiteral("split") : QFileInfo(path).fileName();
            m_secondaryTabWidget->setTabText(0, modified ? (QStringLiteral("● ") + base) : base);
        });
    }

    m_splitOrientation = orientation;
    m_splitter->setOrientation(orientation);
    m_splitActive = true;

    // Load companion header/source file if present, or mirror the active primary editor buffer.
    if (auto* primaryView = qobject_cast<SourceCodeView*>(currentWidget())) {
        const QString activePath = primaryView->property("absolutePath").toString();
        const QFileInfo fi(activePath);
        QString targetPath = activePath;
        QString targetCode = primaryView->toPlainText();
        if (fi.exists()) {
            const QString ext = fi.suffix().toLower();
            const bool isHdr = (ext == QStringLiteral("hpp") || ext == QStringLiteral("h") || ext == QStringLiteral("hxx"));
            const QStringList exts = isHdr ? QStringList{QStringLiteral("cpp"), QStringLiteral("c")}
                                           : QStringList{QStringLiteral("hpp"), QStringLiteral("h")};
            for (const QString& e : exts) {
                const QString cand = fi.dir().filePath(fi.completeBaseName() + QLatin1Char('.') + e);
                QFile cf(cand);
                if (cf.open(QIODevice::ReadOnly | QIODevice::Text)) {
                    const QFileInfo cfi(cand);
                    targetPath = !cfi.canonicalFilePath().isEmpty() ? cfi.canonicalFilePath() : cfi.absoluteFilePath();
                    targetCode = QString::fromUtf8(cf.readAll());
                    targetCode.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
                    break;
                }
            }
        }
        m_secondaryEditorView->setProperty("absolutePath", targetPath);
        m_secondaryEditorView->setSourceCode(targetCode);
        m_secondaryEditorView->setCurrentFile(targetPath);
        m_secondaryEditorView->setActiveFocusRank(m_activeFocusRank);
        const auto breakpoints = core::ConfigManager::instance().getBreakpoints();
        m_secondaryEditorView->setBreakpoints(breakpoints.contains(targetPath) ? breakpoints[targetPath] : QSet<int>());

        const QString tabTitle = QFileInfo(targetPath).fileName().isEmpty()
                                     ? QStringLiteral("split")
                                     : QFileInfo(targetPath).fileName();
        m_secondaryTabWidget->setTabText(0, tabTitle);
        m_secondaryTabWidget->setTabToolTip(0, targetPath);
    }

    m_secondaryTabWidget->show();
    const int span = (orientation == Qt::Horizontal) ? std::max(240, width()) : std::max(180, height());
    m_splitter->setSizes({span / 2, span - (span / 2)});
}

void EditorTabManager::closeSplit() {
    if (m_splitActive && m_secondaryEditorView && m_secondaryEditorView->document() &&
        m_secondaryEditorView->document()->isModified()) {
        const QString path = m_secondaryEditorView->property("absolutePath").toString();
        const auto choice = promptUnsavedDecision(path);
        if (choice == QMessageBox::Save) {
            if (!m_secondaryEditorView->saveToFile()) return;
        } else if (choice != QMessageBox::Discard) {
            return;
        }
        m_secondaryEditorView->document()->setModified(false);
    }
    m_splitActive = false;
    m_lastActiveSecondary = false;
    if (m_secondaryTabWidget) m_secondaryTabWidget->hide();
    if (m_splitter) m_splitter->setSizes({std::max(240, width()), 0});
    emit currentChanged(currentIndex());
}

void EditorTabManager::rehighlightAllTabs() {
    for (int i = 0; i < count(); ++i) {
        if (auto* view = qobject_cast<SourceCodeView*>(widget(i))) view->reloadStyle();
    }
    if (m_secondaryEditorView) m_secondaryEditorView->reloadStyle();
}

void EditorTabManager::setActiveFocusRank(int rankId) {
    m_activeFocusRank = std::max(0, rankId);
    for (auto* view : m_openFiles) {
        if (view) view->setActiveFocusRank(m_activeFocusRank);
    }
    if (m_secondaryEditorView) m_secondaryEditorView->setActiveFocusRank(m_activeFocusRank);
}

QList<SourceCodeView*> EditorTabManager::openViews() const {
    QList<SourceCodeView*> views = m_openFiles.values();
    if (m_splitActive && m_secondaryEditorView && !views.contains(m_secondaryEditorView)) {
        views.append(m_secondaryEditorView);
    }
    return views;
}

void EditorTabManager::setInlineVariableValues(const QMap<QString, QString>& varValues) {
    if (auto* view = currentSourceCodeView()) view->setInlineVariableValues(varValues);
}

void EditorTabManager::clearInlineVariableValues() {
    for (int i = 0; i < count(); ++i) {
        if (auto* v = qobject_cast<SourceCodeView*>(widget(i))) v->clearInlineVariableValues();
    }
    if (m_secondaryEditorView) m_secondaryEditorView->clearInlineVariableValues();
}

void EditorTabManager::setCurrentIndex(int index) {
    m_lastActiveSecondary = false;
    if (m_primaryTabWidget) m_primaryTabWidget->setCurrentIndex(index);
}

void EditorTabManager::setCurrentWidget(QWidget* w) {
    if (m_splitActive && w == m_secondaryEditorView) {
        m_lastActiveSecondary = true;
        return;
    }
    m_lastActiveSecondary = false;
    if (m_primaryTabWidget) m_primaryTabWidget->setCurrentWidget(w);
}

void EditorTabManager::setTabText(int index, const QString& text) {
    if (m_primaryTabWidget) m_primaryTabWidget->setTabText(index, text);
}

void EditorTabManager::setDocumentMode(bool enabled) {
    if (m_primaryTabWidget) m_primaryTabWidget->setDocumentMode(enabled);
    if (m_secondaryTabWidget) m_secondaryTabWidget->setDocumentMode(enabled);
}

}  // namespace gridlock::ui
