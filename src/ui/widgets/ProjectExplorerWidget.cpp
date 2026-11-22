/**
 * @file ProjectExplorerWidget.cpp
 * @brief Dock UI construction, symbol/string table models, and fixture seeding for ProjectExplorerWidget.
 *
 * Implements the tri-mode explorer layout (`FILES | SYMBOLS | STRINGS`), custom
 * ExplorerFileSystemModel, sortable numeric address/size table items, and symbol/string
 * table population.
 */

#include "ProjectExplorerWidget.hpp"

#include <QAbstractItemView>
#include <QBrush>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QPalette>
#include <QShortcut>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include "../../core/managers/ThemeManager.hpp"
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

/** @brief Constructs a new ProjectExplorerWidget dock panel. */
ProjectExplorerWidget::ProjectExplorerWidget(QWidget* parent)
    : QDockWidget("Project / Symbols", parent) {
  m_iconProvider = nullptr;
  auto* content = new FlexibleDockContent(this);
  auto* mainLayout = new QVBoxLayout(content);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  auto* modeBar = new QWidget(content);
  modeBar->setObjectName("projectModeBar");
  modeBar->setFixedHeight(30);
  auto* modeLayout = new QHBoxLayout(modeBar);
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

  auto* explorerFilterBar = new QWidget(content);
  explorerFilterBar->setObjectName(QStringLiteral("explorerFilterBar"));
  explorerFilterBar->setFixedHeight(28);
  auto* explorerFilterLayout = new QHBoxLayout(explorerFilterBar);
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

  auto* filesPage = new QWidget(m_stack);
  auto* filesLayout = new QVBoxLayout(filesPage);
  filesLayout->setContentsMargins(0, 0, 0, 0);
  filesLayout->setSpacing(0);

  auto* topBar = new QWidget(filesPage);
  topBar->setObjectName(QStringLiteral("explorerFilesTopBar"));
  topBar->setFixedHeight(30);
  auto* topLayout = new QHBoxLayout(topBar);
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

  m_treeView = new QTreeView(filesPage);
  m_treeView->setFrameShape(QFrame::NoFrame);
  m_treeView->setAlternatingRowColors(true);
  m_treeView->setHeaderHidden(false);
  m_treeView->header()->setVisible(true);
  m_treeView->header()->setMinimumSectionSize(48);
  m_treeView->header()->setStretchLastSection(true);
  m_treeView->setContextMenuPolicy(Qt::CustomContextMenu);

  class ExplorerFileSystemModel final : public QFileSystemModel {
  public:
    using QFileSystemModel::QFileSystemModel;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override {
      if (role == Qt::ForegroundRole && index.isValid() && FileIconProvider::isBuildArtifactFile(fileInfo(index))) {
        auto& tm = gridlock::core::managers::ThemeManager::instance();
        QColor muted(tm.paletteForTheme(tm.activeThemeId()).textHex);
        muted.setAlpha(128);
        return QBrush(muted);
      }
      return QFileSystemModel::data(index, role);
    }
  };

  m_rootPath = QDir::currentPath();
  m_fileSystemModel = new ExplorerFileSystemModel(this);
  m_fileSystemModel->setFilter(QDir::NoDotAndDotDot | QDir::AllDirs | QDir::Files);
  m_fileSystemModel->setRootPath(m_rootPath);

  m_iconProvider = new FileIconProvider();
  m_fileSystemModel->setIconProvider(m_iconProvider);
  m_treeView->setIconSize(QSize(16, 16));
  m_treeView->setModel(m_fileSystemModel);
  m_treeView->setRootIndex(m_fileSystemModel->index(QDir::currentPath()));

  reloadStyle();

  m_treeView->hideColumn(1);
  m_treeView->hideColumn(2);
  m_treeView->hideColumn(3);

  filesLayout->addWidget(m_treeView, 1);
  m_stack->addWidget(filesPage);

  auto* symbolsPage = new QWidget(m_stack);
  auto* symbolsLayout = new QVBoxLayout(symbolsPage);
  symbolsLayout->setContentsMargins(0, 0, 0, 0);
  symbolsLayout->setSpacing(0);
  m_symbolsTable = new QTableWidget(0, 3, symbolsPage);
  configureSortableTable(m_symbolsTable, QStringLiteral("symbolsTable"), {QStringLiteral("SYMBOL"), QStringLiteral("ADDRESS"), QStringLiteral("SIZE")}, {130, 88, 56});
  symbolsLayout->addWidget(m_symbolsTable, 1);
  m_stack->addWidget(symbolsPage);

  auto* stringsPage = new QWidget(m_stack);
  auto* stringsLayout = new QVBoxLayout(stringsPage);
  stringsLayout->setContentsMargins(0, 0, 0, 0);
  stringsLayout->setSpacing(0);
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

  connect(m_filesModeBtn, &QPushButton::clicked, this, [this]() { setSymbolsMode(false); });
  connect(m_symbolsModeBtn, &QPushButton::clicked, this, [this]() { setSymbolsMode(true); });
  connect(m_stringsModeBtn, &QPushButton::clicked, this, [this]() { setStringsMode(true); });
  connect(m_explorerFilterEdit, &QLineEdit::textChanged, this, &ProjectExplorerWidget::applyExplorerFilter);

  auto onSymbolActivated = [this](int row, int /*col*/) {
    if (auto* symItem = m_symbolsTable->item(row, 0)) emit symbolAddressSelected(symItem->data(Qt::UserRole).toULongLong(), symItem->text());
  };
  connect(m_symbolsTable, &QTableWidget::cellDoubleClicked, this, onSymbolActivated);
  connect(m_symbolsTable, &QTableWidget::cellActivated, this, onSymbolActivated);

  auto onStringActivated = [this](int row, int /*col*/) {
    auto* addrItem = m_stringsTable->item(row, 0); auto* strItem = m_stringsTable->item(row, 2);
    if (addrItem && strItem) emit symbolAddressSelected(addrItem->data(Qt::UserRole).toULongLong(), strItem->text());
  };
  connect(m_stringsTable, &QTableWidget::cellDoubleClicked, this, onStringActivated);
  connect(m_stringsTable, &QTableWidget::cellActivated, this, onStringActivated);

  connect(m_openFolderBtn, &QPushButton::clicked, this, &ProjectExplorerWidget::onOpenFolder);
  connect(m_collapseAllBtn, &QPushButton::clicked, m_treeView, &QTreeView::collapseAll);
  connect(m_treeView, &QTreeView::doubleClicked, this, &ProjectExplorerWidget::onDoubleClicked);
  connect(m_treeView, &QTreeView::customContextMenuRequested, this, &ProjectExplorerWidget::onTreeCustomContextMenu);

  auto* renameShortcut = new QShortcut(QKeySequence(Qt::Key_F2), m_treeView);
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

  setSymbolsMode(false);
  reloadStyle();

  connect(&gridlock::core::managers::ThemeManager::instance(), &gridlock::core::managers::ThemeManager::themeModeChanged,
          this, [this](bool) { reloadStyle(); });
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
  populateSymbolsTable(isSymbolsMode() && m_explorerFilterEdit ? m_explorerFilterEdit->text() : QString());
}

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

