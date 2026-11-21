/**
 * @file ArrayPlotCanvas.cpp
 * @brief Implementation of ArrayPlotCanvas, slice expression parsing, and rank subset parsing.
 */

#include "ArrayPlotCanvas.hpp"
#include "ArrayPlotDialog.hpp"

#include <algorithm>
#include <cmath>

#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QSet>

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

/** @brief Formats a Y-axis tick label in either linear or base-10 log space. */
QString formatAxisValue(double displayVal, bool logScale) {
    if (logScale) {
        return QString::number(std::pow(10.0, displayVal), 'e', 1);
    }
    return QString::number(displayVal, 'g', 5);
}

}  // namespace

ArraySliceSpec parseArraySliceExpression(const QString& expression) {
    ArraySliceSpec spec;
    const QString trimmed = expression.trimmed();
    spec.baseExpression = trimmed;
    if (trimmed.isEmpty() || !trimmed.endsWith(QLatin1Char(']'))) return spec;

    const int openIdx = trimmed.lastIndexOf(QLatin1Char('['));
    if (openIdx <= 0) return spec;

    const QString inside = trimmed.mid(openIdx + 1, trimmed.size() - openIdx - 2).trimmed();
    const QString base = trimmed.left(openIdx).trimmed();
    if (base.isEmpty()) return spec;

    // 1. Inclusive range syntax: `expr[start..end]` (optional `:stride`).
    static const QRegularExpression kRangeRe(
        QStringLiteral(R"(^\s*(\d+)\s*\.\.\s*(\d+)(?:\s*:\s*(\d+))?\s*$)"));
    const QRegularExpressionMatch rm = kRangeRe.match(inside);
    if (rm.hasMatch()) {
        const int start = qMax(0, rm.captured(1).toInt());
        const int end = qMax(start, rm.captured(2).toInt());
        const int stride = rm.captured(3).isEmpty() ? 1 : qMax(1, rm.captured(3).toInt());
        spec.baseExpression = base;
        spec.offset = start;
        spec.stride = stride;
        spec.count = (end - start) / stride + 1;
        spec.hasSlice = true;
        return spec;
    }

    // 2. Python/NumPy slice syntax: `expr[start:end]` or `expr[start:end:stride]`.
    if (!inside.contains(QLatin1Char(':'))) return spec;
    const QStringList parts = inside.split(QLatin1Char(':'));
    if (parts.size() < 2 || parts.size() > 3) return spec;

    auto parseOptInt = [](const QString& token, bool* hasVal, int* outVal) -> bool {
        const QString t = token.trimmed();
        if (t.isEmpty()) {
            *hasVal = false;
            return true;
        }
        bool ok = false;
        const int v = t.toInt(&ok);
        if (!ok || v < 0) return false;
        *hasVal = true;
        *outVal = v;
        return true;
    };

    bool hasStart = false, hasEnd = false, hasStride = false;
    int start = 0, end = 0, stride = 1;
    if (!parseOptInt(parts[0], &hasStart, &start)) return spec;
    if (!parseOptInt(parts[1], &hasEnd, &end)) return spec;
    if (parts.size() == 3 && !parseOptInt(parts[2], &hasStride, &stride)) return spec;

    stride = hasStride ? qMax(1, stride) : 1;
    start = hasStart ? qMax(0, start) : 0;

    spec.baseExpression = base;
    spec.offset = start;
    spec.stride = stride;
    if (hasEnd) {
        spec.count = (end > start) ? ((end - start + stride - 1) / stride) : 1;
    } else {
        spec.count = 0;
    }
    spec.hasSlice = true;
    return spec;
}

