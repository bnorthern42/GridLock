/**
 * @file ProjectExplorerWidget.cpp
 * @brief Multi-mode project explorer, symbol table, and binary strings inspector implementation.
 *
 * Implements ProjectExplorerWidget, providing stacked views for filesystem directory browsing,
 * ELF/PE symbol searching, and binary string inspection with synchronized address seek dispatches.
 */

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

#include "../../core/managers/ThemeManager.hpp"
#include "FileIconProvider.hpp"

namespace gridlock::ui {

namespace {
/**
 * @class FlexibleDockContent
 * @brief Container widget providing flexible layout size hints for docking.
 */
class FlexibleDockContent : public QWidget {
public:
    using QWidget::QWidget;
    QSize sizeHint() const override { return QSize(260, 500); }
    QSize minimumSizeHint() const override { return QSize(150, 120); }
};
}  // namespace

/**
 * @brief Constructs a new ProjectExplorerWidget dock panel.
 *
 * Initializes the top mode switcher buttons (FILES, SYMBOLS, STRINGS),
 * constructs stacked pages for each inspection mode, wires search filters,
 * and sets up selection signal connections.
 *
 * @param parent Optional parent widget.
 */
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

  // Configure borderless file system tree view with visible column header and 48px minimum section size.
  m_treeView = new QTreeView(filesPage);
  m_treeView->setFrameShape(QFrame::NoFrame);
  m_treeView->setAlternatingRowColors(true);
  m_treeView->setHeaderHidden(false);
  m_treeView->header()->setVisible(true);
  m_treeView->header()->setMinimumSectionSize(48);
  m_treeView->header()->setStretchLastSection(true);

  // Initialize Qt standard file system model for directory traversal
  m_fileSystemModel = new QFileSystemModel(this);
  m_fileSystemModel->setFilter(QDir::NoDotAndDotDot | QDir::AllDirs |
                               QDir::Files);
  m_fileSystemModel->setRootPath(QDir::currentPath());

  // Attach high-DPI theme-aware icon provider to model
  m_iconProvider = new FileIconProvider();
  m_fileSystemModel->setIconProvider(m_iconProvider);
  m_treeView->setModel(m_fileSystemModel);
  m_treeView->setRootIndex(m_fileSystemModel->index(QDir::currentPath()));

  // Apply font size and item padding from configuration
  reloadStyle();

  // Hide file size, type, and date columns in the compact sidebar
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

  // Filter bar housing live symbol substring search input
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

  // Three-column symbols table: SYMBOL, ADDRESS, SIZE
  m_symbolsTable = new QTableWidget(0, 3, symbolsPage);
  m_symbolsTable->setObjectName("symbolsTable");
  m_symbolsTable->setFrameShape(QFrame::NoFrame);
  m_symbolsTable->setAlternatingRowColors(true);
  m_symbolsTable->setHorizontalHeaderLabels({"SYMBOL", "ADDRESS", "SIZE"});
  m_symbolsTable->verticalHeader()->setVisible(false);
  m_symbolsTable->verticalHeader()->setDefaultSectionSize(20);
  m_symbolsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_symbolsTable->setSelectionMode(QAbstractItemView::SingleSelection);
  m_symbolsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_symbolsTable->setShowGrid(true);
  m_symbolsTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_symbolsTable->horizontalHeader()->setVisible(true);
  m_symbolsTable->horizontalHeader()->setMinimumSectionSize(42);
  m_symbolsTable->horizontalHeader()->setStretchLastSection(false);
  // Column 0 stretches with window width, columns 1 and 2 fit contents tightly (78px / 42px)
  m_symbolsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  m_symbolsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  m_symbolsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  m_symbolsTable->setColumnWidth(1, 78);
  m_symbolsTable->setColumnWidth(2, 42);

  symbolsLayout->addWidget(m_symbolsTable, 1);
  m_stack->addWidget(symbolsPage);

