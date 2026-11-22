/**
 * @file ArrayPlotDialog.cpp
 * @brief UI layout, coordinator signal binding, and slice/rank control logic for ArrayPlotDialog.
 *
 * Binary buffer decoding, complex projections, statistical aggregation, and CSV/PNG/Gnuplot
 * export routines reside in ArrayPlotExport.cpp.
 */

#include "ArrayPlotDialog.hpp"

#include <algorithm>
#include <utility>

#include <QAbstractItemView>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include "../../../core/hpc/dap/DapCoordinator.hpp"
#include "../../../core/hpc/gdb/GdbRankCoordinator.hpp"
#include "../../../core/hpc/IBackendCoordinator.hpp"

namespace gridlock::ui {

ArrayPlotDialog::ArrayPlotDialog(::IBackendCoordinator* coordinator, QWidget* parent)
    : QDialog(parent, Qt::Window) {
    setObjectName(QStringLiteral("ArrayPlotDialog"));
    setWindowTitle(QStringLiteral("Scientific 1D/2D/3D Array, Complex & Tensor Plotter"));
    setModal(false); setMinimumSize(960, 560); resize(1080, 640);

    auto* root = new QVBoxLayout(this); root->setContentsMargins(10, 10, 10, 10); root->setSpacing(6);
    auto makeCombo = [this](const QString& name, int h) {
        auto* cb = new QComboBox(this); cb->setObjectName(name); cb->setFixedHeight(h);
        connect(cb, &QComboBox::currentIndexChanged, this, [cb](int idx) {
            if (idx >= 0) cb->setToolTip(cb->itemData(idx, Qt::ToolTipRole).toString());
        });
        return cb;
    };
    auto addItemWithTip = [](QComboBox* cb, const QString& label, const QVariant& data, const QString& tip) {
        const int idx = cb->count(); cb->addItem(label, data); cb->setItemData(idx, tip, Qt::ToolTipRole);
        if (idx == 0) cb->setToolTip(tip);
    };
    auto makeSpin = [this](const QString& name, int minV, int maxV, int initV, int maxW) {
        auto* sp = new QSpinBox(this); sp->setObjectName(name); sp->setRange(minV, maxV);
        sp->setValue(initV); sp->setFixedHeight(24); sp->setMaximumWidth(maxW); return sp;
    };
    auto makeBtn = [this](const QString& text, const QString& name, int h = 24) {
        auto* b = new QPushButton(text, this); b->setObjectName(name); b->setFixedHeight(h); return b;
    };
    auto makeCheck = [this](const QString& text, const QString& name) {
        auto* c = new QCheckBox(text, this); c->setObjectName(name); c->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed); return c;
    };

    // Row 1: Expression with live autocomplete, ElementType, ComplexComponent, PlotMode, Colormap, Refresh, Close.
    auto* topRow = new QHBoxLayout(); topRow->setSpacing(6);
    m_exprEdit = new QLineEdit(this); m_exprEdit->setObjectName(QStringLiteral("arrayPlotExprEdit"));
    m_exprEdit->setPlaceholderText(QStringLiteral("Expr or slice (e.g., u[16:128:2], data[0], residual)..."));
    m_exprEdit->setFixedHeight(26);
    m_completerModel = new QStringListModel(this);
    m_completer = new QCompleter(m_completerModel, this);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setFilterMode(Qt::MatchContains);
    if (auto* pop = m_completer->popup()) {
        pop->setObjectName(QStringLiteral("arrayPlotCompleterPopup"));
        pop->setStyleSheet(QStringLiteral("QAbstractItemView { background-color: #1e222a; color: #e5e9f0; selection-background-color: #3b82f6; selection-color: #ffffff; border: 1px solid #3b4252; padding: 2px; font-size: 12px; }"));
    }
    m_exprEdit->setCompleter(m_completer);

