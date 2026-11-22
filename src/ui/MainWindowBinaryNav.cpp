/**
 * @file MainWindowBinaryNav.cpp
 * @brief Address-space ribbon bar, address seek/history routing, disassembly/decompiler sync, and symbol jumps.
 *
 * Implements the interactive binary section AddressSpaceNavBar widget, cross-pane
 * seekToAddressOrExpression() synchronization and seek history recording,
 * disassembly/CFG/decompiler pane visibility rules, and F12 Go-to-Definition dispatch.
 */

#include "MainWindow.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <utility>
#include <vector>

#include <QInputDialog>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QShortcut>
#include <QTextCursor>
#include <QTextDocument>
#include <QVariant>

#include "../core/managers/ConfigManager.hpp"
#include "../core/utils/ElfBinaryInspector.hpp"
#include "EditorTabManager.hpp"
#include "views/DisassemblyView.hpp"
#include "views/MemView.hpp"
#include "views/SourceCodeView.hpp"
#include "widgets/ProjectExplorerWidget.hpp"

namespace gridlock::ui {

namespace {

/**
 * @class AddressSpaceNavBar
 * @brief Interactive visual ribbon depicting memory and binary section segments.
 *
 * Visualizes .plt, .text, .rodata, .data, and stack sections with proportional
 * weights, highlighting the active instruction pointer / seek address marker.
 */
class AddressSpaceNavBar : public QWidget {
public:
    /** @brief Describes a discrete memory region with address bounds and color accents. */
    struct Segment {
        QString name;
        uint64_t baseAddr;
        uint64_t endAddr;
        int weight;
        QColor fillColor;
        QColor textColor;
    };

    /**
     * @brief Constructs the navigation ribbon with standard ELF sections.
     * @param onSeek Callback invoked when user clicks a memory segment.
     * @param parent Optional parent QWidget.
     */
    explicit AddressSpaceNavBar(std::function<void(uint64_t)> onSeek, QWidget* parent = nullptr)
        : QWidget(parent), m_onSeek(std::move(onSeek)) {
        setObjectName("addressNavBar");
        setFixedHeight(20);
        setCursor(Qt::PointingHandCursor);
        // Pre-configure standard binary section topology for x86_64 executables.
        m_segments = {
            {QStringLiteral(".plt 0x401000"), 0x00401000ULL, 0x0040111fULL, 14, QColor("#2c2238"), QColor("#c586c0")},
            {QStringLiteral(".text 0x401120 (sym.main)"), 0x00401120ULL, 0x00401fffULL, 34, QColor("#1e3038"), QColor("#4ec9b0")},
            {QStringLiteral(".rodata 0x402000"), 0x00402000ULL, 0x00403fffULL, 18, QColor("#1e2836"), QColor("#569cd6")},
            {QStringLiteral(".data 0x404000"), 0x00404000ULL, 0x00405fffULL, 16, QColor("#2f291e"), QColor("#dcdcaa")},
            {QStringLiteral("stack 0x7fffffffe000"), 0x7fffffffe000ULL, 0x7fffffffffffULL, 18, QColor("#332222"), QColor("#ce9178")},
        };
    }

    /**
     * @brief Updates the active instruction pointer / seek address indicator.
     * @param addr Memory address in hex.
     */
    void setCurrentAddress(uint64_t addr) {
        if (addr != 0ULL) {
            m_currentAddr = addr;
            setProperty("currentAddress", QVariant::fromValue<qulonglong>(addr));
            update();
        }
    }

protected:
    /** @brief Renders proportional segment blocks and gold position needles. */
    void paintEvent(QPaintEvent* /*event*/) override {
        const QVariant prop = property("currentAddress");
        if (prop.isValid()) {
            const uint64_t propAddr = prop.toULongLong();
            if (propAddr != 0ULL) m_currentAddr = propAddr;
        }
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, false);
        p.fillRect(rect(), QColor("#141619"));

        int totalWeight = 0;
        for (const auto& seg : m_segments) totalWeight += seg.weight;
        if (totalWeight <= 0) return;

        const int w = width();
        const int h = height();
        int x = 0;
        int activeCursorX = -1;