  // Page 2: Strings View (.rodata / .dynstr)
  QWidget *stringsPage = new QWidget(m_stack);
  QVBoxLayout *stringsLayout = new QVBoxLayout(stringsPage);
  stringsLayout->setContentsMargins(0, 0, 0, 0);
  stringsLayout->setSpacing(0);

  // Filter bar housing live binary strings search input
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

  // Three-column binary strings table: ADDRESS, SECTION, STRING
  m_stringsTable = new QTableWidget(0, 3, stringsPage);
  m_stringsTable->setObjectName("stringsTable");
  m_stringsTable->setFrameShape(QFrame::NoFrame);
  m_stringsTable->setAlternatingRowColors(true);
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
  m_stringsTable->horizontalHeader()->setVisible(true);
  m_stringsTable->horizontalHeader()->setMinimumSectionSize(48);
  m_stringsTable->horizontalHeader()->setStretchLastSection(true);
  // Address and section columns have fixed widths >= 48px, string value stretches
  m_stringsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
  m_stringsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
  m_stringsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
  m_stringsTable->setColumnWidth(0, 82);
  m_stringsTable->setColumnWidth(1, 64);

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

  // Mode switcher button slots
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

  // Re-render symbol and string table foregrounds whenever Dark/Light mode toggles
  connect(&gridlock::core::managers::ThemeManager::instance(),
          &gridlock::core::managers::ThemeManager::themeModeChanged, this, [this](bool) {
            populateSymbolsTable(m_symbolFilterEdit ? m_symbolFilterEdit->text() : QString());
            populateStringsTable(m_stringFilterEdit ? m_stringFilterEdit->text() : QString());
          });
}


/**
 * @brief Destructor releasing the file icon provider.
 */
ProjectExplorerWidget::~ProjectExplorerWidget() { delete m_iconProvider; }

/**
 * @brief Seeds initial default binary symbols for simulation and preview fixtures.
 */
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

/**
 * @brief Seeds initial binary strings from simulated .rodata and .dynstr sections.
 */
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

/**
 * @brief Sets externally parsed symbols and refreshes the symbols table view.
 * @param symbols Vector of symbol entries.
 */
void ProjectExplorerWidget::setSymbols(const std::vector<SymbolEntry>& symbols) {
  m_symbols = symbols;
  populateSymbolsTable(m_symbolFilterEdit ? m_symbolFilterEdit->text() : QString());
}

/**
 * @brief Sets externally parsed strings and refreshes the strings table view.
 * @param strings Vector of string entries.
 */
void ProjectExplorerWidget::setStrings(const std::vector<StringEntry>& strings) {
  m_strings = strings;
  populateStringsTable(m_stringFilterEdit ? m_stringFilterEdit->text() : QString());
}

/**
 * @brief Populates the symbols QTableWidget, applying an optional substring filter.
 * @param filterText Filter query matched against symbol name or hexadecimal address.
 */
