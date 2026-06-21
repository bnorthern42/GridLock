#include "EditorTabManager.hpp"
#include "views/SourceCodeView.hpp"
#include "../core/managers/ConfigManager.hpp"
#include <QFileInfo>
#include <QFile>

namespace gridlock::ui {

EditorTabManager::EditorTabManager(QWidget* parent) : QTabWidget(parent) {
    setTabsClosable(true);
    setMovable(true);
    connect(this, &QTabWidget::tabCloseRequested, this, &EditorTabManager::onTabCloseRequested);
}

SourceCodeView* EditorTabManager::openFile(const QString& filePath) {
    QString absolutePath = QFileInfo(filePath).absoluteFilePath();
    if (m_openFiles.contains(absolutePath)) {
        setCurrentWidget(m_openFiles[absolutePath]);
        return m_openFiles[absolutePath];
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        file.setFileName("../" + filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return nullptr;
        }
    }

    QString code = file.readAll();
    code.replace("\r\n", "\n");
    file.close();

    SourceCodeView* view = new SourceCodeView(this);
    view->setProperty("absolutePath", absolutePath);
    view->setSourceCode(code);
    view->setCurrentFile(file.fileName());

    auto breakpoints = core::ConfigManager::instance().getBreakpoints();
    if (breakpoints.contains(absolutePath)) {
        view->setBreakpoints(breakpoints[absolutePath]);
    } else {
        view->setBreakpoints(QSet<int>());
    }

    connect(view, &SourceCodeView::toggleBreakpointRequested, this, &EditorTabManager::toggleBreakpointRequested);
    connect(view, &SourceCodeView::breakpointToggled, this, &EditorTabManager::breakpointToggled);
    connect(view, &SourceCodeView::hoverVariableRequested, this, &EditorTabManager::hoverVariableRequested);
    connect(view, &SourceCodeView::semanticHoverRequested, this, &EditorTabManager::semanticHoverRequested);
    connect(view, &SourceCodeView::pinVariableRequested, this, &EditorTabManager::pinVariableRequested);

    QFileInfo fi(file.fileName());
    int index = addTab(view, fi.fileName());
    setTabToolTip(index, absolutePath);
    
    m_openFiles[absolutePath] = view;
    setCurrentIndex(index);
    
    return view;
}

SourceCodeView* EditorTabManager::currentSourceCodeView() const {
    return qobject_cast<SourceCodeView*>(currentWidget());
}

SourceCodeView* EditorTabManager::getSourceCodeViewForFile(const QString& filePath) const {
    QString absolutePath = QFileInfo(filePath).absoluteFilePath();
    if (m_openFiles.contains(absolutePath)) {
        return m_openFiles[absolutePath];
    }
    return nullptr;
}

void EditorTabManager::onTabCloseRequested(int index) {
    QWidget* widget = this->widget(index);
    if (SourceCodeView* view = qobject_cast<SourceCodeView*>(widget)) {
        QString absolutePath = view->property("absolutePath").toString();
        m_openFiles.remove(absolutePath);
    }
    removeTab(index);
    widget->deleteLater();
}

} // namespace gridlock::ui