QVector<int> parseRankSubset(const QString& spec, int totalRanks) {
    const QString trimmed = spec.trimmed();
    QVector<int> result;
    const bool hasTotal = (totalRanks > 0);
    const int upperLimit = hasTotal ? (totalRanks - 1) : 63;

    if (trimmed.isEmpty() || trimmed.compare(QStringLiteral("all"), Qt::CaseInsensitive) == 0 ||
        trimmed == QStringLiteral("*")) {
        const int n = hasTotal ? totalRanks : 4;
        result.reserve(n);
        for (int r = 0; r < n; ++r) result.append(r);
        return result;
    }

    QSet<int> unique;
    static const QRegularExpression kTokenRangeRe(QStringLiteral(R"(^\s*(\d+)\s*(?:\.\.|-)\s*(\d+)\s*$)"));
    for (const QString& rawTok : trimmed.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QString tok = rawTok.trimmed();
        const QRegularExpressionMatch m = kTokenRangeRe.match(tok);
        if (m.hasMatch()) {
            int a = m.captured(1).toInt();
            int b = m.captured(2).toInt();
            if (a > b) std::swap(a, b);
            a = std::clamp(a, 0, upperLimit);
            b = std::clamp(b, 0, upperLimit);
            for (int r = a; r <= b; ++r) unique.insert(r);
        } else {
            bool ok = false;
            const int r = tok.toInt(&ok);
            if (ok && r >= 0 && r <= upperLimit) unique.insert(r);
        }
    }

    for (int r : unique) result.append(r);
    std::sort(result.begin(), result.end());
    if (result.isEmpty()) result.append(0);
    return result;
}

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

QColor ArrayPlotCanvas::rankOverlayColor(int rankId, bool isFocused) {
    if (isFocused) return QColor(QStringLiteral("#38bdf8"));
    static const QColor kRankPalette[6] = {
        QColor(QStringLiteral("#34d399")), QColor(QStringLiteral("#a78bfa")),
        QColor(QStringLiteral("#fbbf24")), QColor(QStringLiteral("#f472b6")),
        QColor(QStringLiteral("#60a5fa")), QColor(QStringLiteral("#fb923c"))};
    return kRankPalette[qMax(0, rankId) % 6];
}

void ArrayPlotCanvas::setLogScale(bool enabled) {
    if (m_logScale == enabled) return;
    m_logScale = enabled;
    m_hasZoomValY = false;
    update();
}

void ArrayPlotCanvas::resetZoom() {
    const bool wasZoomed = m_zoomed;
    m_zoomed = false;
    m_isDraggingZoom = false;
    m_hasZoomValY = false;
    m_zoomXMin = 0;
    m_zoomXMax = 0;
    m_zoomYMin = 0;
    m_zoomYMax = 0;
    if (wasZoomed) emit zoomChanged(false);
    update();
}

void ArrayPlotCanvas::setZoomIndexWindow(int xMin, int xMax) {
    if (!m_owner) return;
    const bool is2D = (m_owner->plotMode() == ArrayPlotDialog::PlotMode::Heatmap2D || m_owner->rows() > 1);
    const int maxIdx = is2D ? qMax(0, m_owner->cols() - 1)
                            : qMax(0, static_cast<int>(m_owner->currentSamples().size()) - 1);
    if (xMin > xMax) std::swap(xMin, xMax);
    m_zoomXMin = std::clamp(xMin, 0, maxIdx);
    m_zoomXMax = std::clamp(xMax, m_zoomXMin, maxIdx);
    m_zoomYMin = 0;
    m_zoomYMax = qMax(0, m_owner->rows() - 1);
    m_zoomed = true;
    emit zoomChanged(true);
    update();
}

QPair<int, int> ArrayPlotCanvas::visibleXRange() const {
    if (!m_owner) return {0, 0};
    const bool is2D = (m_owner->plotMode() == ArrayPlotDialog::PlotMode::Heatmap2D || m_owner->rows() > 1);
    const int maxIdx = is2D ? qMax(0, m_owner->cols() - 1)
                            : qMax(0, static_cast<int>(m_owner->currentSamples().size()) - 1);
    if (!m_zoomed) return {0, maxIdx};
    const int lo = std::clamp(m_zoomXMin, 0, maxIdx);
    const int hi = std::clamp(m_zoomXMax, lo, maxIdx);
    return {lo, hi};
}

QPair<int, int> ArrayPlotCanvas::visibleYRange() const {
    if (!m_owner) return {0, 0};
    const int maxRow = qMax(0, m_owner->rows() - 1);
    if (!m_zoomed) return {0, maxRow};
    const int lo = std::clamp(m_zoomYMin, 0, maxRow);
    const int hi = std::clamp(m_zoomYMax, lo, maxRow);
    return {lo, hi};
}

QRect ArrayPlotCanvas::plotAreaRect() const {
    return rect().adjusted(68, 18, -76, -34);
}