void ProjectExplorerWidget::populateSymbolsTable(const QString &filterText) {
  if (!m_symbolsTable) {
    return;
  }
  const bool isDark = gridlock::core::managers::ThemeManager::instance().isDarkMode();
  QPalette symPal = m_symbolsTable->palette();
  symPal.setColor(QPalette::Base, isDark ? QColor("#16181d") : QColor("#ffffff"));
  symPal.setColor(QPalette::Text, isDark ? QColor("#d7dae0") : QColor("#1f2328"));
  m_symbolsTable->setPalette(symPal);

  const QString query = filterText.trimmed();
  m_symbolsTable->setRowCount(0);

  // Iterate loaded symbols matching query
  for (const auto &entry : m_symbols) {
    const QString addrStr =
        QStringLiteral("0x%1").arg(entry.address, 8, 16, QChar('0'));
    if (!query.isEmpty() && !entry.name.contains(query, Qt::CaseInsensitive) &&
        !addrStr.contains(query, Qt::CaseInsensitive)) {
      continue;
    }

    const int row = m_symbolsTable->rowCount();
    m_symbolsTable->insertRow(row);

    // Symbol name in pastel yellow (dark mode) or dark amber (light mode)
    auto *nameItem = new QTableWidgetItem(entry.name);
    nameItem->setForeground(isDark ? QColor("#dcdcaa") : QColor("#953800"));
    nameItem->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(entry.address));

    // Hex address in cyan (dark mode) or deep navy (light mode)
    auto *addrItem = new QTableWidgetItem(addrStr);
    addrItem->setForeground(isDark ? QColor("#4ec9b0") : QColor("#0550ae"));

    // Size in bytes
    auto *sizeItem = new QTableWidgetItem(QString::number(entry.size));
    sizeItem->setForeground(isDark ? QColor("#b5cea8") : QColor("#116329"));
    sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_symbolsTable->setItem(row, 0, nameItem);
    m_symbolsTable->setItem(row, 1, addrItem);
    m_symbolsTable->setItem(row, 2, sizeItem);
  }

  if (m_symbolsTable->rowCount() > 0) {
    m_symbolsTable->selectRow(3 < m_symbolsTable->rowCount() ? 3 : 0);
  }
}

/**
 * @brief Populates the binary strings QTableWidget with optional substring filtering.
 * @param filterText Filter query matched against string value, section, or address.
 */
void ProjectExplorerWidget::populateStringsTable(const QString &filterText) {
  if (!m_stringsTable) {
    return;
  }
  const bool isDark = gridlock::core::managers::ThemeManager::instance().isDarkMode();
  QPalette strPal = m_stringsTable->palette();
  strPal.setColor(QPalette::Base, isDark ? QColor("#16181d") : QColor("#ffffff"));
  strPal.setColor(QPalette::Text, isDark ? QColor("#d7dae0") : QColor("#1f2328"));
  m_stringsTable->setPalette(strPal);

  const QString query = filterText.trimmed();
  m_stringsTable->setRowCount(0);

  // Iterate strings matching filter
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
    addrItem->setForeground(isDark ? QColor("#4ec9b0") : QColor("#0550ae"));
    addrItem->setData(Qt::UserRole,
                      QVariant::fromValue<qulonglong>(entry.address));

    auto *secItem = new QTableWidgetItem(entry.section);
    secItem->setForeground(isDark ? QColor("#569cd6") : QColor("#0969da"));

    auto *valItem = new QTableWidgetItem(entry.value);
    valItem->setForeground(isDark ? QColor("#ce9178") : QColor("#953800"));

    m_stringsTable->setItem(row, 0, addrItem);
    m_stringsTable->setItem(row, 1, secItem);
    m_stringsTable->setItem(row, 2, valItem);
  }

  if (m_stringsTable->rowCount() > 0) {
    m_stringsTable->selectRow(0);
  }
}

/**
 * @brief Switches the active view between Files mode (page 0) and Symbols mode (page 1).
 * @param enabled True to show Symbols table; false to show Files tree.
 */
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

/**
 * @brief Checks if the active view is Symbols mode.
 * @return True if showing Symbols table.
 */
bool ProjectExplorerWidget::isSymbolsMode() const {
  return m_stack && m_stack->currentIndex() == 1;
}

/**
 * @brief Returns total number of visible symbols in the table.
 * @return Row count.
 */
int ProjectExplorerWidget::symbolCount() const {
  return m_symbolsTable ? m_symbolsTable->rowCount()
                        : static_cast<int>(m_symbols.size());
}

/**
 * @brief Switches the active stacked page to Strings mode (page 2).
 * @param enabled True to show Strings table.
 */
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

/**
 * @brief Checks if the active view is Strings mode.
 * @return True if showing Strings table.
 */
bool ProjectExplorerWidget::isStringsMode() const {
  return m_stack && m_stack->currentIndex() == 2;
}

