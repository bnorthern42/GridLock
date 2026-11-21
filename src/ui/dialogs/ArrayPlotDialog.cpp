/**
 * @file ArrayPlotDialog.cpp
 * @brief Implementation of the live auto-refreshing 1D/2D scientific array, multi-rank overlay,
 *        strided slice, and scalar time-series history plotter.
 */

#include "ArrayPlotDialog.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>
#include <QVBoxLayout>

#include "../../core/hpc/DapCoordinator.hpp"
#include "../../core/hpc/GdbRankCoordinator.hpp"
#include "../../core/hpc/IBackendCoordinator.hpp"
#include "../../core/managers/ThemeManager.hpp"

namespace gridlock::ui {

int ArrayPlotDialog::byteSizeOf(ElementType type) {
    switch (type) {
        case ElementType::Float64: return 8;
        case ElementType::Float32: return 4;
        case ElementType::Int32:   return 4;
        case ElementType::Int64:   return 8;
        case ElementType::UInt8:   return 1;
    }
    return 8;
}

QVector<double> ArrayPlotDialog::decodeStridedBytes(const QByteArray& rawBytes, ElementType type,
                                                    int maxCount, int offset, int stride) {
    const int elemSize = byteSizeOf(type);
    const int totalAvailable = static_cast<int>(rawBytes.size()) / elemSize;
    const int safeOffset = qMax(0, offset);
    const int safeStride = qMax(1, stride);
    if (totalAvailable <= safeOffset) return {};

    const int stridedAvailable = (totalAvailable - safeOffset - 1) / safeStride + 1;
    const int count = (maxCount > 0) ? qMin(stridedAvailable, maxCount) : stridedAvailable;
    if (count <= 0) return {};

    auto readAt = [](const char* src, auto tag) -> double {
        using T = decltype(tag);
        T v{};
        std::memcpy(&v, src, sizeof(v));
        return static_cast<double>(v);
    };

    QVector<double> decoded(count);
    const char* ptr = rawBytes.constData();
    for (int i = 0; i < count; ++i) {
        const char* src = ptr + static_cast<qsizetype>(safeOffset + i * safeStride) * elemSize;
        switch (type) {
            case ElementType::Float64: decoded[i] = readAt(src, double{}); break;
            case ElementType::Float32: decoded[i] = readAt(src, float{}); break;
            case ElementType::Int32:   decoded[i] = readAt(src, int32_t{}); break;
            case ElementType::Int64:   decoded[i] = readAt(src, int64_t{}); break;
            case ElementType::UInt8:   decoded[i] = readAt(src, uint8_t{}); break;
        }
    }
    return decoded;
}

bool ArrayPlotDialog::parseScalarResult(const QString& resultText, double* outVal) {
    if (!outVal) return false;
    QString s = resultText.trimmed();
    if (s.isEmpty()) return false;
    if (const int eq = s.lastIndexOf(QLatin1Char('=')); eq >= 0) s = s.mid(eq + 1).trimmed();
    static const QRegularExpression kCastRe(QStringLiteral(R"(^\([^)]*\)\s*)"));
    s.remove(kCastRe);
    s = s.trimmed();

    const QString lower = s.toLower();
    if (lower == QStringLiteral("nan") || lower == QStringLiteral("-nan")) {
        *outVal = std::numeric_limits<double>::quiet_NaN();
        return true;
    }
    if (lower == QStringLiteral("inf") || lower == QStringLiteral("+inf") || lower == QStringLiteral("infinity")) {
        *outVal = std::numeric_limits<double>::infinity();
        return true;
    }
    if (lower == QStringLiteral("-inf") || lower == QStringLiteral("-infinity")) {
        *outVal = -std::numeric_limits<double>::infinity();
        return true;
    }
    if (lower.startsWith(QStringLiteral("0x"))) {
        bool ok = false;
        const quint64 hv = lower.mid(2).section(QLatin1Char(' '), 0, 0).toULongLong(&ok, 16);
        if (ok) { *outVal = static_cast<double>(hv); return true; }
    }
    bool ok = false;
    if (const double direct = s.toDouble(&ok); ok) { *outVal = direct; return true; }
    static const QRegularExpression kFloatRe(QStringLiteral(R"([-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?)"));
    if (const QRegularExpressionMatch m = kFloatRe.match(s); m.hasMatch()) {
        if (const double v = m.captured(0).toDouble(&ok); ok) { *outVal = v; return true; }
    }
    return false;
}

ArrayPlotDialog::ArrayPlotDialog(::IBackendCoordinator* coordinator, QWidget* parent)
    : QDialog(parent, Qt::Window) {
    setObjectName(QStringLiteral("ArrayPlotDialog"));
    setWindowTitle(QStringLiteral("Scientific 1D/2D Array & Field Plotter"));
    setModal(false);
    setMinimumSize(940, 540);
    resize(1040, 620);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(6);

    // Row 1: Expression, ElementType, PlotMode, Colormap, Refresh, Close.
    auto* topRow = new QHBoxLayout();
    topRow->setSpacing(6);

    m_exprEdit = new QLineEdit(this);
    m_exprEdit->setObjectName(QStringLiteral("arrayPlotExprEdit"));
    m_exprEdit->setPlaceholderText(QStringLiteral("Expr or slice (e.g., u[16:128:2], grid.data(), residual)..."));
    m_exprEdit->setFixedHeight(26);

    m_typeCombo = new QComboBox(this);
    m_typeCombo->setObjectName(QStringLiteral("arrayPlotTypeCombo"));
    m_typeCombo->setFixedHeight(26);
    m_typeCombo->addItem(QStringLiteral("double"), static_cast<int>(ElementType::Float64));
    m_typeCombo->addItem(QStringLiteral("float"), static_cast<int>(ElementType::Float32));
    m_typeCombo->addItem(QStringLiteral("int32_t"), static_cast<int>(ElementType::Int32));
    m_typeCombo->addItem(QStringLiteral("int64_t"), static_cast<int>(ElementType::Int64));
    m_typeCombo->addItem(QStringLiteral("uint8_t"), static_cast<int>(ElementType::UInt8));

    m_modeCombo = new QComboBox(this);
    m_modeCombo->setObjectName(QStringLiteral("arrayPlotModeCombo"));
    m_modeCombo->setFixedHeight(26);
    m_modeCombo->addItem(QStringLiteral("1D Line Plot"), static_cast<int>(PlotMode::Line1D));
    m_modeCombo->addItem(QStringLiteral("1D Bar / Stem"), static_cast<int>(PlotMode::Stem1D));
    m_modeCombo->addItem(QStringLiteral("2D Heatmap"), static_cast<int>(PlotMode::Heatmap2D));
    m_modeCombo->addItem(QStringLiteral("Scalar History (Over Stops)"), static_cast<int>(PlotMode::ScalarHistory));

    m_colormapCombo = new QComboBox(this);
    m_colormapCombo->setObjectName(QStringLiteral("arrayPlotColormapCombo"));
    m_colormapCombo->setFixedHeight(26);
    m_colormapCombo->addItem(QStringLiteral("Viridis"), static_cast<int>(Colormap::Viridis));
    m_colormapCombo->addItem(QStringLiteral("Plasma"), static_cast<int>(Colormap::Plasma));
    m_colormapCombo->addItem(QStringLiteral("Cool-Warm"), static_cast<int>(Colormap::CoolWarm));
    m_colormapCombo->addItem(QStringLiteral("Grayscale"), static_cast<int>(Colormap::Grayscale));

    m_refreshBtn = new QPushButton(QStringLiteral("Plot / Refresh"), this);
    m_refreshBtn->setObjectName(QStringLiteral("arrayPlotRefreshBtn"));
    m_refreshBtn->setFixedHeight(26);

    m_closeBtn = new QPushButton(QStringLiteral("✕ CLOSE"), this);
    m_closeBtn->setObjectName(QStringLiteral("arrayPlotCloseBtn"));
    m_closeBtn->setFixedHeight(26);

    topRow->addWidget(new QLabel(QStringLiteral("Expr:"), this));
    topRow->addWidget(m_exprEdit, 1);
    topRow->addWidget(new QLabel(QStringLiteral("Type:"), this));
    topRow->addWidget(m_typeCombo);
    topRow->addWidget(m_modeCombo);
    topRow->addWidget(m_colormapCombo);
    topRow->addWidget(m_refreshBtn);
    topRow->addWidget(m_closeBtn);
    root->addLayout(topRow);

    // Row 2: Shape (Rows, Cols), Slicing (Offset, Stride), Log10 Scale, Reset Zoom, Clear History.
    auto* sliceRow = new QHBoxLayout();
    sliceRow->setSpacing(6);

    m_rowsSpin = new QSpinBox(this);
    m_rowsSpin->setObjectName(QStringLiteral("arrayPlotRowsSpin"));
    m_rowsSpin->setRange(1, 4096);
    m_rowsSpin->setValue(1);
    m_rowsSpin->setFixedHeight(24);
    m_rowsSpin->setMaximumWidth(68);

    m_colsSpin = new QSpinBox(this);
    m_colsSpin->setObjectName(QStringLiteral("arrayPlotColsSpin"));
    m_colsSpin->setRange(1, 65536);
    m_colsSpin->setValue(64);
    m_colsSpin->setFixedHeight(24);
    m_colsSpin->setMaximumWidth(74);

    m_offsetSpin = new QSpinBox(this);
    m_offsetSpin->setObjectName(QStringLiteral("arrayPlotOffsetSpin"));
    m_offsetSpin->setRange(0, 10000000);
    m_offsetSpin->setValue(0);
    m_offsetSpin->setFixedHeight(24);
    m_offsetSpin->setMaximumWidth(76);
    m_offsetSpin->setToolTip(QStringLiteral("Starting element offset for strided slicing (offset + i * stride)"));

    m_strideSpin = new QSpinBox(this);
    m_strideSpin->setObjectName(QStringLiteral("arrayPlotStrideSpin"));
    m_strideSpin->setRange(1, 65536);
    m_strideSpin->setValue(1);
    m_strideSpin->setFixedHeight(24);
    m_strideSpin->setMaximumWidth(64);
    m_strideSpin->setToolTip(QStringLiteral("Step between consecutive plotted elements"));

    m_logScaleCheck = new QCheckBox(QStringLiteral("Log10"), this);
    m_logScaleCheck->setObjectName(QStringLiteral("arrayPlotLogScaleCheck"));
    m_logScaleCheck->setToolTip(QStringLiteral("Use base-10 logarithmic Y axis / 2D heatmap color scale"));
    m_logScaleCheck->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);

