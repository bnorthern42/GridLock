#include "ProjectExplorerWidget.hpp"
#include <QAbstractItemView>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPainter>
#include <QPixmap>
#include <QSettings>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include "FileIconProvider.hpp"

namespace gridlock::ui {

ProjectExplorerWidget::ProjectExplorerWidget(QWidget *parent)
    : QDockWidget("Project / Symbols", parent) {
  m_iconProvider = nullptr;
  QWidget *content = new QWidget(this);
  QVBoxLayout *mainLayout = new QVBoxLayout(content);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  // Mode switcher bar: FILES | SYMBOLS
  QWidget *modeBar = new QWidget(content);
  modeBar->setObjectName("projectModeBar");
  QHBoxLayout *modeLayout = new QHBoxLayout(modeBar);
  modeLayout->setContentsMargins(2, 2, 2, 2);
  modeLayout->setSpacing(2);

  m_filesModeBtn = new QPushButton("FILES", modeBar);
  m_filesModeBtn->setObjectName("explorerFilesModeBtn");
  m_filesModeBtn->setCheckable(true);
  m_filesModeBtn->setChecked(true);
  m_filesModeBtn->setFixedHeight(22);

  m_symbolsModeBtn = new QPushButton("SYMBOLS", modeBar);
  m_symbolsModeBtn->setObjectName("explorerSymbolsModeBtn");
  m_symbolsModeBtn->setCheckable(true);
  m_symbolsModeBtn->setChecked(false);
  m_symbolsModeBtn->setFixedHeight(22);

  modeLayout->addWidget(m_filesModeBtn, 1);
  modeLayout->addWidget(m_symbolsModeBtn, 1);
  mainLayout->addWidget(modeBar);

  m_stack = new QStackedWidget(content);

  // Page 0: Files View
  QWidget *filesPage = new QWidget(m_stack);
  QVBoxLayout *filesLayout = new QVBoxLayout(filesPage);
  filesLayout->setContentsMargins(0, 0, 0, 0);
  filesLayout->setSpacing(0);

  QWidget *topBar = new QWidget(filesPage);
  QHBoxLayout *topLayout = new QHBoxLayout(topBar);
  topLayout->setContentsMargins(4, 2, 2, 2);
  topLayout->setSpacing(4);

  m_openFolderBtn = new QPushButton("Open Folder", topBar);
  m_openFolderBtn->setFixedHeight(22);
  m_collapseAllBtn = new QPushButton(topBar);
  m_collapseAllBtn->setText(QString::fromUtf8("\xEF\x86\x92"));
  m_collapseAllBtn->setFixedSize(24, 22);
  m_collapseAllBtn->setFlat(true);
  m_collapseAllBtn->setStyleSheet("font-family: 'Symbols Nerd Font'; font-size: 16px;");

  topLayout->addWidget(m_openFolderBtn);
  topLayout->addStretch();
  topLayout->addWidget(m_collapseAllBtn);
  filesLayout->addWidget(topBar);

  m_treeView = new QTreeView(filesPage);
  m_treeView->setHeaderHidden(true);

  m_fileSystemModel = new QFileSystemModel(this);
  m_fileSystemModel->setFilter(QDir::NoDotAndDotDot | QDir::AllDirs |
                               QDir::Files);
  m_fileSystemModel->setRootPath(QDir::currentPath());

  m_iconProvider = new FileIconProvider();
  m_fileSystemModel->setIconProvider(m_iconProvider);
  m_treeView->setModel(m_fileSystemModel);
  m_treeView->setRootIndex(m_fileSystemModel->index(QDir::currentPath()));

  reloadStyle();

  m_treeView->hideColumn(1);
  m_treeView->hideColumn(2);
  m_treeView->hideColumn(3);

  filesLayout->addWidget(m_treeView, 1);
  m_stack->addWidget(filesPage);

  // Page 1: Symbols & Functions View
  QWidget *symbolsPage = new QWidget(m_stack);
  QVBoxLayout *symbolsLayout = new QVBoxLayout(symbolsPage);
  symbolsLayout->setContentsMargins(0, 0, 0, 0);
  symbolsLayout->setSpacing(0);

  QWidget *filterBar = new QWidget(symbolsPage);
  QHBoxLayout *filterLayout = new QHBoxLayout(filterBar);
  filterLayout->setContentsMargins(3, 3, 3, 3);
  filterLayout->setSpacing(2);

  m_symbolFilterEdit = new QLineEdit(filterBar);
  m_symbolFilterEdit->setObjectName("symbolFilterEdit");
  m_symbolFilterEdit->setPlaceholderText("Filter functions / symbols...");
  m_symbolFilterEdit->setFixedHeight(22);
  filterLayout->addWidget(m_symbolFilterEdit);
  symbolsLayout->addWidget(filterBar);

  m_symbolsTable = new QTableWidget(0, 3, symbolsPage);
  m_symbolsTable->setObjectName("symbolsTable");
  m_symbolsTable->setHorizontalHeaderLabels({"SYMBOL", "ADDRESS", "SIZE"});
  m_symbolsTable->verticalHeader()->setVisible(false);
  m_symbolsTable->verticalHeader()->setDefaultSectionSize(20);
  m_symbolsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_symbolsTable->setSelectionMode(QAbstractItemView::SingleSelection);
  m_symbolsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_symbolsTable->setShowGrid(true);
  m_symbolsTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_symbolsTable->horizontalHeader()->setMinimumSectionSize(34);
  m_symbolsTable->horizontalHeader()->setStretchLastSection(false);
  m_symbolsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  m_symbolsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
  m_symbolsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
  m_symbolsTable->setColumnWidth(1, 88);
  m_symbolsTable->setColumnWidth(2, 44);

  symbolsLayout->addWidget(m_symbolsTable, 1);
  m_stack->addWidget(symbolsPage);

  mainLayout->addWidget(m_stack, 1);
  setWidget(content);

  seedDefaultSymbols();
  populateSymbolsTable();

  connect(m_filesModeBtn, &QPushButton::clicked, this, [this]() {
    setSymbolsMode(false);
  });
  connect(m_symbolsModeBtn, &QPushButton::clicked, this, [this]() {
    setSymbolsMode(true);
  });
  connect(m_symbolFilterEdit, &QLineEdit::textChanged, this,
          &ProjectExplorerWidget::filterSymbols);
  connect(m_symbolsTable, &QTableWidget::cellDoubleClicked, this,
          [this](int row, int /*col*/) {
            if (auto *symItem = m_symbolsTable->item(row, 0)) {
              activateSymbol(symItem->text());
            }
          });
  connect(m_symbolsTable, &QTableWidget::cellActivated, this,
          [this](int row, int /*col*/) {
            if (auto *symItem = m_symbolsTable->item(row, 0)) {
              activateSymbol(symItem->text());
            }
          });

  connect(m_openFolderBtn, &QPushButton::clicked, this,
          &ProjectExplorerWidget::onOpenFolder);
  connect(m_collapseAllBtn, &QPushButton::clicked, m_treeView,
          &QTreeView::collapseAll);
  connect(m_treeView, &QTreeView::doubleClicked, this,
          &ProjectExplorerWidget::onDoubleClicked);

  this->setObjectName("ProjectExplorerWidget");

  this->setStyleSheet(
      "#ProjectExplorerWidget::close-button, #ProjectExplorerWidget::float-button {"
      "    icon-size: 14px;"
      "    width: 20px;"
      "    height: 20px;"
      "}"
      "#ProjectExplorerWidget::close-button:hover, #ProjectExplorerWidget::float-button:hover {"
      "    background: rgba(255, 255, 255, 0.1);"
      "    border-radius: 3px;"
      "}");

  // Default to Symbols view so the binary symbol table is immediately inspectable
  setSymbolsMode(true);
}

ProjectExplorerWidget::~ProjectExplorerWidget() { delete m_iconProvider; }

void ProjectExplorerWidget::seedDefaultSymbols() {
  m_symbols = {
      {QStringLiteral("_start"), 0x00401000ULL, 32},
      {QStringLiteral("MPI_Init@plt"), 0x00401050ULL, 16},
      {QStringLiteral("MPI_Finalize@plt"), 0x00401060ULL, 16},
      {QStringLiteral("sym.main"), 0x00401120ULL, 58},
      {QStringLiteral("sym.init_deeply_nested"), 0x00401142ULL, 38},
      {QStringLiteral("sym.rank_coordinator"), 0x00401168ULL, 64},
  };
}

void ProjectExplorerWidget::populateSymbolsTable(const QString &filterText) {
  if (!m_symbolsTable) {
    return;
  }
  const QString query = filterText.trimmed();
  m_symbolsTable->setRowCount(0);

  for (const auto &entry : m_symbols) {
    const QString addrStr =
        QStringLiteral("0x%1").arg(entry.address, 8, 16, QChar('0'));
    if (!query.isEmpty() && !entry.name.contains(query, Qt::CaseInsensitive) &&
        !addrStr.contains(query, Qt::CaseInsensitive)) {
      continue;
    }

    const int row = m_symbolsTable->rowCount();
    m_symbolsTable->insertRow(row);

    auto *nameItem = new QTableWidgetItem(entry.name);
    nameItem->setForeground(QColor("#dcdcaa"));
    nameItem->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(entry.address));

    auto *addrItem = new QTableWidgetItem(addrStr);
    addrItem->setForeground(QColor("#4ec9b0"));

    auto *sizeItem = new QTableWidgetItem(QString::number(entry.size));
    sizeItem->setForeground(QColor("#b5cea8"));
    sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_symbolsTable->setItem(row, 0, nameItem);
    m_symbolsTable->setItem(row, 1, addrItem);
    m_symbolsTable->setItem(row, 2, sizeItem);
  }

