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
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QSettings>
#include <QShortcut>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include "../../core/managers/ThemeManager.hpp"
#include "../utils/FileDialogHelper.hpp"
#include "FileIconProvider.hpp"

namespace gridlock::ui {

namespace {
/** @brief Container widget providing flexible layout size hints for docking. */
class FlexibleDockContent : public QWidget {
public:
    using QWidget::QWidget;
    QSize sizeHint() const override { return QSize(260, 500); }
    QSize minimumSizeHint() const override { return QSize(150, 120); }
};

/** @brief QTableWidgetItem sorting by numeric Qt::UserRole (hex addresses / sizes) when present. */
class NumericSortTableItem : public QTableWidgetItem {
public:
    using QTableWidgetItem::QTableWidgetItem;
    bool operator<(const QTableWidgetItem& other) const override {
        const QVariant a = data(Qt::UserRole), b = other.data(Qt::UserRole);
        if (a.isValid() && b.isValid() && a.canConvert<qulonglong>() && b.canConvert<qulonglong>()) {
            return a.toULongLong() < b.toULongLong();
        }
        return QTableWidgetItem::operator<(other);
    }
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

  // Mode switcher bar: FILES | SYMBOLS | STRINGS (30px strip hosting 24px segmented buttons)
  QWidget *modeBar = new QWidget(content);
  modeBar->setObjectName("projectModeBar");
  modeBar->setFixedHeight(30);
  QHBoxLayout *modeLayout = new QHBoxLayout(modeBar);
  modeLayout->setContentsMargins(4, 2, 4, 2);
  modeLayout->setSpacing(4);

  m_filesModeBtn = new QPushButton("FILES", modeBar);
  m_filesModeBtn->setObjectName("explorerFilesModeBtn");
  m_filesModeBtn->setCheckable(true);
  m_filesModeBtn->setChecked(true);

  m_symbolsModeBtn = new QPushButton("SYMBOLS", modeBar);
  m_symbolsModeBtn->setObjectName("explorerSymbolsModeBtn");
  m_symbolsModeBtn->setCheckable(true);
  m_symbolsModeBtn->setChecked(false);

  m_stringsModeBtn = new QPushButton("STRINGS", modeBar);
  m_stringsModeBtn->setObjectName("explorerStringsModeBtn");
  m_stringsModeBtn->setCheckable(true);
  m_stringsModeBtn->setChecked(false);

  modeLayout->addWidget(m_filesModeBtn, 1);
  modeLayout->addWidget(m_symbolsModeBtn, 1);
  modeLayout->addWidget(m_stringsModeBtn, 1);
  mainLayout->addWidget(modeBar);

  // Unified search bar filtering active mode (FILES tree, SYMBOLS table, or STRINGS table).
  QWidget *explorerFilterBar = new QWidget(content);
  explorerFilterBar->setObjectName(QStringLiteral("explorerFilterBar"));
  explorerFilterBar->setFixedHeight(28);
  QHBoxLayout *explorerFilterLayout = new QHBoxLayout(explorerFilterBar);
  explorerFilterLayout->setContentsMargins(4, 2, 4, 2);
  explorerFilterLayout->setSpacing(2);
  m_explorerFilterEdit = new QLineEdit(explorerFilterBar);
  m_explorerFilterEdit->setObjectName(QStringLiteral("explorerFilterEdit"));
  m_explorerFilterEdit->setPlaceholderText(QStringLiteral("Filter files..."));
  m_explorerFilterEdit->setClearButtonEnabled(true);
  m_explorerFilterEdit->setFixedHeight(24);
  explorerFilterLayout->addWidget(m_explorerFilterEdit);
  mainLayout->addWidget(explorerFilterBar);

  m_stack = new QStackedWidget(content);

  // Page 0: Files View
  QWidget *filesPage = new QWidget(m_stack);
  QVBoxLayout *filesLayout = new QVBoxLayout(filesPage);
  filesLayout->setContentsMargins(0, 0, 0, 0);
  filesLayout->setSpacing(0);

  QWidget *topBar = new QWidget(filesPage);
  topBar->setObjectName(QStringLiteral("explorerFilesTopBar"));
  topBar->setFixedHeight(30);
  QHBoxLayout *topLayout = new QHBoxLayout(topBar);
  topLayout->setContentsMargins(4, 2, 4, 2);
  topLayout->setSpacing(4);

  m_openFolderBtn = new QPushButton("Open Folder", topBar);
  m_collapseAllBtn = new QPushButton(topBar);
  m_collapseAllBtn->setText(QString::fromUtf8("\xEF\x86\x92"));
  m_collapseAllBtn->setFixedSize(24, 24);
  m_collapseAllBtn->setFlat(true);

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
  m_treeView->setContextMenuPolicy(Qt::CustomContextMenu);

  // Initialize Qt standard file system model for directory traversal
  m_rootPath = QDir::currentPath();
  m_fileSystemModel = new QFileSystemModel(this);
  m_fileSystemModel->setFilter(QDir::NoDotAndDotDot | QDir::AllDirs | QDir::Files);
  m_fileSystemModel->setRootPath(m_rootPath);

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