    m_resetZoomBtn = new QPushButton(QStringLiteral("Reset Zoom"), this);
    m_resetZoomBtn->setObjectName(QStringLiteral("arrayPlotResetZoomBtn"));
    m_resetZoomBtn->setFixedHeight(24);
    m_resetZoomBtn->setEnabled(false);

    m_clearHistoryBtn = new QPushButton(QStringLiteral("Clear History"), this);
    m_clearHistoryBtn->setObjectName(QStringLiteral("arrayPlotClearHistoryBtn"));
    m_clearHistoryBtn->setFixedHeight(24);
    m_clearHistoryBtn->setToolTip(QStringLiteral("Clear accumulated Scalar History samples"));

    sliceRow->addWidget(new QLabel(QStringLiteral("Rows:"), this));
    sliceRow->addWidget(m_rowsSpin);
    sliceRow->addWidget(new QLabel(QStringLiteral("Cols:"), this));
    sliceRow->addWidget(m_colsSpin);
    sliceRow->addWidget(new QLabel(QStringLiteral("Offset:"), this));
    sliceRow->addWidget(m_offsetSpin);
    sliceRow->addWidget(new QLabel(QStringLiteral("Stride:"), this));
    sliceRow->addWidget(m_strideSpin);
    sliceRow->addWidget(m_logScaleCheck);
    sliceRow->addWidget(m_resetZoomBtn);
    sliceRow->addWidget(m_clearHistoryBtn);
    sliceRow->addStretch(1);
    root->addLayout(sliceRow);