        QFont segFont("JetBrains Mono", 8, QFont::DemiBold);
        segFont.setStyleHint(QFont::Monospace);
        p.setFont(segFont);

        // Iterate over memory segments and draw tinted rectangular bounding boxes.
        for (size_t i = 0; i < m_segments.size(); ++i) {
            const auto& seg = m_segments[i];
            const int segW = (i + 1 == m_segments.size()) ? (w - x) : (w * seg.weight) / totalWeight;
            QRect segRect(x, 1, segW - 1, h - 2);
            p.fillRect(segRect, seg.fillColor);
            p.setPen(QColor("#2b2b2b"));
            p.drawRect(segRect.adjusted(0, 0, -1, -1));

            p.setPen(seg.textColor);
            p.drawText(segRect.adjusted(6, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft, seg.name);

            if (m_currentAddr >= seg.baseAddr && m_currentAddr <= seg.endAddr) {
                double ratio = 0.25;
                if (seg.endAddr > seg.baseAddr) {
                    ratio = std::clamp(static_cast<double>(m_currentAddr - seg.baseAddr) /
                                           static_cast<double>(std::min<uint64_t>(0x200ULL, seg.endAddr - seg.baseAddr)),
                                       0.08, 0.92);
                }
                activeCursorX = x + static_cast<int>(segW * ratio);
            }
            x += segW;
        }

        if (activeCursorX < 0 && !m_segments.empty()) {
            activeCursorX = (w * m_segments[0].weight) / totalWeight + 18;
        }

        // Render yellow indicator line and dual arrow heads at active cursor position.
        if (activeCursorX >= 0) {
            p.setPen(QPen(QColor("#ffcc00"), 1));
            p.drawLine(activeCursorX, 0, activeCursorX, 4);
            p.drawLine(activeCursorX, h - 4, activeCursorX, h);
            QPolygon topMarker;
            topMarker << QPoint(activeCursorX - 4, 0) << QPoint(activeCursorX + 4, 0) << QPoint(activeCursorX, 4);
            QPolygon botMarker;
            botMarker << QPoint(activeCursorX - 4, h) << QPoint(activeCursorX + 4, h) << QPoint(activeCursorX, h - 4);
            p.setBrush(QColor("#ffcc00"));
            p.setPen(Qt::NoPen);
            p.drawPolygon(topMarker);
            p.drawPolygon(botMarker);
        }
    }

    /** @brief Translates user mouse clicks on ribbon segments into address seek events. */
    void mousePressEvent(QMouseEvent* event) override {
        int totalWeight = 0;
        for (const auto& seg : m_segments) totalWeight += seg.weight;
        const int w = std::max(1, width());
        int x = 0;
        for (size_t i = 0; i < m_segments.size(); ++i) {
            const auto& seg = m_segments[i];
            const int segW = (i + 1 == m_segments.size()) ? (w - x) : (w * seg.weight) / totalWeight;
            if (event->position().x() >= x && event->position().x() <= x + segW) {
                setCurrentAddress(seg.baseAddr);
                if (m_onSeek) m_onSeek(seg.baseAddr);
                break;
            }
            x += segW;
        }
    }

private:
    std::vector<Segment> m_segments;
    uint64_t m_currentAddr = 0x00401120ULL;
    std::function<void(uint64_t)> m_onSeek;
};

}  // namespace

/**
 * @brief Constructs and configures the interactive address-space ribbon widget.
 * @param window The hosting MainWindow instance.
 * @param parent Optional parent widget (e.g. toolbar).
 * @return Configured QWidget pointer.
 */
QWidget* createAddressSpaceNavBar(MainWindow* window, QWidget* parent) {
    auto* nav = new AddressSpaceNavBar(
        [window](uint64_t addr) {
            window->seekToAddressOrExpression(QStringLiteral("0x%1").arg(addr, 8, 16, QChar('0')));
        },
        parent);

    // Ctrl+Shift+F global workspace search shortcut.
    auto* searchShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F), window);
    QObject::connect(searchShortcut, &QShortcut::activated, window, [window]() {
        bool ok = false;
        const QString query = QInputDialog::getText(
            window, QObject::tr("Workspace Search"),
            QObject::tr("Search symbols, strings, disassembly, and decompiler:"),
            QLineEdit::Normal, QString(), &ok);
        if (ok && !query.trimmed().isEmpty()) {
            const auto matches = window->searchWorkspace(query.trimmed());
            if (!matches.isEmpty()) {
                window->seekToAddressOrExpression(QStringLiteral("0x%1").arg(matches.first().address, 8, 16, QChar('0')));
            }
        }
    });

    return nav;
}

