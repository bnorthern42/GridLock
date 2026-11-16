/**
 * @file MemoryProfilerWidget.cpp
 * @brief Implementation of the visual library, `/proc/<pid>/smaps`, and caller-heap memory profiler.
 *
 * Implements dual-engine memory attribution:
 * 1. Engine A parses `/proc/<pid>/smaps` (with `/proc/<pid>/maps` fallback) to classify shared
 *    libraries, executable segments, MPI `/dev/shm` mappings, thread stacks, and anonymous arenas.
 * 2. Engine B compiles a lightweight `LD_PRELOAD` interposer (`gridlock_memtrack`) that unwinds
 *    past `libc`/`libstdc++` `operator new` frames to attribute `>= 4 KB` heap allocations to the
 *    originating shared library or binary symbol.
 */

#include "MemoryProfilerWidget.hpp"
#include "../../core/managers/ConfigManager.hpp"
#include "../../core/managers/ThemeManager.hpp"
#include <QAbstractItemView>
#include <QCoreApplication>
#include <QDir>
#include <QDynamicPropertyChangeEvent>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QRegularExpression>
#include <QSplitter>
#include <QStandardPaths>
#include <QToolTip>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <cxxabi.h>
#include <unistd.h>

namespace gridlock::ui {

namespace {

/**
 * @brief QTableWidgetItem subclass sorting numerically by `Qt::UserRole` `double` values.
 *
 * Ensures megabyte (`"4.00 MB"`) and percentage (`"34.2%"`) cells sort by underlying
 * floating-point magnitude rather than lexicographical character order.
 */
class NumericMegabyteItem : public QTableWidgetItem {
 public:
  using QTableWidgetItem::QTableWidgetItem;
  bool operator<(const QTableWidgetItem& other) const override {
    const QVariant lv = data(Qt::UserRole), rv = other.data(Qt::UserRole);
    return (lv.isValid() && rv.isValid()) ? (lv.toDouble() < rv.toDouble()) : QTableWidgetItem::operator<(other);
  }
};

/**
 * @brief Returns a deterministic, theme-legible accent color for each memory category and index.
 * @param category Subsystem classification (`Heap / Call Site`, `Shared Lib`, `MPI /dev/shm`, `Stack`, `Anon mmap`).
 * @param index Sorted entry position used to stagger sibling brightness slightly.
 * @param isDark True when an active dark theme palette is selected.
 * @return Harmonized tile fill `QColor` with high white-label contrast.
 */
static QColor interpolatePalette(const QVector<QColor>& s, double t) {
  if (s.isEmpty()) return QColor("#3b82f6");
  const double c = std::clamp(t, 0.0, 1.0) * static_cast<double>(s.size() - 1);
  const int lo = static_cast<int>(std::floor(c)), hi = std::min(static_cast<int>(s.size()) - 1, lo + 1);
  const double f = c - static_cast<double>(lo);
  return QColor::fromRgbF(s[lo].redF() + (s[hi].redF() - s[lo].redF()) * f, s[lo].greenF() + (s[hi].greenF() - s[lo].greenF()) * f, s[lo].blueF() + (s[hi].blueF() - s[lo].blueF()) * f);
}

/** @brief Computes a distinct, scheme-aware fill color for a treemap/table entry. */
QColor colorForEntryImpl(int index, int totalCount, const MemoryProfileEntry& entry, const QString& scheme) {
  const double rankT = (totalCount > 1) ? static_cast<double>(index) / static_cast<double>(totalCount - 1) : 0.0;
  const uint h = qHash(entry.name + QLatin1Char('|') + entry.topSymbolOrRange);
  // Interpolate perceptually uniform scientific/thermal/ocean gradients by sorted size rank.
  if (scheme == QLatin1String("Viridis")) return interpolatePalette({QColor("#440154"), QColor("#3b528b"), QColor("#21918c"), QColor("#5ec962"), QColor("#fde725")}, rankT);
  if (scheme == QLatin1String("Plasma")) return interpolatePalette({QColor("#0d0887"), QColor("#9c179e"), QColor("#ed7953"), QColor("#f0f921")}, rankT);
  if (scheme == QLatin1String("Ocean")) return interpolatePalette({QColor("#0f2c59"), QColor("#0891b2"), QColor("#10b981"), QColor("#f59e0b")}, rankT);
  // HighContrast: Okabe-Ito colorblind-safe categorical palette keyed by module/symbol hash.
  if (scheme == QLatin1String("HighContrast")) {
    static const QVector<QColor> kOkabe = {QColor("#0072b2"), QColor("#e69f00"), QColor("#009e73"), QColor("#cc79a7"), QColor("#56b4e9"), QColor("#d55e00"), QColor("#f0e442"), QColor("#332288")};
    return kOkabe.at(static_cast<int>((h + static_cast<uint>(index)) % static_cast<uint>(kOkabe.size())));
  }
  // Distinct: multi-hue categorical palette differentiating individual libraries and Heap / Call Site blocks.
  static const QVector<QColor> kDistinct = {QColor("#0d9488"), QColor("#4f46e5"), QColor("#e11d48"), QColor("#d97706"), QColor("#0284c7"), QColor("#7c3aed"), QColor("#059669"), QColor("#db2777"), QColor("#2563eb"), QColor("#ca8a04")};
  const QColor base = kDistinct.at(static_cast<int>((h + static_cast<uint>(index)) % static_cast<uint>(kDistinct.size())));
  // Apply subtle sibling lightness variation so adjacent blocks remain distinguishable.
  const int shift = ((index % 3) - 1) * 7;
  return shift >= 0 ? base.lighter(100 + shift) : base.darker(100 - shift);
}

/**
 * @brief Embedded C source for the lightweight `LD_PRELOAD` >=4 KB caller-attribution interposer.
 *
 * Uses a static bootstrap arena for `dlsym` startup safety, a thread-local reentrancy guard,
 * `_Unwind_Backtrace` + `dladdr` to skip `libc`/`libstdc++` wrapper frames, and atomic `.tmp`
 * file renames to publish `/tmp/gridlock_mem_<pid>.tsv` with `< 1%` runtime overhead.
 */
const char* preloadTrackerCSource() {
  return R"C_SRC(
// Lightweight >=4 KB malloc/calloc/realloc/free caller-attribution preload library.
// Intercepts page-sized and larger allocations, unwinds past libc/libstdc++ frames via
// _Unwind_Backtrace + dladdr, and atomically publishes /tmp/gridlock_mem_<pid>.tsv.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <unwind.h>
// Ignore sub-page allocations (<4 KB) to keep multi-rank MPI solver overhead under 1%.
#define MIN_TRACK_BYTES 4096ULL
#define PTR_SLOTS 16384U
#define BUCKET_SLOTS 512U
#define TOMBSTONE ((uintptr_t)1U)
// Open-addressed live pointer slot, aggregated caller bucket, and stack unwind buffer.
typedef struct { uintptr_t ptr; uint64_t size; uint32_t bucket; } PtrRec;
typedef struct { char module[192]; char symbol[128]; uint64_t live_bytes, peak_bytes, alloc_count; int used; } Bucket;
typedef struct { uintptr_t pcs[8]; int count; } TraceState;
// Next-in-line libc allocator function pointers and static bootstrap bump arena.
static void* (*real_malloc)(size_t) = NULL;
static void* (*real_calloc)(size_t, size_t) = NULL;
static void* (*real_realloc)(void*, size_t) = NULL;
static void  (*real_free)(void*) = NULL;
static unsigned char g_boot_buf[65536];
static size_t g_boot_off = 0;
static volatile int g_init_state = 0;
// Thread-local reentrancy flag preventing dladdr/fopen internal allocations from recursing.
static __thread int g_in_hook = 0;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static PtrRec g_ptrs[PTR_SLOTS];
static uint32_t g_ptr_used = 0;
static volatile uint32_t g_live_tracked = 0;
static Bucket g_buckets[BUCKET_SLOTS];
static uint64_t g_unsaved_delta = 0, g_last_flush_ms = 0;
static int g_ever_flushed = 0;
// Serve early dlsym() allocations from static BSS so dynamic linker startup never recurses.
static int is_boot_ptr(const void* p) { const unsigned char* b = (const unsigned char*)p; return b >= g_boot_buf && b < (g_boot_buf + sizeof(g_boot_buf)); }
static void* boot_alloc(size_t sz) {
  size_t aligned = (sz + 15U) & ~((size_t)15U);
  if (g_boot_off + aligned + 16U > sizeof(g_boot_buf)) return NULL;
  ((size_t*)(g_boot_buf + g_boot_off))[0] = sz;
  void* out = (void*)(g_boot_buf + g_boot_off + 16U);
  g_boot_off += aligned + 16U; memset(out, 0, sz); return out;
}
// Lazily bind next-in-line libc allocator symbols via RTLD_NEXT once per process.
static void resolve_real_funcs(void) {
  if (__atomic_load_n(&g_init_state, __ATOMIC_ACQUIRE) == 2) return;
  int exp = 0;
  if (__atomic_compare_exchange_n(&g_init_state, &exp, 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
    *(void**)(&real_malloc) = dlsym(RTLD_NEXT, "malloc"); *(void**)(&real_calloc) = dlsym(RTLD_NEXT, "calloc");
    *(void**)(&real_realloc) = dlsym(RTLD_NEXT, "realloc"); *(void**)(&real_free) = dlsym(RTLD_NEXT, "free");
    __atomic_store_n(&g_init_state, 2, __ATOMIC_RELEASE);
  }
}
// Walk up to 8 stack frames via libgcc _Unwind_Backtrace safely under -fomit-frame-pointer.
static _Unwind_Reason_Code unwind_cb(struct _Unwind_Context* ctx, void* arg) {
  TraceState* st = (TraceState*)arg; uintptr_t ip = _Unwind_GetIP(ctx);
  if (ip && st->count < 8) { st->pcs[st->count++] = ip; if (st->count >= 8) return _URC_END_OF_STACK; }
  return _URC_NO_REASON;
}
// Skip allocator wrapper shared objects so C++ operator new attributes to the calling library.
static int is_wrapper_lib(const char* f) {
  return !f || !f[0] || strstr(f, "gridlock_memtrack") || strstr(f, "libstdc++.so") || strstr(f, "libc.so") || strstr(f, "libgcc_s.so") || strstr(f, "ld-linux");
}
// Resolve owning .so / binary path and nearest symbol or +0xoffset for the active call site.
static void resolve_caller(char* mod_out, size_t mod_sz, char* sym_out, size_t sym_sz) {
  TraceState st; st.count = 0; _Unwind_Backtrace(unwind_cb, &st);
  const char *best_mod = "[heap]", *best_sym = NULL; uintptr_t best_off = 0U;
  for (int i = 1; i < st.count; ++i) {
    Dl_info info; uintptr_t call_pc = st.pcs[i] - 1U;
    if (dladdr((void*)call_pc, &info) && info.dli_fname && info.dli_fname[0]) {
      best_mod = info.dli_fname; best_sym = (info.dli_sname && info.dli_sname[0]) ? info.dli_sname : NULL;
      best_off = call_pc >= (uintptr_t)info.dli_fbase ? (call_pc - (uintptr_t)info.dli_fbase) : call_pc;
      if (!is_wrapper_lib(info.dli_fname)) break;
    }
  }
  snprintf(mod_out, mod_sz, "%s", best_mod);
  if (best_sym) snprintf(sym_out, sym_sz, "%s", best_sym);
  else snprintf(sym_out, sym_sz, "+0x%llx", (unsigned long long)best_off);
}
// Atomically write /tmp/gridlock_mem_<pid>.tsv whenever >=64 KB changes or >=35 ms elapses.
static void maybe_flush_locked(int force) {
  struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
  uint64_t now = (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000L);
  if (!force && g_ever_flushed && g_unsaved_delta < 65536ULL && (now - g_last_flush_ms) < 35ULL) return;
  char tmp_p[128], out_p[128]; int pid = (int)getpid();
  snprintf(tmp_p, sizeof(tmp_p), "/tmp/gridlock_mem_%d.tsv.tmp", pid); snprintf(out_p, sizeof(out_p), "/tmp/gridlock_mem_%d.tsv", pid);
  FILE* fp = fopen(tmp_p, "w"); if (!fp) return;
  for (uint32_t i = 0; i < BUCKET_SLOTS; ++i)
    if (g_buckets[i].used && (g_buckets[i].live_bytes > 0 || g_buckets[i].peak_bytes >= 65536ULL))
      fprintf(fp, "%s\t%s\t%llu\t%llu\t%llu\n", g_buckets[i].module, g_buckets[i].symbol,
              (unsigned long long)g_buckets[i].live_bytes, (unsigned long long)g_buckets[i].peak_bytes, (unsigned long long)g_buckets[i].alloc_count);
  fclose(fp); rename(tmp_p, out_p); g_unsaved_delta = 0; g_last_flush_ms = now; g_ever_flushed = 1;
}
// Find or allocate an FNV-1a hash bucket keyed by (module_path, symbol_name).
static uint32_t find_or_create_bucket(const char* mod, const char* sym) {
  uint32_t h = 2166136261U;
  for (const char* p = mod; *p; ++p) h = (h ^ (unsigned char)(*p)) * 16777619U;
  for (const char* p = sym; *p; ++p) h = (h ^ (unsigned char)(*p)) * 16777619U;
  for (uint32_t s = 0; s < BUCKET_SLOTS; ++s) {
    uint32_t idx = (h + s) & (BUCKET_SLOTS - 1U);
    if (!g_buckets[idx].used) { g_buckets[idx].used = 1; snprintf(g_buckets[idx].module, sizeof(g_buckets[idx].module), "%s", mod); snprintf(g_buckets[idx].symbol, sizeof(g_buckets[idx].symbol), "%s", sym); return idx; }
    if (strcmp(g_buckets[idx].module, mod) == 0 && strcmp(g_buckets[idx].symbol, sym) == 0) return idx;
  }
  return 0U;
}
static uint32_t ptr_hash(uintptr_t p) { return (uint32_t)(((p >> 4) * 11400714819323198485ULL) >> 50); }
// Record a newly allocated >=4 KB heap block and update peak/live counters for its caller.
static void record_alloc(void* p, size_t sz) {
  if (!p || sz < MIN_TRACK_BYTES) return;
  g_in_hook = 1; char mod[192], sym[128]; resolve_caller(mod, sizeof(mod), sym, sizeof(sym));
  pthread_mutex_lock(&g_lock);
  if (g_ptr_used < (PTR_SLOTS * 3U) / 4U) {
    uint32_t b = find_or_create_bucket(mod, sym), h = ptr_hash((uintptr_t)p); int64_t tomb = -1;
    for (uint32_t s = 0; s < PTR_SLOTS; ++s) {
      uint32_t idx = (h + s) & (PTR_SLOTS - 1U);
      if (g_ptrs[idx].ptr == 0U) {
        uint32_t dst = (tomb >= 0) ? (uint32_t)tomb : idx; if (tomb < 0) ++g_ptr_used;
        g_ptrs[dst].ptr = (uintptr_t)p; g_ptrs[dst].size = sz; g_ptrs[dst].bucket = b;
        g_buckets[b].live_bytes += sz; g_buckets[b].alloc_count += 1U;
        if (g_buckets[b].live_bytes > g_buckets[b].peak_bytes) g_buckets[b].peak_bytes = g_buckets[b].live_bytes;
        __atomic_add_fetch(&g_live_tracked, 1U, __ATOMIC_RELAXED); g_unsaved_delta += sz; maybe_flush_locked(0); break;
      }
      if (g_ptrs[idx].ptr == TOMBSTONE && tomb < 0) tomb = (int64_t)idx;
    }
  }
  pthread_mutex_unlock(&g_lock); g_in_hook = 0;
}
// Decrement live bytes when a previously tracked >=4 KB pointer is released via free/realloc.
static void record_free(void* p) {
  if (!p || __atomic_load_n(&g_live_tracked, __ATOMIC_RELAXED) == 0U) return;
  g_in_hook = 1; pthread_mutex_lock(&g_lock); uint32_t h = ptr_hash((uintptr_t)p);
  for (uint32_t s = 0; s < PTR_SLOTS; ++s) {
    uint32_t idx = (h + s) & (PTR_SLOTS - 1U);
    if (g_ptrs[idx].ptr == 0U) break;
    if (g_ptrs[idx].ptr == (uintptr_t)p) {
      uint32_t b = g_ptrs[idx].bucket; uint64_t sz = g_ptrs[idx].size;
      g_buckets[b].live_bytes = (g_buckets[b].live_bytes >= sz) ? (g_buckets[b].live_bytes - sz) : 0U;
      g_ptrs[idx].ptr = TOMBSTONE; g_ptrs[idx].size = 0U;
      __atomic_sub_fetch(&g_live_tracked, 1U, __ATOMIC_RELAXED); g_unsaved_delta += sz; maybe_flush_locked(0); break;
    }
  }
  pthread_mutex_unlock(&g_lock); g_in_hook = 0;
}
// Exported malloc/calloc/free/realloc interposers with bootstrap and reentrancy guards.
void* malloc(size_t sz) {
  if (__atomic_load_n(&g_init_state, __ATOMIC_ACQUIRE) != 2) { if (__atomic_load_n(&g_init_state, __ATOMIC_ACQUIRE) == 1) return boot_alloc(sz); resolve_real_funcs(); }
  void* p = real_malloc(sz); if (!g_in_hook && sz >= MIN_TRACK_BYTES) record_alloc(p, sz); return p;
}
void* calloc(size_t n, size_t sz) {
  if (__atomic_load_n(&g_init_state, __ATOMIC_ACQUIRE) != 2) { if (__atomic_load_n(&g_init_state, __ATOMIC_ACQUIRE) == 1) return boot_alloc(n * sz); resolve_real_funcs(); }
  void* p = real_calloc(n, sz); if (!g_in_hook && (uint64_t)n * (uint64_t)sz >= MIN_TRACK_BYTES) record_alloc(p, n * sz); return p;
}
void free(void* p) {
  if (!p || is_boot_ptr(p)) return;
  if (__atomic_load_n(&g_init_state, __ATOMIC_ACQUIRE) != 2) resolve_real_funcs();
  if (!g_in_hook) record_free(p); real_free(p);
}
void* realloc(void* old_p, size_t new_sz) {
  if (is_boot_ptr(old_p)) {
    resolve_real_funcs(); void* np = real_malloc(new_sz);
    if (np && old_p) { size_t osz = ((const size_t*)((const unsigned char*)old_p - 16U))[0]; memcpy(np, old_p, osz < new_sz ? osz : new_sz); }
    return np;
  }
  if (__atomic_load_n(&g_init_state, __ATOMIC_ACQUIRE) != 2) resolve_real_funcs();
  if (!g_in_hook && old_p) record_free(old_p);
  void* np = real_realloc(old_p, new_sz); if (!g_in_hook && np && new_sz >= MIN_TRACK_BYTES) record_alloc(np, new_sz); return np;
}
// Flush any pending unsaved allocation deltas on graceful process teardown.
__attribute__((destructor)) static void gridlock_memtrack_fini(void) {
  if (g_ever_flushed) { g_in_hook = 1; pthread_mutex_lock(&g_lock); maybe_flush_locked(1); pthread_mutex_unlock(&g_lock); g_in_hook = 0; }
}
)C_SRC";
}

}  // namespace

// ============================================================================
// MemoryTreemapCanvas — Stacked Proportional Bar + 2D Balanced Treemap
// ============================================================================

/**
 * @brief Constructs the interactive 2D memory treemap and proportional stacked bar canvas.
 * @param parent Owning splitter or container widget.
 */
MemoryTreemapCanvas::MemoryTreemapCanvas(QWidget* parent) : QWidget(parent) {
  // Enable hover tracking for instant module tooltips without requiring mouse button presses.
  setObjectName(QStringLiteral("memoryTreemapCanvas"));
  setMouseTracking(true);
  setMinimumHeight(175);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

/**
 * @brief Replaces displayed memory profile entries and triggers a layout + repaint cycle.
 */
void MemoryTreemapCanvas::setEntries(const QVector<MemoryProfileEntry>& entries, uint64_t totalBytes) {
  m_entries = entries; m_totalBytes = totalBytes; m_hoveredEntry = -1;
  if (m_selectedEntry >= m_entries.size()) m_selectedEntry = -1;
  rebuildLayout(); update();
}

/**
 * @brief Clears all rendered stacked bar and treemap tiles back to the idle state.
 */
void MemoryTreemapCanvas::clear() {
  m_entries.clear(); m_barTiles.clear(); m_treemapTiles.clear();
  m_totalBytes = 0; m_hoveredEntry = -1; m_selectedEntry = -1; update();
}

/**
 * @brief Updates the active selection border index and schedules a repaint.
 */
void MemoryTreemapCanvas::setSelectedEntryIndex(int entryIndex) {
  if (m_selectedEntry != entryIndex) { m_selectedEntry = entryIndex; update(); }
}

/** @brief Recomputes top proportional stacked-bar slices and 2D balanced treemap rectangles. */
void MemoryTreemapCanvas::rebuildLayout() {
  m_barTiles.clear();
  m_treemapTiles.clear();
  if (m_entries.isEmpty() || m_totalBytes == 0 || width() < 24 || height() < 48) return;

  // 1. Lay out full-width proportional stacked bar across the top 22px ribbon.
  const QRectF barBounds(6.0, 6.0, std::max(16.0, width() - 12.0), 22.0);
  double curX = barBounds.left();
  for (int i = 0; i < m_entries.size(); ++i) {
    const double frac = static_cast<double>(m_entries[i].liveBytes) / static_cast<double>(m_totalBytes);
    const double sliceW = (i + 1 == m_entries.size()) ? std::max(0.0, barBounds.right() - curX)
                                                      : std::max(1.0, barBounds.width() * frac);
    if (curX + sliceW > barBounds.right()) {
      m_barTiles.push_back({QRectF(curX, barBounds.top(), std::max(0.0, barBounds.right() - curX), barBounds.height()), i});
      break;
    }
    m_barTiles.push_back({QRectF(curX, barBounds.top(), sliceW, barBounds.height()), i});
    curX += sliceW;
  }

  // 2. Recursively partition the lower canvas into balanced low-aspect-ratio 2D treemap cards.
  const QRectF treeBounds(6.0, 34.0, std::max(16.0, width() - 12.0), std::max(24.0, height() - 40.0));
  subdivideTreemap(0, static_cast<int>(m_entries.size()), treeBounds);
}

/** @brief Balanced binary-split treemap layout producing readable low-aspect-ratio cards. */
void MemoryTreemapCanvas::subdivideTreemap(int beginIdx, int endIdx, const QRectF& bounds) {
  if (beginIdx >= endIdx || bounds.width() <= 1.0 || bounds.height() <= 1.0) return;
  if (endIdx - beginIdx == 1) {
    m_treemapTiles.push_back({bounds.adjusted(1.5, 1.5, -1.5, -1.5), beginIdx});
    return;
  }
  // Sum total resident/heap bytes across [beginIdx, endIdx) to find the 50% split pivot.
  uint64_t sum = 0, halfSum = 0;
  for (int i = beginIdx; i < endIdx; ++i) sum += m_entries[i].liveBytes;
  if (sum == 0) { m_treemapTiles.push_back({bounds.adjusted(1.5, 1.5, -1.5, -1.5), beginIdx}); return; }

  int splitIdx = beginIdx + 1;
  for (int i = beginIdx; i < endIdx - 1; ++i) {
    halfSum += m_entries[i].liveBytes;
    splitIdx = i + 1;
    if (halfSum * 2ULL >= sum) break;
  }
  // Split horizontally when wider than tall, or vertically when taller than wide.
  const double ratio = std::clamp(static_cast<double>(halfSum) / static_cast<double>(sum), 0.08, 0.92);
  if (bounds.width() >= bounds.height()) {
    const double leftW = bounds.width() * ratio;
    subdivideTreemap(beginIdx, splitIdx, QRectF(bounds.left(), bounds.top(), leftW, bounds.height()));
    subdivideTreemap(splitIdx, endIdx, QRectF(bounds.left() + leftW, bounds.top(), bounds.width() - leftW, bounds.height()));
  } else {
    const double topH = bounds.height() * ratio;
    subdivideTreemap(beginIdx, splitIdx, QRectF(bounds.left(), bounds.top(), bounds.width(), topH));
    subdivideTreemap(splitIdx, endIdx, QRectF(bounds.left(), bounds.top() + topH, bounds.width(), bounds.height() - topH));
  }
}

/**
 * @brief Hit-tests `pos` against 2D treemap cards and top stacked-bar slices.
 * @param pos Widget-local cursor position.
 * @return Matching `m_entries` index, or `-1` when outside any tile.
 */
int MemoryTreemapCanvas::tileAtPosition(const QPointF& pos) const {
  for (const auto& t : m_treemapTiles) if (t.rect.contains(pos)) return t.entryIndex;
  for (const auto& t : m_barTiles) if (t.rect.contains(pos)) return t.entryIndex;
  return -1;
}

/**
 * @brief Renders the proportional stacked bar and 2D squarified/balanced treemap cards.
 * @param event Qt paint event region.
 */
void MemoryTreemapCanvas::paintEvent(QPaintEvent* /*event*/) {
  // Initialize antialiased painter and fill canvas background from active ThemePalette.
  QPainter p(this); p.setRenderHint(QPainter::Antialiasing, true);
  auto& tm = gridlock::core::ThemeManager::instance();
  const auto pal = tm.paletteForTheme(tm.activeThemeId());
  p.fillRect(rect(), QColor(pal.canvasHex));

  // Render clean empty-state guidance banner when no live rank has been sampled yet.
  if (m_entries.isEmpty() || m_totalBytes == 0) {
    QColor mutedText(pal.textHex); mutedText.setAlpha(165);
    p.setPen(mutedText);
    QFont emptyFont = font(); emptyFont.setPixelSize(12); p.setFont(emptyFont);
    p.drawText(rect(), Qt::AlignCenter,
               QStringLiteral("No live rank memory sampled yet — pause a rank or click 'Refresh Now' to inspect library & heap usage."));
    return;
  }

  // Paint top proportional stacked ribbon across active modules and arenas.
  for (const auto& tile : m_barTiles) {
    if (tile.entryIndex < 0 || tile.entryIndex >= m_entries.size()) continue;
    const auto& e = m_entries[tile.entryIndex];
    p.fillRect(tile.rect, (tile.entryIndex == m_hoveredEntry) ? e.color.lighter(115) : e.color);
    p.setPen(QPen(QColor(pal.canvasHex), 1.0)); p.drawRect(tile.rect);
  }

  // Paint 2D treemap cards with bold module name and MB / % labels inside each block.
  QFont titleFont = font(); titleFont.setPixelSize(11); titleFont.setBold(true);
  QFont subFont = font(); subFont.setPixelSize(10);
  const QFontMetrics fmTitle(titleFont), fmSub(subFont);

  for (const auto& tile : m_treemapTiles) {
    if (tile.entryIndex < 0 || tile.entryIndex >= m_entries.size()) continue;
    const auto& e = m_entries[tile.entryIndex];
    const bool hovered = (tile.entryIndex == m_hoveredEntry), selected = (tile.entryIndex == m_selectedEntry);

    // Highlight hovered tile fill and draw crisp white selection ring on active card.
    p.setBrush(hovered ? e.color.lighter(114) : e.color);
    p.setPen(QPen(selected ? QColor("#ffffff") : QColor(pal.borderHex), selected ? 2.0 : 1.0));
    p.drawRoundedRect(tile.rect, 3.0, 3.0);

    // Draw elided library/call-site title and size metrics whenever card dimensions permit.
    if (tile.rect.width() >= 48.0 && tile.rect.height() >= 22.0) {
      const QRectF textBox = tile.rect.adjusted(5.0, 3.0, -5.0, -3.0);
      const double lum = 0.2126 * e.color.redF() + 0.7152 * e.color.greenF() + 0.0722 * e.color.blueF();
      const QColor fg = (lum > 0.56) ? QColor("#111827") : QColor("#ffffff");
      const QColor subFg = (lum > 0.56) ? QColor(17, 24, 39, 220) : QColor(255, 255, 255, 225);
      p.setPen(fg); p.setFont(titleFont);
      p.drawText(QRectF(textBox.left(), textBox.top(), textBox.width(), fmTitle.height() + 2),
                 Qt::AlignLeft | Qt::AlignVCenter, fmTitle.elidedText(e.name, Qt::ElideMiddle, static_cast<int>(textBox.width())));
      if (textBox.height() >= 32.0) {
        const double mb = static_cast<double>(e.liveBytes) / (1024.0 * 1024.0), pct = (100.0 * static_cast<double>(e.liveBytes)) / static_cast<double>(m_totalBytes);
        p.setFont(subFont); p.setPen(subFg);
        p.drawText(QRectF(textBox.left(), textBox.top() + fmTitle.height() + 2, textBox.width(), fmSub.height() + 2),
                   Qt::AlignLeft | Qt::AlignVCenter, fmSub.elidedText(QStringLiteral("%1 MB (%2%)").arg(mb, 0, 'f', 2).arg(pct, 0, 'f', 1), Qt::ElideRight, static_cast<int>(textBox.width())));
      }
    }
  }
}

/**
 * @brief Rebuilds stacked bar and treemap tile coordinates whenever the widget resizes.
 */
void MemoryTreemapCanvas::resizeEvent(QResizeEvent* event) { QWidget::resizeEvent(event); rebuildLayout(); }

/**
 * @brief Tracks cursor hover across treemap tiles and displays detailed per-module tooltips.
 */
void MemoryTreemapCanvas::mouseMoveEvent(QMouseEvent* event) {
  const int hit = tileAtPosition(event->position());
  if (hit != m_hoveredEntry) { m_hoveredEntry = hit; update(); }
  if (hit >= 0 && hit < m_entries.size() && m_totalBytes > 0) {
    // Format rich multi-line tooltip summarizing category, RSS, private dirty, and top call site.
    const auto& e = m_entries[hit];
    const double mb = static_cast<double>(e.liveBytes) / (1024.0 * 1024.0);
    const double dirtyMb = static_cast<double>(e.dirtyBytes) / (1024.0 * 1024.0);
    const double pct = (100.0 * static_cast<double>(e.liveBytes)) / static_cast<double>(m_totalBytes);
    const QString tip = QStringLiteral("%1\nCategory: %2\nLive / RSS: %3 MB (%4%)\nPrivate Dirty: %5 MB\nDetail: %6")
                            .arg(e.name, e.category).arg(mb, 0, 'f', 2).arg(pct, 0, 'f', 1).arg(dirtyMb, 0, 'f', 2).arg(e.topSymbolOrRange);
    QToolTip::showText(event->globalPosition().toPoint(), tip, this);
  } else {
    QToolTip::hideText();
  }
  QWidget::mouseMoveEvent(event);
}

/**
 * @brief Clears active hover highlight when the pointer exits the canvas viewport.
 */
void MemoryTreemapCanvas::leaveEvent(QEvent* event) {
  if (m_hoveredEntry != -1) { m_hoveredEntry = -1; update(); }
  QWidget::leaveEvent(event);
}

/**
 * @brief Selects the clicked tile and synchronizes `#memoryBreakdownTable` row selection.
 */
void MemoryTreemapCanvas::mousePressEvent(QMouseEvent* event) {
  const int hit = tileAtPosition(event->position());
  setSelectedEntryIndex(hit);
  if (hit >= 0 && hit < m_entries.size()) emit entryClicked(m_entries[hit].name, m_entries[hit].startAddress);
  QWidget::mousePressEvent(event);
}

/**
 * @brief Emits `addressActivated` on tile double-click so `MemView` jumps `HEX DUMP` to the VMA start.
 */
void MemoryTreemapCanvas::mouseDoubleClickEvent(QMouseEvent* event) {
  const int hit = tileAtPosition(event->position());
  if (hit >= 0 && hit < m_entries.size() && m_entries[hit].startAddress != 0ULL) emit addressActivated(m_entries[hit].startAddress);
  QWidget::mouseDoubleClickEvent(event);
}

// ============================================================================
// MemoryProfilerWidget — Controls, /proc/smaps + LD_PRELOAD Collector & Table
// ============================================================================

/**
 * @brief Constructs the interactive Library & Heap Memory Profiler widget.
 * @param parent Owning `MemView` stack widget.
 */
MemoryProfilerWidget::MemoryProfilerWidget(QWidget* parent) : QWidget(parent) {
  // Configure zero-margin vertical layout hosting the control bar above the treemap/table splitter.
  setObjectName(QStringLiteral("memoryProfilerWidget"));
  auto* rootLayout = new QVBoxLayout(this);
  rootLayout->setContentsMargins(0, 0, 0, 0); rootLayout->setSpacing(0);

  // Build compact top control bar (<= 28px height for workstation ergonomics compliance).
  auto* ctrlBar = new QWidget(this);
  ctrlBar->setObjectName(QStringLiteral("memProfileControlBar")); ctrlBar->setFixedHeight(28);
  auto* ctrlLayout = new QHBoxLayout(ctrlBar);
  ctrlLayout->setContentsMargins(6, 2, 6, 2); ctrlLayout->setSpacing(6);

  // Rank selector switching between individual MPI ranks (`0..N-1`) and `All Ranks Overview`.
  m_rankCombo = new QComboBox(ctrlBar);
  m_rankCombo->setObjectName(QStringLiteral("memProfileRankCombo"));
  m_rankCombo->setFixedHeight(22); m_rankCombo->setMinimumWidth(118);
  m_rankCombo->addItem(QStringLiteral("Rank 0"), 0);
  m_rankCombo->addItem(QStringLiteral("All Ranks Overview"), -1);
  ctrlLayout->addWidget(m_rankCombo);

  // Manual refresh button re-sampling `/proc/<pid>/smaps` and `/tmp/gridlock_mem_<pid>.tsv`.
  m_refreshBtn = new QPushButton(QStringLiteral("Refresh Now"), ctrlBar);
  m_refreshBtn->setObjectName(QStringLiteral("memProfileRefreshBtn")); m_refreshBtn->setFixedHeight(22);
  ctrlLayout->addWidget(m_refreshBtn);
  // Toggle controlling whether newly launched MPI ranks inherit `LD_PRELOAD` heap caller tracking.
  m_preloadCheck = new QCheckBox(QStringLiteral("Track Heap Callers (LD_PRELOAD)"), ctrlBar);
  m_preloadCheck->setObjectName(QStringLiteral("memTrackPreloadCheck")); m_preloadCheck->setChecked(true);
  m_preloadCheck->setToolTip(QStringLiteral("Preload lightweight >=4 KB malloc/new caller tracker into newly launched MPI ranks"));
  ctrlLayout->addWidget(m_preloadCheck);
  // Palette selector synchronized with ConfigManager & Preferences -> Appearance.
  m_schemeCombo = new QComboBox(ctrlBar); m_schemeCombo->setObjectName(QStringLiteral("memProfileColorSchemeCombo")); m_schemeCombo->setFixedHeight(22);
  for (const auto& opt : {QPair<QString, QString>{QStringLiteral("Distinct"), QStringLiteral("Distinct")}, {QStringLiteral("Viridis"), QStringLiteral("Viridis")}, {QStringLiteral("Plasma"), QStringLiteral("Plasma")}, {QStringLiteral("Ocean"), QStringLiteral("Ocean")}, {QStringLiteral("High-Contrast"), QStringLiteral("HighContrast")}}) m_schemeCombo->addItem(opt.first, opt.second);
  if (const int si = m_schemeCombo->findData(gridlock::core::ConfigManager::instance().memoryMapColorScheme()); si >= 0) m_schemeCombo->setCurrentIndex(si);
  connect(m_schemeCombo, &QComboBox::currentIndexChanged, this, [this](int) { gridlock::core::ConfigManager::instance().setMemoryMapColorScheme(m_schemeCombo->currentData().toString()); reapplyColors(); });
  ctrlLayout->addWidget(m_schemeCombo);
  if (auto* app = QCoreApplication::instance()) app->installEventFilter(this);
  // Live case-insensitive substring filter across library names, categories, and symbols.
  m_searchEdit = new QLineEdit(ctrlBar); m_searchEdit->setObjectName(QStringLiteral("memProfileSearchEdit")); m_searchEdit->setPlaceholderText(QStringLiteral("Filter libraries, symbols, or categories...")); m_searchEdit->setClearButtonEnabled(true); m_searchEdit->setFixedHeight(22); m_searchEdit->setMinimumWidth(180); ctrlLayout->addWidget(m_searchEdit, 1);
  // Summary badge displaying sampled rank/PID context, total resident MB, and region count.
  m_summaryLabel = new QLabel(QStringLiteral("TOTAL: 0.00 MB"), ctrlBar); m_summaryLabel->setObjectName(QStringLiteral("memProfileSummaryLabel")); ctrlLayout->addWidget(m_summaryLabel); rootLayout->addWidget(ctrlBar);
  // Pair interactive 2D treemap canvas above sortable, interactive 6-column breakdown table.
  auto* splitter = new QSplitter(Qt::Vertical, this); splitter->setChildrenCollapsible(false); splitter->setOpaqueResize(true);
  m_canvas = new MemoryTreemapCanvas(splitter); splitter->addWidget(m_canvas);
  // Configure sortable breakdown table with interactive resizable column headers.
  m_table = new QTableWidget(0, 6, splitter); m_table->setObjectName(QStringLiteral("memoryBreakdownTable"));
  m_table->setHorizontalHeaderLabels({QStringLiteral("Library / Region"), QStringLiteral("Category"), QStringLiteral("Live / RSS (MB)"), QStringLiteral("Private Dirty (MB)"), QStringLiteral("% of Total"), QStringLiteral("Top Symbol / Address Range")});
  m_table->verticalHeader()->setVisible(false); m_table->setSelectionBehavior(QAbstractItemView::SelectRows); m_table->setSelectionMode(QAbstractItemView::SingleSelection); m_table->setEditTriggers(QAbstractItemView::NoEditTriggers); m_table->setSortingEnabled(true);
  if (QHeaderView* hdr = m_table->horizontalHeader()) {
    hdr->setSectionsMovable(false); hdr->setStretchLastSection(true); hdr->setMinimumSectionSize(56);
    const QList<int> widths = {210, 125, 115, 125, 88, 260};
    for (int c = 0; c < widths.size(); ++c) { hdr->setSectionResizeMode(c, QHeaderView::Interactive); m_table->setColumnWidth(c, widths[c]); }
    hdr->setSortIndicatorShown(true); hdr->setSortIndicator(m_sortColumn, m_sortOrder);
    connect(hdr, &QHeaderView::sectionClicked, this, [this, hdr](int) { m_sortColumn = hdr->sortIndicatorSection(); m_sortOrder = hdr->sortIndicatorOrder(); });
  }
  splitter->addWidget(m_table); splitter->setStretchFactor(0, 3); splitter->setStretchFactor(1, 2); rootLayout->addWidget(splitter, 1);
  // Compile and register LD_PRELOAD tracker library immediately while enabled by default.
  syncPreloadTrackerEnvironment(m_preloadCheck->isChecked());
  // Wire toolbar controls to live refresh, preload toggle, and search filter handlers.
  connect(m_refreshBtn, &QPushButton::clicked, this, &MemoryProfilerWidget::onRefreshClicked);
  connect(m_rankCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { onRefreshClicked(); });
  connect(m_preloadCheck, &QCheckBox::toggled, this, [](bool checked) { syncPreloadTrackerEnvironment(checked); });
  connect(m_searchEdit, &QLineEdit::textChanged, this, &MemoryProfilerWidget::applySearchFilter);
  // Synchronize treemap single-click selection and double-click hex inspection with the table.
  connect(m_canvas, &MemoryTreemapCanvas::entryClicked, this, [this](const QString& name, uint64_t) {
    if (!m_table) return;
    for (int r = 0; r < m_table->rowCount(); ++r) {
      if (m_table->item(r, 0) && m_table->item(r, 0)->text() == name && !m_table->isRowHidden(r)) {
        m_table->selectRow(r); m_table->scrollToItem(m_table->item(r, 0)); break;
      }
    }
  });
  connect(m_canvas, &MemoryTreemapCanvas::addressActivated, this, &MemoryProfilerWidget::inspectAddressRequested);
  connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
    if (auto* item = m_table->item(row, 0)) {
      if (const uint64_t addr = item->data(Qt::UserRole).toULongLong(); addr != 0ULL) emit inspectAddressRequested(addr);
    }
  });
  reloadStyle();
}