    // Row 3: Rank selector, Multi-Rank Overlay + Subset filter, Auto-Refresh, and Exports.
    auto* rankRow = new QHBoxLayout();
    rankRow->setSpacing(6);

    m_rankCombo = new QComboBox(this);
    m_rankCombo->setObjectName(QStringLiteral("arrayPlotRankCombo"));
    m_rankCombo->setFixedHeight(24);
    for (int r = 0; r < 8; ++r) m_rankCombo->addItem(QStringLiteral("Rank %1").arg(r), r);

    m_overlayRanksCheck = new QCheckBox(QStringLiteral("Overlay Ranks"), this);
    m_overlayRanksCheck->setObjectName(QStringLiteral("arrayPlotOverlayAllRanksCheck"));
    m_overlayRanksCheck->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);

    m_rankSubsetEdit = new QLineEdit(QStringLiteral("All"), this);
    m_rankSubsetEdit->setObjectName(QStringLiteral("arrayPlotRankSubsetEdit"));
    m_rankSubsetEdit->setPlaceholderText(QStringLiteral("All or 0..3"));
    m_rankSubsetEdit->setFixedHeight(24);
    m_rankSubsetEdit->setFixedWidth(96);
    m_rankSubsetEdit->setToolTip(QStringLiteral("Ranks to overlay (e.g., All, 0..3, 0,2,3)"));

    m_autoRefreshCheck = new QCheckBox(QStringLiteral("Auto-Refresh"), this);
    m_autoRefreshCheck->setObjectName(QStringLiteral("arrayPlotAutoRefreshCheck"));
    m_autoRefreshCheck->setToolTip(QStringLiteral("Automatically refresh plot whenever the focused rank stops"));
    m_autoRefreshCheck->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    m_autoRefreshCheck->setChecked(true);

    m_exportCsvBtn = new QPushButton(QStringLiteral("Export CSV"), this);
    m_exportCsvBtn->setObjectName(QStringLiteral("arrayPlotExportCsvBtn"));
    m_exportCsvBtn->setFixedHeight(24);

    m_savePngBtn = new QPushButton(QStringLiteral("Save PNG"), this);
    m_savePngBtn->setObjectName(QStringLiteral("arrayPlotSavePngBtn"));
    m_savePngBtn->setFixedHeight(24);

    m_gnuplotBtn = new QPushButton(QStringLiteral("Open in gnuplot"), this);
    m_gnuplotBtn->setObjectName(QStringLiteral("arrayPlotGnuplotBtn"));
    m_gnuplotBtn->setFixedHeight(24);

    rankRow->addWidget(m_rankCombo);
    rankRow->addWidget(m_overlayRanksCheck);
    rankRow->addWidget(new QLabel(QStringLiteral("Ranks:"), this));
    rankRow->addWidget(m_rankSubsetEdit);
    rankRow->addWidget(m_autoRefreshCheck);
    rankRow->addStretch(1);
    rankRow->addWidget(m_exportCsvBtn);
    rankRow->addWidget(m_savePngBtn);
    rankRow->addWidget(m_gnuplotBtn);
    root->addLayout(rankRow);

    // Custom scientific plot canvas.
    m_canvas = new ArrayPlotCanvas(this);
    root->addWidget(m_canvas, 1);

    // Live telemetry stats bar + cursor hover readout.
    auto* footerRow = new QHBoxLayout();
    m_statsLabel = new QLabel(QStringLiteral("Min: 0 | Max: 0 | Mean: 0 | L2: 0 | NaN/Inf: 0 | Changed: 0"), this);
    m_statsLabel->setObjectName(QStringLiteral("arrayPlotStatsLabel"));
    m_hoverLabel = new QLabel(QStringLiteral("Drag to zoom, double-click to reset, hover to inspect values"), this);
    m_hoverLabel->setObjectName(QStringLiteral("arrayPlotHoverLabel"));
    footerRow->addWidget(m_statsLabel, 1);
    footerRow->addWidget(m_hoverLabel);
    root->addLayout(footerRow);

    connect(m_refreshBtn, &QPushButton::clicked, this, &ArrayPlotDialog::refreshPlot);
    connect(m_exprEdit, &QLineEdit::returnPressed, this, &ArrayPlotDialog::refreshPlot);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::close);
    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        if (plotMode() == PlotMode::ScalarHistory) {
            m_currentSamples = m_scalarHistory;
            m_previousSamples.clear();
            m_rows = 1;
            m_cols = qMax(1, static_cast<int>(m_scalarHistory.size()));
            m_rankOverlays = m_rankScalarHistories;
            recomputeStats();
        }
        if (m_canvas) {
            m_canvas->resetZoom();
            m_canvas->update();
        }
    });
    connect(m_colormapCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        if (m_canvas) m_canvas->update();
    });
    connect(m_rowsSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int r) {
        m_rows = qMax(1, r);
        if (m_rows > 1 && m_modeCombo) m_modeCombo->setCurrentIndex(static_cast<int>(PlotMode::Heatmap2D));
        if (m_canvas) m_canvas->update();
    });
    connect(m_colsSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int c) {
        m_cols = qMax(1, c);
        if (m_canvas) m_canvas->update();
    });
    connect(m_offsetSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int off) {
        m_sliceOffset = qMax(0, off);
        if (m_canvas) m_canvas->update();
    });
    connect(m_strideSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int str) {
        m_sliceStride = qMax(1, str);
        if (m_canvas) m_canvas->update();
    });
    connect(m_logScaleCheck, &QCheckBox::toggled, this, [this](bool enabled) {
        if (m_canvas) m_canvas->setLogScale(enabled);
    });
    connect(m_resetZoomBtn, &QPushButton::clicked, this, &ArrayPlotDialog::resetZoom);
    connect(m_canvas, &ArrayPlotCanvas::zoomChanged, m_resetZoomBtn, &QPushButton::setEnabled);
    connect(m_clearHistoryBtn, &QPushButton::clicked, this, &ArrayPlotDialog::clearScalarHistory);
    connect(m_rankCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        if (idx >= 0) {
            m_focusedRank = m_rankCombo->itemData(idx).toInt();
            if (isAutoRefreshEnabled()) refreshPlot();
        }
    });
    connect(m_overlayRanksCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (!checked) {
            m_pendingOverlayRanks.clear();
            clearRankOverlays();
        } else {
            refreshPlot();
        }
    });
    connect(m_rankSubsetEdit, &QLineEdit::returnPressed, this, [this]() {
        if (isOverlayRanksEnabled()) refreshPlot();
    });
    connect(m_exportCsvBtn, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export Array CSV"),
                                                          QStringLiteral("array_plot.csv"),
                                                          QStringLiteral("CSV Files (*.csv)"));
        if (!path.isEmpty()) exportCsv(path);
    });
    connect(m_savePngBtn, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Save Plot PNG"),
                                                          QStringLiteral("array_plot.png"),
                                                          QStringLiteral("PNG Images (*.png)"));
        if (!path.isEmpty()) savePng(path);
    });
    connect(m_gnuplotBtn, &QPushButton::clicked, this, [this]() { openInGnuplot(); });

    setCoordinator(coordinator);
    reloadStyle();
}

