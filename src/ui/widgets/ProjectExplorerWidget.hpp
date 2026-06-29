#pragma once
#include <QDockWidget>
#include <QTreeView>
#include <QFileSystemModel>
#include <QIdentityProxyModel>
#include <QPushButton>
#include <QToolButton>

namespace gridlock::ui {

class FileIconProvider;

class ProjectExplorerWidget : public QDockWidget {
    Q_OBJECT
public:
    explicit ProjectExplorerWidget(QWidget* parent = nullptr);
    ~ProjectExplorerWidget() override;
    
    void setRootPath(const QString& path);
    void reloadStyle();

signals:
    void fileDoubleClicked(const QString& filePath);

private slots:
    void onOpenFolder();
    void onDoubleClicked(const QModelIndex& index);

private:
    QTreeView* m_treeView;
    QFileSystemModel* m_fileSystemModel;
    FileIconProvider* m_iconProvider;
    QPushButton* m_openFolderBtn;
    QPushButton* m_collapseAllBtn;
};

} // namespace gridlock::ui