  // Page 1: Symbols & Functions View (sortable, interactive 3-column table).
  QWidget *symbolsPage = new QWidget(m_stack);
  QVBoxLayout *symbolsLayout = new QVBoxLayout(symbolsPage);
  symbolsLayout->setContentsMargins(0, 0, 0, 0);
  symbolsLayout->setSpacing(0);
  m_symbolsTable = new QTableWidget(0, 3, symbolsPage);
  configureSortableTable(m_symbolsTable, QStringLiteral("symbolsTable"), {QStringLiteral("SYMBOL"), QStringLiteral("ADDRESS"), QStringLiteral("SIZE")}, {130, 88, 56});
  symbolsLayout->addWidget(m_symbolsTable, 1);
  m_stack->addWidget(symbolsPage);

  // Page 2: Strings View (.rodata / .dynstr, sortable, interactive 3-column table).
  QWidget *stringsPage = new QWidget(m_stack);
  QVBoxLayout *stringsLayout = new QVBoxLayout(stringsPage);
  stringsLayout->setContentsMargins(0, 0, 0, 0); stringsLayout->setSpacing(0);
  m_stringsTable = new QTableWidget(0, 3, stringsPage);
  configureSortableTable(m_stringsTable, QStringLiteral("stringsTable"), {QStringLiteral("ADDRESS"), QStringLiteral("SECTION"), QStringLiteral("STRING")}, {88, 68, 120});
  stringsLayout->addWidget(m_stringsTable, 1);
  m_stack->addWidget(stringsPage);

  mainLayout->addWidget(m_stack, 1);
  content->setMinimumWidth(150); content->setMaximumWidth(QWIDGETSIZE_MAX); content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  setWidget(content);
  setMinimumWidth(150); setMaximumWidth(QWIDGETSIZE_MAX); setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

  populateSymbolsTable();
  populateStringsTable();

  // Mode switcher button slots and live search filter dispatch across all three modes.
  connect(m_filesModeBtn, &QPushButton::clicked, this, [this]() { setSymbolsMode(false); });
  connect(m_symbolsModeBtn, &QPushButton::clicked, this, [this]() { setSymbolsMode(true); });
  connect(m_stringsModeBtn, &QPushButton::clicked, this, [this]() { setStringsMode(true); });
  connect(m_explorerFilterEdit, &QLineEdit::textChanged, this, &ProjectExplorerWidget::applyExplorerFilter);

  // Read target address directly from clicked/activated sorted row items so sort order is respected.
  auto onSymbolActivated = [this](int row, int /*col*/) {
    if (auto *symItem = m_symbolsTable->item(row, 0)) emit symbolAddressSelected(symItem->data(Qt::UserRole).toULongLong(), symItem->text());
  };
  connect(m_symbolsTable, &QTableWidget::cellDoubleClicked, this, onSymbolActivated);
  connect(m_symbolsTable, &QTableWidget::cellActivated, this, onSymbolActivated);

  auto onStringActivated = [this](int row, int /*col*/) {
    auto *addrItem = m_stringsTable->item(row, 0); auto *strItem = m_stringsTable->item(row, 2);
    if (addrItem && strItem) emit symbolAddressSelected(addrItem->data(Qt::UserRole).toULongLong(), strItem->text());
  };
  connect(m_stringsTable, &QTableWidget::cellDoubleClicked, this, onStringActivated);
  connect(m_stringsTable, &QTableWidget::cellActivated, this, onStringActivated);

  connect(m_openFolderBtn, &QPushButton::clicked, this, &ProjectExplorerWidget::onOpenFolder);
  connect(m_collapseAllBtn, &QPushButton::clicked, m_treeView, &QTreeView::collapseAll);
  connect(m_treeView, &QTreeView::doubleClicked, this, &ProjectExplorerWidget::onDoubleClicked);
  connect(m_treeView, &QTreeView::customContextMenuRequested, this, &ProjectExplorerWidget::onTreeCustomContextMenu);

  // Wire F2 rename shortcut on the FILES tree view
  auto *renameShortcut = new QShortcut(QKeySequence(Qt::Key_F2), m_treeView);
  connect(renameShortcut, &QShortcut::activated, this, [this]() {
    const QModelIndex idx = m_treeView->currentIndex();
    if (!idx.isValid() || !m_fileSystemModel) return;
    const QFileInfo fi = m_fileSystemModel->fileInfo(idx);
    bool ok = false;
    const QString newName = QInputDialog::getText(this, tr("Rename Entry"), tr("New name:"), QLineEdit::Normal, fi.fileName(), &ok);
    if (ok && !newName.trimmed().isEmpty()) renameEntry(fi.absoluteFilePath(), newName.trimmed());
  });

  this->setObjectName("ProjectExplorerWidget");
  this->setStyleSheet(
      "#ProjectExplorerWidget::close-button, #ProjectExplorerWidget::float-button { icon-size: 14px; width: 20px; height: 20px; }"
      "#ProjectExplorerWidget::close-button:hover, #ProjectExplorerWidget::float-button:hover { background: rgba(255, 255, 255, 0.1); border-radius: 3px; }");

  // Default to FILES view with zero binary symbols or strings until an ELF binary is loaded
  setSymbolsMode(false);
  reloadStyle();