void ArrayPlotDialog::setCoordinator(::IBackendCoordinator* coordinator) {
    if (m_coordinator == coordinator) return;
    if (m_coordinator) disconnect(m_coordinator, nullptr, this, nullptr);
    m_coordinator = coordinator;
    if (!m_coordinator) return;

    connect(m_coordinator, &IBackendCoordinator::memoryRead, this,
            [this](int rankId, const QString& /*address*/, const QByteArray& data) {
                if (data.isEmpty()) return;
                if (m_pendingOverlayRanks.remove(rankId)) {
                    const int count = qMax(1, m_cols);
                    const QVector<double> decoded = decodeStridedBytes(
                        data, elementType(), count, m_sliceOffset, m_sliceStride);
                    if (!decoded.isEmpty()) {
                        setRankOverlaySamples(rankId, decoded);
                        if (rankId == m_focusedRank) {
                            setSamples(decoded, 1, decoded.size());
                        }
                    }
                    return;
                }
                if (!m_awaitingCoordinatorRead || rankId != m_focusedRank) return;
                m_awaitingCoordinatorRead = false;
                loadFromRawBytes(data, elementType(), m_rows, m_cols, m_sliceOffset, m_sliceStride);
            });
    connect(m_coordinator, &IBackendCoordinator::expressionEvaluated, this,
            [this](int rankId, const QString& expr, const QString& result) {
                if (plotMode() != PlotMode::ScalarHistory) return;
                const bool expecting = m_pendingScalarRanks.contains(rankId) ||
                                       (m_awaitingScalarEval && rankId == m_focusedRank);
                if (!expecting) return;
                if (!m_pendingScalarExpr.isEmpty() && expr.trimmed() != m_pendingScalarExpr) return;
                m_pendingScalarRanks.remove(rankId);
                if (rankId == m_focusedRank) m_awaitingScalarEval = false;
                double val = 0.0;
                if (parseScalarResult(result, &val)) {
                    appendScalarSample(val, rankId);
                }
            });
    if (auto* gdb = dynamic_cast<gridlock::GdbRankCoordinator*>(m_coordinator)) {
        connect(gdb, &gridlock::GdbRankCoordinator::rankStateChanged, this,
                [this](int rankId, const gridlock::RankState& state) {
                    if (rankId == m_focusedRank && state.currentState == QLatin1String("stopped")) {
                        onTargetStopped(rankId);
                    }
                });
    } else if (auto* dap = dynamic_cast<DapCoordinator*>(m_coordinator)) {
        connect(dap, &DapCoordinator::executionStopped, this,
                [this](int rankId, const QString&) {
                    if (rankId == m_focusedRank) onTargetStopped(rankId);
                });
    }
}