void ProjectExplorerWidget::setSymbols(const std::vector<SymbolEntry>& symbols) {
  m_symbols = symbols;
  populateSymbolsTable(isSymbolsMode() && m_explorerFilterEdit ? m_explorerFilterEdit->text() : QString());
}

void ProjectExplorerWidget::setStrings(const std::vector<StringEntry>& strings) {
  m_strings = strings;
  populateStringsTable(isStringsMode() && m_explorerFilterEdit ? m_explorerFilterEdit->text() : QString());
}

/** @brief Populates the symbols QTableWidget, applying an optional substring filter. */
void ProjectExplorerWidget::populateSymbolsTable(const QString& filterText) {
  if (!m_symbolsTable) return;
  auto& tm = gridlock::core::managers::ThemeManager::instance();
  const auto pal = tm.paletteForTheme(tm.activeThemeId());
  const bool isDark = pal.isDark;
  QPalette symPal = m_symbolsTable->palette();
  symPal.setColor(QPalette::Base, QColor(pal.canvasHex));
  symPal.setColor(QPalette::Text, QColor(pal.textHex));
  m_symbolsTable->setPalette(symPal);

  const bool wasSorting = m_symbolsTable->isSortingEnabled();
  m_symbolsTable->setSortingEnabled(false);
  const QString query = filterText.trimmed();
  m_symbolsTable->setRowCount(0);

  for (const auto& entry : m_symbols) {
    const QString addrStr = QStringLiteral("0x%1").arg(entry.address, 8, 16, QChar('0'));
    if (!query.isEmpty() && !entry.name.contains(query, Qt::CaseInsensitive) &&
        !addrStr.contains(query, Qt::CaseInsensitive)) {
      continue;
    }

    const int row = m_symbolsTable->rowCount();
    m_symbolsTable->insertRow(row);

    auto* nameItem = new QTableWidgetItem(entry.name);
    nameItem->setForeground(isDark ? QColor("#dcdcaa") : QColor("#953800"));
    nameItem->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(entry.address));

    auto* addrItem = new NumericSortTableItem(addrStr);
    addrItem->setForeground(isDark ? QColor("#4ec9b0") : QColor("#0550ae"));
    addrItem->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(entry.address));

    auto* sizeItem = new NumericSortTableItem(QString::number(entry.size));
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

