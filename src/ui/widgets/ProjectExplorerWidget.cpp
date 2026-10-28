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

namespace {
class FlexibleDockContent : public QWidget {
public:
  using QWidget::QWidget;
  QSize sizeHint() const override { return QSize(260, 500); }
  QSize minimumSizeHint() const override { return QSize(150, 120); }
};
} // namespace

ProjectExplorerWidget::ProjectExplorerWidget(QWidget *parent)
    : QDockWidget("Project / Symbols", parent) {
  m_iconProvider = nullptr;
  QWidget *content = new FlexibleDockContent(this);
  QVBoxLayout *mainLayout = new QVBoxLayout(content);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  // Mode switcher bar: FILES | SYMBOLS | STRINGS
  QWidget *modeBar = new QWidget(content);
  modeBar->setObjectName("projectModeBar");
  modeBar->setFixedHeight(24);
  QHBoxLayout *modeLayout = new QHBoxLayout(modeBar);
  modeLayout->setContentsMargins(3, 2, 3, 2);
  modeLayout->setSpacing(2);

  m_filesModeBtn = new QPushButton("FILES", modeBar);
  m_filesModeBtn->setObjectName("explorerFilesModeBtn");
  m_filesModeBtn->setCheckable(true);
  m_filesModeBtn->setChecked(true);
  m_filesModeBtn->setFixedHeight(20);

  m_symbolsModeBtn = new QPushButton("SYMBOLS", modeBar);
  m_symbolsModeBtn->setObjectName("explorerSymbolsModeBtn");
  m_symbolsModeBtn->setCheckable(true);
  m_symbolsModeBtn->setChecked(false);
  m_symbolsModeBtn->setFixedHeight(20);

  m_stringsModeBtn = new QPushButton("STRINGS", modeBar);
  m_stringsModeBtn->setObjectName("explorerStringsModeBtn");
  m_stringsModeBtn->setCheckable(true);
  m_stringsModeBtn->setChecked(false);
  m_stringsModeBtn->setFixedHeight(20);

  modeBar->setStyleSheet(
      "QWidget#projectModeBar { background: #21252b; border-bottom: 1px solid #2c313a; }"
      "QPushButton { background: #252932; color: #abb2bf; border: 1px solid #323844; "
      "border-radius: 2px; padding: 1px 6px; font-family: 'JetBrains Mono', monospace; "
      "font-size: 10px; font-weight: 600; min-height: 18px; max-height: 20px; }"
      "QPushButton:hover { background: #2c323d; border: 1px solid #3e4452; color: #ffffff; }"
      "QPushButton:checked { background: #2c3444; color: #ffffff; border: 1px solid #528bff; }");

  modeLayout->addWidget(m_filesModeBtn, 1);
  modeLayout->addWidget(m_symbolsModeBtn, 1);
  modeLayout->addWidget(m_stringsModeBtn, 1);
  mainLayout->addWidget(modeBar);

  m_stack = new QStackedWidget(content);

  // Page 0: Files View
  QWidget *filesPage = new QWidget(m_stack);
  QVBoxLayout *filesLayout = new QVBoxLayout(filesPage);
  filesLayout->setContentsMargins(0, 0, 0, 0);
  filesLayout->setSpacing(0);

  QWidget *topBar = new QWidget(filesPage);
  topBar->setFixedHeight(24);
  QHBoxLayout *topLayout = new QHBoxLayout(topBar);
  topLayout->setContentsMargins(4, 2, 2, 2);
  topLayout->setSpacing(4);

  m_openFolderBtn = new QPushButton("Open Folder", topBar);
  m_openFolderBtn->setFixedHeight(20);
  m_collapseAllBtn = new QPushButton(topBar);
  m_collapseAllBtn->setText(QString::fromUtf8("\xEF\x86\x92"));
  m_collapseAllBtn->setFixedSize(22, 20);
  m_collapseAllBtn->setFlat(true);
  m_collapseAllBtn->setStyleSheet("font-family: 'Symbols Nerd Font'; font-size: 14px;");

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
  filterBar->setFixedHeight(26);
  QHBoxLayout *filterLayout = new QHBoxLayout(filterBar);
  filterLayout->setContentsMargins(3, 2, 3, 2);
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

  // Page 2: Strings View (.rodata / .dynstr)
  QWidget *stringsPage = new QWidget(m_stack);
  QVBoxLayout *stringsLayout = new QVBoxLayout(stringsPage);
  stringsLayout->setContentsMargins(0, 0, 0, 0);
  stringsLayout->setSpacing(0);

  QWidget *strFilterBar = new QWidget(stringsPage);
  QHBoxLayout *strFilterLayout = new QHBoxLayout(strFilterBar);
  strFilterLayout->setContentsMargins(3, 3, 3, 3);
  strFilterLayout->setSpacing(2);

  m_stringFilterEdit = new QLineEdit(strFilterBar);
  m_stringFilterEdit->setObjectName("stringFilterEdit");
  m_stringFilterEdit->setPlaceholderText("Filter strings...");
  m_stringFilterEdit->setFixedHeight(22);
  m_stringFilterEdit->setStyleSheet(
      "background: #141619; color: #d4d7dc; border: 1px solid #333842; "
      "border-radius: 2px; padding: 1px 5px; font-family: 'JetBrains Mono', monospace; "
      "font-size: 10px;");
  strFilterLayout->addWidget(m_stringFilterEdit);
  stringsLayout->addWidget(strFilterBar);

  m_stringsTable = new QTableWidget(0, 3, stringsPage);
  m_stringsTable->setObjectName("stringsTable");
  m_stringsTable->setStyleSheet(
      "background: #181a1f; border: none; font-family: 'JetBrains Mono', monospace; "
      "font-size: 11px;");
  m_stringsTable->setHorizontalHeaderLabels({"ADDRESS", "SECTION", "STRING"});
  m_stringsTable->verticalHeader()->setVisible(false);
  m_stringsTable->verticalHeader()->setDefaultSectionSize(20);
  m_stringsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_stringsTable->setSelectionMode(QAbstractItemView::SingleSelection);
  m_stringsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_stringsTable->setShowGrid(true);
  m_stringsTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_stringsTable->horizontalHeader()->setMinimumSectionSize(34);
  m_stringsTable->horizontalHeader()->setStretchLastSection(true);
  m_stringsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
  m_stringsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
  m_stringsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
  m_stringsTable->setColumnWidth(0, 82);
  m_stringsTable->setColumnWidth(1, 58);

  stringsLayout->addWidget(m_stringsTable, 1);
  m_stack->addWidget(stringsPage);

  mainLayout->addWidget(m_stack, 1);
  content->setMinimumWidth(150);
  content->setMaximumWidth(QWIDGETSIZE_MAX);
  content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  setWidget(content);
  setMinimumWidth(150);
  setMaximumWidth(QWIDGETSIZE_MAX);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

  seedDefaultSymbols();
  populateSymbolsTable();
  seedDefaultStrings();
  populateStringsTable();

  connect(m_filesModeBtn, &QPushButton::clicked, this, [this]() {
    setSymbolsMode(false);
  });
  connect(m_symbolsModeBtn, &QPushButton::clicked, this, [this]() {
    setSymbolsMode(true);
  });
  connect(m_stringsModeBtn, &QPushButton::clicked, this, [this]() {
    setStringsMode(true);
  });
  connect(m_symbolFilterEdit, &QLineEdit::textChanged, this,
          &ProjectExplorerWidget::filterSymbols);
  connect(m_stringFilterEdit, &QLineEdit::textChanged, this,
          &ProjectExplorerWidget::filterStrings);
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
  connect(m_stringsTable, &QTableWidget::cellDoubleClicked, this,
          [this](int row, int /*col*/) {
            if (auto *strItem = m_stringsTable->item(row, 2)) {
              activateString(strItem->text());
            }
          });
  connect(m_stringsTable, &QTableWidget::cellActivated, this,
          [this](int row, int /*col*/) {
            if (auto *strItem = m_stringsTable->item(row, 2)) {
              activateString(strItem->text());
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

void ProjectExplorerWidget::seedDefaultStrings() {
  m_strings = {
      {0x00402000ULL, QStringLiteral(".rodata"),
       QStringLiteral("MPI_COMM_WORLD")},
      {0x00402010ULL, QStringLiteral(".rodata"),
       QStringLiteral("DeeplyNested rank=%d id=%d")},
      {0x00402030ULL, QStringLiteral(".rodata"),
       QStringLiteral("gridlock_rank_sync_barrier")},
      {0x00402050ULL, QStringLiteral(".dynstr"),
       QStringLiteral("libmpi.so.40")},
      {0x00402060ULL, QStringLiteral(".dynstr"),
       QStringLiteral("GLIBC_2.38")},
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

void ProjectExplorerWidget::populateStringsTable(const QString &filterText) {
  if (!m_stringsTable) {
    return;
  }
  const QString query = filterText.trimmed();
  m_stringsTable->setRowCount(0);

  for (const auto &entry : m_strings) {
    const QString addrStr =
        QStringLiteral("0x%1").arg(entry.address, 8, 16, QChar('0'));
    if (!query.isEmpty() && !entry.value.contains(query, Qt::CaseInsensitive) &&
        !entry.section.contains(query, Qt::CaseInsensitive) &&
        !addrStr.contains(query, Qt::CaseInsensitive)) {
      continue;
    }

    const int row = m_stringsTable->rowCount();
    m_stringsTable->insertRow(row);

    auto *addrItem = new QTableWidgetItem(addrStr);
    addrItem->setForeground(QColor("#4ec9b0"));
    addrItem->setData(Qt::UserRole,
                      QVariant::fromValue<qulonglong>(entry.address));

    auto *secItem = new QTableWidgetItem(entry.section);
    secItem->setForeground(QColor("#569cd6"));

    auto *valItem = new QTableWidgetItem(entry.value);
    valItem->setForeground(QColor("#ce9178"));

    m_stringsTable->setItem(row, 0, addrItem);
    m_stringsTable->setItem(row, 1, secItem);
    m_stringsTable->setItem(row, 2, valItem);
  }

  if (m_stringsTable->rowCount() > 0) {
    m_stringsTable->selectRow(0);
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
  if (m_stringsModeBtn) {
    m_stringsModeBtn->setChecked(false);
  }
}

bool ProjectExplorerWidget::isSymbolsMode() const {
  return m_stack && m_stack->currentIndex() == 1;
}

int ProjectExplorerWidget::symbolCount() const {
  return m_symbolsTable ? m_symbolsTable->rowCount()
                        : static_cast<int>(m_symbols.size());
}

void ProjectExplorerWidget::setStringsMode(bool enabled) {
  if (!enabled) {
    setSymbolsMode(true);
    return;
  }
  if (m_stack) {
    m_stack->setCurrentIndex(2);
  }
  if (m_filesModeBtn) {
    m_filesModeBtn->setChecked(false);
  }
  if (m_symbolsModeBtn) {
    m_symbolsModeBtn->setChecked(false);
  }
  if (m_stringsModeBtn) {
    m_stringsModeBtn->setChecked(true);
  }
}

bool ProjectExplorerWidget::isStringsMode() const {
  return m_stack && m_stack->currentIndex() == 2;
}

int ProjectExplorerWidget::stringCount() const {
  return m_stringsTable ? m_stringsTable->rowCount()
                        : static_cast<int>(m_strings.size());
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

bool ProjectExplorerWidget::activateString(const QString &queryText) {
  const QString query = queryText.trimmed();
  if (query.isEmpty()) {
    return false;
  }

  for (const auto &entry : m_strings) {
    const QString addrStr =
        QStringLiteral("0x%1").arg(entry.address, 8, 16, QChar('0'));
    if (entry.value.contains(query, Qt::CaseInsensitive) ||
        addrStr.compare(query, Qt::CaseInsensitive) == 0) {
      if (m_stringsTable) {
        for (int r = 0; r < m_stringsTable->rowCount(); ++r) {
          if (auto *item = m_stringsTable->item(r, 2)) {
            if (item->text() == entry.value) {
              m_stringsTable->selectRow(r);
              break;
            }
          }
        }
      }
      emit symbolAddressSelected(entry.address, entry.value);
      return true;
    }
  }
  return false;
}

void ProjectExplorerWidget::filterSymbols(const QString &filterText) {
  populateSymbolsTable(filterText);
}

void ProjectExplorerWidget::filterStrings(const QString &filterText) {
  populateStringsTable(filterText);
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