ArraySliceSpec ArrayPlotDialog::syncSliceControlsFromExpression() {
    const ArraySliceSpec spec = parseArraySliceExpression(expression());
    if (spec.hasSlice) {
        setSliceOffset(spec.offset);
        setSliceStride(spec.stride);
        m_hasExplicitSliceCount = (spec.count > 0);
        if (spec.count > 0) {
            m_cols = spec.count;
            if (m_colsSpin && m_colsSpin->value() != m_cols) { QSignalBlocker b(m_colsSpin); m_colsSpin->setValue(m_cols); }
        }
    } else {
        m_hasExplicitSliceCount = false;
    }
    return spec;
}

void ArrayPlotDialog::setExpression(const QString& expr) { if (m_exprEdit) m_exprEdit->setText(expr.trimmed()); syncSliceControlsFromExpression(); }
QString ArrayPlotDialog::expression() const { return m_exprEdit ? m_exprEdit->text().trimmed() : QString(); }
QString ArrayPlotDialog::baseExpression() const { return parseArraySliceExpression(expression()).baseExpression; }

void ArrayPlotDialog::setElementType(ElementType type) {
    if (!m_typeCombo) return;
    if (const int idx = m_typeCombo->findData(static_cast<int>(type)); idx >= 0) m_typeCombo->setCurrentIndex(idx);
}

void ArrayPlotDialog::setElementTypeFromString(const QString& cType) {
    const QString lower = cType.toLower();
    if (lower.contains(QStringLiteral("float"))) setElementType(ElementType::Float32);
    else if (lower.contains(QStringLiteral("uint8")) || lower.contains(QStringLiteral("uchar")) || lower.contains(QStringLiteral("char"))) setElementType(ElementType::UInt8);
    else if (lower.contains(QStringLiteral("int64")) || lower.contains(QStringLiteral("long"))) setElementType(ElementType::Int64);
    else if (lower.contains(QStringLiteral("int"))) setElementType(ElementType::Int32);
    else setElementType(ElementType::Float64);
}

ArrayPlotDialog::ElementType ArrayPlotDialog::elementType() const {
    return m_typeCombo ? static_cast<ElementType>(m_typeCombo->currentData().toInt()) : ElementType::Float64;
}

void ArrayPlotDialog::setPlotMode(PlotMode mode) {
    if (m_modeCombo) { if (const int idx = m_modeCombo->findData(static_cast<int>(mode)); idx >= 0) m_modeCombo->setCurrentIndex(idx); }
    if (m_canvas) m_canvas->update();
}
ArrayPlotDialog::PlotMode ArrayPlotDialog::plotMode() const {
    return m_modeCombo ? static_cast<PlotMode>(m_modeCombo->currentData().toInt()) : PlotMode::Line1D;
}

void ArrayPlotDialog::setColormap(Colormap cm) {
    if (m_colormapCombo) { if (const int idx = m_colormapCombo->findData(static_cast<int>(cm)); idx >= 0) m_colormapCombo->setCurrentIndex(idx); }
    if (m_canvas) m_canvas->update();
}
ArrayPlotDialog::Colormap ArrayPlotDialog::colormap() const {
    return m_colormapCombo ? static_cast<Colormap>(m_colormapCombo->currentData().toInt()) : Colormap::Viridis;
}

void ArrayPlotDialog::setDimensions(int rows, int cols) {
    m_rows = qMax(1, rows); m_cols = qMax(1, cols);
    if (m_rowsSpin && m_rowsSpin->value() != m_rows) { QSignalBlocker b(m_rowsSpin); m_rowsSpin->setValue(m_rows); }
    if (m_colsSpin && m_colsSpin->value() != m_cols) { QSignalBlocker b(m_colsSpin); m_colsSpin->setValue(m_cols); }
    if (m_rows > 1) setPlotMode(PlotMode::Heatmap2D);
    if (m_canvas) m_canvas->update();
}

void ArrayPlotDialog::setSliceOffset(int offset) {
    m_sliceOffset = qMax(0, offset);
    if (m_offsetSpin && m_offsetSpin->value() != m_sliceOffset) { QSignalBlocker b(m_offsetSpin); m_offsetSpin->setValue(m_sliceOffset); }
    if (m_canvas) m_canvas->update();
}

void ArrayPlotDialog::setSliceStride(int stride) {
    m_sliceStride = qMax(1, stride);
    if (m_strideSpin && m_strideSpin->value() != m_sliceStride) { QSignalBlocker b(m_strideSpin); m_strideSpin->setValue(m_sliceStride); }
    if (m_canvas) m_canvas->update();
}