/**
 * @brief Compiles `/tmp/gridlock_memtrack_v2_<uid>.so` via `gcc`/`cc` and sets `GRIDLOCK_MEMTRACK_SO`.
 * @param enabled True to compile/export the preload interposer; false to clear `GRIDLOCK_MEMTRACK_SO`.
 * @return Path to the compiled shared library when active, or an empty string otherwise.
 */
QString MemoryProfilerWidget::syncPreloadTrackerEnvironment(bool enabled) {
  if (!enabled) { qunsetenv("GRIDLOCK_MEMTRACK_SO"); return QString(); }
  const uint uid = static_cast<uint>(::getuid());
  const QString cPath = QStringLiteral("/tmp/gridlock_memtrack_v2_%1.c").arg(uid);
  const QString soPath = QStringLiteral("/tmp/gridlock_memtrack_v2_%1.so").arg(uid);

  // Compile shared preload library once per user session when not already cached in /tmp.
  if (!QFileInfo::exists(soPath)) {
    QFile cf(cPath);
    if (cf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      cf.write(preloadTrackerCSource()); cf.close();
      QString cc = QStandardPaths::findExecutable(QStringLiteral("gcc"));
      if (cc.isEmpty()) cc = QStandardPaths::findExecutable(QStringLiteral("cc"));
      if (!cc.isEmpty()) {
        // Build position-independent shared interposer linked against libdl and libpthread.
        QProcess proc;
        proc.start(cc, {QStringLiteral("-shared"), QStringLiteral("-fPIC"), QStringLiteral("-O2"),
                        cPath, QStringLiteral("-o"), soPath, QStringLiteral("-ldl"), QStringLiteral("-lpthread")});
        proc.waitForFinished(5000);
      }
    }
  }
  if (QFileInfo::exists(soPath)) { qputenv("GRIDLOCK_MEMTRACK_SO", soPath.toUtf8()); return soPath; }
  qunsetenv("GRIDLOCK_MEMTRACK_SO"); return QString();
}

