/**
 * @file ProjectExplorerActions.cpp
 * @brief Mode switching, filtering, theme styling, and filesystem CRUD operations for ProjectExplorerWidget.
 *
 * Implements FILES/SYMBOLS/STRINGS mode transitions, symbol/string activation,
 * live filter bar dispatch, ThemeManager reloadStyle(), workspace root resolution,
 * file/folder creation, renaming, deletion, build-file registration, and tree context menus.
 */

#include "ProjectExplorerWidget.hpp"

#include <algorithm>

#include <QBrush>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontInfo>
#include <QHash>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardItem>

#include "../../core/managers/ThemeManager.hpp"
#include "../utils/FileDialogHelper.hpp"
#include "FileIconProvider.hpp"

namespace gridlock::ui {

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

bool ProjectExplorerWidget::isSymbolsMode() const { return m_stack && m_stack->currentIndex() == 1; }

int ProjectExplorerWidget::symbolCount() const {
  return m_symbolsTable ? m_symbolsTable->rowCount() : static_cast<int>(m_symbols.size());
}

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

bool ProjectExplorerWidget::isStringsMode() const { return m_stack && m_stack->currentIndex() == 2; }

int ProjectExplorerWidget::stringCount() const {
  return m_stringsTable ? m_stringsTable->rowCount() : static_cast<int>(m_strings.size());
}

/** @brief Selects a symbol by name in the table and emits symbolAddressSelected. */
bool ProjectExplorerWidget::activateSymbol(const QString& symbolName) {
  const QString query = symbolName.trimmed();
  if (query.isEmpty()) return false;

  for (const auto& entry : m_symbols) {
    if (entry.name.compare(query, Qt::CaseInsensitive) == 0 ||
        (query.compare(QStringLiteral("main"), Qt::CaseInsensitive) == 0 && entry.name == QStringLiteral("sym.main"))) {
      if (m_symbolsTable) {
        for (int r = 0; r < m_symbolsTable->rowCount(); ++r) {
          if (auto* item = m_symbolsTable->item(r, 0); item && item->text().compare(entry.name, Qt::CaseInsensitive) == 0) {
            m_symbolsTable->selectRow(r);
            break;
          }
        }
      }
      emit symbolAddressSelected(entry.address, entry.name);
      return true;
    }
  }
  return false;
}

/** @brief Selects a string by substring or hex address and emits symbolAddressSelected. */
bool ProjectExplorerWidget::activateString(const QString& queryText) {
  const QString query = queryText.trimmed();
  if (query.isEmpty()) return false;

  for (const auto& entry : m_strings) {
    const QString addrStr = QStringLiteral("0x%1").arg(entry.address, 8, 16, QChar('0'));
    if (entry.value.contains(query, Qt::CaseInsensitive) || addrStr.compare(query, Qt::CaseInsensitive) == 0) {
      if (m_stringsTable) {
        for (int r = 0; r < m_stringsTable->rowCount(); ++r) {
          if (auto* item = m_stringsTable->item(r, 2); item && item->text() == entry.value) {
            m_stringsTable->selectRow(r);
            break;
          }
        }
      }
      emit symbolAddressSelected(entry.address, entry.value);
      return true;
    }
  }
  return false;
}

bool ProjectExplorerWidget::isIgnoredDirectoryName(const QString& dirName) {
  const QString low = dirName.toLower();
  return low == QLatin1String(".git") || low == QLatin1String("builddir") ||
         low == QLatin1String("build") || low == QLatin1String("node_modules") ||
         low == QLatin1String(".cache") || low == QLatin1String("__pycache__") ||
         low == QLatin1String(".venv") || low == QLatin1String("venv") ||
         low == QLatin1String(".svn") || low == QLatin1String(".hg") ||
         low == QLatin1String("cmakefiles");
}

int ProjectExplorerWidget::fuzzyMatchScore(const QString& query, const QString& candidate) {
  const QString q = query.trimmed();
  if (q.isEmpty()) return 0;
  if (candidate.isEmpty()) return -1;

  if (q.contains(QLatin1Char('*')) || q.contains(QLatin1Char('?'))) {
    const QRegularExpression rx = QRegularExpression::fromWildcard(q, Qt::CaseInsensitive);
    return rx.match(candidate).hasMatch() ? 500 : -1;
  }

  const QString ql = q.toLower();
  const QString cl = candidate.toLower();
  if (cl == ql) return 1000;
  if (cl.startsWith(ql)) return 850;
  if (const int pos = cl.indexOf(ql); pos >= 0) {
    int bonus = 0;
    if (pos == 0 || candidate[pos - 1] == QLatin1Char('/') || candidate[pos - 1] == QLatin1Char('_') ||
        candidate[pos - 1] == QLatin1Char('-') || candidate[pos - 1] == QLatin1Char('.') ||
        candidate[pos].isUpper()) {
      bonus = 50;
    }
    return 700 + bonus - qMin(pos, 200);
  }

  int qi = 0;
  int prevMatch = -2;
  int score = 200;
  for (int ci = 0; ci < cl.size() && qi < ql.size(); ++ci) {
    if (cl[ci] == ql[qi]) {
      if (ci == prevMatch + 1) {
        score += 15;
      } else if (prevMatch >= 0) {
        score -= qMin(ci - prevMatch - 1, 12);
      }
      if (ci == 0 || candidate[ci].isUpper() || candidate[ci - 1] == QLatin1Char('/') ||
          candidate[ci - 1] == QLatin1Char('_') || candidate[ci - 1] == QLatin1Char('-') ||
          candidate[ci - 1] == QLatin1Char('.')) {
        score += 20;
      }
      prevMatch = ci;
      ++qi;
    }
  }
  return (qi == ql.size()) ? qMax(1, score) : -1;
}

QFileInfo ProjectExplorerWidget::fileInfoForIndex(const QModelIndex& index) const {
  if (!index.isValid()) return QFileInfo();
  if (m_filteredFileModel && m_treeView && m_treeView->model() == m_filteredFileModel) {
    return QFileInfo(index.data(Qt::UserRole).toString());
  }
  return m_fileSystemModel ? m_fileSystemModel->fileInfo(index) : QFileInfo();
}

void ProjectExplorerWidget::rebuildProjectFileIndex() {
  m_indexedFiles.clear();
  m_fileIndexDirty = false;
  if (m_rootPath.isEmpty()) return;
  const QDir rootDir(m_rootPath);
  if (!rootDir.exists()) return;

  constexpr int kMaxIndexedFiles = 25000;
  QStringList pendingDirs{m_rootPath};
  while (!pendingDirs.isEmpty() && m_indexedFiles.size() < kMaxIndexedFiles) {
    const QString curPath = pendingDirs.takeFirst();
    const QDir curDir(curPath);
    const QFileInfoList entries = curDir.entryInfoList(
        QDir::NoDotAndDotDot | QDir::AllDirs | QDir::Files,
        QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo& fi : entries) {
      if (fi.isSymLink()) continue;
      if (fi.isDir()) {
        if (!isIgnoredDirectoryName(fi.fileName())) pendingDirs.append(fi.absoluteFilePath());
      } else {
        m_indexedFiles.append({rootDir.relativeFilePath(fi.absoluteFilePath()), fi.absoluteFilePath()});
        if (m_indexedFiles.size() >= kMaxIndexedFiles) break;
      }
    }
  }
}

/** @brief Applies the unified explorer filter bar query to whichever page is active. */
void ProjectExplorerWidget::applyExplorerFilter(const QString& filterText) {
  if (isSymbolsMode()) { populateSymbolsTable(filterText); return; }
  if (isStringsMode()) { populateStringsTable(filterText); return; }
  if (!m_fileSystemModel || !m_treeView || !m_filteredFileModel) return;

  const QString query = filterText.trimmed();
  if (query.isEmpty()) {
    m_fileSystemModel->setNameFilters(QStringList());
    if (m_treeView->model() != m_fileSystemModel) {
      m_treeView->setModel(m_fileSystemModel);
      m_treeView->hideColumn(1);
      m_treeView->hideColumn(2);
      m_treeView->hideColumn(3);
    }
    const QModelIndex rootIdx = m_fileSystemModel->index(m_rootPath);
    if (rootIdx.isValid()) m_treeView->setRootIndex(rootIdx);
    m_treeView->collapseAll();
    return;
  }

  if (m_fileIndexDirty) rebuildProjectFileIndex();

  struct ScoredMatch {
    int score = 0;
    QString relPath;
    QString absPath;
  };
  QVector<ScoredMatch> matches;
  matches.reserve(128);
  for (const auto& [relPath, absPath] : m_indexedFiles) {
    const int slash = relPath.lastIndexOf(QLatin1Char('/'));
    const QString fileName = (slash >= 0) ? relPath.mid(slash + 1) : relPath;
    const int nameScore = fuzzyMatchScore(query, fileName);
    const int pathScore = fuzzyMatchScore(query, relPath);
    const int best = qMax(nameScore >= 0 ? nameScore + 200 : -1, pathScore);
    if (best >= 0) matches.append({best, relPath, absPath});
  }

  std::sort(matches.begin(), matches.end(), [](const ScoredMatch& a, const ScoredMatch& b) {
    if (a.score != b.score) return a.score > b.score;
    return a.relPath.compare(b.relPath, Qt::CaseInsensitive) < 0;
  });

  m_filteredFileModel->clear();
  m_filteredFileModel->setHorizontalHeaderLabels({QStringLiteral("Name")});
  QStandardItem* rootItem = m_filteredFileModel->invisibleRootItem();
  QHash<QString, QStandardItem*> folderItems;

  auto& tm = gridlock::core::managers::ThemeManager::instance();
  QColor mutedColor(tm.paletteForTheme(tm.activeThemeId()).textHex);
  mutedColor.setAlpha(128);
  const QBrush mutedBrush(mutedColor);

  const QDir rootDir(m_rootPath);
  for (const ScoredMatch& m : matches) {
    const QStringList parts = m.relPath.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.isEmpty()) continue;

    QStandardItem* parentItem = rootItem;
    QString curRel;
    for (int i = 0; i < parts.size() - 1; ++i) {
      curRel = curRel.isEmpty() ? parts[i] : (curRel + QLatin1Char('/') + parts[i]);
      QStandardItem* folderNode = folderItems.value(curRel, nullptr);
      if (!folderNode) {
        const QString absFolder = rootDir.filePath(curRel);
        folderNode = new QStandardItem(parts[i]);
        folderNode->setEditable(false);
        folderNode->setData(absFolder, Qt::UserRole);
        if (m_iconProvider) folderNode->setIcon(m_iconProvider->icon(QFileInfo(absFolder)));
        parentItem->appendRow(folderNode);
        folderItems.insert(curRel, folderNode);
      }
      parentItem = folderNode;
    }

    const QFileInfo fi(m.absPath);
    auto* fileItem = new QStandardItem(parts.last());
    fileItem->setEditable(false);
    fileItem->setData(m.absPath, Qt::UserRole);
    fileItem->setToolTip(m.relPath);
    if (m_iconProvider) fileItem->setIcon(m_iconProvider->icon(fi));
    if (FileIconProvider::isBuildArtifactFile(fi)) fileItem->setForeground(mutedBrush);
    parentItem->appendRow(fileItem);
  }