/**
 * @brief Synchronizes all inspection views to an address or symbol expression.
 * @param expr Hexadecimal address, symbol, or string query.
 * @return True if target resolved and views updated.
 */
bool MainWindow::seekToAddressOrExpression(const QString& expr) {
    static bool s_inSeek = false;
    if (s_inSeek) return false;
    struct ReentrancyGuard {
        bool& flag;
        ~ReentrancyGuard() { flag = false; }
    } guard{s_inSeek};
    s_inSeek = true;

    const QString cleaned = expr.trimmed();
    if (cleaned.isEmpty()) return false;

    // Auto-prefix hexadecimal values missing the "0x" notation.
    QString normalized = cleaned;
    bool isHexWithoutPrefix = false;
    if (!normalized.startsWith("0x", Qt::CaseInsensitive)) {
        (void)normalized.toULongLong(&isHexWithoutPrefix, 16);
        if (isHexWithoutPrefix && normalized.length() >= 4) {
            normalized = QStringLiteral("0x") + normalized;
        }
    }

    bool hexOk = false;
    QString hexPart = normalized;
    if (hexPart.startsWith("0x", Qt::CaseInsensitive)) hexPart = hexPart.mid(2);
    const uint64_t parsedAddr = hexPart.toULongLong(&hexOk, 16);

    // Try symbol and string table lookups first if expression is non-numeric.
    if ((!hexOk || parsedAddr == 0ULL) && m_projectExplorerWidget) {
        if (m_projectExplorerWidget->activateSymbol(cleaned)) return true;
        if (m_projectExplorerWidget->activateString(cleaned)) return true;
    }

    if (m_seekBar && m_seekBar->text() != normalized) m_seekBar->setText(normalized);

    updateStatusTelemetry(normalized);
    refreshStatusBarMemoryTelemetry();

    if (m_addressNavBar && hexOk && parsedAddr != 0ULL) {
        m_addressNavBar->setProperty("currentAddress", QVariant::fromValue<qulonglong>(parsedAddr));
        m_addressNavBar->update();
    }

    // Synchronize Hexdump memory viewport.
    if (m_memView) {
        if (hexOk && parsedAddr != 0ULL) {
            QByteArray rawBytes;
            if (m_elfInspector && m_elfInspector->isValid()) rawBytes = m_elfInspector->readVirtualMemory(parsedAddr, 256);
            if (rawBytes.isEmpty() && m_previewFixtureOnly) rawBytes = synthesizeMemoryWindow(parsedAddr, 256);
            if (rawBytes.isEmpty() && !m_debugSessionActive && !m_previewFixtureOnly) m_memView->clear();
            const bool blocked = m_memView->blockSignals(true);
            m_memView->seekToAddress(parsedAddr);
            m_memView->blockSignals(blocked);
            if (!rawBytes.isEmpty()) {
                m_memView->setMemoryData(normalized, rawBytes);
                if (!m_previewFixtureOnly && !m_debugSessionActive && m_elfInspector && m_elfInspector->isValid()) {
                    m_memView->setSourceModeLabel(QStringLiteral("STATIC ELF (READ-ONLY)"));
                }
            }
        }
        if (auto* addrEdit = m_memView->findChild<QLineEdit*>()) addrEdit->setText(normalized);
        if (m_coordinator && m_currentTargetState != QStringLiteral("IDLE")) {
            m_coordinator->readMemory(m_focusedRank, normalized, 256);
        }
    }

    // Synchronize Disassembly and Decompiler graph/linear viewports.
    if (m_disassemblyView) {
        if (hexOk && parsedAddr != 0ULL) {
            if (m_elfInspector && m_elfInspector->isValid()) {
                const QString disasm = m_elfInspector->disassembleRange(parsedAddr, 64);
                if (!disasm.isEmpty()) m_disassemblyView->setPlainText(disasm);
            }
            const bool hasSourceMapping =
                ((parsedAddr >= 0x00401120ULL && parsedAddr <= 0x004011ffULL) ||
                 m_dwarfAddressToLine.contains(parsedAddr) || m_dynamicAddressToLine.contains(parsedAddr)) &&
                !m_forceDecompilerFallback;
            m_disassemblyView->seekToAddress(parsedAddr);
            const bool sourceEditorOpen =
                m_editorTabManager && !m_editorTabManager->isHidden() && m_editorTabManager->count() > 0 &&
                m_workspacePerspective.compare(QStringLiteral("Reverse"), Qt::CaseInsensitive) != 0;
            const bool allowDecompiler =
                !sourceEditorOpen || gridlock::core::ConfigManager::instance().showDecompilerWithSource() ||
                m_forceDecompilerFallback;
            if (!hasSourceMapping) {
                m_showingDecompilerFallback = true;
                if (allowDecompiler) m_disassemblyView->setSplitGraphDecompilerMode(true);
                else m_disassemblyView->setDecompilerPaneVisible(false, false);
            } else if (m_showingDecompilerFallback) {
                m_showingDecompilerFallback = false;
                m_disassemblyView->setSplitGraphDecompilerMode(false);
                m_disassemblyView->setGraphMode(false);
            } else if (!allowDecompiler && m_disassemblyView->isDecompilerVisible()) {
                syncDisassemblyDecompilerVisibility(false);
            }
        } else if (!m_disassemblyView->toPlainText().isEmpty()) {
            if (QTextDocument* doc = m_disassemblyView->document()) {
                QTextCursor found = doc->find(normalized);
                if (!found.isNull()) {
                    m_disassemblyView->setTextCursor(found);
                    m_disassemblyView->centerCursor();
                }
            }
        }
    }

    // Push address to navigation history stack if not navigating history.
    if (!m_isNavigatingHistory) {
        if (m_seekHistoryIndex >= 0 && m_seekHistoryIndex + 1 < m_seekHistory.size()) {
            m_seekHistory.resize(m_seekHistoryIndex + 1);
        }
        if (m_seekHistory.isEmpty() || m_seekHistory.last().compare(normalized, Qt::CaseInsensitive) != 0) {
            m_seekHistory.append(normalized);
            m_seekHistoryIndex = m_seekHistory.size() - 1;
        }
        updateNavigationButtons();
    }

    return true;
}