/** @brief Updates the rank dropdown list and binds the live rank-to-PID query callback. */
void MemoryProfilerWidget::setAvailableRanks(int rankCount, int focusedRank, RankPidGetter pidGetter) {
  // Rebuild Rank 0..N-1 + All Ranks Overview items without firing redundant refresh signals.
  m_pidGetter = std::move(pidGetter); m_rankCount = std::max(1, rankCount);
  if (!m_rankCombo) return;
  const bool blocked = m_rankCombo->blockSignals(true);
  const int prevData = m_rankCombo->currentData().toInt();
  m_rankCombo->clear();
  for (int r = 0; r < m_rankCount; ++r) {
    const qint64 pid = m_pidGetter ? m_pidGetter(r) : 0;
    m_rankCombo->addItem(pid > 0 ? QStringLiteral("Rank %1 (PID %2)").arg(r).arg(pid) : QStringLiteral("Rank %1").arg(r), r);
  }
  if (m_rankCount > 1) m_rankCombo->addItem(QStringLiteral("All Ranks Overview"), -1);
  const int targetVal = (prevData == -1 && m_rankCount > 1) ? -1 : std::clamp(focusedRank, 0, m_rankCount - 1);
  if (const int idx = m_rankCombo->findData(targetVal); idx >= 0) m_rankCombo->setCurrentIndex(idx);
  m_rankCombo->blockSignals(blocked);
}