QPair<double, double> ArrayPlotCanvas::compute1DYBounds(int xMin, int xMax, bool* hasNonPositive) const {
    if (hasNonPositive) *hasNonPositive = false;
    if (!m_owner) return {0.0, 1.0};

    double lo = 0.0;
    double hi = 0.0;
    bool haveAny = false;

    auto considerSeries = [&](const QVector<double>& vec) {
        const int end = qMin(xMax, static_cast<int>(vec.size()) - 1);
        for (int i = qMax(0, xMin); i <= end; ++i) {
            const double v = vec.at(i);
            if (!std::isfinite(v)) continue;
            if (m_logScale) {
                if (v <= 0.0) {
                    if (hasNonPositive) *hasNonPositive = true;
                    continue;
                }
                const double lv = std::log10(v);
                if (!haveAny) { lo = hi = lv; haveAny = true; }
                else { lo = qMin(lo, lv); hi = qMax(hi, lv); }
            } else {
                if (!haveAny) { lo = hi = v; haveAny = true; }
                else { lo = qMin(lo, v); hi = qMax(hi, v); }
            }
        }
    };

    considerSeries(m_owner->currentSamples());
    if (m_owner->plotMode() != ArrayPlotDialog::PlotMode::ScalarHistory) {
        considerSeries(m_owner->previousSamples());
    }
    for (auto it = m_owner->rankOverlays().cbegin(); it != m_owner->rankOverlays().cend(); ++it) {
        considerSeries(it.value());
    }

    if (!haveAny) {
        lo = m_logScale ? -1.0 : 0.0;
        hi = m_logScale ? 0.0 : 1.0;
    } else if (std::abs(hi - lo) < 1e-12) {
        const double pad = m_logScale ? 0.5 : (std::abs(lo) > 1e-9 ? std::abs(lo) * 0.1 : 1.0);
        lo -= pad;
        hi += pad;
    }

    if (m_zoomed && m_hasZoomValY && m_zoomValMax > m_zoomValMin + 1e-12) {
        return {m_zoomValMin, m_zoomValMax};
    }
    return {lo, hi};
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

    const QRect plotRect = plotAreaRect();
    if (plotRect.width() < 40 || plotRect.height() < 40) return;

    p.setPen(QPen(axisCol, 1.0));
    p.drawRect(plotRect);

    if (m_owner->currentSamples().isEmpty() && m_owner->rankOverlays().isEmpty()) {
        p.setPen(axisCol);
        const QString hint = (m_owner->plotMode() == ArrayPlotDialog::PlotMode::ScalarHistory)
                                 ? QStringLiteral("No scalar history recorded yet. Step or click Plot / Refresh.")
                                 : QStringLiteral("No array samples loaded. Enter an expression and click Plot / Refresh.");
        p.drawText(plotRect, Qt::AlignCenter, hint);
        return;
    }

    const bool is2D = (m_owner->plotMode() == ArrayPlotDialog::PlotMode::Heatmap2D || m_owner->rows() > 1);
    if (is2D) {
        paintHeatmap2D(p, plotRect, canvasBg, axisCol, textCol, anomCol);
    } else {
        paintPlot1D(p, plotRect, isDark, gridCol, axisCol, textCol, lineCol, prevCol, mutCol, anomCol);
    }

    if (m_isDraggingZoom) {
        const QRect sel = QRect(m_dragStartPos, m_dragCurrentPos).normalized().intersected(plotRect);
        if (sel.width() > 1 && sel.height() > 1) {
            p.setPen(QPen(QColor(QStringLiteral("#38bdf8")), 1.5, Qt::SolidLine));
            p.setBrush(QColor(56, 189, 248, 45));
            p.drawRect(sel);
        }
    }
}