/** @brief Toggles forced split CFG + decompiler fallback mode in DisassemblyView. */
void MainWindow::enableDecompilerFallback(bool enabled) {
    m_forceDecompilerFallback = enabled;
    m_showingDecompilerFallback = enabled;
    if (m_disassemblyView) m_disassemblyView->setSplitGraphDecompilerMode(enabled);
}

/** @brief Synchronizes DisassemblyView and decompiler visibility across Source, Debug, Binary, and MPI perspectives. */
void MainWindow::syncDisassemblyDecompilerVisibility(bool applyDefaultMode) {
    if (!m_disassemblyView) return;
    const bool isBin = (m_workspacePerspective.compare(QStringLiteral("Binary"), Qt::CaseInsensitive) == 0 ||
                        m_workspacePerspective.compare(QStringLiteral("Reverse"), Qt::CaseInsensitive) == 0);
    const bool sourceVisible = m_editorTabManager && !m_editorTabManager->isHidden() && !isBin;
    m_disassemblyView->setSourceEditorActive(sourceVisible);
    if (!sourceVisible) {
        m_disassemblyView->setVisible(true);
        if (!m_disassemblyView->isDecompilerVisible()) m_disassemblyView->setSplitGraphDecompilerMode(true);
        return;
    }
    const auto& cfg = gridlock::core::ConfigManager::instance();
    if (m_workspacePerspective == QStringLiteral("Debug") && cfg.showDecompilerWithSource()) {
        m_disassemblyView->setVisible(true);
    }
    if (!cfg.showDecompilerWithSource()) {
        m_showingDecompilerFallback = false;
        m_forceDecompilerFallback = false;
        m_disassemblyView->setDecompilerPaneVisible(false, applyDefaultMode);
    } else if (applyDefaultMode || !m_disassemblyView->isDecompilerVisible()) {
        m_disassemblyView->setDecompilerPaneVisible(true, true);
    }
}