    m_typeCombo = makeCombo(QStringLiteral("arrayPlotTypeCombo"), 26);
    addItemWithTip(m_typeCombo, QStringLiteral("double"), static_cast<int>(ElementType::Float64), QStringLiteral("64-bit IEEE-754 double-precision floating point (8 bytes)"));
    addItemWithTip(m_typeCombo, QStringLiteral("float"), static_cast<int>(ElementType::Float32), QStringLiteral("32-bit IEEE-754 single-precision floating point (4 bytes)"));
    addItemWithTip(m_typeCombo, QStringLiteral("int32_t"), static_cast<int>(ElementType::Int32), QStringLiteral("32-bit signed two's-complement integer (4 bytes)"));
    addItemWithTip(m_typeCombo, QStringLiteral("int64_t"), static_cast<int>(ElementType::Int64), QStringLiteral("64-bit signed two's-complement integer (8 bytes)"));
    addItemWithTip(m_typeCombo, QStringLiteral("uint8_t"), static_cast<int>(ElementType::UInt8), QStringLiteral("8-bit unsigned byte buffer (1 byte)"));
    addItemWithTip(m_typeCombo, QStringLiteral("Complex64"), static_cast<int>(ElementType::Complex64), QStringLiteral("Interleaved std::complex<float> / float _Complex (2 x 32-bit float: Re, Im)"));
    addItemWithTip(m_typeCombo, QStringLiteral("Complex128"), static_cast<int>(ElementType::Complex128), QStringLiteral("Interleaved std::complex<double> / double _Complex (2 x 64-bit double: Re, Im)"));

    m_componentCombo = makeCombo(QStringLiteral("arrayPlotComponentCombo"), 26);
    addItemWithTip(m_componentCombo, QStringLiteral("Magnitude |z|"), static_cast<int>(ComplexComponent::Magnitude), QStringLiteral("Complex modulus sqrt(Re(z)^2 + Im(z)^2)"));
    addItemWithTip(m_componentCombo, QStringLiteral("Phase arg(z)"), static_cast<int>(ComplexComponent::Phase), QStringLiteral("Complex phase angle atan2(Im(z), Re(z)) in radians [-pi, pi]"));
    addItemWithTip(m_componentCombo, QStringLiteral("Real Re(z)"), static_cast<int>(ComplexComponent::Real), QStringLiteral("Real component Re(z)"));
    addItemWithTip(m_componentCombo, QStringLiteral("Imag Im(z)"), static_cast<int>(ComplexComponent::Imaginary), QStringLiteral("Imaginary component Im(z)"));
    m_componentCombo->setEnabled(false);

    m_modeCombo = makeCombo(QStringLiteral("arrayPlotModeCombo"), 26);
    addItemWithTip(m_modeCombo, QStringLiteral("1D Line Plot"), static_cast<int>(PlotMode::Line1D), QStringLiteral("Continuous 1D line series with optional previous-stop delta curve and multi-rank overlay"));
    addItemWithTip(m_modeCombo, QStringLiteral("1D Bar / Stem"), static_cast<int>(PlotMode::Stem1D), QStringLiteral("Discrete 1D stem/impulse plot suited for FFT bins and histograms"));
    addItemWithTip(m_modeCombo, QStringLiteral("2D Heatmap"), static_cast<int>(PlotMode::Heatmap2D), QStringLiteral("2D false-color matrix heatmap over [Ny x Nx] cells"));
    addItemWithTip(m_modeCombo, QStringLiteral("Scalar History"), static_cast<int>(PlotMode::ScalarHistory), QStringLiteral("Scalar convergence time-series accumulated across debugger stops"));