/** @brief Returns the active 2D treemap tile count rendered in `#memoryTreemapCanvas`. */
int MemoryProfilerWidget::treemapRectCount() const { return m_canvas ? m_canvas->treemapRectCount() : 0; }

/** @brief Resets all profiler visuals, breakdown rows, and totals back to the empty idle state. */
void MemoryProfilerWidget::clear() {
  m_lastPid = 0; m_entries.clear();
  if (m_canvas) m_canvas->clear();
  if (m_table) m_table->setRowCount(0);
  if (m_searchEdit) m_searchEdit->clear();
  if (m_summaryLabel) m_summaryLabel->setText(QStringLiteral("TOTAL: 0.00 MB"));
}

/** @brief Samples either the selected single MPI rank PID or aggregates across all active ranks. */
void MemoryProfilerWidget::onRefreshClicked() {
  if (!m_rankCombo) return;
  const int selRank = m_rankCombo->currentData().toInt();
  if (selRank < 0) { refreshAllRanks(); return; }
  if (const qint64 pid = m_pidGetter ? m_pidGetter(selRank) : m_lastPid; pid > 0) refreshForPid(pid);
}

/** @brief Demangles Itanium C++ ABI symbol names (`_Z...`) via `abi::__cxa_demangle`. */
QString MemoryProfilerWidget::demangleSymbol(const QString& rawSym) {
  const QByteArray utf8 = rawSym.trimmed().toUtf8();
  if (!utf8.startsWith("_Z")) return rawSym.trimmed();
  int status = -1;
  char* dem = abi::__cxa_demangle(utf8.constData(), nullptr, nullptr, &status);
  const QString out = (status == 0 && dem) ? QString::fromUtf8(dem) : rawSym.trimmed();
  std::free(dem); return out;
}

