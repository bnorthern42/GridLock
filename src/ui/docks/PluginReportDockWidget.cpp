/**
 * @file PluginReportDockWidget.cpp
 * @brief Implementation of the declarative Markdown, key-value table, and JSON tree plugin report dock.
 */

#include "PluginReportDockWidget.hpp"

#include "../../core/managers/ThemeManager.hpp"

#include <QAbstractItemView>
#include <QFrame>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QTabBar>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

namespace gridlock::ui {

namespace {

QString formatJsonScalar(const QJsonValue& val) {
  if (val.isString()) return val.toString();
  if (val.isBool()) return val.toBool() ? QStringLiteral("true") : QStringLiteral("false");
  if (val.isDouble()) return QString::number(val.toDouble(), 'g', 12);
  if (val.isNull() || val.isUndefined()) return QStringLiteral("null");
  if (val.isArray()) return QString::fromUtf8(QJsonDocument(val.toArray()).toJson(QJsonDocument::Compact));
  if (val.isObject()) return QString::fromUtf8(QJsonDocument(val.toObject()).toJson(QJsonDocument::Compact));
  return {};
}

void appendJsonNode(QTreeWidgetItem* parentItem, const QString& key, const QJsonValue& value, int depth) {
  auto* item = new QTreeWidgetItem(parentItem);
  item->setText(0, key);
  if (value.isObject()) {
    const QJsonObject obj = value.toObject();
    item->setText(1, QStringLiteral("{%1 field%2}").arg(obj.size()).arg(obj.size() == 1 ? "" : "s"));
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
      appendJsonNode(item, it.key(), it.value(), depth + 1);
    }
    if (depth < 2) item->setExpanded(true);
  } else if (value.isArray()) {
    const QJsonArray arr = value.toArray();
    item->setText(1, QStringLiteral("[%1 item%2]").arg(arr.size()).arg(arr.size() == 1 ? "" : "s"));
    for (qsizetype i = 0; i < arr.size(); ++i) {
      appendJsonNode(item, QStringLiteral("[%1]").arg(i), arr.at(i), depth + 1);
    }
    if (depth < 2) item->setExpanded(true);
  } else {
    const QString scalar = formatJsonScalar(value);
    item->setText(1, scalar);
    item->setToolTip(1, scalar);
  }
}

}  // namespace

PluginReportDockWidget::PluginReportDockWidget(QWidget* parent)
    : QDockWidget(QStringLiteral("Plugin Reports"), parent) {
  setObjectName(QStringLiteral("PluginReportDock"));
  setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable |
              QDockWidget::DockWidgetClosable);

  m_container = new QWidget(this);
  m_container->setObjectName(QStringLiteral("pluginReportContainer"));
  m_container->setContentsMargins(0, 0, 0, 0);

  auto* layout = new QVBoxLayout(m_container);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  m_tabWidget = new QTabWidget(m_container);
  m_tabWidget->setObjectName(QStringLiteral("pluginReportTabWidget"));
  m_tabWidget->setDocumentMode(true);
  m_tabWidget->setTabsClosable(true);
  m_tabWidget->setContentsMargins(0, 0, 0, 0);
  if (QTabBar* bar = m_tabWidget->tabBar()) {
    bar->setUsesScrollButtons(false);
    bar->setElideMode(Qt::ElideNone);
    bar->setExpanding(false);
  }
  connect(m_tabWidget, &QTabWidget::tabCloseRequested, this,
          &PluginReportDockWidget::onTabCloseRequested);

  layout->addWidget(m_tabWidget, 1);
  setWidget(m_container);
  reloadStyle();
}