void ArrayPlotCanvas::paintHeatmap2D(QPainter& p, const QRect& plotRect, const QColor& canvasBg,
                                     const QColor& axisCol, const QColor& textCol,
                                     const QColor& anomCol) const {
    const QVector<double>& samples = m_owner->currentSamples();
    const int ny = qMax(1, m_owner->rows());
    const int nx = qMax(1, m_owner->cols());
    const auto [colMin, colMax] = visibleXRange();
    const auto [rowMin, rowMax] = visibleYRange();
    const int visCols = qMax(1, colMax - colMin + 1);
    const int visRows = qMax(1, rowMax - rowMin + 1);

    double dispMin = m_owner->minValue();
    double dispMax = m_owner->maxValue();
    if (m_logScale) {
        bool havePos = false;
        for (double v : samples) {
            if (!std::isfinite(v) || v <= 0.0) continue;
            const double lv = std::log10(v);
            if (!havePos) { dispMin = dispMax = lv; havePos = true; }
            else { dispMin = qMin(dispMin, lv); dispMax = qMax(dispMax, lv); }
        }
        if (!havePos) { dispMin = -1.0; dispMax = 0.0; }
    }
    const double span = (std::abs(dispMax - dispMin) > 1e-15) ? (dispMax - dispMin) : 1.0;

    const int cmId = static_cast<int>(m_owner->colormap());
    const double cellW = static_cast<double>(plotRect.width()) / visCols;
    const double cellH = static_cast<double>(plotRect.height()) / visRows;

    for (int r = rowMin; r <= rowMax; ++r) {
        for (int c = colMin; c <= colMax; ++c) {
            const int idx = r * nx + c;
            const QRectF cellRect(plotRect.left() + (c - colMin) * cellW,
                                  plotRect.top() + (r - rowMin) * cellH,
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
            } else if (m_logScale && val <= 0.0) {
                p.fillRect(cellRect, QColor(45, 55, 72));
                p.setPen(QPen(axisCol, 0.8, Qt::DotLine));
                p.drawLine(cellRect.bottomLeft(), cellRect.topRight());
            } else {
                const double mapped = m_logScale ? std::log10(val) : val;
                const double norm = (mapped - dispMin) / span;
                p.fillRect(cellRect, sampleColormap(norm, cmId));
            }
        }
    }

    // Draw right-side vertical colorbar legend and zoomed axis coordinates.
    const QRect barRect(plotRect.right() + 14, plotRect.top(), 14, plotRect.height());
    for (int y = 0; y < barRect.height(); ++y) {
        const double t = 1.0 - static_cast<double>(y) / qMax(1, barRect.height() - 1);
        p.setPen(sampleColormap(t, cmId));
        p.drawLine(barRect.left(), barRect.top() + y, barRect.right(), barRect.top() + y);
    }
    p.setPen(axisCol);
    p.drawRect(barRect);
    p.setPen(textCol);
    p.drawText(QRect(barRect.right() + 4, barRect.top() - 4, 54, 16),
               Qt::AlignLeft | Qt::AlignTop, formatAxisValue(dispMax, m_logScale));
    p.drawText(QRect(barRect.right() + 4, barRect.bottom() - 12, 54, 16),
               Qt::AlignLeft | Qt::AlignBottom, formatAxisValue(dispMin, m_logScale));
    if (m_logScale) {
        p.drawText(QRect(barRect.right() + 4, barRect.center().y() - 8, 50, 16),
                   Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("log10"));
    }
    p.drawText(QRect(plotRect.left(), plotRect.bottom() + 6, 90, 18), Qt::AlignLeft,
               QStringLiteral("c=%1").arg(colMin));
    p.drawText(QRect(plotRect.right() - 90, plotRect.bottom() + 6, 90, 18), Qt::AlignRight,
               QStringLiteral("c=%1").arg(colMax));
    p.drawText(QRect(4, plotRect.top() - 4, 60, 18), Qt::AlignRight, QStringLiteral("r=%1").arg(rowMin));
    p.drawText(QRect(4, plotRect.bottom() - 14, 60, 18), Qt::AlignRight, QStringLiteral("r=%1").arg(rowMax));
    Q_UNUSED(ny);
}

