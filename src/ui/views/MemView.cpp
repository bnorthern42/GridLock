/**
 * @file MemView.cpp
 * @brief Core MemView toolbar construction, pop-out windowing, rank focus, seek, and byte patching.
 *
 * Implements MemView UI layout (`HEX DUMP | LIBRARY MEMORY MAP`), standalone Pop-Out /
 * Dock Back window management, rank pinning and live `process_vm_readv` / debugger read
 * dispatch, byte patching, CSV/binary matrix export, and cross-rank diff triggers.
 */

#include "MemView.hpp"

#include <algorithm>
#include <utility>
#include <vector>

#include <QComboBox>
#include <QEvent>
#include <QFileDialog>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPalette>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include "../../core/hpc/memory/MemoryDiffer.hpp"
#include "../../core/hpc/memory/MemoryExporter.hpp"
#include "../../core/managers/ConfigManager.hpp"
#include "../../core/managers/ThemeManager.hpp"
#include "MemoryProfilerWidget.hpp"

namespace gridlock::ui {

/** @brief Parses a string representation of a hex or decimal (`0d`) address into a 64-bit integer. */
uint64_t MemView::parseHexAddress(const QString& address, bool* okOut) {
    QString trimmed = address.trimmed();
    bool isDecimal = false;
    if (trimmed.startsWith("0d", Qt::CaseInsensitive)) { trimmed = trimmed.mid(2); isDecimal = true; }
    else if (trimmed.startsWith("0x", Qt::CaseInsensitive)) { trimmed = trimmed.mid(2); }
    const int spaceIdx = trimmed.indexOf(' ');
    if (spaceIdx > 0) trimmed = trimmed.left(spaceIdx);
    bool ok = false;
    uint64_t val = isDecimal ? trimmed.toULongLong(&ok, 10) : trimmed.toULongLong(&ok, 16);
    if (!ok && !isDecimal) val = trimmed.toULongLong(&ok, 10);
    if (okOut) *okOut = ok;
    return ok ? val : 0ULL;
}

/** @brief Constructs a new MemView widget with segmented HEX DUMP / LIBRARY MEMORY MAP modes. */
MemView::MemView(QWidget* parent) : QWidget(parent) {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_contentContainer = new QWidget(this);
    auto* containerLayout = new QVBoxLayout(m_contentContainer);
    containerLayout->setContentsMargins(0, 0, 0, 0);
    containerLayout->setSpacing(0);

    auto* headerBar = new QWidget(m_contentContainer);
    headerBar->setObjectName("cutterMemHeader");
    headerBar->setFixedHeight(28);
    auto* topLayout = new QHBoxLayout(headerBar);
    topLayout->setContentsMargins(6, 2, 6, 2);
    topLayout->setSpacing(6);

    auto configureToolbarButton = [](QPushButton* btn) {
        QFont boldFont = btn->font(); boldFont.setBold(true); QFontMetrics fm(boldFont);
        const int textW = std::max(fm.horizontalAdvance(btn->text()), fm.horizontalAdvance(btn->text().toUpper()));
        btn->setMinimumWidth(textW + 20); btn->setFixedHeight(22); btn->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    };

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

    m_sourceBadge = new QLabel(QStringLiteral("IDLE (NO BINARY)"), headerBar);
    m_sourceBadge->setObjectName(QStringLiteral("memSourceBadge"));
    m_sourceBadge->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    m_sourceBadge->setFixedHeight(22);
    topLayout->addWidget(m_sourceBadge);

    m_popOutBtn = new QPushButton(QStringLiteral("⤢ POP OUT"), headerBar);
    m_popOutBtn->setObjectName(QStringLiteral("memPopOutBtn"));
    configureToolbarButton(m_popOutBtn);
    topLayout->addWidget(m_popOutBtn);
    containerLayout->addWidget(headerBar);

    auto* hexBar = new QWidget(m_contentContainer);
    hexBar->setObjectName(QStringLiteral("cutterMemHexToolbar"));
    hexBar->setFixedHeight(26);
    auto* hexLayout = new QHBoxLayout(hexBar);
    hexLayout->setContentsMargins(6, 2, 6, 2);
    hexLayout->setSpacing(4);
    m_hexOnlyWidgets.append(hexBar);

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
    m_baselineRankBox = new QSpinBox(hexBar); m_baselineRankBox->setObjectName(QStringLiteral("memBaselineRankSpin"));
    m_baselineRankBox->setRange(0, 1000); m_baselineRankBox->setMinimumWidth(56); m_baselineRankBox->setFixedHeight(22);
    hexLayout->addWidget(m_baselineRankBox);
    m_pinRankBtn = new QPushButton(QStringLiteral("Pin Rank"), hexBar); m_pinRankBtn->setObjectName(QStringLiteral("memPinRankBtn"));
    m_pinRankBtn->setCheckable(true); m_pinRankBtn->setChecked(false);
    m_pinRankBtn->setToolTip(QStringLiteral("Pin MemView to the selected Base rank instead of following global rank focus"));
    configureToolbarButton(m_pinRankBtn); hexLayout->addWidget(m_pinRankBtn);

    hexLayout->addWidget(new QLabel("Target:", hexBar));
    m_targetRankBox = new QSpinBox(hexBar);
    m_targetRankBox->setRange(0, 1000); m_targetRankBox->setMinimumWidth(56); m_targetRankBox->setFixedHeight(22);
    hexLayout->addWidget(m_targetRankBox);

    m_diffBtn = new QPushButton("Diff Ranks", hexBar); configureToolbarButton(m_diffBtn); hexLayout->addWidget(m_diffBtn);
    hexLayout->addStretch(1);
    containerLayout->addWidget(hexBar);

    m_viewStack = new QStackedWidget(m_contentContainer);
    m_dumpEdit = new QTextEdit(m_viewStack);
    m_dumpEdit->setObjectName("cutterHexDumpEdit");
    m_dumpEdit->setReadOnly(true);
    m_dumpEdit->setFrameShape(QFrame::NoFrame);
    QFont font("JetBrains Mono"); font.setStyleHint(QFont::Monospace); font.setPointSize(10); m_dumpEdit->setFont(font);
    m_dumpEdit->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_dumpEdit, &QTextEdit::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu* menu = m_dumpEdit->createStandardContextMenu();
        menu->addSeparator();
        const int byteOff = byteOffsetAtPosition(pos);
        const uint64_t cursorAddr = m_currentBaseAddress + static_cast<uint64_t>(std::max(0, byteOff));
        const QString addrHex = QStringLiteral("0x%1").arg(cursorAddr, 0, 16).toUpper();
        QAction* addrAct = menu->addAction(QStringLiteral("Open Address in Programmer's Calculator (%1)").arg(addrHex));
        int bits = 0;
        const QString wordHex = wordHexAtByteOffset(byteOff, &bits);
        QAction* wordAct = (bits > 0) ? menu->addAction(QStringLiteral("Open %1-bit Word at Cursor in Programmer's Calculator (%2)").arg(bits).arg(wordHex)) : nullptr;
        QAction* chosen = menu->exec(m_dumpEdit->viewport()->mapToGlobal(pos));
        if (chosen == addrAct) emit sendToCalculatorRequested(addrHex);
        else if (wordAct && chosen == wordAct) emit sendToCalculatorRequested(wordHex);
        delete menu;
    });

    QPalette p = m_dumpEdit->palette();
    p.setColor(QPalette::Base, QColor(gridlock::core::ConfigManager::instance().getSourceBackground()));
    p.setColor(QPalette::Text, QColor(gridlock::core::ConfigManager::instance().getSourceText()));
    m_dumpEdit->setPalette(p);

    m_profilerWidget = new MemoryProfilerWidget(m_viewStack);
    m_viewStack->addWidget(m_dumpEdit);
    m_viewStack->addWidget(m_profilerWidget);
    containerLayout->addWidget(m_viewStack, 1);
    mainLayout->addWidget(m_contentContainer, 1);

    m_placeholderWidget = new QWidget(this); m_placeholderWidget->setVisible(false);
    auto* phLayout = new QVBoxLayout(m_placeholderWidget); phLayout->addStretch(1);
    auto* dockBackBtn = new QPushButton(QStringLiteral("Memory View is popped out — Click to Dock Back"), m_placeholderWidget);
    dockBackBtn->setObjectName(QStringLiteral("memDockBackPlaceholderBtn"));
    phLayout->addWidget(dockBackBtn, 0, Qt::AlignCenter); phLayout->addStretch(1);
    mainLayout->addWidget(m_placeholderWidget, 1);

    connect(m_readBtn, &QPushButton::clicked, this, &MemView::onReadClicked);
    connect(m_addressEdit, &QLineEdit::returnPressed, this, &MemView::onReadClicked);
    connect(m_exportBtn, &QPushButton::clicked, this, &MemView::onExportMatrixClicked);
    connect(m_diffBtn, &QPushButton::clicked, this, &MemView::onDiffClicked);
    connect(m_popOutBtn, &QPushButton::clicked, this, [this]() { setPoppedOut(!m_isPoppedOut); });
    connect(dockBackBtn, &QPushButton::clicked, this, [this]() { setPoppedOut(false); });
    connect(m_baselineRankBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int val) {
        m_focusedRank = qMax(0, val);
        if (!isRankPinned()) emit rankSelected(m_focusedRank);
    });

    connect(m_hexModeBtn, &QPushButton::clicked, this, [this]() {
        m_hexModeBtn->setChecked(true); if (m_mapModeBtn) m_mapModeBtn->setChecked(false);
        setHexControlsVisible(true); if (m_viewStack) m_viewStack->setCurrentIndex(0);
    });
    connect(m_mapModeBtn, &QPushButton::clicked, this, [this]() {
        m_mapModeBtn->setChecked(true); if (m_hexModeBtn) m_hexModeBtn->setChecked(false);
        setHexControlsVisible(false); if (m_viewStack) m_viewStack->setCurrentIndex(1);
        const int rank = focusedRank();
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

    clear(); reloadStyle();
    connect(&gridlock::core::ThemeManager::instance(), &gridlock::core::ThemeManager::themeModeChanged, this, [this](bool) { reloadStyle(); });
}

MemView::~MemView() {
    if (!m_popOutWindow) return;
    m_popOutWindow->removeEventFilter(this);
    if (m_isPoppedOut && m_contentContainer) {
        if (auto* l = m_popOutWindow->layout()) l->removeWidget(m_contentContainer);
        m_contentContainer->setParent(this);
    }
    delete m_popOutWindow; m_popOutWindow = nullptr;
}

void MemView::setLiveMemoryCallbacks(RankAddressResolver resolver, RankBytesFetcher fetcher, RankPidGetter pidGetter) {
    m_addressResolver = std::move(resolver); m_bytesFetcher = std::move(fetcher); m_pidGetter = std::move(pidGetter);
    if (m_profilerWidget) m_profilerWidget->setAvailableRanks(1, 0, m_pidGetter);
}

bool MemView::isMemoryMapMode() const { return m_viewStack && m_viewStack->currentIndex() == 1; }
int MemView::focusedRank() const { return m_baselineRankBox ? m_baselineRankBox->value() : m_focusedRank; }
bool MemView::isRankPinned() const { return m_pinRankBtn && m_pinRankBtn->isChecked(); }
void MemView::setRankPinned(bool pinned) { if (m_pinRankBtn) m_pinRankBtn->setChecked(pinned); }

void MemView::setFocusedRank(int rank, int rankCount) {
    const int target = qMax(0, rank);
    if (rankCount > 0) {
        const int maxRank = qMax(1000, rankCount - 1);
        if (m_baselineRankBox && m_baselineRankBox->maximum() < maxRank) m_baselineRankBox->setMaximum(maxRank);
        if (m_targetRankBox && m_targetRankBox->maximum() < maxRank) m_targetRankBox->setMaximum(maxRank);
    }
    if (isRankPinned()) return;
    m_focusedRank = target;
    if (m_baselineRankBox && m_baselineRankBox->value() != target) {
        QSignalBlocker sb(m_baselineRankBox);
        if (target > m_baselineRankBox->maximum()) m_baselineRankBox->setMaximum(target);
        m_baselineRankBox->setValue(target);
    }
    const qint64 pid = m_pidGetter ? m_pidGetter(target) : 0LL;
    notifyRankStoppedOrSelected(target, pid, rankCount > 0 ? rankCount : qMax(1, target + 1));
    if (pid > 0 && m_currentBaseAddress != 0ULL && m_bytesFetcher) {
        const QByteArray bytes = m_bytesFetcher(target, m_currentBaseAddress, m_lengthBox ? m_lengthBox->value() : 256);
        if (!bytes.isEmpty()) setMemoryData(static_cast<qint64>(m_currentBaseAddress), QString::fromLatin1(bytes.toHex()));
    }
}

void MemView::notifyRankStoppedOrSelected(int rank, qint64 pid, int rankCount) {
    if (isRankPinned() && rank != focusedRank()) return;
    if (pid > 0) setSourceModeLabel(QStringLiteral("LIVE RANK %1 (PID %2)").arg(rank).arg(pid));
    if (!m_profilerWidget) return;
    m_profilerWidget->setAvailableRanks(rankCount, rank, m_pidGetter);
    if (isMemoryMapMode() && (isVisible() || m_isPoppedOut) && pid > 0) m_profilerWidget->refreshForPid(pid);
}

void MemView::setHexControlsVisible(bool visible) {
    for (QWidget* w : m_hexOnlyWidgets) if (w) w->setVisible(visible);
}

void MemView::setPoppedOut(bool poppedOut) {
    if (poppedOut == m_isPoppedOut || !m_contentContainer) return;
    m_isPoppedOut = poppedOut;
    auto* rootLayout = qobject_cast<QVBoxLayout*>(layout());
    if (poppedOut) {
        if (!m_popOutWindow) {
            m_popOutWindow = new QWidget(nullptr, Qt::Window | Qt::WindowMinimizeButtonHint | Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint);
            m_popOutWindow->setWindowTitle(QStringLiteral("GridLock — Memory View & Library Memory Map"));
            m_popOutWindow->resize(1280, 820);
            auto* winLayout = new QVBoxLayout(m_popOutWindow); winLayout->setContentsMargins(0, 0, 0, 0);
            m_popOutWindow->installEventFilter(this);
            connect(this, &QObject::destroyed, m_popOutWindow, &QObject::deleteLater);
        }
        if (rootLayout) rootLayout->removeWidget(m_contentContainer);
        if (m_placeholderWidget) m_placeholderWidget->setVisible(true);
        m_popOutWindow->layout()->addWidget(m_contentContainer); m_contentContainer->show();
        if (m_popOutBtn) { m_popOutBtn->setText(QStringLiteral("⤡ DOCK")); m_popOutBtn->setMinimumWidth(m_popOutBtn->fontMetrics().horizontalAdvance(QStringLiteral("⤡ DOCK")) + 24); }
        m_popOutWindow->show(); m_popOutWindow->raise(); m_popOutWindow->activateWindow();
    } else {
        if (m_popOutWindow) { m_popOutWindow->layout()->removeWidget(m_contentContainer); m_popOutWindow->hide(); }
        if (m_placeholderWidget) m_placeholderWidget->setVisible(false);
        if (rootLayout) rootLayout->insertWidget(0, m_contentContainer, 1);
        m_contentContainer->show();
        if (m_popOutBtn) { m_popOutBtn->setText(QStringLiteral("⤢ POP OUT")); m_popOutBtn->setMinimumWidth(m_popOutBtn->fontMetrics().horizontalAdvance(QStringLiteral("⤢ POP OUT")) + 24); }
    }
}

bool MemView::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_popOutWindow && event && event->type() == QEvent::Close) {
        event->ignore();
        setPoppedOut(false);
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void MemView::clear() {
    m_currentBaseAddress = 0ULL; m_previousBaseAddress = 0ULL;
    m_changedByteCount = 0; m_hasDiffBaseline = false;
    m_lastRawData.clear(); m_previousRawData.clear();
    m_unpatchedRawData.clear(); m_patchedBytes.clear();
    if (m_addressEdit) m_addressEdit->clear();
    if (m_dumpEdit) m_dumpEdit->clear();
    if (m_profilerWidget) m_profilerWidget->clear();
    setSourceModeLabel(QStringLiteral("IDLE (NO BINARY)"));
}

bool MemView::seekToAddress(uint64_t address) {
    if (address == 0ULL) return false;
    m_currentBaseAddress = address;
    const QString formatted = QStringLiteral("0x%1").arg(address, 8, 16, QChar('0'));
    if (m_addressEdit) m_addressEdit->setText(formatted);
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
    emit requestMemory(formatted, len);
    return true;
}

void MemView::onReadClicked() {
    const QString addr = m_addressEdit ? m_addressEdit->text().trimmed() : QString();
    if (addr.isEmpty()) return;
    const int rank = m_baselineRankBox ? m_baselineRankBox->value() : 0;
    const int len = m_lengthBox ? m_lengthBox->value() : 256;

    bool ok = false;
    uint64_t resolved = m_addressResolver ? m_addressResolver(rank, addr, &ok) : 0ULL;
    if (!ok || resolved == 0ULL) resolved = parseHexAddress(addr, &ok);
    if (ok && resolved != 0ULL) m_currentBaseAddress = resolved;

    const qint64 pid = m_pidGetter ? m_pidGetter(rank) : 0LL;
    if (ok && resolved != 0ULL && pid > 0 && m_bytesFetcher) {
        const QByteArray liveBytes = m_bytesFetcher(rank, resolved, len);
        if (!liveBytes.isEmpty()) {
            bool isLiteralHex = false;
            (void)parseHexAddress(addr, &isLiteralHex);
            setMemoryData(static_cast<qint64>(resolved), QString::fromLatin1(liveBytes.toHex()));
            setSourceModeLabel(QStringLiteral("LIVE RANK %1 (PID %2)").arg(rank).arg(pid));
            if (!isLiteralHex && m_addressEdit) m_addressEdit->setText(addr);
            return;
        }
    }
    emit requestMemory(addr, len);
}

bool MemView::patchBytes(uint64_t address, const QByteArray& newBytes) {
    if (newBytes.isEmpty()) return false;
    for (int i = 0; i < newBytes.size(); ++i) m_patchedBytes.insert(address + static_cast<uint64_t>(i), newBytes.at(i));
    if (!m_lastRawData.isEmpty() && address >= m_currentBaseAddress &&
        address < m_currentBaseAddress + static_cast<uint64_t>(m_lastRawData.size())) {
        const uint64_t offset = address - m_currentBaseAddress;
        const int copyLen = qMin(newBytes.size(), static_cast<int>(m_lastRawData.size() - offset));
        for (int i = 0; i < copyLen; ++i) m_lastRawData[static_cast<qsizetype>(offset + i)] = newBytes.at(i);
        if (m_dumpEdit) m_dumpEdit->setHtml(formatHexDumpHtml(m_currentBaseAddress, m_lastRawData, m_previousRawData));
    }
    emit bytesPatched(address, newBytes);
    return true;
}

void MemView::revertAllPatches() {
    m_patchedBytes.clear();
    if (!m_unpatchedRawData.isEmpty()) {
        m_lastRawData = m_unpatchedRawData;
        if (m_dumpEdit) m_dumpEdit->setHtml(formatHexDumpHtml(m_currentBaseAddress, m_lastRawData, m_previousRawData));
    }
}

void MemView::onExportMatrixClicked() {
    if (m_lastRawData.isEmpty()) {
        QMessageBox::warning(this, "No Data", "No memory data available to export. Please read memory first.");
        return;
    }
    const QString fileName = QFileDialog::getSaveFileName(this, "Export Memory Matrix", "", "CSV Files (*.csv);;Binary Files (*.bin);;All Files (*)");
    if (fileName.isEmpty()) return;

    std::vector<uint8_t> buffer(m_lastRawData.begin(), m_lastRawData.end());
    const int typeIndex = m_typeBox->currentIndex();
    (void)QtConcurrent::run([buffer, fileName, typeIndex]() {
        if (fileName.endsWith(".csv", Qt::CaseInsensitive)) {
            gridlock::core::hpc::DataType type = gridlock::core::hpc::DataType::Int32;
            int typeSize = sizeof(int32_t);
            if (typeIndex == 1) { type = gridlock::core::hpc::DataType::Float; typeSize = sizeof(float); }
            else if (typeIndex == 2) { type = gridlock::core::hpc::DataType::Double; typeSize = sizeof(double); }
            const int cols = std::max(1, static_cast<int>(buffer.size()) / typeSize);
            gridlock::core::hpc::MemoryExporter::exportToCsv(fileName.toStdString(), buffer, 1, cols, type);
        } else {
            gridlock::core::hpc::MemoryExporter::exportToBinary(fileName.toStdString(), buffer);
        }
    });
}

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

    const qint64 pidA = m_pidGetter ? m_pidGetter(baselineRank) : 0;
    const qint64 pidB = m_pidGetter ? m_pidGetter(targetRank) : 0;
    if ((pidA > 0 || pidB > 0) && m_bytesFetcher) {
        gridlock::core::hpc::CompareResult res;
        if (pidA > 0 && pidB > 0 && addrA == addrB) {
            res = gridlock::core::hpc::MemoryDiffer::compareMemory(
                static_cast<pid_t>(pidA), static_cast<pid_t>(pidB), reinterpret_cast<void*>(addrA), static_cast<size_t>(length));
        }
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

}  // namespace gridlock::ui
