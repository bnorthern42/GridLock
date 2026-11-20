/**
 * @file ArrayPlotDialog.cpp
 * @brief Implementation of the live auto-refreshing 1D/2D scientific array and field plotter.
 *
 * Implements contiguous raw-memory decoding (double, float, int32_t, int64_t, uint8_t),
 * automatic refresh on debugger stop events (*stopped), previous-stop delta tracking,
 * IEEE-754 NaN/Inf anomaly highlighting, 1D Line/Stem and 2D Heatmap QPainter rendering
 * with Viridis/Plasma/CoolWarm/Grayscale colormaps, and CSV/PNG/gnuplot export.
 */

#include "ArrayPlotDialog.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QStandardPaths>
#include <QTextStream>
#include <QToolTip>
#include <QVBoxLayout>

#include "../../core/hpc/DapCoordinator.hpp"
#include "../../core/hpc/GdbRankCoordinator.hpp"
#include "../../core/hpc/IBackendCoordinator.hpp"
#include "../../core/managers/ThemeManager.hpp"

namespace gridlock::ui {

namespace {

/** @brief Linearly interpolates between two RGB colors for @p t in [0.0, 1.0]. */
QColor lerpColor(const QColor& a, const QColor& b, double t) {
    const double u = std::clamp(t, 0.0, 1.0);
    return QColor(static_cast<int>(std::round(a.red() + (b.red() - a.red()) * u)),
                  static_cast<int>(std::round(a.green() + (b.green() - a.green()) * u)),
                  static_cast<int>(std::round(a.blue() + (b.blue() - a.blue()) * u)));
}

/** @brief Samples a 5-stop color ramp for @p t in [0.0, 1.0]. */
QColor sampleRamp(double t, const QColor stops[5]) {
    const double u = std::clamp(t, 0.0, 1.0) * 4.0;
    const int idx = std::clamp(static_cast<int>(std::floor(u)), 0, 3);
    return lerpColor(stops[idx], stops[idx + 1], u - idx);
}

}  // namespace

ArrayPlotCanvas::ArrayPlotCanvas(ArrayPlotDialog* owner)
    : QWidget(owner), m_owner(owner) {
    setObjectName(QStringLiteral("arrayPlotCanvas"));
    setMouseTracking(true);
    setMinimumSize(540, 300);
}

QColor ArrayPlotCanvas::sampleColormap(double t, int colormapId) {
    static const QColor kViridis[5] = {
        QColor(68, 1, 84), QColor(59, 82, 139), QColor(33, 145, 140),
        QColor(94, 201, 98), QColor(253, 231, 37)};
    static const QColor kPlasma[5] = {
        QColor(13, 8, 135), QColor(126, 3, 168), QColor(204, 71, 120),
        QColor(248, 149, 64), QColor(240, 249, 33)};
    static const QColor kCoolWarm[5] = {
        QColor(59, 76, 192), QColor(141, 176, 254), QColor(221, 221, 221),
        QColor(244, 154, 123), QColor(180, 4, 38)};
    static const QColor kGray[5] = {
        QColor(18, 22, 28), QColor(78, 84, 94), QColor(138, 144, 154),
        QColor(196, 202, 210), QColor(248, 250, 252)};

    switch (colormapId) {
        case 1: return sampleRamp(t, kPlasma);
        case 2: return sampleRamp(t, kCoolWarm);
        case 3: return sampleRamp(t, kGray);
        default: return sampleRamp(t, kViridis);
    }
}

void ArrayPlotCanvas::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const auto& tm = gridlock::core::ThemeManager::instance();
    const auto pal = gridlock::core::ThemeManager::paletteForTheme(tm.activeThemeId());
    const bool isDark = pal.isDark;

    const QColor canvasBg = isDark ? QColor(QStringLiteral("#0f141c")) : QColor(QStringLiteral("#ffffff"));
    const QColor gridCol = isDark ? QColor(QStringLiteral("#1e293b")) : QColor(QStringLiteral("#e2e8f0"));
    const QColor axisCol = isDark ? QColor(QStringLiteral("#64748b")) : QColor(QStringLiteral("#475569"));
    const QColor textCol = isDark ? QColor(QStringLiteral("#cbd5e1")) : QColor(QStringLiteral("#1e293b"));
    const QColor lineCol = QColor(QStringLiteral("#38bdf8"));
    const QColor prevCol = isDark ? QColor(QStringLiteral("#64748b")) : QColor(QStringLiteral("#94a3b8"));
    const QColor mutCol = QColor(QStringLiteral("#f59e0b"));
    const QColor anomCol = QColor(QStringLiteral("#ec4899"));