  if (m_treeView->model() != m_filteredFileModel) {
    m_treeView->setModel(m_filteredFileModel);
  }
  m_treeView->setRootIndex(QModelIndex());
  m_treeView->expandAll();
}

void ProjectExplorerWidget::filterSymbols(const QString& filterText) { populateSymbolsTable(filterText); }
void ProjectExplorerWidget::filterStrings(const QString& filterText) { populateStringsTable(filterText); }

/** @brief Reloads font size, item padding, mode buttons, filter bar, and icon provider from theme settings. */
void ProjectExplorerWidget::reloadStyle() {
  QSettings s("gridlock", "debugger");
  const QString style = s.value("appearance/file_tree_style", "Comfortable").toString();

  int padding = 2, fontSize = 12;
  if (style == "Compact") { padding = 1; fontSize = 12; }
  else if (style == "Large") { padding = 4; fontSize = 13; }

  auto& tm = gridlock::core::managers::ThemeManager::instance();
  const auto pal = tm.paletteForTheme(tm.activeThemeId());

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

  if (m_treeView) {
    QString uiFamily = s.value(QStringLiteral("appearance/ui_font_family"), QStringLiteral("Inter")).toString().trimmed();
    if (uiFamily.isEmpty()) uiFamily = QStringLiteral("Inter");
    QFont treeFont(uiFamily); treeFont.setStyleHint(QFont::SansSerif); treeFont.setFixedPitch(false); treeFont.setPixelSize(fontSize);
    if (QFontInfo(treeFont).fixedPitch()) { treeFont = QFont(QStringLiteral("Sans Serif")); treeFont.setStyleHint(QFont::SansSerif); treeFont.setPixelSize(fontSize); }
    m_treeView->setFont(treeFont); m_treeView->setIconSize(QSize(16, 16));
    m_treeView->setStyleSheet(QStringLiteral(
        "QTreeView { font-family: '%7', 'Inter', 'Noto Sans', sans-serif; background-color: %3; color: %4; font-size: %2px; border: none; } "
        "QTreeView::item { padding: %1px 4px; } "
        "QHeaderView::section { font-family: '%7', 'Inter', 'Noto Sans', sans-serif; background-color: %5; color: %4; "
        "border: 1px solid %6; padding: 2px 4px; font-size: 12px; font-weight: 600; }")
        .arg(padding).arg(fontSize).arg(pal.canvasHex, pal.textHex, pal.headerHex, pal.borderHex, uiFamily));
  }
  if (m_fileSystemModel) { auto* np = new FileIconProvider(); m_fileSystemModel->setIconProvider(np); delete m_iconProvider; m_iconProvider = np; }
}