void ArrayPlotDialog::setLogScale(bool enabled) {
    if (m_logScaleCheck && m_logScaleCheck->isChecked() != enabled) m_logScaleCheck->setChecked(enabled);
    else if (m_canvas) m_canvas->setLogScale(enabled);
}
bool ArrayPlotDialog::isLogScale() const { return m_canvas ? m_canvas->isLogScale() : (m_logScaleCheck && m_logScaleCheck->isChecked()); }
void ArrayPlotDialog::resetZoom() { if (m_canvas) m_canvas->resetZoom(); }
bool ArrayPlotDialog::isZoomed() const { return m_canvas && m_canvas->isZoomed(); }
QPair<int, int> ArrayPlotDialog::visibleXRange() const { return m_canvas ? m_canvas->visibleXRange() : QPair<int, int>{0, 0}; }
QPair<int, int> ArrayPlotDialog::visibleYRange() const { return m_canvas ? m_canvas->visibleYRange() : QPair<int, int>{0, 0}; }

void ArrayPlotDialog::setFocusedRank(int rankId) {
    m_focusedRank = qMax(0, rankId);
    if (m_rankCombo) {
        const int idx = m_rankCombo->findData(m_focusedRank);
        if (idx >= 0 && m_rankCombo->currentIndex() != idx) { QSignalBlocker b(m_rankCombo); m_rankCombo->setCurrentIndex(idx); }
    }
}

void ArrayPlotDialog::setOverlayRanksEnabled(bool enabled) { if (m_overlayRanksCheck) m_overlayRanksCheck->setChecked(enabled); }
bool ArrayPlotDialog::isOverlayRanksEnabled() const { return m_overlayRanksCheck && m_overlayRanksCheck->isChecked(); }
void ArrayPlotDialog::setRankSubsetSpec(const QString& spec) { if (m_rankSubsetEdit) m_rankSubsetEdit->setText(spec.trimmed()); }
QString ArrayPlotDialog::rankSubsetSpec() const { return m_rankSubsetEdit ? m_rankSubsetEdit->text().trimmed() : QStringLiteral("All"); }
QVector<int> ArrayPlotDialog::selectedOverlayRanks(int totalRanks) const {
    return parseRankSubset(rankSubsetSpec(), (totalRanks >= 0) ? totalRanks : (m_coordinator ? m_coordinator->getProcessCount() : 0));
}
void ArrayPlotDialog::expectOverlayReadForRank(int rankId) { m_pendingOverlayRanks.insert(qMax(0, rankId)); }
bool ArrayPlotDialog::isAutoRefreshEnabled() const { return m_autoRefreshCheck ? m_autoRefreshCheck->isChecked() : true; }
void ArrayPlotDialog::setAutoRefreshEnabled(bool enabled) { if (m_autoRefreshCheck) m_autoRefreshCheck->setChecked(enabled); }

bool ArrayPlotDialog::loadFromRawBytes(const QByteArray& rawBytes, const QString& typeName,
                                       int rows, int cols, int offset, int stride) {
    setElementTypeFromString(typeName);
    return loadFromRawBytes(rawBytes, elementType(), rows, cols, offset, stride);
}

bool ArrayPlotDialog::loadFromRawBytes(const QByteArray& rawBytes, ElementType type,
                                       int rows, int cols, int offset, int stride) {
    setElementType(type);
    const int effOffset = (offset >= 0) ? offset : m_sliceOffset;
    const int effStride = (stride > 0) ? stride : m_sliceStride;
    if (offset >= 0) setSliceOffset(effOffset);
    if (stride > 0) setSliceStride(effStride);

    const int effRows = qMax(1, rows);
    int requestedCount = 0;
    if (cols > 0) {
        requestedCount = effRows * cols;
    } else if (m_hasExplicitSliceCount && effRows == 1) {
        requestedCount = qMax(1, m_cols);
    }

    const QVector<double> decoded =
        decodeStridedBytes(rawBytes, type, requestedCount, effOffset, effStride);
    if (decoded.isEmpty()) return false;

    const int effCols = (cols > 0) ? cols : qMax(1, static_cast<int>(decoded.size()) / effRows);
    setSamples(decoded, effRows, effCols);
    return true;
}

void ArrayPlotDialog::setSamples(const QVector<double>& samples, int rows, int cols) {
    if (!m_currentSamples.isEmpty()) {
        m_previousSamples = m_currentSamples;
    }
    m_currentSamples = samples;
    const int effRows = qMax(1, rows);
    const int effCols = (cols > 0) ? cols : qMax(1, static_cast<int>(samples.size()) / effRows);
    setDimensions(effRows, effCols);
    recomputeStats();
    if (m_canvas) m_canvas->update();
}

void ArrayPlotDialog::setRankOverlaySamples(int rankId, const QVector<double>& samples) {
    m_rankOverlays.insert(rankId, samples);
    if (m_canvas) m_canvas->update();
}

void ArrayPlotDialog::clearRankOverlays() {
    m_rankOverlays.clear();
    if (m_canvas) m_canvas->update();
}

void ArrayPlotDialog::appendScalarSample(double value, int rankId) {
    const int targetRank = (rankId >= 0) ? rankId : m_focusedRank;
    if (targetRank == m_focusedRank) {
        m_scalarHistory.append(value);
    }
    if (isOverlayRanksEnabled() || targetRank != m_focusedRank || !m_rankScalarHistories.isEmpty()) {
        m_rankScalarHistories[targetRank].append(value);
        if (plotMode() == PlotMode::ScalarHistory) {
            m_rankOverlays[targetRank] = m_rankScalarHistories[targetRank];
        }
    }
    if (plotMode() == PlotMode::ScalarHistory) {
        m_previousSamples.clear();
        m_currentSamples = m_scalarHistory;
        m_rows = 1;
        m_cols = qMax(1, static_cast<int>(m_scalarHistory.size()));
        recomputeStats();
        if (m_canvas) m_canvas->update();
    }
}