void PluginReportDockWidget::upsertPanel(const gridlock::core::plugins::PluginReportPanel& panel) {
  if (!m_tabWidget || panel.panelId.isEmpty()) return;

  auto existingIt = m_panelsById.find(panel.panelId);
  if (existingIt != m_panelsById.end() && existingIt->page != nullptr &&
      existingIt->format == panel.format) {
    existingIt->pluginId = panel.pluginId;
    if (panel.format == GRIDLOCK_REPORT_MARKDOWN) {
      if (auto* browser = qobject_cast<QTextBrowser*>(existingIt->page)) {
        browser->setMarkdown(panel.content);
      }
    } else if (panel.format == GRIDLOCK_REPORT_KEY_VALUE_TABLE) {
      if (auto* tbl = qobject_cast<QTableWidget*>(existingIt->page)) {
        populateKeyValueTable(tbl, panel.content);
      }
    } else if (panel.format == GRIDLOCK_REPORT_JSON_TREE) {
      if (auto* tree = qobject_cast<QTreeWidget*>(existingIt->page)) {
        populateJsonTree(tree, panel.content);
      }
    }
    const int idx = m_tabWidget->indexOf(existingIt->page);
    if (idx >= 0) {
      m_tabWidget->setTabText(idx, panel.title);
      m_tabWidget->setCurrentIndex(idx);
    }
    show();
    raise();
    return;
  }

  if (existingIt != m_panelsById.end()) {
    removePanel(existingIt->pluginId, panel.panelId);
  }

  QWidget* page = nullptr;
  if (panel.format == GRIDLOCK_REPORT_KEY_VALUE_TABLE) {
    page = buildKeyValuePage(panel.content, m_tabWidget);
  } else if (panel.format == GRIDLOCK_REPORT_JSON_TREE) {
    page = buildJsonTreePage(panel.content, m_tabWidget);
  } else {
    page = buildMarkdownPage(panel.content, m_tabWidget);
  }
  page->setObjectName(QStringLiteral("pluginReportPage_") + panel.panelId);

  const int tabIdx = m_tabWidget->addTab(page, panel.title);
  m_tabWidget->setTabToolTip(tabIdx, QStringLiteral("%1 (%2)").arg(panel.title, panel.pluginId));
  m_tabWidget->setCurrentIndex(tabIdx);
  m_panelsById.insert(panel.panelId, PanelEntry{panel.pluginId, panel.panelId, panel.format, page});

  reloadStyle();
  show();
  raise();
}

void PluginReportDockWidget::removePanel(const QString& pluginId, const QString& panelId) {
  auto it = m_panelsById.find(panelId);
  if (it == m_panelsById.end()) return;
  if (!pluginId.isEmpty() && it->pluginId != pluginId) return;

  if (QWidget* page = it->page) {
    const int idx = m_tabWidget ? m_tabWidget->indexOf(page) : -1;
    if (idx >= 0) m_tabWidget->removeTab(idx);
    page->deleteLater();
  }
  m_panelsById.erase(it);
  if (panelCount() == 0) hide();
}

void PluginReportDockWidget::removePanelsForPlugin(const QString& pluginId) {
  QStringList idsToRemove;
  for (auto it = m_panelsById.cbegin(); it != m_panelsById.cend(); ++it) {
    if (pluginId.isEmpty() || it->pluginId == pluginId) idsToRemove.append(it.key());
  }
  for (const QString& id : idsToRemove) {
    removePanel(pluginId, id);
  }
}

QWidget* PluginReportDockWidget::panelWidget(const QString& panelId) const {
  const auto it = m_panelsById.find(panelId);
  return (it != m_panelsById.end()) ? it->page : nullptr;
}

void PluginReportDockWidget::onTabCloseRequested(int index) {
  if (!m_tabWidget || index < 0 || index >= m_tabWidget->count()) return;
  QWidget* page = m_tabWidget->widget(index);
  QString matchedId;
  QString matchedPlugin;
  for (auto it = m_panelsById.cbegin(); it != m_panelsById.cend(); ++it) {
    if (it->page == page) {
      matchedId = it->panelId;
      matchedPlugin = it->pluginId;
      break;
    }
  }
  if (!matchedId.isEmpty()) {
    removePanel(matchedPlugin, matchedId);
    emit panelClosedByUser(matchedPlugin, matchedId);
  }
}

QWidget* PluginReportDockWidget::buildMarkdownPage(const QString& content, QWidget* parent) {
  auto* browser = new QTextBrowser(parent);
  browser->setFrameShape(QFrame::NoFrame);
  browser->setReadOnly(true);
  browser->setOpenExternalLinks(false);
  browser->setOpenLinks(false);
  browser->setMarkdown(content);
  return browser;
}