void ProjectExplorerWidget::onOpenFolder() {
  const QString dir = FileDialogHelper::getExistingDirectory(this, QStringLiteral("Open Folder"), m_rootPath);
  if (!dir.isEmpty()) setRootPath(dir);
}

/** @brief Resolves any directory or `.toml` session path to its workspace directory and updates the FILES tree. */
void ProjectExplorerWidget::setRootPath(const QString& path) {
  const QString trimmed = path.trimmed();
  if (trimmed.isEmpty()) return;

  const QFileInfo rawInfo(trimmed);
  QDir dir = rawInfo.exists() ? (rawInfo.isDir() ? QDir(rawInfo.absoluteFilePath()) : rawInfo.dir())
                              : (rawInfo.fileName().endsWith(QStringLiteral(".toml"), Qt::CaseInsensitive)
                                     ? rawInfo.dir()
                                     : QDir(rawInfo.absoluteFilePath()));
  if (dir.dirName() == QStringLiteral(".gridlock")) dir.cdUp();

  const QFileInfo dirInfo(dir.absolutePath());
  const QString resolvedDir =
      !dirInfo.canonicalFilePath().isEmpty() ? dirInfo.canonicalFilePath() : QDir::cleanPath(dirInfo.absoluteFilePath());
  m_rootPath = resolvedDir;
  m_fileIndexDirty = true;

  if (m_fileSystemModel && m_treeView) {
    m_fileSystemModel->setRootPath(QString());
    const QModelIndex rootIdx = m_fileSystemModel->setRootPath(resolvedDir);
    if (m_treeView->model() != m_fileSystemModel) {
      m_treeView->setModel(m_fileSystemModel);
      m_treeView->hideColumn(1);
      m_treeView->hideColumn(2);
      m_treeView->hideColumn(3);
    }
    m_treeView->setRootIndex(rootIdx.isValid() ? rootIdx : m_fileSystemModel->index(resolvedDir));
    m_treeView->clearSelection();
  }
  setSymbolsMode(false);
  emit workspaceRootChanged(resolvedDir);
}