/**
 * @brief Jumps the active source editor cursor to the clangd-resolved symbol definition (F12).
 * @return True if a local or cross-file definition location was resolved and activated.
 */
bool MainWindow::goToDefinitionAtCursor() {
    if (SourceCodeView* view = getSourceCodeView()) {
        return view->triggerGoToDefinition();
    }
    return false;
}

/**
 * @brief Synthesizes a deterministic memory window for preview/fallback targets.
 * @param addr 64-bit virtual memory address.
 * @param length Number of bytes to generate.
 * @return Byte array containing section-specific machine code, rodata strings, or stack bytes.
 */
QByteArray MainWindow::synthesizeMemoryWindow(uint64_t addr, int length) const {
    const int reqLen = qMax(16, qMin(length, 4096));
    QByteArray result;
    result.resize(reqLen, '\0');

    // 1. .text instructions segment (0x00401000 .. 0x00401fff)
    if (addr >= 0x00401000ULL && addr < 0x00402000ULL) {
        const QByteArray textBytes = QByteArray::fromHex(
            "f30f1efa554889e54881ec9000000048"
            "8dbd78ffffff488db570ffffffe80efe"
            "ffffc745fc00000000837dfc027f298b"
            "45fc83f8007411f20f1005a90e000083"
            "45fc01ebe4f20f1005980e00008345fc"
            "01ebd3e8e3fdffffb800000000c9c390");
        const uint64_t textStart = 0x00401120ULL;
        const uint64_t textEnd = textStart + static_cast<uint64_t>(textBytes.size());

        for (int i = 0; i < reqLen; ++i) {
            const uint64_t curAddr = addr + static_cast<uint64_t>(i);
            if (curAddr >= textStart && curAddr < textEnd) {
                result[i] = textBytes.at(static_cast<qsizetype>(curAddr - textStart));
            } else if (curAddr >= 0x00401000ULL && curAddr < textStart) {
                result[i] = static_cast<char>((curAddr % 16 == 0) ? 0xff : 0x90);
            } else {
                result[i] = static_cast<char>(0x90);
            }
        }
        return result;
    }

    // 2. .rodata read-only data segment (0x00402000 .. 0x00402fff)
    if (addr >= 0x00402000ULL && addr < 0x00403000ULL) {
        QByteArray rodata(256, '\0');
        const char double1[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, static_cast<char>(0xf0), 0x3f};
        const char double2[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40};
        const char strChunk[] = "Rank %d: Processing chunk %d\n";
        const char strDemo[] = "Inspection Demo Running...\n";
        std::memcpy(rodata.data(), double1, 8);
        std::memcpy(rodata.data() + 8, double2, 8);
        std::memcpy(rodata.data() + 16, strChunk, sizeof(strChunk));
        std::memcpy(rodata.data() + 64, strDemo, sizeof(strDemo));

        const uint64_t rodataStart = 0x00402000ULL;
        for (int i = 0; i < reqLen; ++i) {
            const uint64_t curAddr = addr + static_cast<uint64_t>(i);
            const uint64_t offset = curAddr - rodataStart;
            if (offset < static_cast<uint64_t>(rodata.size())) {
                result[i] = rodata.at(static_cast<qsizetype>(offset));
            }
        }
        return result;
    }

    // 3. Stack frame segment (0x7ffffff00000 .. 0x7fffffffffff)
    if (addr >= 0x7ffffff00000ULL) {
        for (int i = 0; i < reqLen; ++i) {
            const uint64_t cur = addr + static_cast<uint64_t>(i);
            result[i] = static_cast<char>((cur * 31ULL + 17ULL) & 0xff);
        }
        return result;
    }

    // 4. Default synthetic memory pattern for any other virtual address
    for (int i = 0; i < reqLen; ++i) {
        result[i] = static_cast<char>((addr + static_cast<uint64_t>(i)) & 0xff);
    }
    return result;
}

}  // namespace gridlock::ui