void ArrayPlotDialog::clearScalarHistory() {
    m_scalarHistory.clear();
    m_rankScalarHistories.clear();
    if (plotMode() == PlotMode::ScalarHistory) {
        m_currentSamples.clear();
        m_previousSamples.clear();
        m_rankOverlays.clear();
        recomputeStats();
    }
    if (m_canvas) {
        m_canvas->resetZoom();
        m_canvas->update();
    }
}

void ArrayPlotDialog::recomputeStats() {
    m_minVal = 0.0;
    m_maxVal = 0.0;
    m_meanVal = 0.0;
    m_l2Norm = 0.0;
    m_nanCount = 0;
    m_infCount = 0;
    m_changedElementCount = 0;

    double sum = 0.0;
    double sumSq = 0.0;
    int finiteCount = 0;

    for (int i = 0; i < m_currentSamples.size(); ++i) {
        const double v = m_currentSamples.at(i);
        if (std::isnan(v)) {
            ++m_nanCount;
        } else if (std::isinf(v)) {
            ++m_infCount;
        } else {
            if (finiteCount == 0 || v < m_minVal) m_minVal = v;
            if (finiteCount == 0 || v > m_maxVal) m_maxVal = v;
            sum += v;
            sumSq += v * v;
            ++finiteCount;
        }

        if (!m_previousSamples.isEmpty() && i < m_previousSamples.size()) {
            const double pv = m_previousSamples.at(i);
            const bool nanChanged = (std::isnan(v) != std::isnan(pv));
            const bool infChanged = (std::isinf(v) != std::isinf(pv));
            const bool valChanged = (!std::isnan(v) && !std::isnan(pv) && std::abs(v - pv) > 1e-12);
            if (nanChanged || infChanged || valChanged) ++m_changedElementCount;
        }
    }

    if (finiteCount > 0) {
        m_meanVal = sum / finiteCount;
        m_l2Norm = std::sqrt(sumSq);
    }

    if (m_statsLabel) {
        if (plotMode() == PlotMode::ScalarHistory) {
            m_statsLabel->setText(
                QStringLiteral("Stops: %1 | Min: %2 | Max: %3 | Mean: %4 | Latest: %5 | NaN/Inf: %6")
                    .arg(m_scalarHistory.size())
                    .arg(QString::number(m_minVal, 'g', 6),
                         QString::number(m_maxVal, 'g', 6),
                         QString::number(m_meanVal, 'g', 6),
                         m_scalarHistory.isEmpty() ? QStringLiteral("-")
                                                   : QString::number(m_scalarHistory.constLast(), 'g', 6))
                    .arg(m_nanCount + m_infCount));
        } else {
            m_statsLabel->setText(
                QStringLiteral("Min: %1 | Max: %2 | Mean: %3 | L2: %4 | NaN/Inf: %5 (%6 NaN, %7 Inf) | Changed: %8")
                    .arg(QString::number(m_minVal, 'g', 6),
                         QString::number(m_maxVal, 'g', 6),
                         QString::number(m_meanVal, 'g', 6),
                         QString::number(m_l2Norm, 'g', 6))
                    .arg(m_nanCount + m_infCount)
                    .arg(m_nanCount)
                    .arg(m_infCount)
                    .arg(m_changedElementCount));
        }
    }
}

void ArrayPlotDialog::refreshPlot() {
    const QString rawExpr = expression();
    if (plotMode() == PlotMode::ScalarHistory) {
        if (m_coordinator && !rawExpr.isEmpty()) {
            m_pendingScalarExpr = rawExpr;
            if (isOverlayRanksEnabled()) {
                for (int r : selectedOverlayRanks(m_coordinator->getProcessCount())) {
                    m_pendingScalarRanks.insert(r);
                    m_coordinator->evaluateExpression(r, rawExpr);
                }
            } else {
                m_awaitingScalarEval = true;
                m_pendingScalarRanks.insert(m_focusedRank);
                m_coordinator->evaluateExpression(m_focusedRank, rawExpr);
            }
        }
        if (m_canvas) m_canvas->update();
        return;
    }

    const ArraySliceSpec spec = syncSliceControlsFromExpression();
    const QString baseExpr = spec.baseExpression;
    if (!baseExpr.isEmpty()) {
        const int elemCount = qMax(1, m_rows) * qMax(1, m_cols);
        const int spanElems = m_sliceOffset + (elemCount - 1) * qMax(1, m_sliceStride) + 1;
        const int totalBytes = spanElems * byteSizeOf(elementType());
        const QString addrExpr = (baseExpr.startsWith(QLatin1Char('&')) || baseExpr.startsWith(QStringLiteral("0x")))
                                     ? baseExpr
                                     : QStringLiteral("&(%1)[0]").arg(baseExpr);

        if (isOverlayRanksEnabled() && plotMode() != PlotMode::Heatmap2D && m_rows == 1) {
            m_pendingOverlayRanks.clear();
            emit multiRankPlotRequested(baseExpr, totalBytes, elementType(), m_sliceOffset, m_sliceStride);
            // Fallback dispatch when no external coordinator bridge (e.g., MainWindow) handled the signal.
            if (m_pendingOverlayRanks.isEmpty() && m_coordinator) {
                for (int r : selectedOverlayRanks(m_coordinator->getProcessCount())) {
                    expectOverlayReadForRank(r);
                    m_coordinator->readMemory(r, addrExpr, totalBytes);
                }
            }
        } else if (m_coordinator) {
            m_awaitingCoordinatorRead = true;
            m_coordinator->readMemory(m_focusedRank, addrExpr, totalBytes);
        }
    }
    if (m_canvas) m_canvas->update();
}

