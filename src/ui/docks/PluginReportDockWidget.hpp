/**
 * @file PluginReportDockWidget.hpp
 * @brief Host-owned dockable report viewer rendering plugin Markdown, key-value, and JSON tree panels.
 *
 * Hosts a closable `QTabWidget` (`#pluginReportTabWidget`) inside a zero-margin container so
 * C99 plugins can publish rich diagnostics without linking any GUI toolkit symbols.
 */

#pragma once

#include "../../core/plugins/PluginUiModels.hpp"

#include <QDockWidget>
#include <QMap>
#include <QPair>
#include <QString>
#include <QTabWidget>
#include <QWidget>

class QTableWidget;
class QTreeWidget;

namespace gridlock::ui {

/**
 * @class PluginReportDockWidget
 * @brief Tabbed workstation dock rendering `GRIDLOCK_REPORT_MARKDOWN`, `KEY_VALUE_TABLE`, and `JSON_TREE` views.
 */
class PluginReportDockWidget : public QDockWidget {
  Q_OBJECT

 public:
  /**
   * @brief Constructs the `PluginReportDockWidget` with zero-margin chrome and closable report tabs.
   * @param parent Optional owning `MainWindow`.
   */
  explicit PluginReportDockWidget(QWidget* parent = nullptr);

  /** @brief Virtual destructor. */
  ~PluginReportDockWidget() override = default;

  /**
   * @brief Inserts or updates a declarative report tab for @p panel, showing and raising the dock.
   * @param panel Validated report specification emitted by `PluginHostBridge::reportViewPublished`.
   */
  void upsertPanel(const gridlock::core::plugins::PluginReportPanel& panel);

  /**
   * @brief Closes and deletes a single report panel owned by @p pluginId.
   * @param pluginId Owning plugin identifier.
   * @param panelId Panel identifier registered via `publish_report_view`.
   */
  void removePanel(const QString& pluginId, const QString& panelId);

  /**
   * @brief Purges all report tabs belonging to @p pluginId (or all plugins when empty).
   * @param pluginId Owning plugin identifier, or empty string to clear all tabs.
   */
  void removePanelsForPlugin(const QString& pluginId);

  /** @brief Returns the number of currently open report tabs. */
  int panelCount() const { return m_tabWidget ? m_tabWidget->count() : 0; }

  /** @brief Returns the internal `QTabWidget` hosting report pages. */
  QTabWidget* tabWidget() const { return m_tabWidget; }

  /** @brief Looks up the rendered widget (`QTextBrowser`, `QTableWidget`, or `QTreeWidget`) for @p panelId. */
  QWidget* panelWidget(const QString& panelId) const;

  /** @brief Reapplies active `ThemeManager` palette styling across the tab bar and report views. */
  Q_INVOKABLE void reloadStyle();

 signals:
  /** @brief Emitted when the user clicks the close button on a report tab. */
  void panelClosedByUser(const QString& pluginId, const QString& panelId);

 private slots:
  /** @brief Handles user tab close clicks on `#pluginReportTabWidget`. */
  void onTabCloseRequested(int index);

 private:
  struct PanelEntry {
    QString pluginId;
    QString panelId;
    GridLockReportFormat format = GRIDLOCK_REPORT_MARKDOWN;
    QWidget* page = nullptr;
  };

  static QWidget* buildMarkdownPage(const QString& content, QWidget* parent);
  static QWidget* buildKeyValuePage(const QString& content, QWidget* parent);
  static QWidget* buildJsonTreePage(const QString& content, QWidget* parent);
  static void populateKeyValueTable(QTableWidget* table, const QString& content);
  static void populateJsonTree(QTreeWidget* tree, const QString& content);

  QWidget* m_container = nullptr;
  QTabWidget* m_tabWidget = nullptr;
  QMap<QString, PanelEntry> m_panelsById;
};

}  // namespace gridlock::ui