void ArrayPlotCanvas::paintPlot1D(QPainter& p, const QRect& plotRect, bool isDark,
                                  const QColor& gridCol, const QColor& axisCol,
                                  const QColor& textCol, const QColor& lineCol,
                                  const QColor& prevCol, const QColor& mutCol,
                                  const QColor& anomCol) const {
    const QVector<double>& samples = m_owner->currentSamples();
    const QVector<double>& prevSamples = m_owner->previousSamples();
    const bool isHistory = (m_owner->plotMode() == ArrayPlotDialog::PlotMode::ScalarHistory);
    const bool isStem = (m_owner->plotMode() == ArrayPlotDialog::PlotMode::Stem1D);
    const auto [xMin, xMax] = visibleXRange();
    const int visCount = qMax(1, xMax - xMin + 1);

    bool hasNonPositive = false;
    const auto [yMin, yMax] = compute1DYBounds(xMin, xMax, &hasNonPositive);
    const double ySpan = (std::abs(yMax - yMin) > 1e-15) ? (yMax - yMin) : 1.0;

    for (int g = 1; g < 4; ++g) {
        const int gy = plotRect.top() + (plotRect.height() * g) / 4;
        const int gx = plotRect.left() + (plotRect.width() * g) / 4;
        p.setPen(QPen(gridCol, 1.0, Qt::DotLine));
        p.drawLine(plotRect.left(), gy, plotRect.right(), gy);
        p.drawLine(gx, plotRect.top(), gx, plotRect.bottom());
    }

    p.setPen(textCol);
    p.drawText(QRect(2, plotRect.top() - 6, 62, 18), Qt::AlignRight | Qt::AlignVCenter,
               formatAxisValue(yMax, m_logScale));
    p.drawText(QRect(2, plotRect.center().y() - 9, 62, 18), Qt::AlignRight | Qt::AlignVCenter,
               formatAxisValue((yMin + yMax) * 0.5, m_logScale));
    p.drawText(QRect(2, plotRect.bottom() - 9, 62, 18), Qt::AlignRight | Qt::AlignVCenter,
               formatAxisValue(yMin, m_logScale));

    const int off = isHistory ? 0 : m_owner->sliceOffset();
    const int str = isHistory ? 1 : m_owner->sliceStride();
    const QString leftLbl = isHistory ? QStringLiteral("Stop %1").arg(xMin)
                                      : QStringLiteral("[%1]").arg(off + xMin * str);
    const QString rightLbl = isHistory ? QStringLiteral("Stop %1").arg(xMax)
                                       : QStringLiteral("[%1]").arg(off + xMax * str);
    p.drawText(QRect(plotRect.left(), plotRect.bottom() + 6, 90, 18), Qt::AlignLeft, leftLbl);
    p.drawText(QRect(plotRect.right() - 90, plotRect.bottom() + 6, 90, 18), Qt::AlignRight, rightLbl);
    if (m_logScale) {
        p.drawText(QRect(plotRect.center().x() - 45, plotRect.bottom() + 6, 90, 18),
                   Qt::AlignCenter, QStringLiteral("Log10 Y"));
    }

    auto mapPoint = [&](int idx, double rawVal, bool* valid) -> QPointF {
        const double xRatio = (visCount > 1) ? (static_cast<double>(idx - xMin) / (visCount - 1)) : 0.5;
        const double xPx = plotRect.left() + xRatio * plotRect.width();
        if (!std::isfinite(rawVal) || (m_logScale && rawVal <= 0.0)) {
            if (valid) *valid = false;
            return QPointF(xPx, plotRect.bottom() - 3.0);
        }
        if (valid) *valid = true;
        const double dispVal = m_logScale ? std::log10(rawVal) : rawVal;
        const double yRatio = std::clamp((dispVal - yMin) / ySpan, 0.0, 1.0);
        return QPointF(xPx, plotRect.bottom() - yRatio * plotRect.height());
    };

    if (m_logScale && hasNonPositive) {
        const double baseY = plotRect.bottom() - 4.0;
        p.setPen(QPen(mutCol, 1.0, Qt::DashLine));
        p.drawLine(QPointF(plotRect.left(), baseY), QPointF(plotRect.right(), baseY));
    }

    p.save();
    p.setClipRect(plotRect.adjusted(0, -1, 0, 1));

    // Ghost curve of previous stop's sample values (1D array modes only).
    if (!isHistory && !prevSamples.isEmpty()) {
        QPainterPath ghostPath;
        bool started = false;
        const int end = qMin(xMax, static_cast<int>(prevSamples.size()) - 1);
        for (int i = xMin; i <= end; ++i) {
            bool ok = false;
            const QPointF pt = mapPoint(i, prevSamples.at(i), &ok);
            if (!ok) { started = false; continue; }
            if (!started) { ghostPath.moveTo(pt); started = true; }
            else ghostPath.lineTo(pt);
        }
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(prevCol, 1.3, Qt::DashLine));
        p.drawPath(ghostPath);
    }

    // Multi-rank overlay comparison curves.
    for (auto it = m_owner->rankOverlays().cbegin(); it != m_owner->rankOverlays().cend(); ++it) {
        if (it.key() == m_owner->focusedRank() && !samples.isEmpty()) continue;
        const QVector<double>& rv = it.value();
        QPainterPath rankPath;
        bool started = false;
        const int end = qMin(xMax, static_cast<int>(rv.size()) - 1);
        for (int i = xMin; i <= end; ++i) {
            bool ok = false;
            const QPointF pt = mapPoint(i, rv.at(i), &ok);
            if (!ok) { started = false; continue; }
            if (!started) { rankPath.moveTo(pt); started = true; }
            else rankPath.lineTo(pt);
        }
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(rankOverlayColor(it.key(), false), 1.6));
        p.drawPath(rankPath);
    }

    // Primary active series (Line1D, Stem1D, or ScalarHistory).
    const double zeroY = m_logScale
                             ? static_cast<double>(plotRect.bottom())
                             : (plotRect.bottom() -
                                std::clamp((std::clamp(0.0, yMin, yMax) - yMin) / ySpan, 0.0, 1.0) *
                                    plotRect.height());
    QPainterPath activePath;
    bool activeStarted = false;
    const int end = qMin(xMax, static_cast<int>(samples.size()) - 1);

    for (int i = xMin; i <= end; ++i) {
        const double v = samples.at(i);
        if (std::isnan(v) || std::isinf(v)) {
            activeStarted = false;
            bool dummy = false;
            const double x = mapPoint(i, 1.0, &dummy).x();
            p.setPen(QPen(anomCol, 1.5, Qt::DashLine));
            p.drawLine(QPointF(x, plotRect.top()), QPointF(x, plotRect.bottom()));
            p.setBrush(anomCol);
            p.setPen(Qt::white);
            p.drawEllipse(QPointF(x, plotRect.top() + 8.0), 4.5, 4.5);
            continue;
        }
        bool ok = false;
        const QPointF pt = mapPoint(i, v, &ok);
        if (!ok) {
            // Finite v <= 0 in Log10 mode: break polyline and draw non-positive marker on baseline.
            activeStarted = false;
            p.setPen(QPen(mutCol, 1.2));
            p.setBrush(Qt::NoBrush);
            p.drawRect(QRectF(pt.x() - 3.0, plotRect.bottom() - 7.0, 6.0, 6.0));
            continue;
        }
        if (isStem) {
            p.setPen(QPen(lineCol, 1.8));
            p.drawLine(QPointF(pt.x(), zeroY), pt);
        } else {
            if (!activeStarted) { activePath.moveTo(pt); activeStarted = true; }
            else activePath.lineTo(pt);
        }
    }
    if (!isStem) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(lineCol, 2.0));
        p.drawPath(activePath);
    }

    // Sample dots and previous-stop mutation highlights.
    for (int i = xMin; i <= end; ++i) {
        const double v = samples.at(i);
        bool ok = false;
        const QPointF pt = mapPoint(i, v, &ok);
        if (!ok) continue;
        const bool changed = (!isHistory && i < prevSamples.size()) &&
                             (std::isnan(prevSamples.at(i)) || std::abs(v - prevSamples.at(i)) > 1e-12);
        if (changed || visCount <= 64 || isStem || isHistory) {
            p.setPen(Qt::NoPen);
            p.setBrush(changed ? mutCol : lineCol);
            const double r = (changed || isHistory) ? 3.8 : 2.5;
            p.drawEllipse(pt, r, r);
        }
    }
    p.restore();

    paintRankLegend(p, plotRect, isDark, axisCol, textCol);

    if (plotRect.contains(m_hoverPos)) {
        p.setPen(QPen(axisCol, 1.0, Qt::DashLine));
        p.drawLine(m_hoverPos.x(), plotRect.top(), m_hoverPos.x(), plotRect.bottom());
        p.drawLine(plotRect.left(), m_hoverPos.y(), plotRect.right(), m_hoverPos.y());
    }
}