    p.fillRect(rect(), canvasBg);
    if (!m_owner) return;

    const QVector<double>& samples = m_owner->currentSamples();
    const QVector<double>& prevSamples = m_owner->previousSamples();
    const QRect plotRect = rect().adjusted(64, 18, -72, -32);
    if (plotRect.width() < 40 || plotRect.height() < 40) return;

    p.setPen(QPen(axisCol, 1.0));
    p.drawRect(plotRect);

    if (samples.isEmpty()) {
        p.setPen(axisCol);
        p.drawText(plotRect, Qt::AlignCenter,
                   QStringLiteral("No array samples loaded. Enter an expression and click Plot / Refresh."));
        return;
    }

    const double minV = m_owner->minValue();
    const double maxV = m_owner->maxValue();
    const double span = (std::abs(maxV - minV) > 1e-15) ? (maxV - minV) : 1.0;

    // 2D Heatmap rendering path.
    if (m_owner->plotMode() == ArrayPlotDialog::PlotMode::Heatmap2D || m_owner->rows() > 1) {
        const int ny = qMax(1, m_owner->rows());
        const int nx = qMax(1, m_owner->cols());
        const int cmId = static_cast<int>(m_owner->colormap());
        const double cellW = static_cast<double>(plotRect.width()) / nx;
        const double cellH = static_cast<double>(plotRect.height()) / ny;

        for (int r = 0; r < ny; ++r) {
            for (int c = 0; c < nx; ++c) {
                const int idx = r * nx + c;
                const QRectF cellRect(plotRect.left() + c * cellW, plotRect.top() + r * cellH,
                                      std::ceil(cellW), std::ceil(cellH));
                if (idx >= samples.size()) {
                    p.fillRect(cellRect, canvasBg);
                    continue;
                }
                const double val = samples.at(idx);
                if (std::isnan(val) || std::isinf(val)) {
                    p.fillRect(cellRect, anomCol);
                    p.setPen(QPen(Qt::white, 1.4));
                    p.drawLine(cellRect.topLeft(), cellRect.bottomRight());
                    p.drawLine(cellRect.topRight(), cellRect.bottomLeft());
                } else {
                    const double norm = (val - minV) / span;
                    p.fillRect(cellRect, sampleColormap(norm, cmId));
                }
            }
        }

        // Draw right-side vertical colorbar legend.
        const QRect barRect(plotRect.right() + 14, plotRect.top(), 14, plotRect.height());
        for (int y = 0; y < barRect.height(); ++y) {
            const double t = 1.0 - static_cast<double>(y) / qMax(1, barRect.height() - 1);
            p.setPen(sampleColormap(t, cmId));
            p.drawLine(barRect.left(), barRect.top() + y, barRect.right(), barRect.top() + y);
        }
        p.setPen(axisCol);
        p.drawRect(barRect);
        p.setPen(textCol);
        p.drawText(QRect(barRect.right() + 4, barRect.top() - 4, 48, 16),
                   Qt::AlignLeft | Qt::AlignTop, QString::number(maxV, 'g', 4));
        p.drawText(QRect(barRect.right() + 4, barRect.bottom() - 12, 48, 16),
                   Qt::AlignLeft | Qt::AlignBottom, QString::number(minV, 'g', 4));
        return;
    }

    // 1D Line / Stem rendering path.
    for (int g = 1; g < 4; ++g) {
        const int gy = plotRect.top() + (plotRect.height() * g) / 4;
        const int gx = plotRect.left() + (plotRect.width() * g) / 4;
        p.setPen(QPen(gridCol, 1.0, Qt::DotLine));
        p.drawLine(plotRect.left(), gy, plotRect.right(), gy);
        p.drawLine(gx, plotRect.top(), gx, plotRect.bottom());
    }