  // Re-render styles, symbol table, and string table foregrounds whenever Dark/Light mode toggles
  connect(&gridlock::core::managers::ThemeManager::instance(), &gridlock::core::managers::ThemeManager::themeModeChanged,
          this, [this](bool) { reloadStyle(); });
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
  populateSymbolsTable(isSymbolsMode() && m_explorerFilterEdit ? m_explorerFilterEdit->text() : QString());
}

/**
 * @brief Seeds initial binary strings from simulated .rodata and .dynstr sections.
 */
void ProjectExplorerWidget::seedDefaultStrings() {
  m_strings = {
      {0x00402000ULL, QStringLiteral(".rodata"), QStringLiteral("MPI_COMM_WORLD")},
      {0x00402010ULL, QStringLiteral(".rodata"), QStringLiteral("DeeplyNested rank=%d id=%d")},
      {0x00402030ULL, QStringLiteral(".rodata"), QStringLiteral("gridlock_rank_sync_barrier")},
      {0x00402050ULL, QStringLiteral(".dynstr"), QStringLiteral("libmpi.so.40")},
      {0x00402060ULL, QStringLiteral(".dynstr"), QStringLiteral("GLIBC_2.38")},
  };
  populateStringsTable(isStringsMode() && m_explorerFilterEdit ? m_explorerFilterEdit->text() : QString());
}

/**
 * @brief Sets externally parsed symbols and refreshes the symbols table view.
 * @param symbols Vector of symbol entries.
 */
void ProjectExplorerWidget::setSymbols(const std::vector<SymbolEntry>& symbols) {
  m_symbols = symbols;
  populateSymbolsTable(isSymbolsMode() && m_explorerFilterEdit ? m_explorerFilterEdit->text() : QString());
}

/**
 * @brief Sets externally parsed strings and refreshes the strings table view.
 * @param strings Vector of string entries.
 */
void ProjectExplorerWidget::setStrings(const std::vector<StringEntry>& strings) {
  m_strings = strings;
  populateStringsTable(isStringsMode() && m_explorerFilterEdit ? m_explorerFilterEdit->text() : QString());
}

/**
 * @brief Populates the symbols QTableWidget, applying an optional substring filter.
 * @param filterText Filter query matched against symbol name or hexadecimal address.
 */
void ProjectExplorerWidget::populateSymbolsTable(const QString &filterText) {
  if (!m_symbolsTable) return;
  auto &tm = gridlock::core::managers::ThemeManager::instance();
  const auto pal = tm.paletteForTheme(tm.activeThemeId());
  const bool isDark = pal.isDark;
  QPalette symPal = m_symbolsTable->palette();
  symPal.setColor(QPalette::Base, QColor(pal.canvasHex));
  symPal.setColor(QPalette::Text, QColor(pal.textHex));
  m_symbolsTable->setPalette(symPal);

  // Suspend live column sorting while rebuilding rows so QTableWidget does not move items mid-insert.
  const bool wasSorting = m_symbolsTable->isSortingEnabled();
  m_symbolsTable->setSortingEnabled(false);
  const QString query = filterText.trimmed();
  m_symbolsTable->setRowCount(0);

  // Iterate loaded symbols matching query and bind numeric sort keys for address and size.
  for (const auto &entry : m_symbols) {
    const QString addrStr = QStringLiteral("0x%1").arg(entry.address, 8, 16, QChar('0'));
    if (!query.isEmpty() && !entry.name.contains(query, Qt::CaseInsensitive) &&
        !addrStr.contains(query, Qt::CaseInsensitive)) {
      continue;
    }

    const int row = m_symbolsTable->rowCount();
    m_symbolsTable->insertRow(row);

    // Symbol name in pastel yellow (dark mode) or dark amber (light mode); stores raw address for activation.
    auto *nameItem = new QTableWidgetItem(entry.name);
    nameItem->setForeground(isDark ? QColor("#dcdcaa") : QColor("#953800"));
    nameItem->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(entry.address));

    // Hex address sorted numerically by 64-bit address in cyan (dark mode) or deep navy (light mode).
    auto *addrItem = new NumericSortTableItem(addrStr);
    addrItem->setForeground(isDark ? QColor("#4ec9b0") : QColor("#0550ae"));
    addrItem->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(entry.address));

    // Byte size sorted numerically by integer size.
    auto *sizeItem = new NumericSortTableItem(QString::number(entry.size));
    sizeItem->setForeground(isDark ? QColor("#b5cea8") : QColor("#116329"));
    sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    sizeItem->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(entry.size));

    m_symbolsTable->setItem(row, 0, nameItem);
    m_symbolsTable->setItem(row, 1, addrItem);
    m_symbolsTable->setItem(row, 2, sizeItem);
  }

  m_symbolsTable->setSortingEnabled(wasSorting);
  if (m_symbolsTable->rowCount() > 0) {
    m_symbolsTable->selectRow(3 < m_symbolsTable->rowCount() ? 3 : 0);
  }
}

/**
 * @brief Populates the binary strings QTableWidget with optional substring filtering.
 * @param filterText Filter query matched against string value, section, or address.
 */