void ArrayPlotCanvas::paintRankLegend(QPainter& p, const QRect& plotRect, bool isDark,
                                      const QColor& axisCol, const QColor& textCol) const {
    if (!m_owner || m_owner->rankOverlays().isEmpty()) return;

    QVector<int> ranks;
    ranks.append(m_owner->focusedRank());
    for (auto it = m_owner->rankOverlays().cbegin(); it != m_owner->rankOverlays().cend(); ++it) {
        if (!ranks.contains(it.key())) ranks.append(it.key());
    }
    std::sort(ranks.begin(), ranks.end());

    const int rowH = 16;
    const int boxW = 92;
    const int boxH = 8 + ranks.size() * rowH;
    const QRect legendRect(plotRect.right() - boxW - 8, plotRect.top() + 8, boxW, boxH);

    p.save();
    p.setBrush(isDark ? QColor(15, 23, 42, 215) : QColor(248, 250, 252, 225));
    p.setPen(QPen(axisCol, 1.0));
    p.drawRoundedRect(legendRect, 4.0, 4.0);

    for (int i = 0; i < ranks.size(); ++i) {
        const int r = ranks.at(i);
        const bool focused = (r == m_owner->focusedRank());
        const int y = legendRect.top() + 4 + i * rowH;
        p.setPen(QPen(rankOverlayColor(r, focused), 2.5));
        p.drawLine(legendRect.left() + 8, y + rowH / 2, legendRect.left() + 22, y + rowH / 2);
        p.setPen(textCol);
        const QString lbl = focused ? QStringLiteral("Rank %1 *").arg(r) : QStringLiteral("Rank %1").arg(r);
        p.drawText(QRect(legendRect.left() + 28, y, boxW - 32, rowH), Qt::AlignVCenter | Qt::AlignLeft, lbl);
    }
    p.restore();
}

