/**
 * @file MemoryProfilerWidget.cpp
 * @brief Controls, `LD_PRELOAD` caller-attribution interposer, and `/proc/<pid>/smaps` VMA collector for MemoryProfilerWidget.
 *
 * Implements the MemoryProfilerWidget control bar and splitter container, compiles the
 * lightweight `>= 4 KB` `LD_PRELOAD` malloc/calloc/realloc/free interposer (`gridlock_memtrack`),
 * and parses `/proc/<pid>/smaps` (with `/proc/<pid>/maps` fallback) plus `/tmp/gridlock_mem_<pid>.tsv`
 * into aggregated library, `/dev/shm`, stack, arena, and heap call-site entries.
 */

#include "MemoryProfilerWidget.hpp"

#include <cxxabi.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <utility>

#include <QAbstractItemView>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QMap>
#include <QProcess>
#include <QRegularExpression>
#include <QSplitter>
#include <QStandardPaths>
#include <QVBoxLayout>

#include "../../core/managers/ConfigManager.hpp"

namespace gridlock::ui {

namespace {

/** @brief Embedded C source for the lightweight `LD_PRELOAD` >=4 KB caller-attribution interposer. */
const char* preloadTrackerCSource() {
  return R"C_SRC(
// Lightweight >=4 KB malloc/calloc/realloc/free caller-attribution preload library.
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
#define MIN_TRACK_BYTES 4096ULL
#define PTR_SLOTS 16384U
#define BUCKET_SLOTS 512U
#define TOMBSTONE ((uintptr_t)1U)
typedef struct { uintptr_t ptr; uint64_t size; uint32_t bucket; } PtrRec;
typedef struct { char module[192]; char symbol[128]; uint64_t live_bytes, peak_bytes, alloc_count; int used; } Bucket;
typedef struct { uintptr_t pcs[8]; int count; } TraceState;
static void* (*real_malloc)(size_t) = NULL;
static void* (*real_calloc)(size_t, size_t) = NULL;
static void* (*real_realloc)(void*, size_t) = NULL;
static void  (*real_free)(void*) = NULL;
static unsigned char g_boot_buf[65536];
static size_t g_boot_off = 0;
static volatile int g_init_state = 0;
static __thread int g_in_hook = 0;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static PtrRec g_ptrs[PTR_SLOTS];
static uint32_t g_ptr_used = 0;
static volatile uint32_t g_live_tracked = 0;
static Bucket g_buckets[BUCKET_SLOTS];
static uint64_t g_unsaved_delta = 0, g_last_flush_ms = 0;
static int g_ever_flushed = 0;
static int is_boot_ptr(const void* p) { const unsigned char* b = (const unsigned char*)p; return b >= g_boot_buf && b < (g_boot_buf + sizeof(g_boot_buf)); }
static void* boot_alloc(size_t sz) {
  size_t aligned = (sz + 15U) & ~((size_t)15U);
  if (g_boot_off + aligned + 16U > sizeof(g_boot_buf)) return NULL;
  ((size_t*)(g_boot_buf + g_boot_off))[0] = sz;
  void* out = (void*)(g_boot_buf + g_boot_off + 16U);
  g_boot_off += aligned + 16U; memset(out, 0, sz); return out;
}
static void resolve_real_funcs(void) {
  if (__atomic_load_n(&g_init_state, __ATOMIC_ACQUIRE) == 2) return;
  int exp = 0;
  if (__atomic_compare_exchange_n(&g_init_state, &exp, 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
    *(void**)(&real_malloc) = dlsym(RTLD_NEXT, "malloc"); *(void**)(&real_calloc) = dlsym(RTLD_NEXT, "calloc");
    *(void**)(&real_realloc) = dlsym(RTLD_NEXT, "realloc"); *(void**)(&real_free) = dlsym(RTLD_NEXT, "free");
    __atomic_store_n(&g_init_state, 2, __ATOMIC_RELEASE);
  }
}
static _Unwind_Reason_Code unwind_cb(struct _Unwind_Context* ctx, void* arg) {
  TraceState* st = (TraceState*)arg; uintptr_t ip = _Unwind_GetIP(ctx);
  if (ip && st->count < 8) { st->pcs[st->count++] = ip; if (st->count >= 8) return _URC_END_OF_STACK; }
  return _URC_NO_REASON;
}
static int is_wrapper_lib(const char* f) {
  return !f || !f[0] || strstr(f, "gridlock_memtrack") || strstr(f, "libstdc++.so") || strstr(f, "libc.so") || strstr(f, "libgcc_s.so") || strstr(f, "ld-linux");
}
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
__attribute__((destructor)) static void gridlock_memtrack_fini(void) {
  if (g_ever_flushed) { g_in_hook = 1; pthread_mutex_lock(&g_lock); maybe_flush_locked(1); pthread_mutex_unlock(&g_lock); g_in_hook = 0; }
}
)C_SRC";
}

}  // namespace

/** @brief Constructs the interactive Library & Heap Memory Profiler widget. */
MemoryProfilerWidget::MemoryProfilerWidget(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("memoryProfilerWidget"));
  auto* rootLayout = new QVBoxLayout(this);
  rootLayout->setContentsMargins(0, 0, 0, 0); rootLayout->setSpacing(0);