  if (m_symbolsTable->rowCount() > 0) {
    m_symbolsTable->selectRow(3 < m_symbolsTable->rowCount() ? 3 : 0);
  }
}

void ProjectExplorerWidget::setSymbolsMode(bool enabled) {
  if (m_stack) {
    m_stack->setCurrentIndex(enabled ? 1 : 0);
  }
  if (m_filesModeBtn) {
    m_filesModeBtn->setChecked(!enabled);
  }
  if (m_symbolsModeBtn) {
    m_symbolsModeBtn->setChecked(enabled);
  }
}

bool ProjectExplorerWidget::isSymbolsMode() const {
  return m_stack && m_stack->currentIndex() == 1;
}

int ProjectExplorerWidget::symbolCount() const {
  return m_symbolsTable ? m_symbolsTable->rowCount()
                        : static_cast<int>(m_symbols.size());
}

bool ProjectExplorerWidget::activateSymbol(const QString &symbolName) {
  const QString query = symbolName.trimmed();
  if (query.isEmpty()) {
    return false;
  }

  for (const auto &entry : m_symbols) {
    if (entry.name.compare(query, Qt::CaseInsensitive) == 0 ||
        (query.compare(QStringLiteral("main"), Qt::CaseInsensitive) == 0 &&
         entry.name == QStringLiteral("sym.main"))) {
      if (m_symbolsTable) {
        for (int r = 0; r < m_symbolsTable->rowCount(); ++r) {
          if (auto *item = m_symbolsTable->item(r, 0)) {
            if (item->text().compare(entry.name, Qt::CaseInsensitive) == 0) {
              m_symbolsTable->selectRow(r);
              break;
            }
          }
        }
      }
      emit symbolAddressSelected(entry.address, entry.name);
      return true;
    }
  }
  return false;
}

void ProjectExplorerWidget::filterSymbols(const QString &filterText) {
  populateSymbolsTable(filterText);
}

void ProjectExplorerWidget::reloadStyle() {
  QSettings s("gridlock", "debugger");
  QString style =
      s.value("appearance/file_tree_style", "Comfortable").toString();

  int padding = 2;
  int fontSize = 11;
  if (style == "Compact") {
    padding = 1;
    fontSize = 10;
  } else if (style == "Large") {
    padding = 4;
    fontSize = 13;
  }

  m_treeView->setStyleSheet(QString("QTreeView { font-size: %2px; }"
                                    "QTreeView::item { padding: %1px; }")
                                .arg(padding)
                                .arg(fontSize));

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
