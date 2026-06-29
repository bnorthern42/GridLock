#include "ProjectExplorerWidget.hpp"
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPainter>
#include <QPixmap>
#include <QSettings>
#include <QVBoxLayout>

#include "FileIconProvider.hpp"

namespace gridlock::ui {

ProjectExplorerWidget::ProjectExplorerWidget(QWidget *parent)
    : QDockWidget("Project Explorer", parent) {
  m_iconProvider = nullptr;
  QWidget *content = new QWidget(this);
  QVBoxLayout *mainLayout = new QVBoxLayout(content);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  // Top bar
  QWidget *topBar = new QWidget(content);
  QHBoxLayout *topLayout = new QHBoxLayout(topBar);
  topLayout->setContentsMargins(4, 4, 2, 2);

  m_openFolderBtn = new QPushButton("Open Folder", topBar);
  m_collapseAllBtn = new QPushButton(topBar);
  m_collapseAllBtn->setText(QString::fromUtf8("\xEF\x86\x92"));
  m_collapseAllBtn->setFixedSize(32, 32);
  m_collapseAllBtn->setToolTip(tr("Collapse All Directories"));
  m_collapseAllBtn->setFlat(true);
  m_collapseAllBtn->setStyleSheet("font-family: 'Symbols Nerd Font'; font-size: 24px;");

  topLayout->addWidget(m_openFolderBtn);
  topLayout->addStretch();
  topLayout->addWidget(m_collapseAllBtn);

  mainLayout->addWidget(topBar);

  // Tree View
  m_treeView = new QTreeView(content);
  m_treeView->setHeaderHidden(true);

  m_fileSystemModel = new QFileSystemModel(this);
  m_fileSystemModel->setFilter(QDir::NoDotAndDotDot | QDir::AllDirs |
                               QDir::Files);
  // Set root path to current dir as default
  m_fileSystemModel->setRootPath(QDir::currentPath());

  m_iconProvider = new FileIconProvider();
  m_fileSystemModel->setIconProvider(m_iconProvider);
  m_treeView->setModel(m_fileSystemModel);
  m_treeView->setRootIndex(m_fileSystemModel->index(QDir::currentPath()));

  reloadStyle();

  // Hide Size, Type, Date Modified (Columns 1, 2, 3)
  m_treeView->hideColumn(1);
  m_treeView->hideColumn(2);
  m_treeView->hideColumn(3);

  mainLayout->addWidget(m_treeView);
  setWidget(content);

  connect(m_openFolderBtn, &QPushButton::clicked, this,
          &ProjectExplorerWidget::onOpenFolder);
  connect(m_collapseAllBtn, &QPushButton::clicked, m_treeView,
          &QTreeView::collapseAll);
  connect(m_treeView, &QTreeView::doubleClicked, this,
          &ProjectExplorerWidget::onDoubleClicked);

  // Set object name to increase CSS specificity against the global ACSS theme
  this->setObjectName("ProjectExplorerWidget");

  // Override ACSS default sizing for QDockWidget title bar buttons
  this->setStyleSheet(
      "#ProjectExplorerWidget::close-button, #ProjectExplorerWidget::float-button {"
      "    icon-size: 20px;"
      "    width: 28px;"
      "    height: 28px;"
      "}"
      "#ProjectExplorerWidget::close-button:hover, #ProjectExplorerWidget::float-button:hover {"
      "    background: rgba(255, 255, 255, 0.1);"
      "    border-radius: 4px;"
      "}");
}

ProjectExplorerWidget::~ProjectExplorerWidget() { delete m_iconProvider; }

void ProjectExplorerWidget::reloadStyle() {
  QSettings s("gridlock", "debugger");
  QString style =
      s.value("appearance/file_tree_style", "Comfortable").toString();

  int padding = 4;
  int fontSize = 13;
  if (style == "Compact") {
    padding = 2;
    fontSize = 10;
  } else if (style == "Large") {
    padding = 6;
    fontSize = 14;
  }

  m_treeView->setStyleSheet(QString("QTreeView { font-size: %2px; }"
                                    "QTreeView::item { padding: %1px; }")
                                .arg(padding)
                                .arg(fontSize));

  // Force model to recreate icons by passing a new provider (and deleting old)
  auto *newProvider = new FileIconProvider();
  m_fileSystemModel->setIconProvider(newProvider);
  delete m_iconProvider;
  m_iconProvider = newProvider;
}

void ProjectExplorerWidget::onOpenFolder() {
  QString dir = QFileDialog::getExistingDirectory(
      this, "Open Folder", QDir::currentPath(),
      QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
  if (!dir.isEmpty()) {
    m_fileSystemModel->setRootPath(dir);
    m_treeView->setRootIndex(m_fileSystemModel->index(dir));
  }
}

void ProjectExplorerWidget::setRootPath(const QString &path) {
  if (!path.isEmpty()) {
    m_fileSystemModel->setRootPath(path);
    m_treeView->setRootIndex(m_fileSystemModel->index(path));
  }
}

void ProjectExplorerWidget::onDoubleClicked(const QModelIndex &index) {
  if (!index.isValid())
    return;

  QFileInfo info = m_fileSystemModel->fileInfo(index);
  if (info.isFile()) {
    emit fileDoubleClicked(info.absoluteFilePath());
  }
}

} // namespace gridlock::ui