/** @brief Formats a byte count into a fixed two-decimal megabyte string (`"X.XX MB"`). */
QString MemoryProfilerWidget::formatMegabytes(uint64_t bytes) {
  return QStringLiteral("%1 MB").arg(static_cast<double>(bytes) / (1024.0 * 1024.0), 0, 'f', 2);
}

/**
 * @brief Collects VMA Resident/Dirty segments from `/proc/<pid>/smaps` and merges `/tmp/gridlock_mem_<pid>.tsv`.
 * @param pid Target OS process ID.
 * @return Aggregated vector of shared libraries, `/dev/shm` segments, stacks, arenas, and heap call sites.
 */
QVector<MemoryProfileEntry> MemoryProfilerWidget::collectEntriesForPid(qint64 pid) {
  QVector<MemoryProfileEntry> result;
  if (pid <= 0) return result;

  // Per-owner VMA accumulator tracking resident, dirty, virtual span, and address bounds.
  struct VmaAgg {
    QString name, fullPath, category;
    uint64_t rssBytes = 0, dirtyBytes = 0, vmaBytes = 0, startAddr = 0, endAddr = 0;
  };
  QMap<QString, VmaAgg> groups;
  uint64_t rawHeapRss = 0, rawHeapDirty = 0, heapStart = 0, heapEnd = 0;
  uint64_t anonRss = 0, anonDirty = 0, anonLargestSpan = 0, anonStart = 0, anonEnd = 0;

  // 1. Engine A: Read `/proc/<pid>/smaps` via `readAll()` (`st_size == 0` on procfs breaks `atEnd()`).
  QFile smaps(QStringLiteral("/proc/%1/smaps").arg(pid));
  const QByteArray smapsRaw = smaps.open(QIODevice::ReadOnly | QIODevice::Text) ? smaps.readAll() : QByteArray();
  if (!smapsRaw.isEmpty()) {
    QString curKey; bool curIsHeap = false, curIsAnonRw = false;
    static const QRegularExpression hdrRe(QStringLiteral("^([0-9a-fA-F]+)-([0-9a-fA-F]+)\\s+(\\S+)\\s+\\S+\\s+\\S+\\s+\\d+\\s*(.*)$"));

    for (const QByteArray& rawLine : smapsRaw.split('\n')) {
      const QString line = QString::fromUtf8(rawLine).trimmed();
      if (line.isEmpty()) continue;
      // Match VMA header lines (`start-end perms offset dev inode pathname`) to switch active owner.
      if (const QRegularExpressionMatch hm = hdrRe.match(line); hm.hasMatch()) {
        const uint64_t curStart = hm.captured(1).toULongLong(nullptr, 16), curEnd = hm.captured(2).toULongLong(nullptr, 16);
        const QString perms = hm.captured(3), path = hm.captured(4).trimmed();
        curIsHeap = (path == QLatin1String("[heap]"));
        curIsAnonRw = path.isEmpty() && perms.startsWith(QLatin1String("rw"));
        curKey.clear();

        if (curIsHeap) {
          if (heapStart == 0 || curStart < heapStart) heapStart = curStart;
          if (curEnd > heapEnd) heapEnd = curEnd;
        } else if (curIsAnonRw) {
          // Track the largest anonymous rw-p arena address span for hex dump inspection jumps.
          if (curEnd > curStart && (curEnd - curStart) > anonLargestSpan) { anonLargestSpan = curEnd - curStart; anonStart = curStart; anonEnd = curEnd; }
        } else if (!path.isEmpty() && !path.startsWith(QLatin1String("[v"))) {
          // Classify file-backed mappings into MPI /dev/shm, Thread Stacks, or Shared Libraries / Binaries.
          QString cat = QStringLiteral("Shared Lib"), disp = path;
          if (path.startsWith(QLatin1String("/dev/shm")) || path.contains(QLatin1String("SYSV")) || path.startsWith(QLatin1String("/dev/"))) {
            cat = QStringLiteral("MPI /dev/shm");
            disp = QFileInfo(path).fileName().isEmpty() ? path : QFileInfo(path).fileName();
          } else if (path.startsWith(QLatin1String("[stack"))) {
            cat = QStringLiteral("Stack"); disp = QStringLiteral("Thread Stacks ([stack])");
          } else if (path.startsWith(QLatin1Char('/'))) {
            disp = QFileInfo(path).fileName();
          }
          curKey = cat + QLatin1Char('|') + disp;
          auto& g = groups[curKey];
          if (g.name.isEmpty()) { g.name = disp; g.fullPath = path; g.category = cat; }
          if (g.startAddr == 0 || curStart < g.startAddr) g.startAddr = curStart;
          if (curEnd > g.endAddr) g.endAddr = curEnd;
        }
        continue;
      }

      // Accumulate Rss, Private_Dirty, and Size kB counters into the active VMA owner group.
      auto parseKb = [&](const QLatin1String pfx, uint64_t& dst) {
        if (line.startsWith(pfx)) dst += line.mid(pfx.size()).trimmed().section(QLatin1Char(' '), 0, 0).toULongLong() * 1024ULL;
      };
      if (curIsHeap) { parseKb(QLatin1String("Rss:"), rawHeapRss); parseKb(QLatin1String("Private_Dirty:"), rawHeapDirty); }
      else if (curIsAnonRw) { parseKb(QLatin1String("Rss:"), anonRss); parseKb(QLatin1String("Private_Dirty:"), anonDirty); }
      else if (!curKey.isEmpty()) {
        auto& g = groups[curKey];
        parseKb(QLatin1String("Rss:"), g.rssBytes); parseKb(QLatin1String("Private_Dirty:"), g.dirtyBytes); parseKb(QLatin1String("Size:"), g.vmaBytes);
      }
    }
  } else {
    // Fallback to `/proc/<pid>/maps` address spans via `readAll()` when smaps cannot be read.
    QFile maps(QStringLiteral("/proc/%1/maps").arg(pid));
    if (maps.open(QIODevice::ReadOnly | QIODevice::Text)) {
      for (const QByteArray& rawLine : maps.readAll().split('\n')) {
        const QString line = QString::fromUtf8(rawLine).trimmed();
        const QString range = line.section(QLatin1Char(' '), 0, 0), path = line.section(QLatin1Char(' '), 5).trimmed();
        const uint64_t s = range.section(QLatin1Char('-'), 0, 0).toULongLong(nullptr, 16), e = range.section(QLatin1Char('-'), 1, 1).toULongLong(nullptr, 16);
        if (e <= s || path.isEmpty()) continue;
        const QString disp = path.startsWith(QLatin1Char('/')) ? QFileInfo(path).fileName() : path;
        auto& g = groups[disp];
        g.name = disp; g.fullPath = path; g.category = QStringLiteral("Shared Lib"); g.rssBytes += (e - s);
        if (g.startAddr == 0 || s < g.startAddr) g.startAddr = s;
        if (e > g.endAddr) g.endAddr = e;
      }
    }
  }

  // 2. Engine B: Merge live caller-attributed heap allocations from `/tmp/gridlock_mem_<pid>.tsv`.
  static QHash<QString, QString> s_offsetSymCache;
  uint64_t trackedHeapTotal = 0;
  QFile tsv(QStringLiteral("/tmp/gridlock_mem_%1.tsv").arg(pid));
  if (tsv.open(QIODevice::ReadOnly | QIODevice::Text)) {
    for (const QByteArray& rawRow : tsv.readAll().split('\n')) {
      const QStringList cols = QString::fromUtf8(rawRow).trimmed().split(QLatin1Char('\t'));
      if (cols.size() < 5) continue;
      const uint64_t liveB = cols[2].toULongLong(), peakB = cols[3].toULongLong(), count = cols[4].toULongLong();
      if (liveB == 0) continue;
      // Resolve DWARF/+0xoffset symbols via cached `addr2line` when non-rdynamic binaries omit .dynsym names.
      const QString modBase = cols[0].startsWith(QLatin1Char('/')) ? QFileInfo(cols[0]).fileName() : cols[0];
      QString symLabel = demangleSymbol(cols[1]);
      if (symLabel.startsWith(QLatin1String("+0x")) && QFileInfo::exists(cols[0])) {
        const QString cacheKey = cols[0] + QLatin1Char('@') + symLabel;
        if (!s_offsetSymCache.contains(cacheKey)) {
          QProcess a2l; a2l.start(QStringLiteral("addr2line"), {QStringLiteral("-f"), QStringLiteral("-C"), QStringLiteral("-e"), cols[0], symLabel.mid(1)});
          const QString fn = (a2l.waitForFinished(800) && a2l.exitCode() == 0) ? QString::fromUtf8(a2l.readAllStandardOutput()).section(QLatin1Char('\n'), 0, 0).trimmed() : QString();
          s_offsetSymCache.insert(cacheKey, (!fn.isEmpty() && !fn.startsWith(QLatin1Char('?'))) ? fn : symLabel);
        }
        symLabel = s_offsetSymCache.value(cacheKey, symLabel);
      }
      result.push_back({QStringLiteral("%1 :: %2").arg(modBase, symLabel), cols[0], QStringLiteral("Heap / Call Site"),
                        QStringLiteral("%1 [%2 allocs, peak %3]").arg(symLabel).arg(count).arg(formatMegabytes(peakB)),
                        liveB, liveB, peakB, anonStart != 0 ? anonStart : heapStart, anonEnd != 0 ? anonEnd : heapEnd, count, QColor()});
      trackedHeapTotal += liveB;
    }
  }

  // Deduct caller-attributed heap bytes from raw `[heap]` + anonymous `rw-p` arenas to prevent double-counting.
  uint64_t deduct = trackedHeapTotal;
  const uint64_t takeAnon = std::min(anonRss, deduct);
  anonRss -= takeAnon; anonDirty = (anonDirty > takeAnon) ? (anonDirty - takeAnon) : 0; deduct -= takeAnon;
  const uint64_t takeHeap = std::min(rawHeapRss, deduct);
  rawHeapRss -= takeHeap; rawHeapDirty = (rawHeapDirty > takeHeap) ? (rawHeapDirty - takeHeap) : 0;

  // Emit residual unattributed [heap], anonymous mmap arenas, and file-backed library groups.
  if (rawHeapRss > 0) {
    result.push_back({QStringLiteral("Process Heap ([heap])"), QStringLiteral("[heap]"), QStringLiteral("Heap / Call Site"),
                      QStringLiteral("0x%1 - 0x%2").arg(heapStart, 0, 16).arg(heapEnd, 0, 16),
                      rawHeapRss, rawHeapDirty, rawHeapRss, heapStart, heapEnd, 0, QColor()});
  }
  if (anonRss > 0) {
    result.push_back({QStringLiteral("Anonymous mmap Arenas"), QStringLiteral("[anon:rw-p]"), QStringLiteral("Anon mmap"),
                      QStringLiteral("0x%1 - 0x%2").arg(anonStart, 0, 16).arg(anonEnd, 0, 16),
                      anonRss, anonDirty, anonRss, anonStart, anonEnd, 0, QColor()});
  }
  for (auto it = groups.constBegin(); it != groups.constEnd(); ++it) {
    const auto& g = it.value();
    if (g.rssBytes == 0) continue;
    result.push_back({g.name, g.fullPath, g.category,
                      QStringLiteral("0x%1 - 0x%2 (%3)").arg(g.startAddr, 0, 16).arg(g.endAddr, 0, 16).arg(g.fullPath),
                      g.rssBytes, g.dirtyBytes, std::max(g.rssBytes, g.vmaBytes), g.startAddr, g.endAddr, 0, QColor()});
  }
  return result;
}

