/**
 * @file ArrayPlotCanvas.hpp
 * @brief Custom scientific 1D/2D plot canvas, slice-expression parser, and rank-subset helpers.
 *
 * Implements `ArrayPlotCanvas` with anti-aliased 1D Line, 1D Bar/Stem, 2D Heatmap, and
 * Scalar Time-Series History rendering, logarithmic (`Log10`) Y/color scaling with safe
 * non-positive handling, interactive drag-box zoom + double-click reset, multi-rank
 * curve overlays with a color legend, and Python/NumPy `start:end:stride` slice parsing.
 */

#pragma once

#include <QColor>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPair>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QVector>
#include <QWidget>

namespace gridlock::ui {

class ArrayPlotDialog;

/**
 * @struct ArraySliceSpec
 * @brief Parsed components of an array slice expression (e.g., `u[16:128:2]` or `field[0..63]`).
 */
struct ArraySliceSpec {
    QString baseExpression;  ///< Stripped C/C++ array or pointer expression without slice suffix.
    int offset = 0;          ///< Starting element index (`start`, >= 0).
    int count = 0;           ///< Number of strided elements (`<= 0` when end bound is omitted).
    int stride = 1;          ///< Step between consecutive plotted elements (`>= 1`).
    bool hasSlice = false;   ///< True when trailing `[start:end:stride]` or `[start..end]` was matched.
};

/**
 * @brief Parses Python/NumPy (`arr[start:end:stride]`, `arr[:end]`, `arr[start::stride]`)
 *        and inclusive (`arr[start..end]`) slice suffixes from @p expression.
 * @param expression Raw user expression string.
 * @return Parsed `ArraySliceSpec`; leaves `hasSlice = false` for plain C/C++ expressions like `&vec[0]`.
 */
ArraySliceSpec parseArraySliceExpression(const QString& expression);

/**
 * @brief Parses a multi-rank subset specification (`"All"`, `"0..3"`, `"0,2,3"`, `"0..1, 3"`).
 * @param spec Rank filter string from the toolbar.
 * @param totalRanks Total MPI process count (`<= 0` falls back to 4 for `"All"` or parsed range bounds).
 * @return Sorted, deduplicated list of 0-based MPI rank IDs.
 */
QVector<int> parseRankSubset(const QString& spec, int totalRanks);

/**
 * @class ArrayPlotCanvas
 * @brief Anti-aliased QPainter canvas supporting 1D/2D plots, Log10 scaling, drag-box zoom, and multi-rank legends.
 */
class ArrayPlotCanvas : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief Constructs the scientific plot canvas widget.
     * @param owner Owning ArrayPlotDialog providing sample buffers and display settings.
     */
    explicit ArrayPlotCanvas(ArrayPlotDialog* owner);

    /** @brief Virtual destructor. */
    ~ArrayPlotCanvas() override = default;

    /**
     * @brief Maps a normalized scalar @p t in [0.0, 1.0] to a colorblind-safe RGB color for @p colormapId.
     * @param t Normalized scalar in [0.0, 1.0].
     * @param colormapId Colormap index (0 = Viridis, 1 = Plasma, 2 = CoolWarm, 3 = Grayscale).
     * @return Interpolated QColor.
     */
    static QColor sampleColormap(double t, int colormapId);

    /** @brief Returns the palette color assigned to overlay rank @p rankId (focused rank uses primary cyan). */
    static QColor rankOverlayColor(int rankId, bool isFocused);

    /** @brief Enables or disables base-10 logarithmic scaling for 1D Y-axis and 2D colormap normalization. */
    void setLogScale(bool enabled);
    /** @brief Returns true when Log10 scaling is active. */
    bool isLogScale() const { return m_logScale; }

    /** @brief Resets any active drag-box zoom window back to full dataset bounds. */
    void resetZoom();
    /** @brief Returns true when a custom drag-box zoom window is active. */
    bool isZoomed() const { return m_zoomed; }

    /** @brief Programmatically sets the visible 1D/column index window `[xMin, xMax]` (inclusive). */
    void setZoomIndexWindow(int xMin, int xMax);

    /** @brief Returns the active visible X (sample index or 2D column) range `[min, max]` inclusive. */
    QPair<int, int> visibleXRange() const;

    /** @brief Returns the active visible Y (2D row) range `[min, max]` inclusive. */
    QPair<int, int> visibleYRange() const;

    /** @brief Returns the inner drawable plot rectangle excluding axis margins and colorbar/legend padding. */
    QRect plotAreaRect() const;

signals:
    /** @brief Emitted whenever interactive zoom activates (`true`) or resets (`false`). */
    void zoomChanged(bool zoomed);

protected:
    /** @brief Renders the active 1D line/stem/history plot or 2D heatmap with axes, legend, and zoom marquee. */
    void paintEvent(QPaintEvent* event) override;

    /** @brief Initiates left-button drag-box zoom selection inside the plot region. */
    void mousePressEvent(QMouseEvent* event) override;

    /** @brief Updates zoom selection marquee and cursor hover readout. */
    void mouseMoveEvent(QMouseEvent* event) override;

    /** @brief Commits the drag-box zoom window when the left mouse button is released. */
    void mouseReleaseEvent(QMouseEvent* event) override;

    /** @brief Resets active zoom on double-click inside the canvas. */
    void mouseDoubleClickEvent(QMouseEvent* event) override;

    /** @brief Clears the hover crosshair when the cursor leaves the plot canvas. */
    void leaveEvent(QEvent* event) override;

private:
    /** @brief Computes displayed Y-axis bounds `[yMin, yMax]` (linear or log10 space) across active & overlaid series. */
    QPair<double, double> compute1DYBounds(int xMin, int xMax, bool* hasNonPositive = nullptr) const;

    /** @brief Renders 2D colormapped heatmap (linear or log10) with colorbar and zoom support. */
    void paintHeatmap2D(QPainter& p, const QRect& plotRect, const QColor& canvasBg,
                        const QColor& axisCol, const QColor& textCol, const QColor& anomCol) const;

    /** @brief Renders 1D Line, 1D Stem, or Scalar History plot with ghost curve, overlays, legend, and Log10 support. */
    void paintPlot1D(QPainter& p, const QRect& plotRect, bool isDark, const QColor& gridCol,
                     const QColor& axisCol, const QColor& textCol, const QColor& lineCol,
                     const QColor& prevCol, const QColor& mutCol, const QColor& anomCol) const;

    /** @brief Draws the top-right multi-rank color legend box when rank overlays are active. */
    void paintRankLegend(QPainter& p, const QRect& plotRect, bool isDark,
                         const QColor& axisCol, const QColor& textCol) const;

    ArrayPlotDialog* m_owner = nullptr;
    QPoint m_hoverPos{-1, -1};
    bool m_logScale = false;

    bool m_isDraggingZoom = false;
    QPoint m_dragStartPos;
    QPoint m_dragCurrentPos;

    bool m_zoomed = false;
    int m_zoomXMin = 0;
    int m_zoomXMax = 0;
    int m_zoomYMin = 0;
    int m_zoomYMax = 0;
    bool m_hasZoomValY = false;
    double m_zoomValMin = 0.0;
    double m_zoomValMax = 0.0;
};

}  // namespace gridlock::ui