void ArrayPlotCanvas::mousePressEvent(QMouseEvent* event) {
    if (!event || !m_owner) return;
    const QRect plotRect = plotAreaRect();
    if (event->button() == Qt::LeftButton && plotRect.contains(event->pos()) &&
        (!m_owner->currentSamples().isEmpty() || !m_owner->rankOverlays().isEmpty())) {
        m_isDraggingZoom = true;
        m_dragStartPos = event->pos();
        m_dragCurrentPos = event->pos();
        update();
    }
    QWidget::mousePressEvent(event);
}

void ArrayPlotCanvas::mouseMoveEvent(QMouseEvent* event) {
    if (!event || !m_owner) return;
    m_hoverPos = event->pos();
    const QRect plotRect = plotAreaRect();

    if (m_isDraggingZoom) {
        m_dragCurrentPos = QPoint(std::clamp(event->pos().x(), plotRect.left(), plotRect.right()),
                                  std::clamp(event->pos().y(), plotRect.top(), plotRect.bottom()));
    }

    const QVector<double>& samples = m_owner->currentSamples();
    if (!plotRect.contains(m_hoverPos) || samples.isEmpty()) {
        update();
        return;
    }

    const bool is2D = (m_owner->plotMode() == ArrayPlotDialog::PlotMode::Heatmap2D || m_owner->rows() > 1);
    if (is2D) {
        const auto [colMin, colMax] = visibleXRange();
        const auto [rowMin, rowMax] = visibleYRange();
        const int visCols = qMax(1, colMax - colMin + 1);
        const int visRows = qMax(1, rowMax - rowMin + 1);
        const int c = std::clamp(colMin + ((m_hoverPos.x() - plotRect.left()) * visCols) / plotRect.width(),
                                 colMin, colMax);
        const int r = std::clamp(rowMin + ((m_hoverPos.y() - plotRect.top()) * visRows) / plotRect.height(),
                                 rowMin, rowMax);
        const int idx = r * qMax(1, m_owner->cols()) + c;
        if (idx < samples.size()) {
            m_owner->setHoverSummaryText(
                QStringLiteral("Hover: [%1, %2] (idx %3) = %4")
                    .arg(r).arg(c).arg(idx).arg(QString::number(samples.at(idx), 'g', 8)));
        }
    } else {
        const auto [xMin, xMax] = visibleXRange();
        const int visSpan = qMax(0, xMax - xMin);
        const double ratio = static_cast<double>(m_hoverPos.x() - plotRect.left()) / qMax(1, plotRect.width());
        const int idx = std::clamp(xMin + static_cast<int>(std::round(ratio * visSpan)), xMin, xMax);
        if (idx < samples.size()) {
            const double val = samples.at(idx);
            const bool isHistory = (m_owner->plotMode() == ArrayPlotDialog::PlotMode::ScalarHistory);
            if (isHistory) {
                QString deltaText;
                if (idx > 0) {
                    const double delta = val - samples.at(idx - 1);
                    deltaText = QStringLiteral("  (delta = %1)").arg(QString::number(delta, 'g', 6));
                }
                m_owner->setHoverSummaryText(
                    QStringLiteral("Hover: Stop %1 = %2%3").arg(idx).arg(QString::number(val, 'g', 8), deltaText));
            } else {
                QString deltaText;
                if (idx < m_owner->previousSamples().size()) {
                    const double delta = val - m_owner->previousSamples().at(idx);
                    deltaText = QStringLiteral("  (prev = %1, delta = %2)")
                                    .arg(QString::number(m_owner->previousSamples().at(idx), 'g', 6),
                                         QString::number(delta, 'g', 6));
                }
                const int elemIdx = m_owner->sliceOffset() + idx * m_owner->sliceStride();
                m_owner->setHoverSummaryText(
                    QStringLiteral("Hover: [%1] = %2%3")
                        .arg(elemIdx).arg(QString::number(val, 'g', 8), deltaText));
            }
        }
    }
    update();
}