    m_colormapCombo = makeCombo(QStringLiteral("arrayPlotColormapCombo"), 26);
    addItemWithTip(m_colormapCombo, QStringLiteral("Viridis"), static_cast<int>(Colormap::Viridis), QStringLiteral("Perceptually uniform Viridis scientific colormap"));
    addItemWithTip(m_colormapCombo, QStringLiteral("Plasma"), static_cast<int>(Colormap::Plasma), QStringLiteral("High-contrast Plasma thermal colormap"));
    addItemWithTip(m_colormapCombo, QStringLiteral("Cool-Warm"), static_cast<int>(Colormap::CoolWarm), QStringLiteral("Diverging blue-to-red Cool-Warm colormap for signed fields"));
    addItemWithTip(m_colormapCombo, QStringLiteral("Grayscale"), static_cast<int>(Colormap::Grayscale), QStringLiteral("Linear luminance grayscale colormap"));

    m_refreshBtn = makeBtn(QStringLiteral("Plot / Refresh"), QStringLiteral("arrayPlotRefreshBtn"), 26);
    m_closeBtn = makeBtn(QStringLiteral("✕ CLOSE"), QStringLiteral("arrayPlotCloseBtn"), 26);

    topRow->addWidget(new QLabel(QStringLiteral("Expr:"), this)); topRow->addWidget(m_exprEdit, 1);
    topRow->addWidget(new QLabel(QStringLiteral("Type:"), this)); topRow->addWidget(m_typeCombo);
    topRow->addWidget(m_componentCombo); topRow->addWidget(m_modeCombo); topRow->addWidget(m_colormapCombo);
    topRow->addWidget(m_refreshBtn); topRow->addWidget(m_closeBtn);
    root->addLayout(topRow);

    // Row 2: Tensor shape (Ny, Nx, Nz, Z, Memory Order), Slicing (Off, Str), Log10, Zoom, History.
    auto* sliceRow = new QHBoxLayout(); sliceRow->setSpacing(5);
    m_rowsSpin = makeSpin(QStringLiteral("arrayPlotRowsSpin"), 1, 8192, 1, 64);
    m_colsSpin = makeSpin(QStringLiteral("arrayPlotColsSpin"), 1, 65536, 64, 70);
    m_depthSpin = makeSpin(QStringLiteral("arrayPlotDepthSpin"), 1, 4096, 1, 60);
    m_zSliceSpin = makeSpin(QStringLiteral("arrayPlotZSliceSpin"), 0, 0, 0, 56);
    m_zSliceSpin->setEnabled(false);

    m_memoryOrderCombo = makeCombo(QStringLiteral("arrayPlotMemoryOrderCombo"), 24);
    addItemWithTip(m_memoryOrderCombo, QStringLiteral("C Row-Major"), static_cast<int>(TensorSliceTableModel::MemoryOrder::CRowMajor), QStringLiteral("C/C++ row-major contiguous layout: ((z * Ny + row) * Nx) + col"));
    addItemWithTip(m_memoryOrderCombo, QStringLiteral("Fortran Col"), static_cast<int>(TensorSliceTableModel::MemoryOrder::FortranColMajor), QStringLiteral("Fortran/BLAS column-major contiguous layout: ((z * Nx + col) * Ny) + row"));

    m_offsetSpin = makeSpin(QStringLiteral("arrayPlotOffsetSpin"), 0, 10000000, 0, 72);
    m_strideSpin = makeSpin(QStringLiteral("arrayPlotStrideSpin"), 1, 65536, 1, 58);
    m_logScaleCheck = makeCheck(QStringLiteral("Log10"), QStringLiteral("arrayPlotLogScaleCheck"));
    m_resetZoomBtn = makeBtn(QStringLiteral("Reset Zoom"), QStringLiteral("arrayPlotResetZoomBtn"));
    m_resetZoomBtn->setEnabled(false);
    m_clearHistoryBtn = makeBtn(QStringLiteral("Clear History"), QStringLiteral("arrayPlotClearHistoryBtn"));
    m_undoClearBtn = makeBtn(QStringLiteral("Undo Clear"), QStringLiteral("arrayPlotUndoClearBtn"));
    m_undoClearBtn->setEnabled(false);