    p.setPen(textCol);
    p.drawText(QRect(4, plotRect.top() - 6, 56, 18), Qt::AlignRight | Qt::AlignVCenter,
               QString::number(maxV, 'g', 5));
    p.drawText(QRect(4, plotRect.center().y() - 9, 56, 18), Qt::AlignRight | Qt::AlignVCenter,
               QString::number((minV + maxV) * 0.5, 'g', 5));
    p.drawText(QRect(4, plotRect.bottom() - 9, 56, 18), Qt::AlignRight | Qt::AlignVCenter,
               QString::number(minV, 'g', 5));
    p.drawText(QRect(plotRect.left(), plotRect.bottom() + 6, 60, 18), Qt::AlignLeft, QStringLiteral("[0]"));
    p.drawText(QRect(plotRect.right() - 70, plotRect.bottom() + 6, 70, 18), Qt::AlignRight,
               QStringLiteral("[%1]").arg(samples.size() - 1));

    const int n = samples.size();
    auto mapPoint = [&](int idx, double v) -> QPointF {
        const double xRatio = (n > 1) ? (static_cast<double>(idx) / (n - 1)) : 0.5;
        const double yRatio = std::clamp((v - minV) / span, 0.0, 1.0);
        return QPointF(plotRect.left() + xRatio * plotRect.width(),
                       plotRect.bottom() - yRatio * plotRect.height());
    };

    // Draw dashed ghost curve of previous stop's sample values when available.
    if (!prevSamples.isEmpty()) {
        QPainterPath ghostPath;
        bool started = false;
        const int prevN = qMin(n, static_cast<int>(prevSamples.size()));
        for (int i = 0; i < prevN; ++i) {
            const double pv = prevSamples.at(i);
            if (std::isnan(pv) || std::isinf(pv)) { started = false; continue; }
            const QPointF pt = mapPoint(i, pv);
            if (!started) { ghostPath.moveTo(pt); started = true; }
            else ghostPath.lineTo(pt);
        }
        p.setPen(QPen(prevCol, 1.3, Qt::DashLine));
        p.drawPath(ghostPath);
    }

    // Draw any multi-rank comparison overlay curves.
    static const QColor kRankPalette[6] = {
        QColor(QStringLiteral("#34d399")), QColor(QStringLiteral("#a78bfa")),
        QColor(QStringLiteral("#fbbf24")), QColor(QStringLiteral("#f472b6")),
        QColor(QStringLiteral("#60a5fa")), QColor(QStringLiteral("#fb923c"))};
    int overlayColorIdx = 0;
    for (auto it = m_owner->rankOverlays().cbegin(); it != m_owner->rankOverlays().cend(); ++it) {
        if (it.key() == m_owner->focusedRank()) continue;
        const QVector<double>& rv = it.value();
        QPainterPath rankPath;
        bool started = false;
        for (int i = 0; i < qMin(n, static_cast<int>(rv.size())); ++i) {
            if (std::isnan(rv.at(i)) || std::isinf(rv.at(i))) { started = false; continue; }
            const QPointF pt = mapPoint(i, rv.at(i));
            if (!started) { rankPath.moveTo(pt); started = true; }
            else rankPath.lineTo(pt);
        }
        p.setPen(QPen(kRankPalette[overlayColorIdx++ % 6], 1.4));
        p.drawPath(rankPath);
    }

    // Draw primary active series (Line1D or Stem1D) plus mutation and NaN/Inf markers.
    const bool isStem = (m_owner->plotMode() == ArrayPlotDialog::PlotMode::Stem1D);
    const double zeroY = mapPoint(0, std::clamp(0.0, minV, maxV)).y();
    QPainterPath activePath;
    bool activeStarted = false;

    for (int i = 0; i < n; ++i) {
        const double v = samples.at(i);
        if (std::isnan(v) || std::isinf(v)) {
            activeStarted = false;
            const double x = mapPoint(i, minV).x();
            p.setPen(QPen(anomCol, 1.5, Qt::DashLine));
            p.drawLine(QPointF(x, plotRect.top()), QPointF(x, plotRect.bottom()));
            p.setBrush(anomCol);
            p.setPen(Qt::white);
            p.drawEllipse(QPointF(x, plotRect.top() + 8.0), 4.5, 4.5);
            continue;
        }
        const QPointF pt = mapPoint(i, v);
        if (isStem) {
            p.setPen(QPen(lineCol, 1.8));
            p.drawLine(QPointF(pt.x(), zeroY), pt);
        } else {
            if (!activeStarted) { activePath.moveTo(pt); activeStarted = true; }
            else activePath.lineTo(pt);
        }
    }
    if (!isStem) {
        p.setPen(QPen(lineCol, 2.0));
        p.drawPath(activePath);
    }

