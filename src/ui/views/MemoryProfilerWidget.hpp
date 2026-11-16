/**
 * @file MemoryProfilerWidget.hpp
 * @brief Visual library, VMA region, and caller-attributed heap memory profiler widget.
 *
 * Combines live `/proc/<pid>/smaps` virtual-memory-area aggregation with an optional
 * lightweight `LD_PRELOAD` caller-attribution tracker (`gridlock_memtrack`) to render
 * an interactive proportional stacked bar, 2D squarified treemap, and sortable table.
 */

#pragma once

#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRectF>
#include <QString>
#include <QTableWidget>
#include <QVector>
#include <QWidget>
#include <cstdint>
#include <functional>

namespace gridlock::ui {

/**
 * @brief Aggregated memory usage record for a shared library, VMA category, or heap call site.
 */
struct MemoryProfileEntry {
  QString name;              ///< Display module or region label (e.g. `libHYPRE.so`, `barrier_hex`).
  QString fullPath;          ///< Full filesystem path or VMA region tag.
  QString category;          ///< Classification (`Shared Lib`, `Heap / Call Site`, `Anon mmap`, `MPI /dev/shm`, `Stack`).
  QString topSymbolOrRange;  ///< Demangled top allocator call site or representative hex VMA range.
  uint64_t liveBytes = 0;    ///< Active resident (`Rss`) or caller-attributed live heap bytes.
  uint64_t dirtyBytes = 0;   ///< Private dirty bytes (`Private_Dirty` or live heap bytes).
  uint64_t peakBytes = 0;    ///< Peak observed bytes (from `LD_PRELOAD` tracker or VMA `Size`).
  uint64_t startAddress = 0; ///< Lowest virtual start address for double-click hex inspection.
  uint64_t endAddress = 0;   ///< Highest virtual end address for range display.
  uint64_t allocCount = 0;   ///< Tracked allocation count when Engine B data is available.
  QColor color;              ///< Theme-harmonized tile color used in the stacked bar and treemap.
};

/**
 * @brief Interactive 2D proportional stacked bar and balanced treemap canvas widget.
 */
class MemoryTreemapCanvas : public QWidget {
  Q_OBJECT

 public:
  /// Positioned visual rectangle bound to an index in `m_entries`.
  struct Tile {
    QRectF rect;
    int entryIndex = -1;
  };

  explicit MemoryTreemapCanvas(QWidget* parent = nullptr);
  ~MemoryTreemapCanvas() override = default;

  /// Replaces the rendered memory entries and recomputes stacked bar + treemap geometry.
  void setEntries(const QVector<MemoryProfileEntry>& entries, uint64_t totalBytes);

  /// Clears all entries, selection state, and cached tile rectangles.
  void clear();

  /// Returns the number of laid-out 2D treemap rectangles currently visible on the canvas.
  int treemapRectCount() const { return static_cast<int>(m_treemapTiles.size()); }

  /// Highlights the tile corresponding to `entryIndex` (`-1` clears selection).
  void setSelectedEntryIndex(int entryIndex);

 signals:
  /// Emitted when the user single-clicks a tile to focus/filter the breakdown table.
  void entryClicked(const QString& entryName, uint64_t startAddress);
  /// Emitted when the user double-clicks a tile to jump `MemView` `HEX DUMP` to `startAddress`.
  void addressActivated(uint64_t startAddress);

 protected:
  /// Renders the top proportional stacked bar and 2D treemap cards via `QPainter`.
  void paintEvent(QPaintEvent* event) override;
  /// Recomputes tile rectangles whenever the canvas viewport resizes.
  void resizeEvent(QResizeEvent* event) override;
  /// Updates hovered tile highlight and displays rich per-module memory tooltips.
  void mouseMoveEvent(QMouseEvent* event) override;
  /// Clears hover highlight when the cursor leaves the canvas.
  void leaveEvent(QEvent* event) override;
  /// Selects the clicked module tile and notifies the breakdown table.
  void mousePressEvent(QMouseEvent* event) override;
  /// Activates hex-dump navigation at the clicked tile's start address.
  void mouseDoubleClickEvent(QMouseEvent* event) override;

 private:
  void rebuildLayout();
  void subdivideTreemap(int beginIdx, int endIdx, const QRectF& bounds);
  int tileAtPosition(const QPointF& pos) const;

  QVector<MemoryProfileEntry> m_entries;
  QVector<Tile> m_barTiles;
  QVector<Tile> m_treemapTiles;
  uint64_t m_totalBytes = 0;
  int m_hoveredEntry = -1;
  int m_selectedEntry = -1;
};

/**
 * @brief Interactive visual library, `/proc/<pid>/smaps`, and caller-heap memory profiler.
 */
class MemoryProfilerWidget : public QWidget {
  Q_OBJECT

 public:
  /// Callback resolving the live OS PID for a given MPI rank index.
  using RankPidGetter = std::function<qint64(int rank)>;

  explicit MemoryProfilerWidget(QWidget* parent = nullptr);
  ~MemoryProfilerWidget() override = default;

  /// Binds the rank-to-PID callback and updates the rank selector combo box.
  void setAvailableRanks(int rankCount, int focusedRank, RankPidGetter pidGetter);

  /// Parses `/proc/<pid>/smaps` + `/tmp/gridlock_mem_<pid>.tsv` and updates treemap and table.
  Q_INVOKABLE void refreshForPid(qint64 pid);

  /// Aggregates `/proc/<pid>/smaps` + tracker files across all active ranks (`0..rankCount-1`).
  Q_INVOKABLE void refreshAllRanks();

  /// Returns the number of non-empty 2D treemap tiles rendered in `#memoryTreemapCanvas`.
  Q_INVOKABLE int treemapRectCount() const;

  /// Resets treemap tiles, breakdown rows, search filter, and summary badge to idle state.
  void clear();

  /// Re-applies active `ThemeManager` colors to controls, treemap canvas, and breakdown table.
  void reloadStyle();

  /// Compiles `/tmp/gridlock_memtrack_v1_<uid>.so` if available and syncs `GRIDLOCK_MEMTRACK_SO`.
  static QString syncPreloadTrackerEnvironment(bool enabled);

 signals:
  /// Emitted when the user double-clicks a treemap tile or breakdown row to inspect hex bytes.
  void inspectAddressRequested(uint64_t address);

 private slots:
  void onRefreshClicked();
  void applySearchFilter(const QString& text);

 private:
  static QVector<MemoryProfileEntry> collectEntriesForPid(qint64 pid);
  static QString demangleSymbol(const QString& rawSym);
  static QString formatMegabytes(uint64_t bytes);
  void populateFromEntries(const QVector<MemoryProfileEntry>& rawEntries, const QString& contextLabel);

  QComboBox* m_rankCombo = nullptr;
  QPushButton* m_refreshBtn = nullptr;
  QCheckBox* m_preloadCheck = nullptr;
  QLineEdit* m_searchEdit = nullptr;
  QLabel* m_summaryLabel = nullptr;
  MemoryTreemapCanvas* m_canvas = nullptr;
  QTableWidget* m_table = nullptr;

  RankPidGetter m_pidGetter{};
  int m_rankCount = 1;
  qint64 m_lastPid = 0;
  int m_sortColumn = 2;
  Qt::SortOrder m_sortOrder = Qt::DescendingOrder;
  QVector<MemoryProfileEntry> m_entries;
};

}  // namespace gridlock::ui
