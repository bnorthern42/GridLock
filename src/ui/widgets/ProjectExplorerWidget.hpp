#pragma once
#include <QDockWidget>
#include <QTreeView>
#include <QFileSystemModel>
#include <QIdentityProxyModel>
#include <QPushButton>
#include <QToolButton>

namespace gridlock::ui {

class IconProxyModel : public QIdentityProxyModel {
    Q_OBJECT
public:
    explicit IconProxyModel(QObject* parent = nullptr);
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
private:
    QIcon generateColorIcon(const QString& text, const QColor& color) const;
};

class ProjectExplorerWidget : public QDockWidget {
    Q_OBJECT
public:
    explicit ProjectExplorerWidget(QWidget* parent = nullptr);
    ~ProjectExplorerWidget() override = default;

signals:
    void fileDoubleClicked(const QString& filePath);

private slots:
    void onOpenFolder();
    void onDoubleClicked(const QModelIndex& index);

private:
    QTreeView* m_treeView;
    QFileSystemModel* m_fileSystemModel;
    IconProxyModel* m_proxyModel;
    QPushButton* m_openFolderBtn;
    QToolButton* m_collapseAllBtn;
};

} // namespace gridlock::ui