/**
 * @brief Returns total number of visible strings in the table.
 * @return Row count.
 */
int ProjectExplorerWidget::stringCount() const {
  return m_stringsTable ? m_stringsTable->rowCount()
                        : static_cast<int>(m_strings.size());
}

/**
 * @brief Selects a symbol by name in the table and emits symbolAddressSelected.
 * @param symbolName Target symbol name.
 * @return True if symbol found; false otherwise.
 */
bool ProjectExplorerWidget::activateSymbol(const QString &symbolName) {
  // Strip whitespace from query
  const QString query = symbolName.trimmed();
  if (query.isEmpty()) {
    return false;
  }

  // Linear scan across registered binary symbols
  for (const auto &entry : m_symbols) {
    // Match exact symbol name or alias "main" -> "sym.main"
    if (entry.name.compare(query, Qt::CaseInsensitive) == 0 ||
        (query.compare(QStringLiteral("main"), Qt::CaseInsensitive) == 0 &&
         entry.name == QStringLiteral("sym.main"))) {
      // Synchronize visual selection in symbols table
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
      // Emit signal to navigate disassembly, decompiler, and memory views
      emit symbolAddressSelected(entry.address, entry.name);
      return true;
    }
  }
  return false;
}

/**
 * @brief Selects a string by substring or hex address and emits symbolAddressSelected.
 * @param queryText Query string.
 * @return True if match found; false otherwise.
 */
bool ProjectExplorerWidget::activateString(const QString &queryText) {
  // Clean input string
  const QString query = queryText.trimmed();
  if (query.isEmpty()) {
    return false;
  }

  // Scan extracted strings table
  for (const auto &entry : m_strings) {
    const QString addrStr =
        QStringLiteral("0x%1").arg(entry.address, 8, 16, QChar('0'));
    // Match against content substring or hexadecimal address
    if (entry.value.contains(query, Qt::CaseInsensitive) ||
        addrStr.compare(query, Qt::CaseInsensitive) == 0) {
      // Select matching row in strings table
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
      // Emit signal to navigate views to this string's address
      emit symbolAddressSelected(entry.address, entry.value);
      return true;
    }
  }
  return false;
}

/**
 * @brief Filters the symbols table on query changes.
 * @param filterText Search substring.
 */
void ProjectExplorerWidget::filterSymbols(const QString &filterText) {
  populateSymbolsTable(filterText);
}

/**
 * @brief Filters the strings table on query changes.
 * @param filterText Search substring.
 */
void ProjectExplorerWidget::filterStrings(const QString &filterText) {
  populateStringsTable(filterText);
}

/**
 * @brief Reloads font size, item padding, and icon provider from configuration.
 */
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

/**
 * @brief Prompts user for a directory and opens it in the file tree view.
 */
void ProjectExplorerWidget::onOpenFolder() {
  QString dir = QFileDialog::getExistingDirectory(
      this, "Open Folder", QDir::currentPath(),
      QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
  if (!dir.isEmpty()) {
    m_fileSystemModel->setRootPath(dir);
    m_treeView->setRootIndex(m_fileSystemModel->index(dir));
  }
}

/**
 * @brief Sets the root filesystem path displayed in the tree view.
 * @param path Directory path.
 */
void ProjectExplorerWidget::setRootPath(const QString &path) {
  if (!path.isEmpty()) {
    m_fileSystemModel->setRootPath(path);
    m_treeView->setRootIndex(m_fileSystemModel->index(path));
  }
}

/**
 * @brief Handles double clicking file items to emit fileDoubleClicked.
 * @param index Model index of clicked node.
 */
void ProjectExplorerWidget::onDoubleClicked(const QModelIndex &index) {
  if (!index.isValid())
    return;

  QFileInfo info = m_fileSystemModel->fileInfo(index);
  if (info.isFile()) {
    emit fileDoubleClicked(info.absoluteFilePath());
  }
}

}  // namespace gridlock::ui