    sliceRow->addWidget(new QLabel(QStringLiteral("Ny:"), this)); sliceRow->addWidget(m_rowsSpin);
    sliceRow->addWidget(new QLabel(QStringLiteral("Nx:"), this)); sliceRow->addWidget(m_colsSpin);
    sliceRow->addWidget(new QLabel(QStringLiteral("Nz:"), this)); sliceRow->addWidget(m_depthSpin);
    sliceRow->addWidget(new QLabel(QStringLiteral("Z:"), this));  sliceRow->addWidget(m_zSliceSpin);
    sliceRow->addWidget(m_memoryOrderCombo);
    sliceRow->addWidget(new QLabel(QStringLiteral("Off:"), this)); sliceRow->addWidget(m_offsetSpin);
    sliceRow->addWidget(new QLabel(QStringLiteral("Str:"), this)); sliceRow->addWidget(m_strideSpin);
    sliceRow->addWidget(m_logScaleCheck); sliceRow->addWidget(m_resetZoomBtn); sliceRow->addWidget(m_clearHistoryBtn); sliceRow->addWidget(m_undoClearBtn);
    sliceRow->addStretch(1); root->addLayout(sliceRow);

    // Row 3: Chart / Tensor Table view switcher, Rank controls, Auto-Refresh, and Exports.
    auto* rankRow = new QHBoxLayout(); rankRow->setSpacing(6);
    m_chartViewBtn = makeBtn(QStringLiteral("Chart"), QStringLiteral("arrayPlotChartViewBtn")); m_chartViewBtn->setCheckable(true); m_chartViewBtn->setChecked(true);
    m_tableViewBtn = makeBtn(QStringLiteral("Tensor Table"), QStringLiteral("arrayPlotTableViewBtn")); m_tableViewBtn->setCheckable(true);

    m_rankCombo = makeCombo(QStringLiteral("arrayPlotRankCombo"), 24);
    const int initRanks = qMax(1, coordinator ? coordinator->getProcessCount() : 1);
    for (int r = 0; r < initRanks; ++r) addItemWithTip(m_rankCombo, QStringLiteral("Rank %1").arg(r), r, QStringLiteral("Inspect array memory on MPI Rank %1").arg(r));
    m_overlayRanksCheck = makeCheck(QStringLiteral("Overlay Ranks"), QStringLiteral("arrayPlotOverlayAllRanksCheck"));
    m_rankSubsetEdit = new QLineEdit(QStringLiteral("All"), this);
    m_rankSubsetEdit->setObjectName(QStringLiteral("arrayPlotRankSubsetEdit"));
    m_rankSubsetEdit->setPlaceholderText(QStringLiteral("All or 0..3"));
    m_rankSubsetEdit->setFixedSize(92, 24);
    m_autoRefreshCheck = makeCheck(QStringLiteral("Auto-Refresh"), QStringLiteral("arrayPlotAutoRefreshCheck"));
    m_autoRefreshCheck->setChecked(true);

    m_exportCsvBtn = makeBtn(QStringLiteral("Export CSV"), QStringLiteral("arrayPlotExportCsvBtn"));
    m_savePngBtn = makeBtn(QStringLiteral("Save PNG"), QStringLiteral("arrayPlotSavePngBtn"));
    m_gnuplotBtn = makeBtn(QStringLiteral("Open in gnuplot"), QStringLiteral("arrayPlotGnuplotBtn"));

    rankRow->addWidget(m_chartViewBtn); rankRow->addWidget(m_tableViewBtn);
    rankRow->addWidget(m_rankCombo); rankRow->addWidget(m_overlayRanksCheck);
    rankRow->addWidget(new QLabel(QStringLiteral("Ranks:"), this)); rankRow->addWidget(m_rankSubsetEdit);
    rankRow->addWidget(m_autoRefreshCheck); rankRow->addStretch(1);
    rankRow->addWidget(m_exportCsvBtn); rankRow->addWidget(m_savePngBtn); rankRow->addWidget(m_gnuplotBtn);
    root->addLayout(rankRow);