void ProjectExplorerWidget::populateStringsTable(const QString &filterText) {
  if (!m_stringsTable) return;
  auto &tm = gridlock::core::managers::ThemeManager::instance();
  const auto pal = tm.paletteForTheme(tm.activeThemeId());
  const bool isDark = pal.isDark;
  QPalette strPal = m_stringsTable->palette();
  strPal.setColor(QPalette::Base, QColor(pal.canvasHex));
  strPal.setColor(QPalette::Text, QColor(pal.textHex));
  m_stringsTable->setPalette(strPal);

  // Suspend live column sorting while rebuilding rows so QTableWidget does not move items mid-insert.
  const bool wasSorting = m_stringsTable->isSortingEnabled();
  m_stringsTable->setSortingEnabled(false);
  const QString query = filterText.trimmed();
  m_stringsTable->setRowCount(0);

  // Iterate strings matching filter and attach 64-bit numeric sort key to column 0.
  for (const auto &entry : m_strings) {
    const QString addrStr = QStringLiteral("0x%1").arg(entry.address, 8, 16, QChar('0'));
    if (!query.isEmpty() && !entry.value.contains(query, Qt::CaseInsensitive) &&
        !entry.section.contains(query, Qt::CaseInsensitive) &&
        !addrStr.contains(query, Qt::CaseInsensitive)) {
      continue;
    }

    const int row = m_stringsTable->rowCount();
    m_stringsTable->insertRow(row);

    auto *addrItem = new NumericSortTableItem(addrStr);
    addrItem->setForeground(isDark ? QColor("#4ec9b0") : QColor("#0550ae"));
    addrItem->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(entry.address));

    auto *secItem = new QTableWidgetItem(entry.section);
    secItem->setForeground(isDark ? QColor("#569cd6") : QColor("#0969da"));
    secItem->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(entry.address));

    auto *valItem = new QTableWidgetItem(entry.value);
    valItem->setForeground(isDark ? QColor("#ce9178") : QColor("#953800"));
    valItem->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(entry.address));

    m_stringsTable->setItem(row, 0, addrItem);
    m_stringsTable->setItem(row, 1, secItem);
    m_stringsTable->setItem(row, 2, valItem);
  }

  m_stringsTable->setSortingEnabled(wasSorting);
  if (m_stringsTable->rowCount() > 0) m_stringsTable->selectRow(0);
}

/**
 * @brief Switches the active view between Files mode (page 0) and Symbols mode (page 1).
 * @param enabled True to show Symbols table; false to show Files tree.
 */
void ProjectExplorerWidget::setSymbolsMode(bool enabled) {
  if (m_stack) m_stack->setCurrentIndex(enabled ? 1 : 0);
  if (m_filesModeBtn) m_filesModeBtn->setChecked(!enabled);
  if (m_symbolsModeBtn) m_symbolsModeBtn->setChecked(enabled);
  if (m_stringsModeBtn) m_stringsModeBtn->setChecked(false);
  if (m_explorerFilterEdit) {
    m_explorerFilterEdit->setPlaceholderText(
        enabled ? QStringLiteral("Filter symbols by name or address...")
                : QStringLiteral("Filter files by name (e.g. main, *.cpp)..."));
  }
  applyExplorerFilter(m_explorerFilterEdit ? m_explorerFilterEdit->text() : QString());
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
  return m_symbolsTable ? m_symbolsTable->rowCount() : static_cast<int>(m_symbols.size());
}

/**
 * @brief Switches the active stacked page to Strings mode (page 2).
 * @param enabled True to show Strings table.
 */
void ProjectExplorerWidget::setStringsMode(bool enabled) {
  if (!enabled) { setSymbolsMode(true); return; }
  if (m_stack) m_stack->setCurrentIndex(2);
  if (m_filesModeBtn) m_filesModeBtn->setChecked(false);
  if (m_symbolsModeBtn) m_symbolsModeBtn->setChecked(false);
  if (m_stringsModeBtn) m_stringsModeBtn->setChecked(true);
  if (m_explorerFilterEdit) {
    m_explorerFilterEdit->setPlaceholderText(QStringLiteral("Filter strings, sections, or addresses..."));
  }
  applyExplorerFilter(m_explorerFilterEdit ? m_explorerFilterEdit->text() : QString());
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
  return m_stringsTable ? m_stringsTable->rowCount() : static_cast<int>(m_strings.size());
}

/**
 * @brief Selects a symbol by name in the table and emits symbolAddressSelected.
 * @param symbolName Target symbol name.
 * @return True if symbol found; false otherwise.
 */