/** @brief Samples `/proc/<pid>/smaps` + preload tracker data for `pid` and refreshes visuals. */
void MemoryProfilerWidget::refreshForPid(qint64 pid) {
  if (pid <= 0) return;
  m_lastPid = pid;
  populateFromEntries(collectEntriesForPid(pid), QStringLiteral("PID %1").arg(pid));
}

/** @brief Aggregates memory attribution across all active MPI ranks (`0..m_rankCount-1`). */
void MemoryProfilerWidget::refreshAllRanks() {
  if (!m_pidGetter) return;
  QMap<QString, MemoryProfileEntry> merged; int sampledRanks = 0;
  for (int r = 0; r < m_rankCount; ++r) {
    const qint64 pid = m_pidGetter(r);
    if (pid <= 0) continue;
    ++sampledRanks;
    for (const auto& e : collectEntriesForPid(pid)) {
      auto& dst = merged[e.category + QLatin1Char('|') + e.name];
      if (dst.name.isEmpty()) dst = e;
      else { dst.liveBytes += e.liveBytes; dst.dirtyBytes += e.dirtyBytes; dst.peakBytes += e.peakBytes; dst.allocCount += e.allocCount; }
    }
  }
  QVector<MemoryProfileEntry> combined;
  for (auto it = merged.constBegin(); it != merged.constEnd(); ++it) combined.push_back(it.value());
  populateFromEntries(combined, QStringLiteral("%1 Ranks").arg(sampledRanks));
}