QWidget* PluginReportDockWidget::buildKeyValuePage(const QString& content, QWidget* parent) {
  auto* table = new QTableWidget(0, 2, parent);
  table->setHorizontalHeaderLabels({QStringLiteral("Key"), QStringLiteral("Value")});
  table->setFrameShape(QFrame::NoFrame);
  table->setAlternatingRowColors(true);
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->setSelectionMode(QAbstractItemView::SingleSelection);
  table->verticalHeader()->setVisible(false);
  table->verticalHeader()->setDefaultSectionSize(22);

  QHeaderView* hdr = table->horizontalHeader();
  hdr->setMinimumSectionSize(64);
  hdr->setStretchLastSection(true);
  hdr->setSectionResizeMode(0, QHeaderView::Interactive);
  hdr->setSectionResizeMode(1, QHeaderView::Stretch);
  table->setColumnWidth(0, 140);

  populateKeyValueTable(table, content);
  return table;
}

void PluginReportDockWidget::populateKeyValueTable(QTableWidget* table, const QString& content) {
  if (!table) return;
  table->setRowCount(0);

  QList<QPair<QString, QString>> rows;
  QJsonParseError err{};
  const QJsonDocument doc = QJsonDocument::fromJson(content.toUtf8(), &err);
  if (err.error == QJsonParseError::NoError && doc.isObject()) {
    const QJsonObject obj = doc.object();
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
      rows.append(qMakePair(it.key(), formatJsonScalar(it.value())));
    }
  } else if (err.error == QJsonParseError::NoError && doc.isArray()) {
    const QJsonArray arr = doc.array();
    for (qsizetype i = 0; i < arr.size(); ++i) {
      if (arr.at(i).isObject()) {
        const QJsonObject entry = arr.at(i).toObject();
        if (entry.contains(QStringLiteral("key")) && entry.contains(QStringLiteral("value"))) {
          rows.append(qMakePair(formatJsonScalar(entry.value(QStringLiteral("key"))),
                                formatJsonScalar(entry.value(QStringLiteral("value")))));
          continue;
        }
      }
      rows.append(qMakePair(QStringLiteral("[%1]").arg(i), formatJsonScalar(arr.at(i))));
    }
  } else {
    for (const QString& rawLine : content.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
      const QString trimmed = rawLine.trimmed();
      if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#'))) continue;
      int sep = trimmed.indexOf(QLatin1Char('='));
      if (sep < 0) sep = trimmed.indexOf(QLatin1Char(':'));
      if (sep > 0) {
        rows.append(qMakePair(trimmed.left(sep).trimmed(), trimmed.mid(sep + 1).trimmed()));
      } else {
        rows.append(qMakePair(trimmed, QString()));
      }
    }
  }

  table->setRowCount(rows.size());
  for (int r = 0; r < rows.size(); ++r) {
    auto* kItem = new QTableWidgetItem(rows.at(r).first);
    auto* vItem = new QTableWidgetItem(rows.at(r).second);
    kItem->setToolTip(rows.at(r).first);
    vItem->setToolTip(rows.at(r).second);
    table->setItem(r, 0, kItem);
    table->setItem(r, 1, vItem);
  }
}

QWidget* PluginReportDockWidget::buildJsonTreePage(const QString& content, QWidget* parent) {
  auto* tree = new QTreeWidget(parent);
  tree->setColumnCount(2);
  tree->setHeaderLabels({QStringLiteral("Field"), QStringLiteral("Value")});
  tree->setFrameShape(QFrame::NoFrame);
  tree->setAlternatingRowColors(true);
  tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
  tree->setSelectionBehavior(QAbstractItemView::SelectRows);
  if (QHeaderView* hdr = tree->header()) {
    hdr->setMinimumSectionSize(64);
    hdr->setStretchLastSection(true);
    hdr->setSectionResizeMode(0, QHeaderView::Interactive);
    tree->setColumnWidth(0, 160);
  }
  populateJsonTree(tree, content);
  return tree;
}

