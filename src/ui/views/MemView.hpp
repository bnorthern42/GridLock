/**
 * @file MemView.hpp
 * @brief Memory hex dump and differential inspection view widget.
 *
 * Provides synchronized hexadecimal and ASCII memory dumps, address seeking,
 * MPI rank memory diff overlays with changed-byte highlighting, and binary/matrix export.
 */

#pragma once
#include <QByteArray>
#include <QMap>
#include <QString>
#include <QWidget>
#include <cstdint>
#include <functional>
#include "../../core/hpc/MemoryDiffer.hpp"

class QLineEdit;
class QSpinBox;
class QPushButton;
class QTextEdit;
class QComboBox;
class QLabel;
class QStackedWidget;
class QTextCursor;

namespace gridlock::ui {

class MemoryProfilerWidget;

/**
 * @brief Workstation memory inspector displaying hex dumps, cross-rank diffs, and library treemaps.
 */
class MemView : public QWidget {
    Q_OBJECT
public:
    /// Resolves a hex literal or register/symbol expression (e.g. `$rsp`) for a given MPI rank.
    using RankAddressResolver = std::function<uint64_t(int rank, const QString& expr, bool* okOut)>;
    /// Reads `length` raw bytes at `address` from `rank` (`process_vm_readv` + GDB/DAP fallback).
    using RankBytesFetcher = std::function<QByteArray(int rank, uint64_t address, int length)>;
    /// Queries the live OS child PID for `rank` (`0` when no active target rank is running).
    using RankPidGetter = std::function<qint64(int rank)>;

    explicit MemView(QWidget* parent = nullptr);
    ~MemView() override;

    /// Binds live rank address resolution, `process_vm_readv` / debugger byte reads, and PID lookup.
    void setLiveMemoryCallbacks(RankAddressResolver resolver,
                                RankBytesFetcher fetcher,
                                RankPidGetter pidGetter = {});

    /// Synchronizes active rank/PID context with `MemoryProfilerWidget` and refreshes when visible.
    void notifyRankStoppedOrSelected(int rank, qint64 pid, int rankCount);
    /// Synchronizes MemView's inspected rank to global focused @p rank unless `Pin Rank` is enabled.
    void setFocusedRank(int rank, int rankCount = -1);
    /// Returns the 0-based MPI rank currently targeted by MemView.
    int focusedRank() const;
    /// Returns true when `Pin Rank` is toggled on (suppresses automatic global focused-rank updates).
    bool isRankPinned() const;
    /// Toggles `Pin Rank` mode on or off.
    void setRankPinned(bool pinned);
    /// Returns true when the `LIBRARY MEMORY MAP` view is currently selected.
    bool isMemoryMapMode() const;
    /// Returns the embedded visual library and heap memory profiler widget.
    MemoryProfilerWidget* memoryProfiler() const { return m_profilerWidget; }

    /// Updates the toolbar source badge label (`IDLE (NO BINARY)`, `STATIC ELF (READ-ONLY)`, `LIVE RANK ...`).
    void setSourceModeLabel(const QString& label);
    /// Clears hex dump viewport, buffers, patches, profiler state, and resets source badge to `IDLE (NO BINARY)`.
    void clear();

    /// Loads raw hex string data starting at beginAddress.
    void setMemoryData(qint64 beginAddress, const QString& hexContents);
    /// Loads binary byte array data at formatted address.
    void setMemoryData(const QString& address, const QByteArray& data);
    /// Highlights memory differences between two MPI ranks.
    void displayMemoryDiff(const gridlock::core::hpc::CompareResult& result, void* remoteAddr);