void ArrayPlotCanvas::mouseReleaseEvent(QMouseEvent* event) {
    if (!event || !m_owner || !m_isDraggingZoom || event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    m_isDraggingZoom = false;
    const QRect plotRect = plotAreaRect();
    const QRect sel = QRect(m_dragStartPos, event->pos()).normalized().intersected(plotRect);

    if (sel.width() >= 6 && sel.height() >= 6) {
        const auto [curXMin, curXMax] = visibleXRange();
        const double rx0 = static_cast<double>(sel.left() - plotRect.left()) / qMax(1, plotRect.width());
        const double rx1 = static_cast<double>(sel.right() - plotRect.left()) / qMax(1, plotRect.width());
        const bool is2D = (m_owner->plotMode() == ArrayPlotDialog::PlotMode::Heatmap2D || m_owner->rows() > 1);

        if (is2D) {
            const auto [curYMin, curYMax] = visibleYRange();
            const int visCols = qMax(1, curXMax - curXMin + 1);
            const int visRows = qMax(1, curYMax - curYMin + 1);
            const double ry0 = static_cast<double>(sel.top() - plotRect.top()) / qMax(1, plotRect.height());
            const double ry1 = static_cast<double>(sel.bottom() - plotRect.top()) / qMax(1, plotRect.height());
            m_zoomXMin = std::clamp(curXMin + static_cast<int>(std::floor(rx0 * visCols)), curXMin, curXMax);
            m_zoomXMax = std::clamp(curXMin + static_cast<int>(std::ceil(rx1 * visCols)) - 1, m_zoomXMin, curXMax);
            m_zoomYMin = std::clamp(curYMin + static_cast<int>(std::floor(ry0 * visRows)), curYMin, curYMax);
            m_zoomYMax = std::clamp(curYMin + static_cast<int>(std::ceil(ry1 * visRows)) - 1, m_zoomYMin, curYMax);
            m_hasZoomValY = false;
        } else {
            const auto [curValMin, curValMax] = compute1DYBounds(curXMin, curXMax);
            const int xSpan = qMax(0, curXMax - curXMin);
            int newXMin = std::clamp(curXMin + static_cast<int>(std::floor(rx0 * xSpan)), curXMin, curXMax);
            int newXMax = std::clamp(curXMin + static_cast<int>(std::ceil(rx1 * xSpan)), newXMin, curXMax);
            if (newXMax == newXMin && curXMax > curXMin) {
                if (newXMax < curXMax) ++newXMax;
                else --newXMin;
            }
            const double ryBottom = 1.0 - static_cast<double>(sel.bottom() - plotRect.top()) / qMax(1, plotRect.height());
            const double ryTop = 1.0 - static_cast<double>(sel.top() - plotRect.top()) / qMax(1, plotRect.height());
            const double valSpan = curValMax - curValMin;
            m_zoomXMin = newXMin;
            m_zoomXMax = newXMax;
            m_zoomValMin = curValMin + std::clamp(ryBottom, 0.0, 1.0) * valSpan;
            m_zoomValMax = curValMin + std::clamp(ryTop, 0.0, 1.0) * valSpan;
            m_hasZoomValY = (m_zoomValMax > m_zoomValMin + 1e-12);
        }
        m_zoomed = true;
        emit zoomChanged(true);
    }
    update();
}

void ArrayPlotCanvas::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event && event->button() == Qt::LeftButton) {
        resetZoom();
    }
    QWidget::mouseDoubleClickEvent(event);
}

void ArrayPlotCanvas::leaveEvent(QEvent* /*event*/) {
    m_hoverPos = QPoint(-1, -1);
    update();
}

}  // namespace gridlock::ui