void PluginReportDockWidget::populateJsonTree(QTreeWidget* tree, const QString& content) {
  if (!tree) return;
  tree->clear();

  QJsonParseError err{};
  const QJsonDocument doc = QJsonDocument::fromJson(content.toUtf8(), &err);
  if (err.error != QJsonParseError::NoError) {
    auto* fallback = new QTreeWidgetItem(tree);
    fallback->setText(0, QStringLiteral("(unparsed payload)"));
    fallback->setText(1, content.trimmed());
    return;
  }

  if (doc.isObject()) {
    const QJsonObject obj = doc.object();
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
      auto* top = new QTreeWidgetItem(tree);
      top->setText(0, it.key());
      if (it.value().isObject() || it.value().isArray()) {
        const bool isObj = it.value().isObject();
        const qsizetype sz = isObj ? it.value().toObject().size() : it.value().toArray().size();
        top->setText(1, isObj ? QStringLiteral("{%1}").arg(sz) : QStringLiteral("[%1]").arg(sz));
        if (isObj) {
          const QJsonObject sub = it.value().toObject();
          for (auto sit = sub.constBegin(); sit != sub.constEnd(); ++sit) {
            appendJsonNode(top, sit.key(), sit.value(), 1);
          }
        } else {
          const QJsonArray sub = it.value().toArray();
          for (qsizetype i = 0; i < sub.size(); ++i) {
            appendJsonNode(top, QStringLiteral("[%1]").arg(i), sub.at(i), 1);
          }
        }
        top->setExpanded(true);
      } else {
        top->setText(1, formatJsonScalar(it.value()));
      }
    }
  } else if (doc.isArray()) {
    const QJsonArray arr = doc.array();
    for (qsizetype i = 0; i < arr.size(); ++i) {
      auto* top = new QTreeWidgetItem(tree);
      top->setText(0, QStringLiteral("[%1]").arg(i));
      if (arr.at(i).isObject() || arr.at(i).isArray()) {
        const QJsonValue v = arr.at(i);
        if (v.isObject()) {
          const QJsonObject sub = v.toObject();
          top->setText(1, QStringLiteral("{%1}").arg(sub.size()));
          for (auto sit = sub.constBegin(); sit != sub.constEnd(); ++sit) {
            appendJsonNode(top, sit.key(), sit.value(), 1);
          }
        } else {
          const QJsonArray sub = v.toArray();
          top->setText(1, QStringLiteral("[%1]").arg(sub.size()));
          for (qsizetype j = 0; j < sub.size(); ++j) {
            appendJsonNode(top, QStringLiteral("[%1]").arg(j), sub.at(j), 1);
          }
        }
        top->setExpanded(true);
      } else {
        top->setText(1, formatJsonScalar(arr.at(i)));
      }
    }
  }
}

void PluginReportDockWidget::reloadStyle() {
  const auto& tm = gridlock::core::ThemeManager::instance();
  const auto pal = tm.paletteForTheme(tm.activeThemeId());
  const QString altRowHex = pal.isDark ? QStringLiteral("#1c2026") : QStringLiteral("#f1f4f8");
  if (m_container) {
    m_container->setStyleSheet(QStringLiteral(
        "#pluginReportContainer { background-color: %1; border: none; } "
        "QTabWidget#pluginReportTabWidget::pane { border: none; background-color: %1; } "
        "QTabWidget#pluginReportTabWidget QTabBar { background-color: %2; border-bottom: 1px solid %3; } "
        "QTabWidget#pluginReportTabWidget QTabBar::tab { background-color: %2; color: %4; "
        "padding: 3px 8px; min-height: 20px; max-height: 24px; border-right: 1px solid %3; font-size: 12px; font-weight: 600; } "
        "QTabWidget#pluginReportTabWidget QTabBar::tab:selected { background-color: %1; color: %4; border-bottom: 2px solid %5; } "
        "QTextBrowser { background-color: %1; color: %4; border: none; padding: 6px; font-size: 12px; } "
        "QTableWidget, QTreeWidget { background-color: %1; alternate-background-color: %6; "
        "color: %4; gridline-color: %3; border: none; font-family: 'JetBrains Mono', monospace; font-size: 12px; } "
        "QHeaderView::section { background-color: %2; color: %4; border: none; "
        "border-right: 1px solid %3; border-bottom: 1px solid %3; padding: 3px 6px; font-size: 12px; font-weight: 600; }")
        .arg(pal.canvasHex, pal.headerHex, pal.borderHex, pal.textHex, pal.accentHex, altRowHex));
  }
}

}  // namespace gridlock::ui