    // Central stacked view: [0] = ArrayPlotCanvas, [1] = virtualized TensorSliceTableModel QTableView.
    m_viewStack = new QStackedWidget(this);
    m_viewStack->setObjectName(QStringLiteral("arrayPlotViewStack"));
    m_canvas = new ArrayPlotCanvas(this);
    m_viewStack->addWidget(m_canvas);

    m_tensorModel = new TensorSliceTableModel(this);
    m_tensorModel->setSourceData(&m_currentSamples, nullptr);
    m_tensorModel->setShape(m_cols, m_rows, m_depth);

    m_tensorTableView = new QTableView(this);
    m_tensorTableView->setObjectName(QStringLiteral("arrayPlotTensorTableView"));
    m_tensorTableView->setModel(m_tensorModel);
    m_tensorTableView->setAlternatingRowColors(true);
    m_tensorTableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_tensorTableView->horizontalHeader()->setDefaultSectionSize(90);
    m_tensorTableView->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_tensorTableView->verticalHeader()->setDefaultSectionSize(22);
    m_viewStack->addWidget(m_tensorTableView);
    root->addWidget(m_viewStack, 1);

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
    connect(m_typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        if (m_componentCombo) m_componentCombo->setEnabled(isComplexType(elementType()));
    });
    connect(m_componentCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { recomputeComplexProjection(); });
    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        if (plotMode() == PlotMode::ScalarHistory) {
            m_currentSamples = m_scalarHistory; m_previousSamples.clear(); m_complexPairs.clear();
            m_rows = 1; m_cols = qMax(1, static_cast<int>(m_scalarHistory.size()));
            m_rankOverlays = m_rankScalarHistories;
            m_tensorModel->setSourceData(&m_currentSamples, nullptr);
            m_tensorModel->setShape(m_cols, m_rows, m_depth);
            recomputeStats();
        }
        if (m_canvas) { m_canvas->resetZoom(); m_canvas->update(); }
    });
    connect(m_colormapCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { if (m_canvas) m_canvas->update(); });
    connect(m_rowsSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int r) {
        m_rows = qMax(1, r);
        if (m_rows > 1 && m_modeCombo) m_modeCombo->setCurrentIndex(static_cast<int>(PlotMode::Heatmap2D));
        m_tensorModel->setShape(m_cols, m_rows, m_depth);
        if (m_canvas) m_canvas->update();
    });
    connect(m_colsSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int c) {
        m_cols = qMax(1, c); m_tensorModel->setShape(m_cols, m_rows, m_depth); if (m_canvas) m_canvas->update();
    });
    connect(m_depthSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int d) {
        m_depth = qMax(1, d);
        if (m_zSliceSpin) { m_zSliceSpin->setRange(0, qMax(0, m_depth - 1)); m_zSliceSpin->setEnabled(m_depth > 1); }
        m_tensorModel->setShape(m_cols, m_rows, m_depth);
    });
    connect(m_zSliceSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int z) { m_tensorModel->setZSlice(z); });
    connect(m_memoryOrderCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { m_tensorModel->setMemoryOrder(memoryOrder()); });
    connect(m_offsetSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int off) { m_sliceOffset = qMax(0, off); if (m_canvas) m_canvas->update(); });
    connect(m_strideSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int str) { m_sliceStride = qMax(1, str); if (m_canvas) m_canvas->update(); });
    connect(m_logScaleCheck, &QCheckBox::toggled, this, [this](bool enabled) { if (m_canvas) m_canvas->setLogScale(enabled); });
    connect(m_resetZoomBtn, &QPushButton::clicked, this, &ArrayPlotDialog::resetZoom);
    connect(m_canvas, &ArrayPlotCanvas::zoomChanged, m_resetZoomBtn, &QPushButton::setEnabled);
    connect(m_clearHistoryBtn, &QPushButton::clicked, this, &ArrayPlotDialog::clearScalarHistory);
    connect(m_undoClearBtn, &QPushButton::clicked, this, &ArrayPlotDialog::undoClearHistory);
    connect(m_chartViewBtn, &QPushButton::clicked, this, [this]() { setTensorTableActive(false); });
    connect(m_tableViewBtn, &QPushButton::clicked, this, [this]() { setTensorTableActive(true); });
    connect(m_rankCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        if (idx >= 0) { m_focusedRank = m_rankCombo->itemData(idx).toInt(); if (isAutoRefreshEnabled()) refreshPlot(); }
    });
    connect(m_overlayRanksCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (!checked) { m_pendingOverlayRanks.clear(); clearRankOverlays(); } else refreshPlot();
    });
    connect(m_rankSubsetEdit, &QLineEdit::returnPressed, this, [this]() { if (isOverlayRanksEnabled()) refreshPlot(); });
    connect(m_exportCsvBtn, &QPushButton::clicked, this, [this]() {
        const QString p = QFileDialog::getSaveFileName(this, QStringLiteral("Export Array CSV"), QStringLiteral("array_plot.csv"), QStringLiteral("CSV Files (*.csv)"));
        if (!p.isEmpty()) exportCsv(p);
    });
    connect(m_savePngBtn, &QPushButton::clicked, this, [this]() {
        const QString p = QFileDialog::getSaveFileName(this, QStringLiteral("Save Plot PNG"), QStringLiteral("array_plot.png"), QStringLiteral("PNG Images (*.png)"));
        if (!p.isEmpty()) savePng(p);
    });
    connect(m_gnuplotBtn, &QPushButton::clicked, this, [this]() { openInGnuplot(); });

    setCoordinator(coordinator);
    reloadStyle();
}

