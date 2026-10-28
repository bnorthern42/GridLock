/**
 * @file MemView.hpp
 * @brief Memory hex dump and differential inspection view widget.
 *
 * Provides synchronized hexadecimal and ASCII memory dumps, address seeking,
 * MPI rank memory diff overlays with changed-byte highlighting, and binary/matrix export.
 */

#pragma once
#include <QByteArray>
#include <QString>
#include <QWidget>
#include <cstdint>
#include "../../core/hpc/MemoryDiffer.hpp"

class QLineEdit;
class QSpinBox;
class QPushButton;
class QTextEdit;
class QComboBox;

namespace gridlock::ui {

/**
 * @brief Workstation memory inspector displaying hex dumps and cross-rank memory diffs.
 */
class MemView : public QWidget {
    Q_OBJECT
public:
    explicit MemView(QWidget* parent = nullptr);
    ~MemView() override = default;

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

signals:
    /// Emitted when user initiates a manual memory read.
    void requestMemory(const QString& address, int length);
    /// Emitted when user requests cross-rank memory difference comparison.
    void requestMemoryDiff(int baseRank, int targetRank, const QString& address, int length);

private slots:
    void onReadClicked();
    void onExportMatrixClicked();
    void onDiffClicked();

private:
    QString formatHexDump(qint64 startAddress, const QString& hexData);
    QString formatHexDumpHtml(uint64_t startAddress, const QByteArray& currentData, const QByteArray& prevData);
    static uint64_t parseHexAddress(const QString& address, bool* okOut = nullptr);

    QLineEdit* m_addressEdit = nullptr;
    QSpinBox* m_lengthBox = nullptr;
    QPushButton* m_readBtn = nullptr;
    QPushButton* m_exportBtn = nullptr;
    QComboBox* m_typeBox = nullptr;
    QSpinBox* m_baselineRankBox = nullptr;
    QSpinBox* m_targetRankBox = nullptr;
    QPushButton* m_diffBtn = nullptr;
    QTextEdit* m_dumpEdit = nullptr;

    QByteArray m_lastRawData;
    QByteArray m_previousRawData;
    uint64_t m_currentBaseAddress = 0x00401120ULL;
    uint64_t m_previousBaseAddress = 0ULL;
    int m_changedByteCount = 0;
    bool m_hasDiffBaseline = false;
};

}  // namespace gridlock::ui