void ProjectExplorerWidget::onDoubleClicked(const QModelIndex& index) {
  if (!index.isValid()) return;
  const QFileInfo info = fileInfoForIndex(index);
  if (info.isFile()) emit fileDoubleClicked(info.absoluteFilePath());
}

void ProjectExplorerWidget::refreshTreeViewPath(const QString& focusPath) {
  if (!m_fileSystemModel || !m_treeView) return;
  m_fileIndexDirty = true;
  const QString root = !m_rootPath.isEmpty() ? m_rootPath : m_fileSystemModel->rootPath();
  m_fileSystemModel->setRootPath(QString());
  const QModelIndex rootIdx = m_fileSystemModel->setRootPath(root);
  if (m_explorerFilterEdit && !m_explorerFilterEdit->text().trimmed().isEmpty() && !isSymbolsMode() && !isStringsMode()) {
    applyExplorerFilter(m_explorerFilterEdit->text());
    return;
  }
  if (m_treeView->model() != m_fileSystemModel) {
    m_treeView->setModel(m_fileSystemModel);
    m_treeView->hideColumn(1);
    m_treeView->hideColumn(2);
    m_treeView->hideColumn(3);
  }
  m_treeView->setRootIndex(rootIdx.isValid() ? rootIdx : m_fileSystemModel->index(root));
  if (!focusPath.isEmpty()) {
    const QModelIndex idx = m_fileSystemModel->index(focusPath);
    if (idx.isValid()) { m_treeView->expand(idx.parent()); m_treeView->setCurrentIndex(idx); }
  }
}