bool ProjectExplorerWidget::activateSymbol(const QString &symbolName) {
  // Strip whitespace from query
  const QString query = symbolName.trimmed();
  if (query.isEmpty()) return false;

  // Linear scan across registered binary symbols
  for (const auto &entry : m_symbols) {
    // Match exact symbol name or alias "main" -> "sym.main"
    if (entry.name.compare(query, Qt::CaseInsensitive) == 0 ||
        (query.compare(QStringLiteral("main"), Qt::CaseInsensitive) == 0 && entry.name == QStringLiteral("sym.main"))) {
      // Synchronize visual selection in symbols table
      if (m_symbolsTable) {
        for (int r = 0; r < m_symbolsTable->rowCount(); ++r) {
          if (auto *item = m_symbolsTable->item(r, 0); item && item->text().compare(entry.name, Qt::CaseInsensitive) == 0) {
            m_symbolsTable->selectRow(r);
            break;
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
  if (query.isEmpty()) return false;

  // Scan extracted strings table
  for (const auto &entry : m_strings) {
    const QString addrStr = QStringLiteral("0x%1").arg(entry.address, 8, 16, QChar('0'));
    // Match against content substring or hexadecimal address
    if (entry.value.contains(query, Qt::CaseInsensitive) || addrStr.compare(query, Qt::CaseInsensitive) == 0) {
      // Select matching row in strings table
      if (m_stringsTable) {
        for (int r = 0; r < m_stringsTable->rowCount(); ++r) {
          if (auto *item = m_stringsTable->item(r, 2); item && item->text() == entry.value) {
            m_stringsTable->selectRow(r);
            break;
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
 * @brief Configures common sorting, selection, and interactive header geometry on explorer tables.
 */
void ProjectExplorerWidget::configureSortableTable(QTableWidget* table, const QString& objName,
                                                   const QStringList& headers, const QList<int>& widths) {
  if (!table) return;
  table->setObjectName(objName);
  table->setColumnCount(headers.size());
  table->setHorizontalHeaderLabels(headers);
  table->verticalHeader()->setVisible(false);
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->setSelectionMode(QAbstractItemView::SingleSelection);
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table->setSortingEnabled(true);
  if (QHeaderView* hdr = table->horizontalHeader()) {
    hdr->setVisible(true);
    hdr->setMinimumSectionSize(42);
    hdr->setSectionsMovable(false);
    hdr->setStretchLastSection(true);
    for (int i = 0; i < headers.size(); ++i) {
      hdr->setSectionResizeMode(i, QHeaderView::Interactive);
      if (i < widths.size()) table->setColumnWidth(i, widths.at(i));
    }
  }
}

/**
 * @brief Applies the unified explorer filter bar query to whichever page (`FILES`, `SYMBOLS`, or `STRINGS`) is active.
 * @param filterText Search substring or glob pattern entered in `#explorerFilterEdit`.
 */
void ProjectExplorerWidget::applyExplorerFilter(const QString &filterText) {
  if (isSymbolsMode()) { populateSymbolsTable(filterText); return; }
  if (isStringsMode()) { populateStringsTable(filterText); return; }
  if (!m_fileSystemModel) return;
  const QString query = filterText.trimmed();
  m_fileSystemModel->setNameFilterDisables(false);
  if (query.isEmpty()) {
    m_fileSystemModel->setNameFilters(QStringList());
    return;
  }
  const QString pattern = (query.contains(QLatin1Char('*')) || query.contains(QLatin1Char('?')))
                              ? query
                              : (QStringLiteral("*") + query + QStringLiteral("*"));
  m_fileSystemModel->setNameFilters(QStringList{pattern});
}

/**
 * @brief Filters the symbols table on query changes.
 * @param filterText Search substring.
 */
void ProjectExplorerWidget::filterSymbols(const QString &filterText) { populateSymbolsTable(filterText); }

/**
 * @brief Filters the strings table on query changes.
 * @param filterText Search substring.
 */
void ProjectExplorerWidget::filterStrings(const QString &filterText) { populateStringsTable(filterText); }

/**
 * @brief Reloads font size, item padding, mode buttons, unified filter bar, and icon provider from theme settings.
 */
void ProjectExplorerWidget::reloadStyle() {
  // Read user-configured tree row density from persistent settings with a strict >= 12px typography floor.
  QSettings s("gridlock", "debugger");
  const QString style = s.value("appearance/file_tree_style", "Comfortable").toString();

  // Compute vertical item padding and >= 12px font size from selected density preset.
  int padding = 2, fontSize = 12;
  if (style == "Compact") { padding = 1; fontSize = 12; }
  else if (style == "Large") { padding = 4; fontSize = 13; }

  // Resolve active workstation color palette from ThemeManager without hardcoded dark hex values.
  auto& tm = gridlock::core::managers::ThemeManager::instance();
  const auto pal = tm.paletteForTheme(tm.activeThemeId());

  // Style the FILES | SYMBOLS | STRINGS segmented mode bar from active theme tokens.
  const QString btnStyle = QStringLiteral(
      "QPushButton { background-color: %1; color: %2; border: 1px solid %3; "
      "border-radius: 3px; padding: 0px 10px; font-size: 12px; font-weight: 600; min-height: 24px; max-height: 24px; }"
      "QPushButton:hover { background-color: %3; border: 1px solid %4; color: %2; }"
      "QPushButton:checked { background-color: %4; color: #ffffff; border: 1px solid %4; }")
      .arg(pal.headerHex, pal.textHex, pal.borderHex, pal.accentHex);

  if (auto* modeBar = findChild<QWidget*>(QStringLiteral("projectModeBar"))) {
    modeBar->setStyleSheet(QStringLiteral("QWidget#projectModeBar { background-color: %1; border-bottom: 1px solid %2; } ").arg(pal.headerHex, pal.borderHex) + btnStyle);
  }
  for (QPushButton* btn : {m_filesModeBtn, m_symbolsModeBtn, m_stringsModeBtn}) {
    if (btn) btn->setStyleSheet(btnStyle);
  }
  if (auto* filesTop = findChild<QWidget*>(QStringLiteral("explorerFilesTopBar"))) {
    filesTop->setStyleSheet(QStringLiteral("QWidget#explorerFilesTopBar { background-color: %1; border-bottom: 1px solid %2; }").arg(pal.headerHex, pal.borderHex));
  }
  if (m_openFolderBtn) {
    m_openFolderBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: %1; color: %2; border: 1px solid %3; border-radius: 3px; "
        "padding: 0px 10px; font-size: 12px; font-weight: 600; min-height: 22px; max-height: 24px; }"
        "QPushButton:hover { background-color: %3; border: 1px solid %4; color: %2; }")
        .arg(pal.headerHex, pal.textHex, pal.borderHex, pal.accentHex));
  }
  if (m_collapseAllBtn) {
    m_collapseAllBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: transparent; color: %1; border: none; font-family: 'Symbols Nerd Font'; font-size: 14px; }"
        "QPushButton:hover { background-color: %2; border-radius: 3px; }").arg(pal.textHex, pal.borderHex));
  }

  // Style the unified explorer filter bar (#explorerFilterEdit) dynamically from active theme palette.
  const QString filterStyle = QStringLiteral(
      "QLineEdit { background-color: %1; color: %2; border: 1px solid %3; "
      "border-radius: 3px; padding: 1px 6px; font-size: 12px; }"
      "QLineEdit:focus { border: 1px solid %4; }").arg(pal.canvasHex, pal.textHex, pal.borderHex, pal.accentHex);
  if (m_explorerFilterEdit) {
    m_explorerFilterEdit->setStyleSheet(filterStyle);
    if (m_explorerFilterEdit->parentWidget()) {
      m_explorerFilterEdit->parentWidget()->setStyleSheet(
          QStringLiteral("background-color: %1; border-bottom: 1px solid %2;").arg(pal.headerHex, pal.borderHex));
    }
  }

  // Style tables (12px monospace cells) and table column headers (12px UI sans-serif) from active theme palette.
  const QString tableStyle = QStringLiteral(
      "QTableWidget { background-color: %1; color: %2; gridline-color: %3; border: none; "
      "font-family: 'JetBrains Mono', monospace; font-size: 12px; }"
      "QHeaderView::section { font-family: 'Inter', sans-serif; background-color: %4; color: %2; "
      "border: 1px solid %3; padding: 2px 4px; font-size: 12px; font-weight: 600; }")
      .arg(pal.canvasHex, pal.textHex, pal.borderHex, pal.headerHex);

  if (m_symbolsTable) {
    m_symbolsTable->setStyleSheet(tableStyle);
    populateSymbolsTable(isSymbolsMode() && m_explorerFilterEdit ? m_explorerFilterEdit->text() : QString());
  }
  if (m_stringsTable) {
    m_stringsTable->setStyleSheet(tableStyle);
    populateStringsTable(isStringsMode() && m_explorerFilterEdit ? m_explorerFilterEdit->text() : QString());
  }

  // Apply canvas background, 12px UI sans-serif header style, text color, and item padding to the filesystem tree view.
  if (m_treeView) {
    m_treeView->setStyleSheet(QStringLiteral(
        "QTreeView { background-color: %3; color: %4; font-size: %2px; border: none; } "
        "QTreeView::item { padding: %1px; } "
        "QHeaderView::section { font-family: 'Inter', sans-serif; background-color: %5; color: %4; "
        "border: 1px solid %6; padding: 2px 4px; font-size: 12px; font-weight: 600; }")
        .arg(padding).arg(fontSize).arg(pal.canvasHex, pal.textHex, pal.headerHex, pal.borderHex));
  }

  // Rebuild FileIconProvider so folder and source icons reflect the current theme.
  if (m_fileSystemModel) {
    auto *newProvider = new FileIconProvider();
    m_fileSystemModel->setIconProvider(newProvider);
    delete m_iconProvider;
    m_iconProvider = newProvider;
  }
}

/**
 * @brief Prompts user for a directory and opens it in the file tree view.
 */
void ProjectExplorerWidget::onOpenFolder() {
  const QString dir = FileDialogHelper::getExistingDirectory(this, QStringLiteral("Open Folder"), m_rootPath);
  if (!dir.isEmpty()) setRootPath(dir);
}

/**
 * @brief Resolves any directory or `.toml`/`.gridlock` session file path to its workspace directory,
 *        rebuilds the `FILES` tree root index, switches the active tab to `FILES`, and notifies listeners.
 * @param path Directory path or `.toml` session file path.
 */
void ProjectExplorerWidget::setRootPath(const QString &path) {
  const QString trimmed = path.trimmed();
  if (trimmed.isEmpty()) return;

  // Normalize `.gridlock/workspace.toml` or `.toml` session file paths to their workspace directory.
  const QFileInfo rawInfo(trimmed);
  QDir dir = rawInfo.exists() ? (rawInfo.isDir() ? QDir(rawInfo.absoluteFilePath()) : rawInfo.dir())
                              : (rawInfo.fileName().endsWith(QStringLiteral(".toml"), Qt::CaseInsensitive)
                                     ? rawInfo.dir()
                                     : QDir(rawInfo.absoluteFilePath()));
  // Step up out of `.gridlock/` metadata directory when a `.gridlock/workspace.toml` path is passed.
  if (dir.dirName() == QStringLiteral(".gridlock")) dir.cdUp();

  const QFileInfo dirInfo(dir.absolutePath());
  const QString resolvedDir =
      !dirInfo.canonicalFilePath().isEmpty() ? dirInfo.canonicalFilePath() : QDir::cleanPath(dirInfo.absoluteFilePath());
  m_rootPath = resolvedDir;

  // Reset and re-root QFileSystemModel + QTreeView strictly at the resolved workspace directory.
  if (m_fileSystemModel && m_treeView) {
    m_fileSystemModel->setRootPath(QString());
    const QModelIndex rootIdx = m_fileSystemModel->setRootPath(resolvedDir);
    m_treeView->setRootIndex(rootIdx.isValid() ? rootIdx : m_fileSystemModel->index(resolvedDir));
    m_treeView->clearSelection();
  }
  // Switch active mode toggle to FILES (page 0) so the chosen folder hierarchy is immediately visible.
  setSymbolsMode(false);
  emit workspaceRootChanged(resolvedDir);
}

/**
 * @brief Handles double clicking file items to emit fileDoubleClicked.
 * @param index Model index of clicked node.
 */
void ProjectExplorerWidget::onDoubleClicked(const QModelIndex &index) {
  if (!index.isValid()) return;
  QFileInfo info = m_fileSystemModel->fileInfo(index);
  if (info.isFile()) emit fileDoubleClicked(info.absoluteFilePath());
}

/**
 * @brief Refreshes the QFileSystemModel root directory and optionally selects focusPath in the tree view.
 * @param focusPath Optional file or directory path to expand and select.
 */
void ProjectExplorerWidget::refreshTreeViewPath(const QString &focusPath) {
  if (!m_fileSystemModel || !m_treeView) return;
  const QString root = !m_rootPath.isEmpty() ? m_rootPath : m_fileSystemModel->rootPath();
  m_fileSystemModel->setRootPath(QString());
  const QModelIndex rootIdx = m_fileSystemModel->setRootPath(root);
  m_treeView->setRootIndex(rootIdx.isValid() ? rootIdx : m_fileSystemModel->index(root));
  if (!focusPath.isEmpty()) {
    const QModelIndex idx = m_fileSystemModel->index(focusPath);
    if (idx.isValid()) { m_treeView->expand(idx.parent()); m_treeView->setCurrentIndex(idx); }
  }
}

/**
 * @brief Creates a new source or header file on disk with a standard boilerplate stub and emits fileCreated(path).
 * @param parentDir Parent directory path on disk.
 * @param fileName File name including extension.
 * @return True if created and written successfully.
 */
bool ProjectExplorerWidget::createNewFile(const QString &parentDir, const QString &fileName) {
  const QString cleanName = fileName.trimmed();
  if (cleanName.isEmpty() || cleanName.contains(QStringLiteral(".."))) return false;
  const QString baseDir = parentDir.trimmed().isEmpty() ? (!m_rootPath.isEmpty() ? m_rootPath : QDir::currentPath()) : parentDir.trimmed();
  QDir dir(baseDir);
  if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) return false;
  const QString fullPath = QFileInfo(dir.filePath(cleanName)).absoluteFilePath();
  QFileInfo targetInfo(fullPath);
  if (!targetInfo.dir().exists() && !targetInfo.dir().mkpath(QStringLiteral("."))) return false;

  // Write standard C/C++ header guard or implementation stub based on file extension.
  QFile file(fullPath);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return false;
  const QString ext = targetInfo.suffix().toLower();
  if (ext == QStringLiteral("hpp") || ext == QStringLiteral("hxx") || ext == QStringLiteral("hh") || ext == QStringLiteral("h")) {
    file.write(QStringLiteral("#pragma once\n\n// %1\n").arg(targetInfo.fileName()).toUtf8());
  } else {
    file.write(QStringLiteral("#include <cstdint>\n\n// %1\n").arg(targetInfo.fileName()).toUtf8());
  }
  file.close();
  refreshTreeViewPath(fullPath);
  emit fileCreated(fullPath);
  return true;
}

/**
 * @brief Creates a new subdirectory inside parentDir and refreshes the file tree view.
 * @param parentDir Parent directory path on disk.
 * @param folderName Subdirectory name to create.
 * @return True if the directory exists or was created.
 */
bool ProjectExplorerWidget::createNewFolder(const QString &parentDir, const QString &folderName) {
  const QString cleanName = folderName.trimmed();
  if (cleanName.isEmpty() || cleanName.contains(QStringLiteral(".."))) return false;
  const QString baseDir = parentDir.trimmed().isEmpty() ? (!m_rootPath.isEmpty() ? m_rootPath : QDir::currentPath()) : parentDir.trimmed();
  QDir dir(baseDir);
  if (!dir.mkpath(cleanName)) return false;
  refreshTreeViewPath(dir.filePath(cleanName));
  return true;
}

/**
 * @brief Renames an existing file or folder on disk and emits fileRenamed(oldPath, newPath).
 * @param oldPath Existing absolute path.
 * @param newName New base name within the same directory.
 * @return True if rename succeeded on disk.
 */
bool ProjectExplorerWidget::renameEntry(const QString &oldPath, const QString &newName) {
  const QString cleanName = newName.trimmed();
  QFileInfo oldInfo(oldPath);
  if (!oldInfo.exists() || cleanName.isEmpty() || cleanName.contains(QLatin1Char('/')) || cleanName.contains(QStringLiteral(".."))) return false;
  const QString absOld = oldInfo.absoluteFilePath(), absNew = oldInfo.dir().filePath(cleanName);
  if (absOld == absNew) return true;
  if (!QDir().rename(absOld, absNew)) return false;
  refreshTreeViewPath(absNew);
  emit fileRenamed(absOld, absNew);
  return true;
}

/**
 * @brief Removes an existing file or directory from disk and emits fileDeleted(path).
 * @param targetPath Target file or directory path.
 * @return True if removed from disk.
 */
bool ProjectExplorerWidget::deleteEntry(const QString &targetPath) {
  QFileInfo info(targetPath);
  if (!info.exists()) return false;
  const QString absTarget = info.absoluteFilePath();
  if (!(info.isDir() ? QDir(absTarget).removeRecursively() : QFile::remove(absTarget))) return false;
  refreshTreeViewPath();
  emit fileDeleted(absTarget);
  return true;
}

/**
 * @brief Registers a relative source file path in a Meson (meson.build) or CMake (CMakeLists.txt) build specification.
 * @param buildFilePath Absolute path to meson.build or CMakeLists.txt.
 * @param relativeSourcePath Relative path of the source file to register.
 * @return True if updated and written to disk.
 */
bool ProjectExplorerWidget::registerSourceInBuildFile(const QString &buildFilePath, const QString &relativeSourcePath) {
  const QString cleanRel = relativeSourcePath.trimmed();
  if (cleanRel.isEmpty() || !QFileInfo::exists(buildFilePath)) return false;
  QFile bf(buildFilePath);
  if (!bf.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
  QString content = QString::fromUtf8(bf.readAll());
  bf.close();
  if (content.contains(cleanRel)) return true;

  // Insert source entry into Meson executable/library/files block or CMake add_executable/add_library block.
  const bool isMeson = buildFilePath.endsWith(QStringLiteral("meson.build"), Qt::CaseInsensitive);
  const int closeParen = content.lastIndexOf(QLatin1Char(')'));
  if (closeParen > 0) {
    content.insert(closeParen, isMeson ? QStringLiteral("  '%1',\n").arg(cleanRel) : QStringLiteral("  %1\n").arg(cleanRel));
  } else {
    content += isMeson ? QStringLiteral("\n# Added source: '%1'\n").arg(cleanRel) : QStringLiteral("\n# Added source: %1\n").arg(cleanRel);
  }
  if (!bf.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return false;
  bf.write(content.toUtf8());
  bf.close();
  return true;
}

/**
 * @brief Displays the interactive right-click CRUD context menu on the FILES tree view.
 * @param pos Local viewport click coordinate.
 */
void ProjectExplorerWidget::onTreeCustomContextMenu(const QPoint &pos) {
  if (!m_treeView || !m_fileSystemModel) return;
  const QModelIndex idx = m_treeView->indexAt(pos);
  const QFileInfo selInfo = idx.isValid() ? m_fileSystemModel->fileInfo(idx) : QFileInfo(m_fileSystemModel->rootPath());
  const QString targetDir = selInfo.isDir() ? selInfo.absoluteFilePath() : selInfo.absolutePath();

  // Construct context menu with source/header creation, folder creation, rename, and delete actions.
  QMenu menu(this);
  QAction *newCppAct = menu.addAction(tr("New Source File (.cpp)..."));
  QAction *newHppAct = menu.addAction(tr("New Header File (.hpp)..."));
  QAction *newFolderAct = menu.addAction(tr("New Folder..."));
  menu.addSeparator();
  QAction *renameAct = menu.addAction(tr("Rename...\tF2"));
  QAction *deleteAct = menu.addAction(tr("Delete..."));
  renameAct->setEnabled(idx.isValid()); deleteAct->setEnabled(idx.isValid());

  connect(newCppAct, &QAction::triggered, this, [this, targetDir]() {
    bool ok = false; const QString n = QInputDialog::getText(this, tr("New Source File"), tr("File name:"), QLineEdit::Normal, QStringLiteral("new_kernel.cpp"), &ok);
    if (ok && !n.trimmed().isEmpty()) createNewFile(targetDir, n.trimmed());
  });
  connect(newHppAct, &QAction::triggered, this, [this, targetDir]() {
    bool ok = false; const QString n = QInputDialog::getText(this, tr("New Header File"), tr("Header name:"), QLineEdit::Normal, QStringLiteral("new_kernel.hpp"), &ok);
    if (ok && !n.trimmed().isEmpty()) createNewFile(targetDir, n.trimmed());
  });
  connect(newFolderAct, &QAction::triggered, this, [this, targetDir]() {
    bool ok = false; const QString n = QInputDialog::getText(this, tr("New Folder"), tr("Folder name:"), QLineEdit::Normal, QStringLiteral("kernels"), &ok);
    if (ok && !n.trimmed().isEmpty()) createNewFolder(targetDir, n.trimmed());
  });
  connect(renameAct, &QAction::triggered, this, [this, selInfo]() {
    bool ok = false; const QString n = QInputDialog::getText(this, tr("Rename Entry"), tr("New name:"), QLineEdit::Normal, selInfo.fileName(), &ok);
    if (ok && !n.trimmed().isEmpty()) renameEntry(selInfo.absoluteFilePath(), n.trimmed());
  });
  connect(deleteAct, &QAction::triggered, this, [this, selInfo]() {
    if (QMessageBox::question(this, tr("Delete Entry"), tr("Delete '%1' from disk?").arg(selInfo.fileName())) == QMessageBox::Yes) deleteEntry(selInfo.absoluteFilePath());
  });
  menu.exec(m_treeView->viewport()->mapToGlobal(pos));
}

}  // namespace gridlock::ui