/**
 * @brief Populates `#memoryTreemapCanvas` and `#memoryBreakdownTable` from collected entries.
 */
void MemoryProfilerWidget::populateFromEntries(const QVector<MemoryProfileEntry>& rawEntries, const QString& contextLabel) {
  // Sort largest-first for balanced treemap layout and assign theme-aware category colors.
  m_entries = rawEntries;
  std::sort(m_entries.begin(), m_entries.end(), [](const MemoryProfileEntry& a, const MemoryProfileEntry& b) { return a.liveBytes > b.liveBytes; });
  const bool isDark = gridlock::core::ThemeManager::instance().isDarkMode();
  const QString scheme = m_schemeCombo ? m_schemeCombo->currentData().toString() : gridlock::core::ConfigManager::instance().memoryMapColorScheme();
  uint64_t totalBytes = 0;
  for (int i = 0; i < m_entries.size(); ++i) {
    m_entries[i].color = colorForEntry(i, m_entries.size(), m_entries[i], scheme);
    totalBytes += m_entries[i].liveBytes;
  }
  if (m_summaryLabel) {
    m_summaryLabel->setText(QStringLiteral("%1 — TOTAL: %2 (%3 regions)").arg(contextLabel, formatMegabytes(totalBytes)).arg(m_entries.size()));
  }
  if (m_canvas) m_canvas->setEntries(m_entries, totalBytes);
  if (!m_table) return;

  // Suspend live table sorting while populating rows so cells stay aligned with row index.
  const bool wasSorting = m_table->isSortingEnabled();
  m_table->setSortingEnabled(false); m_table->setRowCount(0);

  for (const auto& e : m_entries) {
    const int row = m_table->rowCount(); m_table->insertRow(row);
    const double mb = static_cast<double>(e.liveBytes) / (1024.0 * 1024.0), dirtyMb = static_cast<double>(e.dirtyBytes) / (1024.0 * 1024.0);
    const double pct = totalBytes > 0 ? (100.0 * static_cast<double>(e.liveBytes)) / static_cast<double>(totalBytes) : 0.0;

    // Bind startAddress to column 0 UserRole and numeric sort values to columns 2, 3, and 4.
    auto* nameItem = new QTableWidgetItem(e.name);
    nameItem->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(e.startAddress)); nameItem->setForeground(e.color.lighter(isDark ? 130 : 85));
    auto* catItem = new QTableWidgetItem(e.category);
    auto* rssItem = new NumericMegabyteItem(QStringLiteral("%1 MB").arg(mb, 0, 'f', 2)); rssItem->setData(Qt::UserRole, mb); rssItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto* dirtyItem = new NumericMegabyteItem(QStringLiteral("%1 MB").arg(dirtyMb, 0, 'f', 2)); dirtyItem->setData(Qt::UserRole, dirtyMb); dirtyItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto* pctItem = new NumericMegabyteItem(QStringLiteral("%1%").arg(pct, 0, 'f', 1)); pctItem->setData(Qt::UserRole, pct); pctItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto* symItem = new QTableWidgetItem(e.topSymbolOrRange);

    // Populate cells and copy full text to item tooltips so long paths/symbols remain legible.
    const QList<QTableWidgetItem*> rowItems = {nameItem, catItem, rssItem, dirtyItem, pctItem, symItem};
    for (int c = 0; c < rowItems.size(); ++c) { rowItems[c]->setToolTip(rowItems[c]->text()); m_table->setItem(row, c, rowItems[c]); }
  }
  m_table->setSortingEnabled(wasSorting);
  m_table->sortItems(m_sortColumn, m_sortOrder);
  if (auto* hdr = m_table->horizontalHeader()) hdr->setSortIndicator(m_sortColumn, m_sortOrder);
  applySearchFilter(m_searchEdit ? m_searchEdit->text() : QString());
}