void ArrayPlotDialog::setCoordinator(::IBackendCoordinator* coordinator) {
    if (m_coordinator == coordinator) { setFocusedRank(m_focusedRank); return; }
    if (m_coordinator) disconnect(m_coordinator, nullptr, this, nullptr);
    m_coordinator = coordinator;
    setFocusedRank(m_focusedRank);
    if (!m_coordinator) return;

    connect(m_coordinator, &IBackendCoordinator::memoryRead, this,
            [this](int rankId, const QString& /*address*/, const QByteArray& data) {
                if (data.isEmpty()) return;
                if (m_pendingOverlayRanks.remove(rankId)) {
                    const int count = qMax(1, m_cols);
                    QVector<QPair<double, double>> pairs;
                    const QVector<double> decoded = decodeStridedBytes(
                        data, elementType(), complexComponent(), count, m_sliceOffset, m_sliceStride,
                        (rankId == m_focusedRank && isComplexType(elementType())) ? &pairs : nullptr);
                    if (!decoded.isEmpty()) {
                        setRankOverlaySamples(rankId, decoded);
                        if (rankId == m_focusedRank) {
                            setSamples(decoded, 1, decoded.size());
                            m_complexPairs = std::move(pairs);
                            m_tensorModel->setSourceData(&m_currentSamples,
                                                         m_complexPairs.isEmpty() ? nullptr : &m_complexPairs);
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
                if (parseScalarResult(result, &val)) appendScalarSample(val, rankId);
            });
    if (auto* gdb = dynamic_cast<gridlock::GdbRankCoordinator*>(m_coordinator)) {
        connect(gdb, &gridlock::GdbRankCoordinator::rankStateChanged, this,
                [this](int rankId, const gridlock::RankState& state) {
                    if (rankId == m_focusedRank && state.currentState == QLatin1String("stopped")) onTargetStopped(rankId);
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
            if (m_tensorModel) m_tensorModel->setShape(m_cols, m_rows, m_depth);
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
    if (m_componentCombo) m_componentCombo->setEnabled(isComplexType(type));
}

void ArrayPlotDialog::setElementTypeFromString(const QString& cType) {
    const QString lower = cType.toLower();
    if (lower.contains(QStringLiteral("complex64")) || (lower.contains(QStringLiteral("complex")) && lower.contains(QStringLiteral("float")))) setElementType(ElementType::Complex64);
    else if (lower.contains(QStringLiteral("complex"))) setElementType(ElementType::Complex128);
    else if (lower.contains(QStringLiteral("float"))) setElementType(ElementType::Float32);
    else if (lower.contains(QStringLiteral("uint8")) || lower.contains(QStringLiteral("uchar")) || lower.contains(QStringLiteral("char"))) setElementType(ElementType::UInt8);
    else if (lower.contains(QStringLiteral("int64")) || lower.contains(QStringLiteral("long"))) setElementType(ElementType::Int64);
    else if (lower.contains(QStringLiteral("int"))) setElementType(ElementType::Int32);
    else setElementType(ElementType::Float64);
}

ArrayPlotDialog::ElementType ArrayPlotDialog::elementType() const {
    return m_typeCombo ? static_cast<ElementType>(m_typeCombo->currentData().toInt()) : ElementType::Float64;
}

void ArrayPlotDialog::setComplexComponent(ComplexComponent component) {
    if (!m_componentCombo) return;
    if (const int idx = m_componentCombo->findData(static_cast<int>(component)); idx >= 0) {
        if (m_componentCombo->currentIndex() != idx) m_componentCombo->setCurrentIndex(idx);
        else recomputeComplexProjection();
    }
}

ArrayPlotDialog::ComplexComponent ArrayPlotDialog::complexComponent() const {
    return m_componentCombo ? static_cast<ComplexComponent>(m_componentCombo->currentData().toInt())
                            : ComplexComponent::Magnitude;
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

void ArrayPlotDialog::setDimensions(int rows, int cols) { setTensorDimensions(cols, rows, m_depth); }

void ArrayPlotDialog::setTensorDimensions(int nx, int ny, int nz) {
    m_cols = qMax(1, nx); m_rows = qMax(1, ny); m_depth = qMax(1, nz);
    if (m_rowsSpin && m_rowsSpin->value() != m_rows) { QSignalBlocker b(m_rowsSpin); m_rowsSpin->setValue(m_rows); }
    if (m_colsSpin && m_colsSpin->value() != m_cols) { QSignalBlocker b(m_colsSpin); m_colsSpin->setValue(m_cols); }
    if (m_depthSpin && m_depthSpin->value() != m_depth) { QSignalBlocker b(m_depthSpin); m_depthSpin->setValue(m_depth); }
    if (m_zSliceSpin) {
        QSignalBlocker b(m_zSliceSpin);
        m_zSliceSpin->setRange(0, qMax(0, m_depth - 1));
        m_zSliceSpin->setValue(std::clamp(m_zSliceSpin->value(), 0, qMax(0, m_depth - 1)));
        m_zSliceSpin->setEnabled(m_depth > 1);
    }
    if (m_tensorModel) m_tensorModel->setShape(m_cols, m_rows, m_depth);
    if (m_rows > 1) setPlotMode(PlotMode::Heatmap2D);
    if (m_canvas) m_canvas->update();
}

void ArrayPlotDialog::setZSlice(int zSlice) {
    if (m_zSliceSpin) m_zSliceSpin->setValue(std::clamp(zSlice, 0, qMax(0, m_depth - 1)));
    else if (m_tensorModel) m_tensorModel->setZSlice(zSlice);
}
int ArrayPlotDialog::zSlice() const { return m_tensorModel ? m_tensorModel->zSlice() : 0; }

void ArrayPlotDialog::setMemoryOrder(TensorSliceTableModel::MemoryOrder order) {
    if (m_memoryOrderCombo) {
        if (const int idx = m_memoryOrderCombo->findData(static_cast<int>(order)); idx >= 0) m_memoryOrderCombo->setCurrentIndex(idx);
    }
    if (m_tensorModel) m_tensorModel->setMemoryOrder(order);
}
TensorSliceTableModel::MemoryOrder ArrayPlotDialog::memoryOrder() const {
    return m_memoryOrderCombo
               ? static_cast<TensorSliceTableModel::MemoryOrder>(m_memoryOrderCombo->currentData().toInt())
               : TensorSliceTableModel::MemoryOrder::CRowMajor;
}

void ArrayPlotDialog::setTensorTableActive(bool active) {
    if (m_viewStack) m_viewStack->setCurrentIndex(active ? 1 : 0);
    if (m_chartViewBtn) m_chartViewBtn->setChecked(!active);
    if (m_tableViewBtn) m_tableViewBtn->setChecked(active);
}
bool ArrayPlotDialog::isTensorTableActive() const { return m_viewStack && m_viewStack->currentIndex() == 1; }

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
    if (!m_rankCombo) return;
    const int needed = qMax(1, qMax(m_coordinator ? m_coordinator->getProcessCount() : 0, m_focusedRank + 1));
    QSignalBlocker b(m_rankCombo);
    if (m_rankCombo->count() != needed || m_rankCombo->findData(m_focusedRank) < 0) {
        m_rankCombo->clear();
        for (int r = 0; r < needed; ++r) { m_rankCombo->addItem(QStringLiteral("Rank %1").arg(r), r); m_rankCombo->setItemData(r, QStringLiteral("Inspect array memory on MPI Rank %1").arg(r), Qt::ToolTipRole); }
    }
    if (const int idx = m_rankCombo->findData(m_focusedRank); idx >= 0 && m_rankCombo->currentIndex() != idx) m_rankCombo->setCurrentIndex(idx);
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

void ArrayPlotDialog::refreshPlot() {
    const QString rawExpr = expression();
    if (plotMode() == PlotMode::ScalarHistory) {
        if (m_coordinator && !rawExpr.isEmpty()) {
            m_pendingScalarExpr = rawExpr;
            if (isOverlayRanksEnabled()) {
                for (int r : selectedOverlayRanks(m_coordinator->getProcessCount())) {
                    m_pendingScalarRanks.insert(r); m_coordinator->evaluateExpression(r, rawExpr);
                }
            } else {
                m_awaitingScalarEval = true; m_pendingScalarRanks.insert(m_focusedRank);
                m_coordinator->evaluateExpression(m_focusedRank, rawExpr);
            }
        }
        if (m_canvas) m_canvas->update();
        return;
    }

    const ArraySliceSpec spec = syncSliceControlsFromExpression();
    const QString baseExpr = spec.baseExpression;
    if (!baseExpr.isEmpty()) {
        const int elemCount = qMax(1, m_depth) * qMax(1, m_rows) * qMax(1, m_cols);
        const int spanElems = m_sliceOffset + (elemCount - 1) * qMax(1, m_sliceStride) + 1;
        const int totalBytes = spanElems * byteSizeOf(elementType());
        const QString addrExpr = (baseExpr.startsWith(QLatin1Char('&')) || baseExpr.startsWith(QStringLiteral("0x")))
                                     ? baseExpr : QStringLiteral("&(%1)[0]").arg(baseExpr);

        if (isOverlayRanksEnabled() && plotMode() != PlotMode::Heatmap2D && m_rows == 1 && m_depth == 1) {
            m_pendingOverlayRanks.clear();
            emit multiRankPlotRequested(baseExpr, totalBytes, elementType(), m_sliceOffset, m_sliceStride);
            if (m_pendingOverlayRanks.isEmpty() && m_coordinator) {
                for (int r : selectedOverlayRanks(m_coordinator->getProcessCount())) {
                    expectOverlayReadForRank(r); m_coordinator->readMemory(r, addrExpr, totalBytes);
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
    if (isAutoRefreshEnabled()) { ++m_stopRefreshCount; refreshPlot(); }
}

}  // namespace gridlock::ui