    // Draw sample dots and highlight elements mutated since the previous stop in amber.
    for (int i = 0; i < n; ++i) {
        const double v = samples.at(i);
        if (std::isnan(v) || std::isinf(v)) continue;
        const bool changed = (i < prevSamples.size()) &&
                             (std::isnan(prevSamples.at(i)) || std::abs(v - prevSamples.at(i)) > 1e-12);
        if (changed || n <= 64 || isStem) {
            const QPointF pt = mapPoint(i, v);
            p.setPen(Qt::NoPen);
            p.setBrush(changed ? mutCol : lineCol);
            const double r = changed ? 4.0 : 2.5;
            p.drawEllipse(pt, r, r);
        }
    }

    // Draw hover crosshair if cursor lies inside the plot region.
    if (plotRect.contains(m_hoverPos)) {
        p.setPen(QPen(axisCol, 1.0, Qt::DashLine));
        p.drawLine(m_hoverPos.x(), plotRect.top(), m_hoverPos.x(), plotRect.bottom());
        p.drawLine(plotRect.left(), m_hoverPos.y(), plotRect.right(), m_hoverPos.y());
    }
}

void ArrayPlotCanvas::mouseMoveEvent(QMouseEvent* event) {
    if (!event || !m_owner) return;
    m_hoverPos = event->pos();
    const QRect plotRect = rect().adjusted(64, 18, -72, -32);
    const QVector<double>& samples = m_owner->currentSamples();
    if (!plotRect.contains(m_hoverPos) || samples.isEmpty()) {
        update();
        return;
    }

    if (m_owner->plotMode() == ArrayPlotDialog::PlotMode::Heatmap2D || m_owner->rows() > 1) {
        const int ny = qMax(1, m_owner->rows());
        const int nx = qMax(1, m_owner->cols());
        const int c = std::clamp(static_cast<int>(((m_hoverPos.x() - plotRect.left()) * nx) / plotRect.width()), 0, nx - 1);
        const int r = std::clamp(static_cast<int>(((m_hoverPos.y() - plotRect.top()) * ny) / plotRect.height()), 0, ny - 1);
        const int idx = r * nx + c;
        if (idx < samples.size()) {
            m_owner->setHoverSummaryText(
                QStringLiteral("Hover: [%1, %2] (idx %3) = %4")
                    .arg(r).arg(c).arg(idx).arg(QString::number(samples.at(idx), 'g', 8)));
        }
    } else {
        const int n = samples.size();
        const double ratio = static_cast<double>(m_hoverPos.x() - plotRect.left()) / qMax(1, plotRect.width());
        const int idx = std::clamp(static_cast<int>(std::round(ratio * (n - 1))), 0, n - 1);
        const double val = samples.at(idx);
        QString deltaText;
        if (idx < m_owner->previousSamples().size()) {
            const double delta = val - m_owner->previousSamples().at(idx);
            deltaText = QStringLiteral("  (prev = %1, delta = %2)")
                            .arg(QString::number(m_owner->previousSamples().at(idx), 'g', 6),
                                 QString::number(delta, 'g', 6));
        }
        m_owner->setHoverSummaryText(
            QStringLiteral("Hover: [%1] = %2%3")
                .arg(idx).arg(QString::number(val, 'g', 8), deltaText));
    }
    update();
}

void ArrayPlotCanvas::leaveEvent(QEvent* /*event*/) {
    m_hoverPos = QPoint(-1, -1);
    update();
}

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

