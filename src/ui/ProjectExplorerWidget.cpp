#include "ProjectExplorerWidget.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QFileInfo>
#include <QPainter>
#include <QPixmap>
#include <QDir>
#include <QHeaderView>

namespace gridlock::ui {

IconProxyModel::IconProxyModel(QObject* parent) : QIdentityProxyModel(parent) {
}

QIcon IconProxyModel::generateColorIcon(const QString& text, const QColor& color) const {
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawRoundedRect(0, 0, 16, 16, 4, 4);
    
    painter.setPen(Qt::white);
    QFont font = painter.font();
    font.setPixelSize(10);
    font.setBold(true);
    painter.setFont(font);
    painter.drawText(0, 0, 16, 16, Qt::AlignCenter, text);
    
    return QIcon(pixmap);
}

QVariant IconProxyModel::data(const QModelIndex& index, int role) const {
    if (role == Qt::DecorationRole && index.column() == 0) {
        if (sourceModel()) {
            QFileSystemModel* fsModel = qobject_cast<QFileSystemModel*>(sourceModel());
            if (fsModel) {
                QFileInfo info = fsModel->fileInfo(mapToSource(index));
                if (info.isDir()) {
                    return generateColorIcon("D", QColor("#89b4fa")); // Blue
                } else {
                    QString ext = info.suffix().toLower();
                    if (ext == "cpp" || ext == "c" || ext == "cxx") {
                        return generateColorIcon("C", QColor("#89b4fa")); // Blue
                    } else if (ext == "hpp" || ext == "h" || ext == "hxx") {
                        return generateColorIcon("H", QColor("#a6e3a1")); // Green
                    } else if (ext == "py") {
                        return generateColorIcon("Py", QColor("#f9e2af")); // Yellow
                    } else if (info.fileName() == "meson.build") {
                        return generateColorIcon("M", QColor("#cba6f7")); // Purple
                    } else if (ext == "md" || ext == "txt") {
                        return generateColorIcon("T", QColor("#bac2de")); // Grey
                    } else {
                        // Fallback
                        return generateColorIcon("?", QColor("#6c7086"));
                    }
                }
            }
        }
    }
    return QIdentityProxyModel::data(index, role);
}

ProjectExplorerWidget::ProjectExplorerWidget(QWidget* parent) : QDockWidget("Project Explorer", parent) {
    QWidget* content = new QWidget(this);
    QVBoxLayout* mainLayout = new QVBoxLayout(content);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Top bar
    QWidget* topBar = new QWidget(content);
    QHBoxLayout* topLayout = new QHBoxLayout(topBar);
    topLayout->setContentsMargins(4, 4, 4, 4);
    
    m_openFolderBtn = new QPushButton("Open Folder", topBar);
    m_collapseAllBtn = new QToolButton(topBar);
    m_collapseAllBtn->setText("-");
    m_collapseAllBtn->setToolTip("Collapse All");
    
    topLayout->addWidget(m_openFolderBtn);
    topLayout->addStretch();
    topLayout->addWidget(m_collapseAllBtn);
    
    mainLayout->addWidget(topBar);

    // Tree View
    m_treeView = new QTreeView(content);
    m_treeView->setHeaderHidden(true);

    m_fileSystemModel = new QFileSystemModel(this);
    m_fileSystemModel->setFilter(QDir::NoDotAndDotDot | QDir::AllDirs | QDir::Files);
    // Set root path to current dir as default
    m_fileSystemModel->setRootPath(QDir::currentPath());

    m_proxyModel = new IconProxyModel(this);
    m_proxyModel->setSourceModel(m_fileSystemModel);

    m_treeView->setModel(m_proxyModel);
    m_treeView->setRootIndex(m_proxyModel->mapFromSource(m_fileSystemModel->index(QDir::currentPath())));
    
    // Hide Size, Type, Date Modified (Columns 1, 2, 3)
    m_treeView->hideColumn(1);
    m_treeView->hideColumn(2);
    m_treeView->hideColumn(3);

    mainLayout->addWidget(m_treeView);
    setWidget(content);

    connect(m_openFolderBtn, &QPushButton::clicked, this, &ProjectExplorerWidget::onOpenFolder);
    connect(m_collapseAllBtn, &QToolButton::clicked, m_treeView, &QTreeView::collapseAll);
    connect(m_treeView, &QTreeView::doubleClicked, this, &ProjectExplorerWidget::onDoubleClicked);
}

void ProjectExplorerWidget::onOpenFolder() {
    QString dir = QFileDialog::getExistingDirectory(this, "Open Folder", QDir::currentPath(), QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!dir.isEmpty()) {
        m_fileSystemModel->setRootPath(dir);
        m_treeView->setRootIndex(m_proxyModel->mapFromSource(m_fileSystemModel->index(dir)));
    }
}

void ProjectExplorerWidget::onDoubleClicked(const QModelIndex& index) {
    if (!index.isValid()) return;
    
    QModelIndex sourceIndex = m_proxyModel->mapToSource(index);
    QFileInfo info = m_fileSystemModel->fileInfo(sourceIndex);
    if (info.isFile()) {
        emit fileDoubleClicked(info.absoluteFilePath());
    }
}

} // namespace gridlock::ui