/**
 * @brief Filters visible breakdown rows case-insensitively by module, category, or symbol/range.
 * @param text Substring filter entered in `#memProfileSearchEdit`.
 */
void MemoryProfilerWidget::applySearchFilter(const QString& text) {
  if (!m_table) return;
  const QString q = text.trimmed();
  // Show every row when the search query is empty; otherwise match across all 6 columns.
  for (int r = 0; r < m_table->rowCount(); ++r) {
    bool match = q.isEmpty();
    for (int c = 0; c < m_table->columnCount() && !match; ++c)
      if (const auto* it = m_table->item(r, c); it && it->text().contains(q, Qt::CaseInsensitive)) match = true;
    m_table->setRowHidden(r, !match);
  }
}

/** @brief Computes a distinct, scheme-aware fill color for a treemap/table entry. */
QColor MemoryProfilerWidget::colorForEntry(int index, int totalCount, const MemoryProfileEntry& entry, const QString& scheme) { return colorForEntryImpl(index, totalCount, entry, scheme); }
/** @brief Recomputes entry colors under the active scheme and updates canvas + table. */
void MemoryProfilerWidget::reapplyColors() {
  const QString scheme = m_schemeCombo ? m_schemeCombo->currentData().toString() : gridlock::core::ConfigManager::instance().memoryMapColorScheme();
  const bool isDark = gridlock::core::ThemeManager::instance().isDarkMode(); uint64_t totalBytes = 0;
  for (int i = 0; i < m_entries.size(); ++i) { m_entries[i].color = colorForEntry(i, m_entries.size(), m_entries[i], scheme); totalBytes += m_entries[i].liveBytes; }
  if (m_canvas) m_canvas->setEntries(m_entries, totalBytes);
  if (m_table) for (int r = 0; r < m_table->rowCount(); ++r) if (auto* it = m_table->item(r, 0)) for (const auto& e : m_entries) if (e.name == it->text()) { it->setForeground(e.color.lighter(isDark ? 130 : 85)); break; }
}
/** @brief Listens for Preferences -> Appearance Memory Map scheme updates via qApp property changes. */
bool MemoryProfilerWidget::eventFilter(QObject* watched, QEvent* event) {
  if (watched == QCoreApplication::instance() && event->type() == QEvent::DynamicPropertyChange && static_cast<QDynamicPropertyChangeEvent*>(event)->propertyName() == "gridlock_memmap_scheme_rev") {
    const QString scheme = gridlock::core::ConfigManager::instance().memoryMapColorScheme();
    if (m_schemeCombo) { QSignalBlocker sb(m_schemeCombo); if (const int idx = m_schemeCombo->findData(scheme); idx >= 0) m_schemeCombo->setCurrentIndex(idx); }
    reapplyColors();
  }
  return QWidget::eventFilter(watched, event);
}
/** @brief Synchronizes control bar, breakdown table, and treemap canvas colors with ThemeManager. */
void MemoryProfilerWidget::reloadStyle() {
  auto& tm = gridlock::core::ThemeManager::instance(); const auto pal = tm.paletteForTheme(tm.activeThemeId());
  if (auto* bar = findChild<QWidget*>(QStringLiteral("memProfileControlBar"))) bar->setStyleSheet(QStringLiteral("#memProfileControlBar { background-color: %1; border-bottom: 1px solid %2; } #memProfileControlBar QLabel, #memProfileControlBar QCheckBox { color: %3; font-size: 11px; font-weight: 600; } #memProfileControlBar QLineEdit, #memProfileControlBar QComboBox { background-color: %4; color: %3; border: 1px solid %2; border-radius: 2px; padding: 0px 5px; font-size: 11px; } #memProfileControlBar QPushButton { background-color: %1; color: %3; border: 1px solid %2; border-radius: 2px; padding: 1px 8px; font-size: 10px; font-weight: 600; } #memProfileControlBar QPushButton:hover { background-color: %2; }").arg(pal.headerHex, pal.borderHex, pal.textHex, pal.canvasHex));
  if (m_table) m_table->setStyleSheet(QStringLiteral("QTableWidget { background-color: %1; color: %2; gridline-color: %3; border: none; font-family: 'JetBrains Mono', monospace; font-size: 11px; } QHeaderView::section { background-color: %4; color: %2; border: 1px solid %3; padding: 2px 5px; font-size: 11px; font-weight: 600; }").arg(pal.canvasHex, pal.textHex, pal.borderHex, pal.headerHex));
  if (m_canvas) m_canvas->update();
}

}  // namespace gridlock::ui
