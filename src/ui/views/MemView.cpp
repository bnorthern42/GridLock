/**
 * @file MemView.cpp
 * @brief Memory inspection, hex dump display, and multi-rank diff viewer.
 *
 * Implements the MemView widget providing synchronized hex dumping,
 * memory editing, multi-rank differential comparisons, and data matrix
 * export capabilities for HPC and binary analysis workloads.
 */

#include "MemView.hpp"
#include "MemoryProfilerWidget.hpp"
#include "../../core/managers/ConfigManager.hpp"
#include "../../core/managers/ThemeManager.hpp"
#include "../../core/hpc/MemoryExporter.hpp"
#include "../../core/hpc/MemoryDiffer.hpp"
#include <QComboBox>
#include <QFileDialog>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPalette>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

namespace gridlock::ui {

/**
 * @brief Parses a string representation of a hex address into a 64-bit integer.
 *
 * Strips optional "0x" prefixes, trims whitespace, and stops at the first
 * space delimiter before parsing hexadecimal digits.
 *
 * @param address The input address string (e.g., "0x00401120").
 * @param okOut Optional pointer receiving true on successful conversion.
 * @return Parsed 64-bit address, or 0 on conversion failure.
 */
uint64_t MemView::parseHexAddress(const QString& address, bool* okOut) {
    // Trim leading/trailing whitespace
    QString trimmed = address.trimmed();

    bool isDecimal = false;
    if (trimmed.startsWith("0d", Qt::CaseInsensitive)) {
        trimmed = trimmed.mid(2);
        isDecimal = true;
    } else if (trimmed.startsWith("0x", Qt::CaseInsensitive)) {
        trimmed = trimmed.mid(2);
    }

    // Stop at any trailing comment or label offset separated by spaces
    int spaceIdx = trimmed.indexOf(' ');
    if (spaceIdx > 0) {
        trimmed = trimmed.left(spaceIdx);
    }

    // Convert hex string (or decimal) to 64-bit unsigned integer
    bool ok = false;
    uint64_t val = isDecimal ? trimmed.toULongLong(&ok, 10)
                             : trimmed.toULongLong(&ok, 16);
    if (!ok && !isDecimal) {
        val = trimmed.toULongLong(&ok, 10);
    }
    if (okOut) {
        *okOut = ok;
    }
    return ok ? val : 0ULL;
}

/**
 * @brief Constructs a new MemView widget.
 *
 * Initializes the compact toolbar controls (address, byte count, data type,
 * read/export/diff buttons, and `HEX DUMP | LIBRARY MEMORY MAP` mode toggles)
 * alongside the stacked hex dump editor and visual `MemoryProfilerWidget`.
 *
 * @param parent Optional parent widget.
 */
MemView::MemView(QWidget* parent) : QWidget(parent) {
    // Configure vertical root layout with zero margins for dock docking.
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Detachable container holding toolbar headerBar + m_viewStack for seamless Pop-Out / Dock Back.
    m_contentContainer = new QWidget(this);
    QVBoxLayout* containerLayout = new QVBoxLayout(m_contentContainer);
    containerLayout->setContentsMargins(0, 0, 0, 0);
    containerLayout->setSpacing(0);

    // Top mode/status bar: [HEX DUMP] [LIBRARY MEMORY MAP] <stretch> [Source Badge] [⤢ POP OUT].
    QWidget* headerBar = new QWidget(m_contentContainer);
    headerBar->setObjectName("cutterMemHeader");
    headerBar->setFixedHeight(28);
    QHBoxLayout* topLayout = new QHBoxLayout(headerBar);
    topLayout->setContentsMargins(6, 2, 6, 2);
    topLayout->setSpacing(6);

    // Helper lambda to measure button text and assign minimum width without truncation or overlap.
    auto configureToolbarButton = [](QPushButton* btn) {
        QFont boldFont = btn->font(); boldFont.setBold(true); QFontMetrics fm(boldFont);
        const int textW = std::max(fm.horizontalAdvance(btn->text()), fm.horizontalAdvance(btn->text().toUpper()));
        btn->setMinimumWidth(textW + 20); btn->setFixedHeight(22); btn->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    };

    // Segmented view-mode switcher toggling between raw HEX DUMP and visual LIBRARY MEMORY MAP.
    m_hexModeBtn = new QPushButton(QStringLiteral("HEX DUMP"), headerBar);
    m_hexModeBtn->setObjectName(QStringLiteral("memHexModeBtn"));
    m_hexModeBtn->setCheckable(true); m_hexModeBtn->setChecked(true);
    configureToolbarButton(m_hexModeBtn);
    topLayout->addWidget(m_hexModeBtn);

    m_mapModeBtn = new QPushButton(QStringLiteral("LIBRARY MEMORY MAP"), headerBar);
    m_mapModeBtn->setObjectName(QStringLiteral("memMapModeBtn"));
    m_mapModeBtn->setCheckable(true);
    configureToolbarButton(m_mapModeBtn);
    topLayout->addWidget(m_mapModeBtn);

    topLayout->addStretch(1);

    // Explicit source badge with Minimum horizontal policy so it never overlaps adjacent mode buttons.
    m_sourceBadge = new QLabel(QStringLiteral("IDLE (NO BINARY)"), headerBar);
    m_sourceBadge->setObjectName(QStringLiteral("memSourceBadge"));
    m_sourceBadge->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    m_sourceBadge->setFixedHeight(22);
    topLayout->addWidget(m_sourceBadge);

    // Pop-Out / Dock Back toggle button at the right edge of the toolbar.
    m_popOutBtn = new QPushButton(QStringLiteral("⤢ POP OUT"), headerBar);
    m_popOutBtn->setObjectName(QStringLiteral("memPopOutBtn"));
    configureToolbarButton(m_popOutBtn);
    topLayout->addWidget(m_popOutBtn);
    containerLayout->addWidget(headerBar);

    // Secondary Hex-Dump-only control bar (hidden automatically in LIBRARY MEMORY MAP mode).
    QWidget* hexBar = new QWidget(m_contentContainer);
    hexBar->setObjectName(QStringLiteral("cutterMemHexToolbar"));
    hexBar->setFixedHeight(26);
    QHBoxLayout* hexLayout = new QHBoxLayout(hexBar);
    hexLayout->setContentsMargins(6, 2, 6, 2);
    hexLayout->setSpacing(4);
    m_hexOnlyWidgets.append(hexBar);

    // Populate hex address, byte length, type interpretation, read/export, and cross-rank diff controls.
    hexLayout->addWidget(new QLabel("Addr:", hexBar));
    m_addressEdit = new QLineEdit(hexBar);
    m_addressEdit->setObjectName("cutterMemAddressEdit");
    m_addressEdit->setMinimumWidth(105); m_addressEdit->setFixedHeight(22);
    m_addressEdit->setPlaceholderText(QStringLiteral("0x00000000"));
    hexLayout->addWidget(m_addressEdit);

    hexLayout->addWidget(new QLabel("Bytes:", hexBar));
    m_lengthBox = new QSpinBox(hexBar);
    m_lengthBox->setRange(1, 4096); m_lengthBox->setValue(256);
    m_lengthBox->setMinimumWidth(68); m_lengthBox->setFixedHeight(22);
    hexLayout->addWidget(m_lengthBox);

    hexLayout->addWidget(new QLabel("Type:", hexBar));
    m_typeBox = new QComboBox(hexBar);
    m_typeBox->addItems({"Int32", "Float", "Double"});
    m_typeBox->setMinimumWidth(74); m_typeBox->setFixedHeight(22);
    hexLayout->addWidget(m_typeBox);

    m_readBtn = new QPushButton("Read", hexBar); configureToolbarButton(m_readBtn); hexLayout->addWidget(m_readBtn);
    m_exportBtn = new QPushButton("Export", hexBar); configureToolbarButton(m_exportBtn); hexLayout->addWidget(m_exportBtn);

    hexLayout->addWidget(new QLabel("Base:", hexBar));
    m_baselineRankBox = new QSpinBox(hexBar);
    m_baselineRankBox->setRange(0, 1000); m_baselineRankBox->setMinimumWidth(56); m_baselineRankBox->setFixedHeight(22);
    hexLayout->addWidget(m_baselineRankBox);

    hexLayout->addWidget(new QLabel("Target:", hexBar));
    m_targetRankBox = new QSpinBox(hexBar);
    m_targetRankBox->setRange(0, 1000); m_targetRankBox->setMinimumWidth(56); m_targetRankBox->setFixedHeight(22);
    hexLayout->addWidget(m_targetRankBox);

    m_diffBtn = new QPushButton("Diff Ranks", hexBar); configureToolbarButton(m_diffBtn); hexLayout->addWidget(m_diffBtn);
    hexLayout->addStretch(1);
    containerLayout->addWidget(hexBar);

    // Stacked container hosting page 0 (`m_dumpEdit`) and page 1 (`m_profilerWidget`).
    m_viewStack = new QStackedWidget(m_contentContainer);
    m_dumpEdit = new QTextEdit(m_viewStack);
    m_dumpEdit->setObjectName("cutterHexDumpEdit");
    m_dumpEdit->setReadOnly(true);
    m_dumpEdit->setFrameShape(QFrame::NoFrame);
    QFont font("JetBrains Mono");
    font.setStyleHint(QFont::Monospace);
    font.setPointSize(10);
    m_dumpEdit->setFont(font);

    QPalette p = m_dumpEdit->palette();
    p.setColor(QPalette::Base, QColor(gridlock::core::ConfigManager::instance().getSourceBackground()));
    p.setColor(QPalette::Text, QColor(gridlock::core::ConfigManager::instance().getSourceText()));
    m_dumpEdit->setPalette(p);

    m_profilerWidget = new MemoryProfilerWidget(m_viewStack);
    m_viewStack->addWidget(m_dumpEdit);
    m_viewStack->addWidget(m_profilerWidget);
    containerLayout->addWidget(m_viewStack, 1);
    mainLayout->addWidget(m_contentContainer, 1);

    // Docked placeholder shown while MemView is popped out into a standalone window.
    m_placeholderWidget = new QWidget(this);
    m_placeholderWidget->setVisible(false);
    auto* phLayout = new QVBoxLayout(m_placeholderWidget);
    phLayout->addStretch(1);
    auto* dockBackBtn = new QPushButton(QStringLiteral("Memory View is popped out — Click to Dock Back"), m_placeholderWidget);
    dockBackBtn->setObjectName(QStringLiteral("memDockBackPlaceholderBtn"));
    phLayout->addWidget(dockBackBtn, 0, Qt::AlignCenter);
    phLayout->addStretch(1);
    mainLayout->addWidget(m_placeholderWidget, 1);

    connect(m_readBtn, &QPushButton::clicked, this, &MemView::onReadClicked);
    connect(m_addressEdit, &QLineEdit::returnPressed, this, &MemView::onReadClicked);
    connect(m_exportBtn, &QPushButton::clicked, this, &MemView::onExportMatrixClicked);
    connect(m_diffBtn, &QPushButton::clicked, this, &MemView::onDiffClicked);
    connect(m_popOutBtn, &QPushButton::clicked, this, [this]() { setPoppedOut(!m_isPoppedOut); });
    connect(dockBackBtn, &QPushButton::clicked, this, [this]() { setPoppedOut(false); });

    // Wire mode toggle buttons and double-click hex navigation from the profiler treemap/table.
    connect(m_hexModeBtn, &QPushButton::clicked, this, [this]() {
        m_hexModeBtn->setChecked(true);
        if (m_mapModeBtn) m_mapModeBtn->setChecked(false);
        setHexControlsVisible(true);
        if (m_viewStack) m_viewStack->setCurrentIndex(0);
    });
    connect(m_mapModeBtn, &QPushButton::clicked, this, [this]() {
        m_mapModeBtn->setChecked(true);
        if (m_hexModeBtn) m_hexModeBtn->setChecked(false);
        setHexControlsVisible(false);
        if (m_viewStack) m_viewStack->setCurrentIndex(1);
        const int rank = m_baselineRankBox ? m_baselineRankBox->value() : 0;
        const qint64 pid = m_pidGetter ? m_pidGetter(rank) : 0LL;
        if (pid > 0 && m_profilerWidget) m_profilerWidget->refreshForPid(pid);
    });
    connect(m_profilerWidget, &MemoryProfilerWidget::inspectAddressRequested, this, [this](uint64_t addr) {
        if (m_hexModeBtn) m_hexModeBtn->setChecked(true);
        if (m_mapModeBtn) m_mapModeBtn->setChecked(false);
        setHexControlsVisible(true);
        if (m_viewStack) m_viewStack->setCurrentIndex(0);
        seekToAddress(addr);
    });

    // Initialize clean empty state until a real ELF binary or live debug rank supplies bytes.
    clear();
    reloadStyle();
    connect(&gridlock::core::ThemeManager::instance(),
            &gridlock::core::ThemeManager::themeModeChanged,
            this, [this](bool) { reloadStyle(); });
}

/** @brief Cleans up any detached pop-out window and reparents m_contentContainer safely. */
MemView::~MemView() {
    if (!m_popOutWindow) return;
    m_popOutWindow->removeEventFilter(this);
    if (m_isPoppedOut && m_contentContainer) {
        if (auto* l = m_popOutWindow->layout()) l->removeWidget(m_contentContainer);
        m_contentContainer->setParent(this);
    }
    delete m_popOutWindow; m_popOutWindow = nullptr;
}

void MemView::setLiveMemoryCallbacks(RankAddressResolver resolver,
                                     RankBytesFetcher fetcher,
                                     RankPidGetter pidGetter) {
    // Store coordinator-backed callbacks for live per-rank address evaluation and byte reads.
    m_addressResolver = std::move(resolver);
    m_bytesFetcher = std::move(fetcher);
    m_pidGetter = std::move(pidGetter);
    if (m_profilerWidget) m_profilerWidget->setAvailableRanks(1, 0, m_pidGetter);
}

bool MemView::isMemoryMapMode() const { return m_viewStack && m_viewStack->currentIndex() == 1; }

/** @brief Updates the profiler's rank selector and refreshes automatically when `LIBRARY MEMORY MAP` is active. */
void MemView::notifyRankStoppedOrSelected(int rank, qint64 pid, int rankCount) {
    if (pid > 0) setSourceModeLabel(QStringLiteral("LIVE RANK %1 (PID %2)").arg(rank).arg(pid));
    if (!m_profilerWidget) return;
    m_profilerWidget->setAvailableRanks(rankCount, rank, m_pidGetter);
    if (isMemoryMapMode() && (isVisible() || m_isPoppedOut) && pid > 0) m_profilerWidget->refreshForPid(pid);
}

/** @brief Shows or hides Hex-Dump-only toolbar controls when switching between HEX DUMP and LIBRARY MEMORY MAP. */
void MemView::setHexControlsVisible(bool visible) {
    for (QWidget* w : m_hexOnlyWidgets) if (w) w->setVisible(visible);
}

/** @brief Detaches or re-docks MemView's content container between the main dock and a standalone window. */
void MemView::setPoppedOut(bool poppedOut) {
    if (poppedOut == m_isPoppedOut || !m_contentContainer) return;
    m_isPoppedOut = poppedOut;
    auto* rootLayout = qobject_cast<QVBoxLayout*>(layout());
    if (poppedOut) {
        if (!m_popOutWindow) {
            // Create a standalone resizable window with minimize/maximize/close buttons and clean re-dock on close.
            m_popOutWindow = new QWidget(nullptr, Qt::Window | Qt::WindowMinimizeButtonHint | Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint);
            m_popOutWindow->setWindowTitle(QStringLiteral("GridLock — Memory View & Library Memory Map"));
            m_popOutWindow->resize(1280, 820);
            auto* winLayout = new QVBoxLayout(m_popOutWindow); winLayout->setContentsMargins(0, 0, 0, 0);
            m_popOutWindow->installEventFilter(this);
            connect(this, &QObject::destroyed, m_popOutWindow, &QObject::deleteLater);
        }
        // Move container from dock layout into floating window and show docked placeholder.
        if (rootLayout) rootLayout->removeWidget(m_contentContainer);
        if (m_placeholderWidget) m_placeholderWidget->setVisible(true);
        m_popOutWindow->layout()->addWidget(m_contentContainer); m_contentContainer->show();
        if (m_popOutBtn) { m_popOutBtn->setText(QStringLiteral("⤡ DOCK")); m_popOutBtn->setMinimumWidth(m_popOutBtn->fontMetrics().horizontalAdvance(QStringLiteral("⤡ DOCK")) + 24); }
        m_popOutWindow->show(); m_popOutWindow->raise(); m_popOutWindow->activateWindow();
    } else {
        // Restore container back into MemView's dock layout and hide floating window.
        if (m_popOutWindow) { m_popOutWindow->layout()->removeWidget(m_contentContainer); m_popOutWindow->hide(); }
        if (m_placeholderWidget) m_placeholderWidget->setVisible(false);
        if (rootLayout) rootLayout->insertWidget(0, m_contentContainer, 1);
        m_contentContainer->show();
        if (m_popOutBtn) { m_popOutBtn->setText(QStringLiteral("⤢ POP OUT")); m_popOutBtn->setMinimumWidth(m_popOutBtn->fontMetrics().horizontalAdvance(QStringLiteral("⤢ POP OUT")) + 24); }
    }
}

/** @brief Re-docks MemView when the user closes the standalone pop-out window. */
bool MemView::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_popOutWindow && event && event->type() == QEvent::Close) {
        event->ignore();
        setPoppedOut(false);
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

/**
 * @brief Updates the toolbar source indicator badge (`IDLE (NO BINARY)`, `STATIC ELF`, `LIVE RANK`).
 * @param label Human-readable data source badge string.
 */
void MemView::setSourceModeLabel(const QString& label) {
    if (!m_sourceBadge) return;
    m_sourceBadge->setText(label);
    const auto pal = gridlock::core::ThemeManager::instance().paletteForTheme(
        gridlock::core::ThemeManager::instance().activeThemeId());
    const bool isLive = label.startsWith(QStringLiteral("LIVE RANK"), Qt::CaseInsensitive);
    const bool isStatic = label.startsWith(QStringLiteral("STATIC ELF"), Qt::CaseInsensitive);
    const QString fg = isLive ? QStringLiteral("#4ec9b0") : (isStatic ? QStringLiteral("#61afef") : pal.textHex);
    m_sourceBadge->setStyleSheet(QStringLiteral(
        "QLabel#memSourceBadge { color: %1; border: 1px solid %2; border-radius: 2px; "
        "padding: 1px 6px; font-family: 'JetBrains Mono', monospace; font-size: 10px; font-weight: 700; }")
        .arg(fg, pal.borderHex));
}

/**
 * @brief Clears hex dump buffers, profiler state, and resets base address and source badge to idle state.
 */
void MemView::clear() {
    m_currentBaseAddress = 0ULL;
    m_previousBaseAddress = 0ULL;
    m_changedByteCount = 0;
    m_hasDiffBaseline = false;
    m_lastRawData.clear();
    m_previousRawData.clear();
    m_unpatchedRawData.clear();
    m_patchedBytes.clear();
    if (m_addressEdit) m_addressEdit->clear();
    if (m_dumpEdit) m_dumpEdit->clear();
    if (m_profilerWidget) m_profilerWidget->clear();
    setSourceModeLabel(QStringLiteral("IDLE (NO BINARY)"));
}

/**
 * @brief Seeks the memory view to a specific 64-bit memory address.
 *
 * Updates the address text box and queries live target memory (or emits `requestMemory`).
 *
 * @param address The target 64-bit address.
 * @return True if the address was non-zero and seek was dispatched; false otherwise.
 */
bool MemView::seekToAddress(uint64_t address) {
    if (address == 0ULL) {
        return false;
    }
    m_currentBaseAddress = address;
    const QString formatted = QStringLiteral("0x%1").arg(address, 8, 16, QChar('0'));
    if (m_addressEdit) {
        m_addressEdit->setText(formatted);
    }
    const int rank = m_baselineRankBox ? m_baselineRankBox->value() : 0;
    const int len = m_lengthBox ? m_lengthBox->value() : 256;
    const qint64 pid = m_pidGetter ? m_pidGetter(rank) : 0LL;
    if (pid > 0 && m_bytesFetcher) {
        const QByteArray liveBytes = m_bytesFetcher(rank, address, len);
        if (!liveBytes.isEmpty()) {
            setMemoryData(static_cast<qint64>(address), QString::fromLatin1(liveBytes.toHex()));
            setSourceModeLabel(QStringLiteral("LIVE RANK %1 (PID %2)").arg(rank).arg(pid));
            return true;
        }
    }
    // Dispatch memory read request for offline ELF or active debug listeners.
    emit requestMemory(formatted, len);
    return true;
}

/**
 * @brief Handles user clicks on the Read button or pressing Enter in the address input.
 *
 * Resolves hex literals or symbolic/register expressions (`$rsp`) for the selected rank and
 * reads live bytes via `NativeMemoryReader` (`process_vm_readv`) with GDB/DAP fallback.
 */
void MemView::onReadClicked() {
    const QString addr = m_addressEdit ? m_addressEdit->text().trimmed() : QString();
    if (addr.isEmpty()) return;
    const int rank = m_baselineRankBox ? m_baselineRankBox->value() : 0;
    const int len = m_lengthBox ? m_lengthBox->value() : 256;

    // Resolve concrete 64-bit address via live rank callback first, falling back to hex parser.
    bool ok = false;
    uint64_t resolved = 0ULL;
    if (m_addressResolver) resolved = m_addressResolver(rank, addr, &ok);
    if (!ok || resolved == 0ULL) resolved = parseHexAddress(addr, &ok);
    if (ok && resolved != 0ULL) m_currentBaseAddress = resolved;

    // Read directly from the live inferior rank process when a debug session is active.
    const qint64 pid = m_pidGetter ? m_pidGetter(rank) : 0LL;
    if (ok && resolved != 0ULL && pid > 0 && m_bytesFetcher) {
        const QByteArray liveBytes = m_bytesFetcher(rank, resolved, len);
        if (!liveBytes.isEmpty()) {
            bool isLiteralHex = false;
            (void)parseHexAddress(addr, &isLiteralHex);
            setMemoryData(static_cast<qint64>(resolved), QString::fromLatin1(liveBytes.toHex()));
            setSourceModeLabel(QStringLiteral("LIVE RANK %1 (PID %2)").arg(rank).arg(pid));
            // Preserve symbolic expressions (e.g., `$rsp`) in the input box for cross-rank diffing.
            if (!isLiteralHex && m_addressEdit) m_addressEdit->setText(addr);
            return;
        }
    }
    emit requestMemory(addr, len);
}

/**
 * @brief Populates the hex dump display with raw hexadecimal memory contents.
 *
 * Compares incoming bytes against the previous snapshot to detect mutated
 * bytes, highlights changed values in red, and updates the HTML render.
 *
 * @param beginAddress 64-bit base address of the memory slice.
 * @param hexContents Hex-encoded string of byte values.
 */
void MemView::setMemoryData(qint64 beginAddress, const QString& hexContents) {
    const uint64_t startAddr = static_cast<uint64_t>(beginAddress);
    const QByteArray currentData = QByteArray::fromHex(hexContents.toLatin1());

    m_changedByteCount = 0;
    QByteArray prevForDiff;

    // Detect changed bytes if a previous snapshot at the same address exists
    if (m_hasDiffBaseline && m_previousBaseAddress == startAddr) {
        prevForDiff = m_previousRawData;
        const int compareLen = qMin(currentData.size(), m_previousRawData.size());
        for (int i = 0; i < compareLen; ++i) {
            if (currentData[i] != m_previousRawData[i]) {
                m_changedByteCount++;
            }
        }
        // Account for any difference in slice length
        m_changedByteCount += qAbs(currentData.size() - m_previousRawData.size());
    }

    // Cache current state as new diff baseline
    m_currentBaseAddress = startAddr;
    m_previousBaseAddress = startAddr;
    m_previousRawData = currentData;
    m_lastRawData = currentData;
    m_unpatchedRawData = currentData;
    m_hasDiffBaseline = true;

    if (m_addressEdit) {
        m_addressEdit->setText(QStringLiteral("0x%1").arg(startAddr, 8, 16, QChar('0')));
    }

    // Render formatted monospace HTML with differential color tags
    if (m_dumpEdit) {
        m_dumpEdit->setHtml(formatHexDumpHtml(startAddr, currentData, prevForDiff));
    }
}

/**
 * @brief Applies live byte patches at specified virtual memory address.
 * @param address 64-bit target memory address.
 * @param newBytes Byte array to write.
 * @return True if byte payload was non-empty and recorded; false otherwise.
 */
bool MemView::patchBytes(uint64_t address, const QByteArray& newBytes) {
    if (newBytes.isEmpty()) {
        return false;
    }
    // Record each byte into active patch map
    for (int i = 0; i < newBytes.size(); ++i) {
        m_patchedBytes.insert(address + static_cast<uint64_t>(i), newBytes.at(i));
    }
    // If target address falls within current viewport slice, update local buffer
    if (!m_lastRawData.isEmpty() && address >= m_currentBaseAddress &&
        address < m_currentBaseAddress + static_cast<uint64_t>(m_lastRawData.size())) {
        const uint64_t offset = address - m_currentBaseAddress;
        const int copyLen = qMin(newBytes.size(), static_cast<int>(m_lastRawData.size() - offset));
        for (int i = 0; i < copyLen; ++i) {
            m_lastRawData[static_cast<qsizetype>(offset + i)] = newBytes.at(i);
        }
        if (m_dumpEdit) {
            m_dumpEdit->setHtml(formatHexDumpHtml(m_currentBaseAddress, m_lastRawData, m_previousRawData));
        }
    }
    emit bytesPatched(address, newBytes);
    return true;
}

/**
 * @brief Reverts all actively applied live byte patches and restores baseline memory buffer.
 */
void MemView::revertAllPatches() {
    m_patchedBytes.clear();
    if (!m_unpatchedRawData.isEmpty()) {
        m_lastRawData = m_unpatchedRawData;
        if (m_dumpEdit) {
            m_dumpEdit->setHtml(formatHexDumpHtml(m_currentBaseAddress, m_lastRawData, m_previousRawData));
        }
    }
}

/**
 * @brief Overloaded helper to set memory data from raw byte arrays.
 *
 * @param address Address string (e.g. "0x00401120").
 * @param data Raw byte array.
 */
void MemView::setMemoryData(const QString& address, const QByteArray& data) {
    bool ok = false;
    uint64_t startAddr = parseHexAddress(address, &ok);
    if (!ok) {
        startAddr = m_currentBaseAddress;
    }
    setMemoryData(static_cast<qint64>(startAddr), QString::fromLatin1(data.toHex()));
}

/**
 * @brief Exports currently cached memory buffer to disk as CSV or raw binary.
 *
 * Prompts user for a save file path and invokes MemoryExporter asynchronously.
 */
void MemView::onExportMatrixClicked() {
    if (m_lastRawData.isEmpty()) {
        QMessageBox::warning(this, "No Data", "No memory data available to export. Please read memory first.");
        return;
    }

    QString fileName = QFileDialog::getSaveFileName(this, "Export Memory Matrix", "", "CSV Files (*.csv);;Binary Files (*.bin);;All Files (*)");
    if (fileName.isEmpty()) return;

    std::vector<uint8_t> buffer(m_lastRawData.begin(), m_lastRawData.end());
    int typeIndex = m_typeBox->currentIndex();

    // Run file export in background thread pool to avoid blocking the GUI
    (void)QtConcurrent::run([buffer, fileName, typeIndex]() {
        if (fileName.endsWith(".csv", Qt::CaseInsensitive)) {
            gridlock::core::hpc::DataType type = gridlock::core::hpc::DataType::Int32;
            int typeSize = sizeof(int32_t);
            
            if (typeIndex == 1) {
                type = gridlock::core::hpc::DataType::Float;
                typeSize = sizeof(float);
            } else if (typeIndex == 2) {
                type = gridlock::core::hpc::DataType::Double;
                typeSize = sizeof(double);
            }
            
            int cols = buffer.size() / typeSize;
            if (cols == 0) cols = 1;
            gridlock::core::hpc::MemoryExporter::exportToCsv(fileName.toStdString(), buffer, 1, cols, type);
        } else {
            gridlock::core::hpc::MemoryExporter::exportToBinary(fileName.toStdString(), buffer);
        }
    });
}

/**
 * @brief Triggers a cross-rank memory differential analysis (`MemoryDiffer` + debugger fallback).
 *
 * Resolves hex or symbolic addresses (`$rsp`) per rank, reads live bytes from `Base` and `Target`
 * OS PIDs via `MemoryDiffer::compareMemory` (`process_vm_readv`) with automatic fallback to
 * debugger memory reads when `yama/ptrace_scope` or per-rank ASLR stack bases apply.
 */
void MemView::onDiffClicked() {
    const int baselineRank = m_baselineRankBox->value();
    const int targetRank = m_targetRankBox->value();
    const QString addrStr = m_addressEdit->text().trimmed();
    const int length = m_lengthBox->value();

    bool okA = false, okB = false;
    uint64_t addrA = m_addressResolver ? m_addressResolver(baselineRank, addrStr, &okA) : 0ULL;
    uint64_t addrB = m_addressResolver ? m_addressResolver(targetRank, addrStr, &okB) : 0ULL;
    if (!okA || addrA == 0ULL) addrA = parseHexAddress(addrStr, &okA);
    if (!okB || addrB == 0ULL) addrB = parseHexAddress(addrStr, &okB);
    if (!okA || !okB) {
        QMessageBox::warning(this, "Invalid Address", "Please enter a valid hex address or register expression.");
        return;
    }

    // Perform live cross-rank comparison when live inferior OS PIDs are attached.
    const qint64 pidA = m_pidGetter ? m_pidGetter(baselineRank) : 0;
    const qint64 pidB = m_pidGetter ? m_pidGetter(targetRank) : 0;
    if ((pidA > 0 || pidB > 0) && m_bytesFetcher) {
        gridlock::core::hpc::CompareResult res;
        // Fast path: direct `process_vm_readv` cross-PID comparison when virtual addresses match.
        if (pidA > 0 && pidB > 0 && addrA == addrB) {
            res = gridlock::core::hpc::MemoryDiffer::compareMemory(
                static_cast<pid_t>(pidA), static_cast<pid_t>(pidB),
                reinterpret_cast<void*>(addrA), static_cast<size_t>(length));
        }
        // Fallback path: per-rank `NativeMemoryReader` / GDB `-data-read-memory-bytes` / DAP `readMemory`.
        if (!res.success) {
            const QByteArray bytesA = m_bytesFetcher(baselineRank, addrA, length);
            const QByteArray bytesB = m_bytesFetcher(targetRank, addrB, length);
            const int n = qMin(bytesA.size(), bytesB.size());
            if (n > 0) {
                res.baselineBuffer.assign(bytesA.constBegin(), bytesA.constBegin() + n);
                res.targetBuffer.assign(bytesB.constBegin(), bytesB.constBegin() + n);
                res.diffMask.resize(static_cast<size_t>(n), false);
                for (int i = 0; i < n; ++i) res.diffMask[static_cast<size_t>(i)] = (bytesA[i] != bytesB[i]);
                res.success = true;
            }
        }
        if (res.success) {
            m_currentBaseAddress = addrA;
            displayMemoryDiff(res, reinterpret_cast<void*>(addrA));
            return;
        }
    }

    emit requestMemoryDiff(baselineRank, targetRank, addrStr, length);
}

/**
 * @brief Renders the visual diff output between two MPI ranks into the hex editor.
 *
 * Highlights divergence between baseline and target memory buffers with red spans
 * and tooltips indicating baseline values.
 *
 * @param result The differential comparison result structure.
 * @param remoteAddr Base pointer of the compared memory slice.
 */
void MemView::displayMemoryDiff(const gridlock::core::hpc::CompareResult& result, void* remoteAddr) {
    if (!result.success) {
        QMessageBox::warning(this, "Diff Failed", "Failed to read memory from one or both PIDs.");
        return;
    }
    
    QString html = "<pre style=\"font-family: 'JetBrains Mono', monospace; font-size: 10pt; margin: 0;\">";
    qint64 startAddress = reinterpret_cast<qint64>(remoteAddr);
    
    const bool isDark = gridlock::core::ThemeManager::instance().isDarkMode();
    const QString offsetHex = isDark ? QStringLiteral("#6a9955") : QStringLiteral("#0550ae");
    const QString zeroHex = isDark ? QStringLiteral("#555c66") : QStringLiteral("#6e7781");
    const QString nonZeroHex = isDark ? QStringLiteral("#d4d7dc") : QStringLiteral("#1f2328");
    const QString asciiHex = isDark ? QStringLiteral("#4ec9b0") : QStringLiteral("#1f2328");

    int byteCount = result.baselineBuffer.size();
    m_changedByteCount = 0;
    // Iterate in 16-byte chunks representing traditional hex dump rows
    for (int i = 0; i < byteCount; i += 16) {
        html += QString("<span style=\"color: %1;\">0x%2</span>  ")
                    .arg(offsetHex,
                         QString::number((quint64)(startAddress + i), 16).rightJustified(16, '0').toUpper());
        
        QString asciiPart = "| ";
        for (int j = 0; j < 16; ++j) {
            if (i + j < byteCount) {
                uint8_t baseByte = result.baselineBuffer[i+j];
                uint8_t targetByte = result.targetBuffer[i+j];
                bool diff = result.diffMask[i+j];
                if (diff) {
                    m_changedByteCount++;
                }
                
                QString byteStr = QString::number(targetByte, 16).rightJustified(2, '0').toUpper();
                
                // Color mutated bytes in red with tooltip, muted zeros in steel (#6e7781), non-zero in #1f2328
                if (diff) {
                    QString tooltip = QString("Baseline: 0x%1 | Target: 0x%2")
                        .arg(QString::number(baseByte, 16).rightJustified(2, '0').toUpper())
                        .arg(byteStr);
                    html += QString("<span style=\"color: #f44747; font-weight: bold;\" title=\"%1\">%2</span> ")
                        .arg(tooltip).arg(byteStr);
                } else if (targetByte == 0) {
                    html += QString("<span style=\"color: %1;\">%2</span> ").arg(zeroHex, byteStr);
                } else {
                    html += QString("<span style=\"color: %1; font-weight: 500;\">%2</span> ").arg(nonZeroHex, byteStr);
                }
                
                // Printable ASCII representation
                char c = static_cast<char>(targetByte);
                if (c >= 32 && c <= 126) {
                    if (diff) {
                        asciiPart += QString("<span style=\"color: #f44747; font-weight: bold;\">%1</span>").arg(QString(c).toHtmlEscaped());
                    } else {
                        asciiPart += QString("<span style=\"color: %1;\">%2</span>").arg(asciiHex, QString(c).toHtmlEscaped());
                    }
                } else {
                    asciiPart += QString("<span style=\"color: %1;\">.</span>").arg(zeroHex);
                }
            } else {
                html += "   ";
            }
        }
        html += " " + asciiPart + " |<br>";
    }
    html += "</pre>";
    
    m_dumpEdit->setHtml(html);
    m_lastRawData = QByteArray(reinterpret_cast<const char*>(result.targetBuffer.data()), result.targetBuffer.size());
}

/**
 * @brief Generates HTML-formatted hex dump with offset headers, syntax colors, and mutation diffs.
 *
 * @param startAddress 64-bit base offset for the first row.
 * @param currentData Current byte array contents.
 * @param prevData Previous snapshot byte array used for mutation highlighting.
 * @return HTML string ready for display in QTextEdit.
 */
QString MemView::formatHexDumpHtml(uint64_t startAddress, const QByteArray& currentData, const QByteArray& prevData) {
    const auto& tm = gridlock::core::ThemeManager::instance();
    const auto pal = tm.paletteForTheme(tm.activeThemeId());
    const bool isDark = pal.isDark;
    const QString headerColor = isDark ? QStringLiteral("#858585") : QStringLiteral("#57606a");
    const QString offsetColor = isDark ? QStringLiteral("#6a9955") : QStringLiteral("#0550ae");
    const QString zeroColor = isDark ? QStringLiteral("#555c66") : QStringLiteral("#6e7781");
    const QString hexColor = isDark ? QStringLiteral("#d4d7dc") : QStringLiteral("#1f2328");
    const QString asciiColor = isDark ? QStringLiteral("#4ec9b0") : QStringLiteral("#1f2328");
    const QString patchColor = isDark ? QStringLiteral("#e5c07b") : QStringLiteral("#9a6700");

    // Monospace header with offset markers 00 through 0F
    QString html = QStringLiteral(
        "<pre style=\"font-family: 'JetBrains Mono', monospace; font-size: 10pt; margin: 4px;\">"
        "<span style=\"color: %1;\">- offset -         00 01 02 03 04 05 06 07  08 09 0A 0B 0C 0D 0E 0F  |0123456789ABCDEF|</span>\n")
        .arg(headerColor);

    const int byteCount = currentData.size();
    // Format 16 bytes per line
    for (int i = 0; i < byteCount; i += 16) {
        // Line address prefix in muted green (dark mode) or deep blue (#0550ae in light mode)
        html += QStringLiteral("<span style=\"color: %1;\">0x%2</span> ")
                    .arg(offsetColor)
                    .arg(startAddress + static_cast<uint64_t>(i), 16, 16, QChar('0'));

        QString asciiPart = QStringLiteral("|");
        for (int j = 0; j < 16; ++j) {
            // Extra spacing at 8-byte boundary for readability
            if (j == 8) {
                html += QStringLiteral(" ");
            }
            const int idx = i + j;
            if (idx < byteCount) {
                const uint64_t byteAddr = startAddress + static_cast<uint64_t>(idx);
                const bool isPatched = m_patchedBytes.contains(byteAddr);
                const uint8_t b = static_cast<uint8_t>(currentData[idx]);
                // Check if byte value mutated compared to previous snapshot
                const bool mutated = !prevData.isEmpty() && idx < prevData.size() &&
                                     static_cast<uint8_t>(prevData[idx]) != b;
                const QString byteStr = QStringLiteral("%1").arg(b, 2, 16, QChar('0')).toUpper();

                if (isPatched) {
                    html += QStringLiteral("<span style=\"color: %1; font-weight: bold;\">%2</span> ").arg(patchColor, byteStr);
                } else if (mutated) {
                    html += QStringLiteral("<span style=\"color: #f44747; font-weight: bold;\">%1</span> ").arg(byteStr);
                } else if (b == 0x00) {
                    html += QStringLiteral("<span style=\"color: %1;\">%2</span> ").arg(zeroColor, byteStr);
                } else {
                    html += isDark
                                ? QStringLiteral("<span style=\"color: %1;\">%2</span> ").arg(hexColor, byteStr)
                                : QStringLiteral("<span style=\"color: %1; font-weight: 500;\">%2</span> ").arg(hexColor, byteStr);
                }

                // Append printable character or dot for non-printables
                const char c = static_cast<char>(b);
                if (c >= 32 && c <= 126) {
                    const QString escaped = QString(QChar(c)).toHtmlEscaped();
                    if (isPatched) {
                        asciiPart += QStringLiteral("<span style=\"color: %1; font-weight: bold;\">%2</span>").arg(patchColor, escaped);
                    } else if (mutated) {
                        asciiPart += QStringLiteral("<span style=\"color: #f44747; font-weight: bold;\">%1</span>").arg(escaped);
                    } else {
                        asciiPart += QStringLiteral("<span style=\"color: %1;\">%2</span>").arg(asciiColor, escaped);
                    }
                } else {
                    if (isPatched) {
                        asciiPart += QStringLiteral("<span style=\"color: %1;\">.</span>").arg(patchColor);
                    } else {
                        asciiPart += QStringLiteral("<span style=\"color: %1;\">.</span>").arg(zeroColor);
                    }
                }
            } else {
                html += QStringLiteral("   ");
                asciiPart += QStringLiteral(" ");
            }
        }
        asciiPart += QStringLiteral("|");
        html += QStringLiteral(" ") + asciiPart + QStringLiteral("\n");
    }

    html += QStringLiteral("</pre>");
    return html;
}

/**
 * @brief Generates a plain-text hex dump representation of hexadecimal contents.
 *
 * @param startAddress 64-bit base offset.
 * @param hexData String containing raw hex characters.
 * @return Plain text hex dump formatted with offsets and ASCII column.
 */
QString MemView::formatHexDump(qint64 startAddress, const QString& hexData) {
    QString result;
    int dataLen = hexData.length();
    int byteCount = dataLen / 2;
    
    // Group hex digits into 16-byte lines
    for (int i = 0; i < byteCount; i += 16) {
        result += QString("0x%1: ").arg(QString::number((quint64)(startAddress + i), 16).rightJustified(16, '0').toUpper());
        
        QString asciiPart = "| ";
        for (int j = 0; j < 16; ++j) {
            if (i + j < byteCount) {
                QString byteStr = hexData.mid((i + j) * 2, 2);
                result += byteStr.toUpper() + " ";
                
                bool ok;
                char c = byteStr.toInt(&ok, 16);
                if (c >= 32 && c <= 126) {
                    asciiPart += c;
                } else {
                    asciiPart += '.';
                }
            } else {
                result += "   ";
            }
        }
        
        asciiPart += " |";
        result += asciiPart + "\n";
    }
    
    return result;
}

/**
 * @brief Returns active canvas background QColor resolved from ThemeManager.
 */
QColor MemView::activeCanvasBackgroundColor() const {
    if (m_dumpEdit) {
        return m_dumpEdit->palette().color(QPalette::Base);
    }
    const auto pal = gridlock::core::ThemeManager::instance().paletteForTheme(
        gridlock::core::ThemeManager::instance().activeThemeId());
    return QColor(pal.canvasHex);
}

/**
 * @brief Returns the active non-zero hex byte text QColor resolved from ThemeManager.
 * @return Dark slate (#1f2328) in light themes, or crisp light gray (#d4d7dc) in dark themes.
 */
QColor MemView::activeHexTextColor() const {
    const bool isDark = gridlock::core::ThemeManager::instance().isDarkMode();
    return isDark ? QColor(QStringLiteral("#d4d7dc")) : QColor(QStringLiteral("#1f2328"));
}

/**
 * @brief Paints the MemView container and ensures light/dark hexdump colors stay synchronized.
 * @param event Paint event payload.
 */
void MemView::paintEvent(QPaintEvent* event) {
    const QString activeTheme = gridlock::core::ThemeManager::instance().activeThemeId();
    if (m_lastStyledThemeId != activeTheme) {
        reloadStyle();
    }
    QWidget::paintEvent(event);
}

/**
 * @brief Reloads hex dump viewport and control header styles from ThemeManager.
 */
void MemView::reloadStyle() {
    auto& tm = gridlock::core::ThemeManager::instance();
    m_lastStyledThemeId = tm.activeThemeId();
    const auto pal = tm.paletteForTheme(m_lastStyledThemeId);
    if (m_dumpEdit) {
        QPalette p = m_dumpEdit->palette();
        p.setColor(QPalette::Base, QColor(pal.canvasHex));
        p.setColor(QPalette::Window, QColor(pal.canvasHex));
        p.setColor(QPalette::Text, activeHexTextColor());
        m_dumpEdit->setPalette(p);
        m_dumpEdit->setStyleSheet(QStringLiteral(
            "QTextEdit#cutterHexDumpEdit { background-color: %1; color: %2; border: none; }")
            .arg(pal.canvasHex, activeHexTextColor().name()));
        if (m_dumpEdit->viewport()) {
            QPalette vp = m_dumpEdit->viewport()->palette();
            vp.setColor(QPalette::Base, QColor(pal.canvasHex));
            vp.setColor(QPalette::Window, QColor(pal.canvasHex));
            vp.setColor(QPalette::Text, activeHexTextColor());
            m_dumpEdit->viewport()->setPalette(vp);
            m_dumpEdit->viewport()->setAutoFillBackground(true);
        }
        if (!m_lastRawData.isEmpty()) {
            m_dumpEdit->setHtml(formatHexDumpHtml(m_currentBaseAddress, m_lastRawData, m_previousRawData));
        }
    }
    if (QWidget* headerBar = findChild<QWidget*>(QStringLiteral("cutterMemHeader"))) {
        headerBar->setStyleSheet(QStringLiteral(
            "#cutterMemHeader { background-color: %1; border-bottom: 1px solid %2; } "
            "#cutterMemHeader QLabel { color: %3; font-size: 11px; font-weight: 600; } "
            "#cutterMemHeader QLineEdit, #cutterMemHeader QSpinBox, #cutterMemHeader QComboBox { "
            "min-height: 20px; max-height: 22px; padding: 0px 4px; margin: 0px; background-color: %4; color: %3; "
            "border: 1px solid %2; border-radius: 2px; } "
            "#cutterMemHeader QPushButton { padding: 1px 8px; min-height: 20px; max-height: 22px; "
            "background-color: %1; color: %3; border: 1px solid %2; border-radius: 2px; font-size: 10px; font-weight: 600; } "
            "#cutterMemHeader QPushButton:hover { background-color: %2; color: %3; } "
            "#cutterMemHeader QPushButton:checked { background-color: %5; color: #ffffff; border: 1px solid %5; }")
            .arg(pal.headerHex, pal.borderHex, pal.textHex, pal.canvasHex, pal.accentHex));
    }
    if (m_sourceBadge) {
        setSourceModeLabel(m_sourceBadge->text());
    }
    if (m_profilerWidget) {
        m_profilerWidget->reloadStyle();
    }
    update();
}

}  // namespace gridlock::ui