  auto* ctrlBar = new QWidget(this);
  ctrlBar->setObjectName(QStringLiteral("memProfileControlBar")); ctrlBar->setFixedHeight(28);
  auto* ctrlLayout = new QHBoxLayout(ctrlBar);
  ctrlLayout->setContentsMargins(6, 2, 6, 2); ctrlLayout->setSpacing(6);

  m_rankCombo = new QComboBox(ctrlBar);
  m_rankCombo->setObjectName(QStringLiteral("memProfileRankCombo"));
  m_rankCombo->setFixedHeight(22); m_rankCombo->setMinimumWidth(118);
  m_rankCombo->addItem(QStringLiteral("Rank 0"), 0);
  m_rankCombo->addItem(QStringLiteral("All Ranks Overview"), -1);
  ctrlLayout->addWidget(m_rankCombo);

  m_refreshBtn = new QPushButton(QStringLiteral("Refresh Now"), ctrlBar);
  m_refreshBtn->setObjectName(QStringLiteral("memProfileRefreshBtn")); m_refreshBtn->setFixedHeight(22);
  ctrlLayout->addWidget(m_refreshBtn);

  m_preloadCheck = new QCheckBox(QStringLiteral("Track Heap Callers (LD_PRELOAD)"), ctrlBar);
  m_preloadCheck->setObjectName(QStringLiteral("memTrackPreloadCheck")); m_preloadCheck->setChecked(true);
  m_preloadCheck->setToolTip(QStringLiteral("Preload lightweight >=4 KB malloc/new caller tracker into newly launched MPI ranks"));
  ctrlLayout->addWidget(m_preloadCheck);

  m_schemeCombo = new QComboBox(ctrlBar); m_schemeCombo->setObjectName(QStringLiteral("memProfileColorSchemeCombo")); m_schemeCombo->setFixedHeight(22);
  for (const auto& opt : {QPair<QString, QString>{QStringLiteral("Distinct"), QStringLiteral("Distinct")}, {QStringLiteral("Viridis"), QStringLiteral("Viridis")}, {QStringLiteral("Plasma"), QStringLiteral("Plasma")}, {QStringLiteral("Ocean"), QStringLiteral("Ocean")}, {QStringLiteral("High-Contrast"), QStringLiteral("HighContrast")}}) m_schemeCombo->addItem(opt.first, opt.second);
  if (const int si = m_schemeCombo->findData(gridlock::core::ConfigManager::instance().memoryMapColorScheme()); si >= 0) m_schemeCombo->setCurrentIndex(si);
  connect(m_schemeCombo, &QComboBox::currentIndexChanged, this, [this](int) { gridlock::core::ConfigManager::instance().setMemoryMapColorScheme(m_schemeCombo->currentData().toString()); reapplyColors(); });
  ctrlLayout->addWidget(m_schemeCombo);
  if (auto* app = QCoreApplication::instance()) app->installEventFilter(this);

  m_searchEdit = new QLineEdit(ctrlBar); m_searchEdit->setObjectName(QStringLiteral("memProfileSearchEdit")); m_searchEdit->setPlaceholderText(QStringLiteral("Filter libraries, symbols, or categories...")); m_searchEdit->setClearButtonEnabled(true); m_searchEdit->setFixedHeight(22); m_searchEdit->setMinimumWidth(180); ctrlLayout->addWidget(m_searchEdit, 1);

  m_summaryLabel = new QLabel(QStringLiteral("TOTAL: 0.00 MB"), ctrlBar); m_summaryLabel->setObjectName(QStringLiteral("memProfileSummaryLabel")); ctrlLayout->addWidget(m_summaryLabel); rootLayout->addWidget(ctrlBar);

  auto* splitter = new QSplitter(Qt::Vertical, this); splitter->setChildrenCollapsible(false); splitter->setOpaqueResize(true);
  m_canvas = new MemoryTreemapCanvas(splitter); splitter->addWidget(m_canvas);

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