/** @brief Populates the binary strings QTableWidget with optional substring filtering. */
void ProjectExplorerWidget::populateStringsTable(const QString& filterText) {
  if (!m_stringsTable) return;
  auto& tm = gridlock::core::managers::ThemeManager::instance();
  const auto pal = tm.paletteForTheme(tm.activeThemeId());
  const bool isDark = pal.isDark;
  QPalette strPal = m_stringsTable->palette();
  strPal.setColor(QPalette::Base, QColor(pal.canvasHex));
  strPal.setColor(QPalette::Text, QColor(pal.textHex));
  m_stringsTable->setPalette(strPal);

  const bool wasSorting = m_stringsTable->isSortingEnabled();
  m_stringsTable->setSortingEnabled(false);
  const QString query = filterText.trimmed();
  m_stringsTable->setRowCount(0);

  for (const auto& entry : m_strings) {
    const QString addrStr = QStringLiteral("0x%1").arg(entry.address, 8, 16, QChar('0'));
    if (!query.isEmpty() && !entry.value.contains(query, Qt::CaseInsensitive) &&
        !entry.section.contains(query, Qt::CaseInsensitive) &&
        !addrStr.contains(query, Qt::CaseInsensitive)) {
      continue;
    }

    const int row = m_stringsTable->rowCount();
    m_stringsTable->insertRow(row);

    auto* addrItem = new NumericSortTableItem(addrStr);
    addrItem->setForeground(isDark ? QColor("#4ec9b0") : QColor("#0550ae"));
    addrItem->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(entry.address));

    auto* secItem = new QTableWidgetItem(entry.section);
    secItem->setForeground(isDark ? QColor("#569cd6") : QColor("#0969da"));
    secItem->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(entry.address));

    auto* valItem = new QTableWidgetItem(entry.value);
    valItem->setForeground(isDark ? QColor("#ce9178") : QColor("#953800"));
    valItem->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(entry.address));

    m_stringsTable->setItem(row, 0, addrItem);
    m_stringsTable->setItem(row, 1, secItem);
    m_stringsTable->setItem(row, 2, valItem);
  }

  m_stringsTable->setSortingEnabled(wasSorting);
  if (m_stringsTable->rowCount() > 0) m_stringsTable->selectRow(0);
}

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

}  // namespace gridlock::ui