    /// Seeks memory dump viewport to 64-bit target address.
    bool seekToAddress(uint64_t address);
    /// Returns active 64-bit base memory address.
    uint64_t currentBaseAddress() const { return m_currentBaseAddress; }
    /// Returns number of modified bytes detected in the current view.
    int changedByteCount() const { return m_changedByteCount; }
    /// Applies live byte patches at specified virtual memory address.
    bool patchBytes(uint64_t address, const QByteArray& newBytes);
    /// Returns total number of actively patched bytes.
    int patchedByteCount() const { return m_patchedBytes.size(); }
    /// Reverts all actively applied live byte patches and restores baseline memory.
    void revertAllPatches();
    /// Returns the active canvas background QColor resolved from ThemeManager.
    QColor activeCanvasBackgroundColor() const;
    /// Returns the active non-zero hex byte text QColor resolved from ThemeManager.
    QColor activeHexTextColor() const;
    /// Reloads hex dump viewport and control header styles from ThemeManager.
    Q_INVOKABLE void reloadStyle();
    /// Toggles standalone floating window mode for MemView (Pop-Out / Dock Back).
    void setPoppedOut(bool poppedOut);
    /// Returns true when MemView is currently detached into its standalone top-level window.
    bool isPoppedOut() const { return m_isPoppedOut; }
    /// Resolves the 0-based byte offset in m_lastRawData corresponding to a QTextCursor inside the hex dump.
    int byteOffsetAtCursor(const QTextCursor& cursor) const;
    /// Resolves the 0-based byte offset in m_lastRawData under viewport coordinate @p pos.
    int byteOffsetAtPosition(const QPoint& pos) const;
    /// Formats the 64-bit (or 32-bit fallback) little-endian word starting at @p byteOffset as "0x...".
    QString wordHexAtByteOffset(int byteOffset, int* outBitWidth = nullptr) const;

protected:
    /// Paints the memory inspector container and synchronizes theme-dependent hex colors.
    void paintEvent(QPaintEvent* event) override;
    /// Intercepts floating window close events so closing the pop-out window docks MemView back cleanly.
    bool eventFilter(QObject* watched, QEvent* event) override;

signals:
    /// Emitted when bytes are patched at target memory address.
    void bytesPatched(uint64_t address, const QByteArray& newBytes);
    /// Emitted when user initiates a manual memory read.
    void requestMemory(const QString& address, int length);
    /// Emitted when user requests cross-rank memory difference comparison.
    void requestMemoryDiff(int baseRank, int targetRank, const QString& address, int length);
    /// Emitted when user selects "Open Address / Word in Programmer's Calculator" from the hex dump context menu.
    void sendToCalculatorRequested(const QString& text);
    /// Emitted when the user changes MemView's Base rank while unpinned.
    void rankSelected(int rank);

private slots:
    void onReadClicked();
    void onExportMatrixClicked();
    void onDiffClicked();

private:
    QString formatHexDump(qint64 startAddress, const QString& hexData);
    QString formatHexDumpHtml(uint64_t startAddress, const QByteArray& currentData, const QByteArray& prevData);
    void setHexControlsVisible(bool visible);
    static uint64_t parseHexAddress(const QString& address, bool* okOut = nullptr);

    QWidget* m_contentContainer = nullptr;
    QWidget* m_placeholderWidget = nullptr;
    QWidget* m_popOutWindow = nullptr;
    QLineEdit* m_addressEdit = nullptr;
    QSpinBox* m_lengthBox = nullptr;
    QPushButton* m_readBtn = nullptr;
    QPushButton* m_exportBtn = nullptr;
    QComboBox* m_typeBox = nullptr;
    QSpinBox* m_baselineRankBox = nullptr;
    QSpinBox* m_targetRankBox = nullptr;
    QPushButton* m_pinRankBtn = nullptr;
    QPushButton* m_diffBtn = nullptr;
    QPushButton* m_hexModeBtn = nullptr;
    QPushButton* m_mapModeBtn = nullptr;
    QPushButton* m_popOutBtn = nullptr;
    QLabel* m_sourceBadge = nullptr;
    QList<QWidget*> m_hexOnlyWidgets;
    QStackedWidget* m_viewStack = nullptr;
    QTextEdit* m_dumpEdit = nullptr;
    MemoryProfilerWidget* m_profilerWidget = nullptr;

    QByteArray m_lastRawData;
    QByteArray m_previousRawData;
    QByteArray m_unpatchedRawData;
    QMap<uint64_t, char> m_patchedBytes;
    uint64_t m_currentBaseAddress = 0ULL;
    uint64_t m_previousBaseAddress = 0ULL;
    int m_changedByteCount = 0;
    int m_focusedRank = 0;
    bool m_hasDiffBaseline = false;
    bool m_isPoppedOut = false;
    QString m_lastStyledThemeId;
    RankAddressResolver m_addressResolver{};
    RankBytesFetcher m_bytesFetcher{};
    RankPidGetter m_pidGetter{};
};

}  // namespace gridlock::ui