void ArrayPlotDialog::onTargetStopped(int rankId) {
    setFocusedRank(rankId);
    if (!isAutoRefreshEnabled()) return;
    ++m_stopRefreshCount;
    refreshPlot();
}

bool ArrayPlotDialog::exportCsv(const QString& filePath) const {
    if (filePath.trimmed().isEmpty() || m_currentSamples.isEmpty()) return false;
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;

    QTextStream out(&file);
    if (plotMode() == PlotMode::ScalarHistory) {
        out << "stop,value\n";
        for (int i = 0; i < m_currentSamples.size(); ++i) {
            out << i << "," << QString::number(m_currentSamples.at(i), 'g', 12) << "\n";
        }
    } else if (m_rows > 1) {
        out << "row,col,value\n";
        for (int r = 0; r < m_rows; ++r) {
            for (int c = 0; c < m_cols; ++c) {
                const int idx = r * m_cols + c;
                if (idx >= m_currentSamples.size()) break;
                out << r << "," << c << "," << QString::number(m_currentSamples.at(idx), 'g', 12) << "\n";
            }
        }
    } else {
        out << "index,value,prev_value,delta\n";
        for (int i = 0; i < m_currentSamples.size(); ++i) {
            const double cur = m_currentSamples.at(i);
            const double prev = (i < m_previousSamples.size()) ? m_previousSamples.at(i) : cur;
            const double delta = cur - prev;
            out << (m_sliceOffset + i * m_sliceStride) << ","
                << QString::number(cur, 'g', 12) << ","
                << QString::number(prev, 'g', 12) << ","
                << QString::number(delta, 'g', 12) << "\n";
        }
    }
    file.close();
    return true;
}

bool ArrayPlotDialog::savePng(const QString& filePath) {
    if (!m_canvas || filePath.trimmed().isEmpty()) return false;
    return m_canvas->grab().save(filePath, "PNG");
}

bool ArrayPlotDialog::openInGnuplot() const {
    if (m_currentSamples.isEmpty()) return false;
    const QString dataPath = QDir::temp().filePath(QStringLiteral("gridlock_array_plot.dat"));
    const QString scriptPath = QDir::temp().filePath(QStringLiteral("gridlock_array_plot.gp"));

    QFile df(dataPath);
    if (!df.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream dout(&df);
    for (int i = 0; i < m_currentSamples.size(); ++i) {
        const int x = (plotMode() == PlotMode::ScalarHistory) ? i : (m_sliceOffset + i * m_sliceStride);
        dout << x << " " << QString::number(m_currentSamples.at(i), 'g', 12) << "\n";
    }
    df.close();

    QFile sf(scriptPath);
    if (!sf.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream sout(&sf);
    sout << "set grid\n";
    if (isLogScale()) sout << "set logscale y\n";
    sout << "set title 'GridLock Array Plot: " << expression() << "'\n";
    sout << "plot '" << dataPath << "' using 1:2 with linespoints title 'Rank " << m_focusedRank << "'\n";
    sf.close();

    const QString gnuplotBin = QStandardPaths::findExecutable(QStringLiteral("gnuplot"));
    if (!gnuplotBin.isEmpty()) {
        QProcess::startDetached(gnuplotBin, {QStringLiteral("-persist"), scriptPath});
    }
    return true;
}

void ArrayPlotDialog::setHoverSummaryText(const QString& text) {
    if (m_hoverLabel) m_hoverLabel->setText(text);
}

void ArrayPlotDialog::reloadStyle() {
    const auto& tm = gridlock::core::ThemeManager::instance();
    const auto pal = gridlock::core::ThemeManager::paletteForTheme(tm.activeThemeId());
    const QString bg = pal.isDark ? QStringLiteral("#141821") : QStringLiteral("#f8fafc");
    const QString inputBg = pal.isDark ? QStringLiteral("#0d1117") : QStringLiteral("#ffffff");
    const QString btnBg = pal.isDark ? QStringLiteral("#252b38") : QStringLiteral("#e2e8f0");
    const QString muted = pal.isDark ? QStringLiteral("#94a3b8") : QStringLiteral("#475569");

    setStyleSheet(QStringLiteral(
        "QDialog#ArrayPlotDialog { background: %1; color: %2; }"
        "QLineEdit, QComboBox, QSpinBox { background: %3; color: %2; border: 1px solid %4; border-radius: 3px; padding: 2px 6px; font-family: 'JetBrains Mono', monospace; font-size: 11px; }"
        "QPushButton { background: %5; color: %2; border: 1px solid %4; border-radius: 3px; padding: 2px 8px; font-size: 11px; }"
        "QPushButton:hover { border: 1px solid %6; }"
        "QPushButton:disabled { color: %7; }"
        "QLabel#arrayPlotStatsLabel { color: %6; font-family: 'JetBrains Mono', monospace; font-size: 10px; font-weight: bold; }"
        "QLabel#arrayPlotHoverLabel { color: %7; font-family: 'JetBrains Mono', monospace; font-size: 10px; }")
                      .arg(bg, pal.textHex, inputBg, pal.borderHex, btnBg, pal.accentHex, muted));
    if (m_canvas) m_canvas->update();
}

}  // namespace gridlock::ui