bool ProjectExplorerWidget::createNewFile(const QString& parentDir, const QString& fileName) {
  const QString cleanName = fileName.trimmed();
  if (cleanName.isEmpty() || cleanName.contains(QStringLiteral(".."))) return false;
  const QString baseDir = parentDir.trimmed().isEmpty() ? (!m_rootPath.isEmpty() ? m_rootPath : QDir::currentPath()) : parentDir.trimmed();
  QDir dir(baseDir);
  if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) return false;
  const QString fullPath = QFileInfo(dir.filePath(cleanName)).absoluteFilePath();
  const QFileInfo targetInfo(fullPath);
  if (!targetInfo.dir().exists() && !targetInfo.dir().mkpath(QStringLiteral("."))) return false;

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

bool ProjectExplorerWidget::createNewFolder(const QString& parentDir, const QString& folderName) {
  const QString cleanName = folderName.trimmed();
  if (cleanName.isEmpty() || cleanName.contains(QStringLiteral(".."))) return false;
  const QString baseDir = parentDir.trimmed().isEmpty() ? (!m_rootPath.isEmpty() ? m_rootPath : QDir::currentPath()) : parentDir.trimmed();
  QDir dir(baseDir);
  if (!dir.mkpath(cleanName)) return false;
  refreshTreeViewPath(dir.filePath(cleanName));
  return true;
}

bool ProjectExplorerWidget::renameEntry(const QString& oldPath, const QString& newName) {
  const QString cleanName = newName.trimmed();
  const QFileInfo oldInfo(oldPath);
  if (!oldInfo.exists() || cleanName.isEmpty() || cleanName.contains(QLatin1Char('/')) || cleanName.contains(QStringLiteral(".."))) return false;
  const QString absOld = oldInfo.absoluteFilePath(), absNew = oldInfo.dir().filePath(cleanName);
  if (absOld == absNew) return true;
  if (!QDir().rename(absOld, absNew)) return false;
  refreshTreeViewPath(absNew);
  emit fileRenamed(absOld, absNew);
  return true;
}

bool ProjectExplorerWidget::deleteEntry(const QString& targetPath) {
  const QFileInfo info(targetPath);
  if (!info.exists()) return false;
  const QString absTarget = info.absoluteFilePath();
  if (!(info.isDir() ? QDir(absTarget).removeRecursively() : QFile::remove(absTarget))) return false;
  refreshTreeViewPath();
  emit fileDeleted(absTarget);
  return true;
}

bool ProjectExplorerWidget::registerSourceInBuildFile(const QString& buildFilePath, const QString& relativeSourcePath) {
  const QString cleanRel = relativeSourcePath.trimmed();
  if (cleanRel.isEmpty() || !QFileInfo::exists(buildFilePath)) return false;
  QFile bf(buildFilePath);
  if (!bf.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
  QString content = QString::fromUtf8(bf.readAll());
  bf.close();
  if (content.contains(cleanRel)) return true;

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

/** @brief Displays the interactive right-click CRUD context menu on the FILES tree view. */
void ProjectExplorerWidget::onTreeCustomContextMenu(const QPoint& pos) {
  if (!m_treeView || !m_fileSystemModel) return;
  const QModelIndex idx = m_treeView->indexAt(pos);
  const QFileInfo selInfo = idx.isValid() ? fileInfoForIndex(idx) : QFileInfo(m_rootPath);
  const QString targetDir = selInfo.isDir() ? selInfo.absoluteFilePath() : selInfo.absolutePath();

  QMenu menu(this);
  QAction* newCppAct = menu.addAction(tr("New Source File (.cpp)..."));
  QAction* newHppAct = menu.addAction(tr("New Header File (.hpp)..."));
  QAction* newFolderAct = menu.addAction(tr("New Folder..."));
  menu.addSeparator();
  QAction* renameAct = menu.addAction(tr("Rename...\tF2"));
  QAction* deleteAct = menu.addAction(tr("Delete..."));
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