  syncPreloadTrackerEnvironment(m_preloadCheck->isChecked());
  connect(m_refreshBtn, &QPushButton::clicked, this, &MemoryProfilerWidget::onRefreshClicked);
  connect(m_rankCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { onRefreshClicked(); });
  connect(m_preloadCheck, &QCheckBox::toggled, this, [](bool checked) { syncPreloadTrackerEnvironment(checked); });
  connect(m_searchEdit, &QLineEdit::textChanged, this, &MemoryProfilerWidget::applySearchFilter);
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

/** @brief Compiles `/tmp/gridlock_memtrack_v2_<uid>.so` via `gcc`/`cc` and sets `GRIDLOCK_MEMTRACK_SO`. */
QString MemoryProfilerWidget::syncPreloadTrackerEnvironment(bool enabled) {
  if (!enabled) { qunsetenv("GRIDLOCK_MEMTRACK_SO"); return QString(); }
  const uint uid = static_cast<uint>(::getuid());
  const QString cPath = QStringLiteral("/tmp/gridlock_memtrack_v2_%1.c").arg(uid);
  const QString soPath = QStringLiteral("/tmp/gridlock_memtrack_v2_%1.so").arg(uid);

  if (!QFileInfo::exists(soPath)) {
    QFile cf(cPath);
    if (cf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      cf.write(preloadTrackerCSource()); cf.close();
      QString cc = QStandardPaths::findExecutable(QStringLiteral("gcc"));
      if (cc.isEmpty()) cc = QStandardPaths::findExecutable(QStringLiteral("cc"));
      if (!cc.isEmpty()) {
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

void MemoryProfilerWidget::setAvailableRanks(int rankCount, int focusedRank, RankPidGetter pidGetter) {
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

void MemoryProfilerWidget::onRefreshClicked() {
  if (!m_rankCombo) return;
  const int selRank = m_rankCombo->currentData().toInt();
  if (selRank < 0) { refreshAllRanks(); return; }
  if (const qint64 pid = m_pidGetter ? m_pidGetter(selRank) : m_lastPid; pid > 0) refreshForPid(pid);
}

QString MemoryProfilerWidget::demangleSymbol(const QString& rawSym) {
  const QByteArray utf8 = rawSym.trimmed().toUtf8();
  if (!utf8.startsWith("_Z")) return rawSym.trimmed();
  int status = -1;
  char* dem = abi::__cxa_demangle(utf8.constData(), nullptr, nullptr, &status);
  const QString out = (status == 0 && dem) ? QString::fromUtf8(dem) : rawSym.trimmed();
  std::free(dem); return out;
}

QString MemoryProfilerWidget::formatMegabytes(uint64_t bytes) {
  return QStringLiteral("%1 MB").arg(static_cast<double>(bytes) / (1024.0 * 1024.0), 0, 'f', 2);
}

/** @brief Collects VMA Resident/Dirty segments from `/proc/<pid>/smaps` and merges `/tmp/gridlock_mem_<pid>.tsv`. */
QVector<MemoryProfileEntry> MemoryProfilerWidget::collectEntriesForPid(qint64 pid) {
  QVector<MemoryProfileEntry> result;
  if (pid <= 0) return result;

  struct VmaAgg {
    QString name, fullPath, category;
    uint64_t rssBytes = 0, dirtyBytes = 0, vmaBytes = 0, startAddr = 0, endAddr = 0;
  };
  QMap<QString, VmaAgg> groups;
  uint64_t rawHeapRss = 0, rawHeapDirty = 0, heapStart = 0, heapEnd = 0;
  uint64_t anonRss = 0, anonDirty = 0, anonLargestSpan = 0, anonStart = 0, anonEnd = 0;

  QFile smaps(QStringLiteral("/proc/%1/smaps").arg(pid));
  const QByteArray smapsRaw = smaps.open(QIODevice::ReadOnly | QIODevice::Text) ? smaps.readAll() : QByteArray();
  if (!smapsRaw.isEmpty()) {
    QString curKey; bool curIsHeap = false, curIsAnonRw = false;
    static const QRegularExpression hdrRe(QStringLiteral("^([0-9a-fA-F]+)-([0-9a-fA-F]+)\\s+(\\S+)\\s+\\S+\\s+\\S+\\s+\\d+\\s*(.*)$"));

    for (const QByteArray& rawLine : smapsRaw.split('\n')) {
      const QString line = QString::fromUtf8(rawLine).trimmed();
      if (line.isEmpty()) continue;
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
          if (curEnd > curStart && (curEnd - curStart) > anonLargestSpan) { anonLargestSpan = curEnd - curStart; anonStart = curStart; anonEnd = curEnd; }
        } else if (!path.isEmpty() && !path.startsWith(QLatin1String("[v"))) {
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

  static QHash<QString, QString> s_offsetSymCache;
  uint64_t trackedHeapTotal = 0;
  QFile tsv(QStringLiteral("/tmp/gridlock_mem_%1.tsv").arg(pid));
  if (tsv.open(QIODevice::ReadOnly | QIODevice::Text)) {
    for (const QByteArray& rawRow : tsv.readAll().split('\n')) {
      const QStringList cols = QString::fromUtf8(rawRow).trimmed().split(QLatin1Char('\t'));
      if (cols.size() < 5) continue;
      const uint64_t liveB = cols[2].toULongLong(), peakB = cols[3].toULongLong(), count = cols[4].toULongLong();
      if (liveB == 0) continue;
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

  uint64_t deduct = trackedHeapTotal;
  const uint64_t takeAnon = std::min(anonRss, deduct);
  anonRss -= takeAnon; anonDirty = (anonDirty > takeAnon) ? (anonDirty - takeAnon) : 0; deduct -= takeAnon;
  const uint64_t takeHeap = std::min(rawHeapRss, deduct);
  rawHeapRss -= takeHeap; rawHeapDirty = (rawHeapDirty > takeHeap) ? (rawHeapDirty - takeHeap) : 0;

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

}  // namespace gridlock::ui