ArrayPlotDialog::ArrayPlotDialog(::IBackendCoordinator* coordinator, QWidget* parent)
    : QDialog(parent, Qt::Window) {
    setObjectName(QStringLiteral("ArrayPlotDialog"));
    setWindowTitle(QStringLiteral("DDD Scientific 1D/2D Array & Field Plotter"));
    setModal(false);
    setMinimumSize(780, 500);
    resize(900, 560);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(6);

    // Top control bar: Expression, ElementType, PlotMode, Colormap, NY x NX, Rank, Auto-Refresh, Close.
    auto* topRow = new QHBoxLayout();
    topRow->setSpacing(6);

    m_exprEdit = new QLineEdit(this);
    m_exprEdit->setObjectName(QStringLiteral("arrayPlotExprEdit"));
    m_exprEdit->setPlaceholderText(QStringLiteral("Array or pointer expression (e.g., u, grid.data(), &vec[0])..."));
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

    m_colormapCombo = new QComboBox(this);
    m_colormapCombo->setObjectName(QStringLiteral("arrayPlotColormapCombo"));
    m_colormapCombo->setFixedHeight(26);
    m_colormapCombo->addItem(QStringLiteral("Viridis"), static_cast<int>(Colormap::Viridis));
    m_colormapCombo->addItem(QStringLiteral("Plasma"), static_cast<int>(Colormap::Plasma));
    m_colormapCombo->addItem(QStringLiteral("Diverging Cool-Warm"), static_cast<int>(Colormap::CoolWarm));
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

    // Second toolbar row: Rows (NY), Cols (NX), Rank selector, Compare Ranks, Auto-Refresh on Stop, and Exports.
    auto* secondRow = new QHBoxLayout();
    secondRow->setSpacing(6);

    m_rowsSpin = new QSpinBox(this);
    m_rowsSpin->setObjectName(QStringLiteral("arrayPlotRowsSpin"));
    m_rowsSpin->setRange(1, 4096);
    m_rowsSpin->setValue(1);
    m_rowsSpin->setFixedHeight(24);

    m_colsSpin = new QSpinBox(this);
    m_colsSpin->setObjectName(QStringLiteral("arrayPlotColsSpin"));
    m_colsSpin->setRange(1, 65536);
    m_colsSpin->setValue(64);
    m_colsSpin->setFixedHeight(24);

    m_rankCombo = new QComboBox(this);
    m_rankCombo->setObjectName(QStringLiteral("arrayPlotRankCombo"));
    m_rankCombo->setFixedHeight(24);
    for (int r = 0; r < 8; ++r) m_rankCombo->addItem(QStringLiteral("Rank %1").arg(r), r);

    m_overlayRanksCheck = new QCheckBox(QStringLiteral("Overlay Ranks"), this);
    m_overlayRanksCheck->setObjectName(QStringLiteral("arrayPlotOverlayAllRanksCheck"));

    m_autoRefreshCheck = new QCheckBox(QStringLiteral("Auto-Refresh on Stop"), this);
    m_autoRefreshCheck->setObjectName(QStringLiteral("arrayPlotAutoRefreshCheck"));
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

    secondRow->addWidget(new QLabel(QStringLiteral("Rows (NY):"), this));
    secondRow->addWidget(m_rowsSpin);
    secondRow->addWidget(new QLabel(QStringLiteral("Cols (NX):"), this));
    secondRow->addWidget(m_colsSpin);
    secondRow->addWidget(m_rankCombo);
    secondRow->addWidget(m_overlayRanksCheck);
    secondRow->addWidget(m_autoRefreshCheck);
    secondRow->addStretch(1);
    secondRow->addWidget(m_exportCsvBtn);
    secondRow->addWidget(m_savePngBtn);
    secondRow->addWidget(m_gnuplotBtn);
    root->addLayout(secondRow);

    // Custom scientific plot canvas.
    m_canvas = new ArrayPlotCanvas(this);
    root->addWidget(m_canvas, 1);

    // Live telemetry stats bar + cursor hover readout.
    auto* footerRow = new QHBoxLayout();
    m_statsLabel = new QLabel(QStringLiteral("Min: 0 | Max: 0 | Mean: 0 | L2: 0 | NaN/Inf: 0 | Changed: 0"), this);
    m_statsLabel->setObjectName(QStringLiteral("arrayPlotStatsLabel"));
    m_hoverLabel = new QLabel(QStringLiteral("Hover over plot canvas to inspect values and deltas"), this);
    m_hoverLabel->setObjectName(QStringLiteral("arrayPlotHoverLabel"));
    footerRow->addWidget(m_statsLabel, 1);
    footerRow->addWidget(m_hoverLabel);
    root->addLayout(footerRow);

    connect(m_refreshBtn, &QPushButton::clicked, this, &ArrayPlotDialog::refreshPlot);
    connect(m_exprEdit, &QLineEdit::returnPressed, this, &ArrayPlotDialog::refreshPlot);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::close);
    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        if (m_canvas) m_canvas->update();
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
    connect(m_rankCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        if (idx >= 0) {
            m_focusedRank = m_rankCombo->itemData(idx).toInt();
            if (isAutoRefreshEnabled()) refreshPlot();
        }
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
                if (!m_awaitingCoordinatorRead || rankId != m_focusedRank || data.isEmpty()) return;
                m_awaitingCoordinatorRead = false;
                loadFromRawBytes(data, elementType(), m_rows, m_cols);
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

void ArrayPlotDialog::setExpression(const QString& expr) {
    if (m_exprEdit) m_exprEdit->setText(expr.trimmed());
}

QString ArrayPlotDialog::expression() const {
    return m_exprEdit ? m_exprEdit->text().trimmed() : QString();
}

void ArrayPlotDialog::setElementType(ElementType type) {
    if (!m_typeCombo) return;
    const int idx = m_typeCombo->findData(static_cast<int>(type));
    if (idx >= 0) m_typeCombo->setCurrentIndex(idx);
}

void ArrayPlotDialog::setElementTypeFromString(const QString& cType) {
    const QString lower = cType.toLower();
    if (lower.contains(QStringLiteral("float"))) setElementType(ElementType::Float32);
    else if (lower.contains(QStringLiteral("uint8")) || lower.contains(QStringLiteral("uchar")) || lower.contains(QStringLiteral("char")))
        setElementType(ElementType::UInt8);
    else if (lower.contains(QStringLiteral("int64")) || lower.contains(QStringLiteral("long")))
        setElementType(ElementType::Int64);
    else if (lower.contains(QStringLiteral("int")))
        setElementType(ElementType::Int32);
    else
        setElementType(ElementType::Float64);
}

ArrayPlotDialog::ElementType ArrayPlotDialog::elementType() const {
    if (!m_typeCombo) return ElementType::Float64;
    return static_cast<ElementType>(m_typeCombo->currentData().toInt());
}

void ArrayPlotDialog::setPlotMode(PlotMode mode) {
    if (!m_modeCombo) return;
    const int idx = m_modeCombo->findData(static_cast<int>(mode));
    if (idx >= 0) m_modeCombo->setCurrentIndex(idx);
    if (m_canvas) m_canvas->update();
}

ArrayPlotDialog::PlotMode ArrayPlotDialog::plotMode() const {
    if (!m_modeCombo) return PlotMode::Line1D;
    return static_cast<PlotMode>(m_modeCombo->currentData().toInt());
}

void ArrayPlotDialog::setColormap(Colormap cm) {
    if (!m_colormapCombo) return;
    const int idx = m_colormapCombo->findData(static_cast<int>(cm));
    if (idx >= 0) m_colormapCombo->setCurrentIndex(idx);
    if (m_canvas) m_canvas->update();
}

ArrayPlotDialog::Colormap ArrayPlotDialog::colormap() const {
    if (!m_colormapCombo) return Colormap::Viridis;
    return static_cast<Colormap>(m_colormapCombo->currentData().toInt());
}

void ArrayPlotDialog::setDimensions(int rows, int cols) {
    m_rows = qMax(1, rows);
    m_cols = qMax(1, cols);
    if (m_rowsSpin && m_rowsSpin->value() != m_rows) {
        m_rowsSpin->blockSignals(true);
        m_rowsSpin->setValue(m_rows);
        m_rowsSpin->blockSignals(false);
    }
    if (m_colsSpin && m_colsSpin->value() != m_cols) {
        m_colsSpin->blockSignals(true);
        m_colsSpin->setValue(m_cols);
        m_colsSpin->blockSignals(false);
    }
    if (m_rows > 1) setPlotMode(PlotMode::Heatmap2D);
    if (m_canvas) m_canvas->update();
}

void ArrayPlotDialog::setFocusedRank(int rankId) {
    m_focusedRank = qMax(0, rankId);
    if (m_rankCombo) {
        const int idx = m_rankCombo->findData(m_focusedRank);
        if (idx >= 0 && m_rankCombo->currentIndex() != idx) {
            m_rankCombo->blockSignals(true);
            m_rankCombo->setCurrentIndex(idx);
            m_rankCombo->blockSignals(false);
        }
    }
}

bool ArrayPlotDialog::isAutoRefreshEnabled() const {
    return m_autoRefreshCheck ? m_autoRefreshCheck->isChecked() : true;
}

void ArrayPlotDialog::setAutoRefreshEnabled(bool enabled) {
    if (m_autoRefreshCheck) m_autoRefreshCheck->setChecked(enabled);
}

bool ArrayPlotDialog::loadFromRawBytes(const QByteArray& rawBytes, const QString& typeName,
                                       int rows, int cols) {
    setElementTypeFromString(typeName);
    return loadFromRawBytes(rawBytes, elementType(), rows, cols);
}

bool ArrayPlotDialog::loadFromRawBytes(const QByteArray& rawBytes, ElementType type,
                                       int rows, int cols) {
    setElementType(type);
    const int elemSize = byteSizeOf(type);
    const int totalAvailable = rawBytes.size() / elemSize;
    if (totalAvailable <= 0) return false;

    const int effRows = qMax(1, rows);
    const int effCols = (cols > 0) ? cols : (totalAvailable / effRows);
    const int count = qMin(totalAvailable, effRows * qMax(1, effCols));
    if (count <= 0) return false;

    QVector<double> decoded;
    decoded.resize(count);
    const char* ptr = rawBytes.constData();

    for (int i = 0; i < count; ++i) {
        const char* src = ptr + i * elemSize;
        switch (type) {
            case ElementType::Float64: {
                double v = 0.0;
                std::memcpy(&v, src, sizeof(v));
                decoded[i] = v;
                break;
            }
            case ElementType::Float32: {
                float v = 0.0f;
                std::memcpy(&v, src, sizeof(v));
                decoded[i] = static_cast<double>(v);
                break;
            }
            case ElementType::Int32: {
                int32_t v = 0;
                std::memcpy(&v, src, sizeof(v));
                decoded[i] = static_cast<double>(v);
                break;
            }
            case ElementType::Int64: {
                int64_t v = 0;
                std::memcpy(&v, src, sizeof(v));
                decoded[i] = static_cast<double>(v);
                break;
            }
            case ElementType::UInt8: {
                uint8_t v = 0;
                std::memcpy(&v, src, sizeof(v));
                decoded[i] = static_cast<double>(v);
                break;
            }
        }
    }

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

void ArrayPlotDialog::refreshPlot() {
    const QString expr = expression();
    if (m_coordinator && !expr.isEmpty()) {
        const size_t totalBytes = static_cast<size_t>(qMax(1, m_rows) * qMax(1, m_cols) * byteSizeOf(elementType()));
        const QString addrExpr = expr.startsWith(QLatin1Char('&')) || expr.startsWith(QStringLiteral("0x"))
                                     ? expr
                                     : QStringLiteral("&(%1)[0]").arg(expr);
        m_awaitingCoordinatorRead = true;
        m_coordinator->readMemory(m_focusedRank, addrExpr, totalBytes);
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
    if (m_rows > 1) {
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
            out << i << ","
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
        dout << i << " " << QString::number(m_currentSamples.at(i), 'g', 12) << "\n";
    }
    df.close();

    QFile sf(scriptPath);
    if (!sf.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream sout(&sf);
    sout << "set grid\nset title 'GridLock Array Plot: " << expression() << "'\n";
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
        "QLabel#arrayPlotStatsLabel { color: %6; font-family: 'JetBrains Mono', monospace; font-size: 10px; font-weight: bold; }"
        "QLabel#arrayPlotHoverLabel { color: %7; font-family: 'JetBrains Mono', monospace; font-size: 10px; }")
                      .arg(bg, pal.textHex, inputBg, pal.borderHex, btnBg, pal.accentHex, muted));
    if (m_canvas) m_canvas->update();
}

}  // namespace gridlock::ui
