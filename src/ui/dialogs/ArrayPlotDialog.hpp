/**
 * @file ArrayPlotDialog.hpp
 * @brief Live auto-refreshing 1D/2D scientific array, multi-rank overlay, and scalar history plotter.
 *
 * Implements `ArrayPlotDialog` as an independent floating `Qt::Window` tool supporting
 * 1D Line, 1D Bar/Stem, 2D Heatmap, and Scalar Time-Series History visualization of raw
 * C/C++ arrays, pointers, strided slices (`start:end:stride`), and scalar convergence
 * expressions across MPI ranks. Automatically re-evaluates on debugger stop events,
 * tracks previous-stop deltas, flags IEEE-754 NaN/+/-Inf anomalies, supports Log10 scale
 * and drag-box zoom, and exports CSV, PNG, or gnuplot scripts.
 */

#pragma once

#include <cstdint>

#include <QByteArray>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QPair>
#include <QPushButton>
#include <QSet>
#include <QSpinBox>
#include <QString>
#include <QVector>

#include "ArrayPlotCanvas.hpp"

class IBackendCoordinator;

namespace gridlock::ui {

/**
 * @class ArrayPlotDialog
 * @brief Floating non-modal 1D/2D array, multi-rank overlay, and scalar history visualizer.
 */
class ArrayPlotDialog : public QDialog {
    Q_OBJECT

public:
    /**
     * @enum ElementType
     * @brief Numeric data type used when decoding contiguous raw memory byte buffers.
     */
    enum class ElementType {
        Float64 = 0,  ///< 64-bit IEEE-754 double (`double`).
        Float32 = 1,  ///< 32-bit IEEE-754 float (`float`).
        Int32   = 2,  ///< 32-bit signed integer (`int32_t`).
        Int64   = 3,  ///< 64-bit signed integer (`int64_t`).
        UInt8   = 4   ///< 8-bit unsigned byte (`uint8_t`).
    };

    /**
     * @enum PlotMode
     * @brief Visual rendering mode of the plot canvas.
     */
    enum class PlotMode {
        Line1D        = 0,  ///< 1D anti-aliased polyline chart with previous-stop ghost curve.
        Stem1D        = 1,  ///< 1D discrete bar / stem chart.
        Heatmap2D     = 2,  ///< 2D Rows x Cols colormapped matrix with colorbar legend.
        ScalarHistory = 3   ///< Scalar expression convergence history plotted across debugger stops.
    };

    /**
     * @enum Colormap
     * @brief Colorblind-safe scientific colormap used in 2D Heatmap mode.
     */
    enum class Colormap {
        Viridis   = 0,
        Plasma    = 1,
        CoolWarm  = 2,
        Grayscale = 3
    };

    /**
     * @brief Constructs the floating Array & Field Plotter dialog.
     * @param coordinator Optional active debugger backend coordinator.
     * @param parent Optional owning MainWindow or parent widget.
     */
    explicit ArrayPlotDialog(::IBackendCoordinator* coordinator = nullptr,
                             QWidget* parent = nullptr);

    /** @brief Virtual destructor. */
    ~ArrayPlotDialog() override = default;

    /**
     * @brief Attaches a debugger backend coordinator and wires memory, scalar eval, and stop notifications.
     * @param coordinator Active IBackendCoordinator instance.
     */
    void setCoordinator(::IBackendCoordinator* coordinator);

    /** @brief Sets the C/C++ array, slice (`arr[start:end:stride]`), or scalar expression and syncs slice controls. */
    void setExpression(const QString& expr);
    /** @brief Returns the active raw expression text from the toolbar input. */
    QString expression() const;
    /** @brief Returns the base expression stripped of any trailing slice suffix. */
    QString baseExpression() const;

    /** @brief Sets the element data type enum used to decode raw byte buffers. */
    void setElementType(ElementType type);
    /** @brief Infers and selects the element data type from a C/C++ type declaration string. */
    void setElementTypeFromString(const QString& cType);
    /** @brief Returns the currently selected element data type. */
    ElementType elementType() const;
    /** @brief Returns the byte size (1, 4, or 8) of one element of @p type. */
    static int byteSizeOf(ElementType type);

    /** @brief Sets the visual rendering mode (1D Line, 1D Stem, 2D Heatmap, or Scalar History). */
    void setPlotMode(PlotMode mode);
    /** @brief Returns the active visual rendering mode. */
    PlotMode plotMode() const;

    /** @brief Sets the 2D heatmap colormap. */
    void setColormap(Colormap cm);
    /** @brief Returns the active 2D heatmap colormap. */
    Colormap colormap() const;

    /** @brief Sets the 2D array shape (`rows` = NY, `cols` = NX) and triggers a canvas update. */
    void setDimensions(int rows, int cols);
    /** @brief Returns the configured row count (`NY`, 1 for 1D plots). */
    int rows() const { return m_rows; }
    /** @brief Returns the configured column count (`NX`). */
    int cols() const { return m_cols; }

    /** @brief Sets the starting element offset for strided slicing (`offset >= 0`). */
    void setSliceOffset(int offset);
    /** @brief Returns the configured starting element offset. */
    int sliceOffset() const { return m_sliceOffset; }

    /** @brief Sets the element stride step for strided slicing (`stride >= 1`). */
    void setSliceStride(int stride);
    /** @brief Returns the configured element stride step. */
    int sliceStride() const { return m_sliceStride; }

    /** @brief Enables or disables base-10 logarithmic scaling (`Log10`). */
    void setLogScale(bool enabled);
    /** @brief Returns true when Log10 scale is enabled. */
    bool isLogScale() const;

    /** @brief Resets interactive drag-box zoom on the canvas. */
    void resetZoom();
    /** @brief Returns true when the plot canvas is currently zoomed into a sub-window. */
    bool isZoomed() const;
    /** @brief Returns the active visible X index/column range `[min, max]` inclusive. */
    QPair<int, int> visibleXRange() const;
    /** @brief Returns the active visible Y row range `[min, max]` inclusive (2D mode). */
    QPair<int, int> visibleYRange() const;
    /** @brief Returns the underlying interactive plot canvas widget. */
    ArrayPlotCanvas* canvas() const { return m_canvas; }

    /** @brief Sets the focused MPI rank index. */
    void setFocusedRank(int rankId);
    /** @brief Returns the focused MPI rank index. */
    int focusedRank() const { return m_focusedRank; }

    /** @brief Enables or disables multi-rank 1D overlay mode. */
    void setOverlayRanksEnabled(bool enabled);
    /** @brief Returns true when multi-rank 1D overlay mode is active. */
    bool isOverlayRanksEnabled() const;

    /** @brief Sets the multi-rank subset specification string (e.g., `"All"`, `"0..3"`, `"0,2,3"`). */
    void setRankSubsetSpec(const QString& spec);
    /** @brief Returns the current rank subset specification text. */
    QString rankSubsetSpec() const;

    /** @brief Resolves the selected overlay rank list given optional @p totalRanks (`< 0` queries coordinator). */
    QVector<int> selectedOverlayRanks(int totalRanks = -1) const;

    /** @brief Registers @p rankId as expecting an incoming `memoryRead` reply for multi-rank overlay rendering. */
    void expectOverlayReadForRank(int rankId);

    /** @brief Returns whether automatic re-evaluation on debugger `*stopped` events is enabled. */
    bool isAutoRefreshEnabled() const;
    /** @brief Enables or disables automatic re-evaluation on debugger `*stopped` events. */
    void setAutoRefreshEnabled(bool enabled);

    /**
     * @brief Decodes raw bytes with optional strided slicing (`offset + i * stride`) into double samples.
     * @param rawBytes Contiguous raw bytes in host little-endian order.
     * @param type Element data type used to decode @p rawBytes.
     * @param rows Number of rows (`NY`, >= 1).
     * @param cols Number of columns (`NX`; `<= 0` uses slice count or infers from buffer size).
     * @param offset Starting element index (`< 0` uses dialog's configured `sliceOffset()`).
     * @param stride Step between elements (`<= 0` uses dialog's configured `sliceStride()`).
     * @return True if at least one sample was decoded.
     */
    bool loadFromRawBytes(const QByteArray& rawBytes, ElementType type, int rows = 1, int cols = -1,
                          int offset = -1, int stride = -1);

    /**
     * @brief Convenience overload decoding @p rawBytes using a C++ type string ("double", "float", "int32_t", etc.).
     */
    bool loadFromRawBytes(const QByteArray& rawBytes, const QString& typeName, int rows = 1, int cols = -1,
                          int offset = -1, int stride = -1);

    /**
     * @brief Loads pre-decoded scalar values directly, preserving the previous stop's samples for mutation comparison.
     */
    void setSamples(const QVector<double>& samples, int rows = 1, int cols = -1);

    /**
     * @brief Stores an additional rank's 1D sample curve for multi-rank halo/ghost-cell overlay comparison.
     */
    void setRankOverlaySamples(int rankId, const QVector<double>& samples);

    /** @brief Clears any secondary multi-rank overlay curves. */
    void clearRankOverlays();

    /**
     * @brief Appends a single scalar measurement for Scalar History mode across debugger stops.
     * @param value Evaluated scalar value at the current stop.
     * @param rankId Optional MPI rank ID (`< 0` targets focused rank).
     */
    void appendScalarSample(double value, int rankId = -1);

    /** @brief Returns the accumulated primary scalar convergence history samples. */
    const QVector<double>& scalarHistory() const { return m_scalarHistory; }

    /** @brief Clears all accumulated scalar time-series history samples (primary and per-rank). */
    void clearScalarHistory();

    /** @brief Returns the currently plotted sample values. */
    const QVector<double>& currentSamples() const { return m_currentSamples; }
    /** @brief Returns the previous stop's sample values (used for ghost-curve and delta calculation). */
    const QVector<double>& previousSamples() const { return m_previousSamples; }
    /** @brief Returns per-rank 1D comparison curves. */
    const QMap<int, QVector<double>>& rankOverlays() const { return m_rankOverlays; }

    /** @brief Minimum finite sample value in the active buffer. */
    double minValue() const { return m_minVal; }
    /** @brief Maximum finite sample value in the active buffer. */
    double maxValue() const { return m_maxVal; }
    /** @brief Arithmetic mean of finite sample values in the active buffer. */
    double meanValue() const { return m_meanVal; }
    /** @brief Euclidean L2 norm (`sqrt(sum(x_i^2))`) of finite sample values. */
    double l2Norm() const { return m_l2Norm; }
    /** @brief Number of IEEE-754 `NaN` values detected in the active buffer. */
    int nanCount() const { return m_nanCount; }
    /** @brief Number of IEEE-754 `+/-Inf` values detected in the active buffer. */
    int infCount() const { return m_infCount; }
    /** @brief Combined count of `NaN` and `+/-Inf` anomalies in the active buffer. */
    int nanInfCount() const { return m_nanCount + m_infCount; }
    /** @brief Number of elements whose value changed compared to the previous stop snapshot. */
    int changedElementCount() const { return m_changedElementCount; }
    /** @brief Total number of stop-triggered auto-refreshes performed. */
    int stopRefreshCount() const { return m_stopRefreshCount; }

    /** @brief Exports the active 1D, 2D, or Scalar History dataset to CSV. */
    bool exportCsv(const QString& filePath) const;
    /** @brief Renders the plot canvas to a PNG image file on disk. */
    bool savePng(const QString& filePath);
    /** @brief Pipes the active dataset to an external `gnuplot -persist` process if installed. */
    bool openInGnuplot() const;

    /** @brief Updates the hover readout bar at the bottom of the dialog. */
    void setHoverSummaryText(const QString& text);
    /** @brief Reloads dialog colors and widget styling from the active ThemeManager theme. */
    void reloadStyle();

signals:
    /**
     * @brief Emitted when multi-rank 1D overlay refresh requests memory slices across selected MPI ranks.
     * @param baseExpression Stripped C/C++ array or pointer expression.
     * @param byteCount Contiguous byte span covering `offset + (count - 1) * stride + 1` elements.
     * @param elementType Element data type for decoding.
     * @param offset Starting element offset.
     * @param stride Step between consecutive elements.
     */
    void multiRankPlotRequested(const QString& baseExpression,
                                int byteCount,
                                gridlock::ui::ArrayPlotDialog::ElementType elementType,
                                int offset,
                                int stride);

public slots:
    /** @brief Requests a fresh memory read (or scalar evaluation in ScalarHistory mode) from the backend. */
    void refreshPlot();

    /** @brief Invoked automatically when MPI rank @p rankId hits a breakpoint/step stop. */
    void onTargetStopped(int rankId);

private:
    /** @brief Decodes raw bytes with explicit @p offset, @p stride, and max @p maxCount into double samples. */
    static QVector<double> decodeStridedBytes(const QByteArray& rawBytes, ElementType type,
                                              int maxCount, int offset, int stride);
    /** @brief Parses a numeric scalar value from a debugger expression evaluation string (e.g., `"$1 = 1.2e-4"`). */
    static bool parseScalarResult(const QString& resultText, double* outVal);
    /** @brief Applies parsed slice parameters from `m_exprEdit` to offset/stride/cols spinboxes. */
    ArraySliceSpec syncSliceControlsFromExpression();
    /** @brief Recomputes Min, Max, Mean, L2 Norm, NaN/Inf counts, and Changed Elements. */
    void recomputeStats();

    ::IBackendCoordinator* m_coordinator = nullptr;
    int m_focusedRank = 0;
    int m_rows = 1;
    int m_cols = 64;
    int m_sliceOffset = 0;
    int m_sliceStride = 1;
    bool m_hasExplicitSliceCount = false;
    int m_stopRefreshCount = 0;
    bool m_awaitingCoordinatorRead = false;
    QSet<int> m_pendingOverlayRanks;
    bool m_awaitingScalarEval = false;
    QSet<int> m_pendingScalarRanks;
    QString m_pendingScalarExpr;

    QVector<double> m_currentSamples;
    QVector<double> m_previousSamples;
    QMap<int, QVector<double>> m_rankOverlays;
    QVector<double> m_scalarHistory;
    QMap<int, QVector<double>> m_rankScalarHistories;

    double m_minVal = 0.0;
    double m_maxVal = 0.0;
    double m_meanVal = 0.0;
    double m_l2Norm = 0.0;
    int m_nanCount = 0;
    int m_infCount = 0;
    int m_changedElementCount = 0;

    QLineEdit* m_exprEdit = nullptr;
    QComboBox* m_typeCombo = nullptr;
    QComboBox* m_modeCombo = nullptr;
    QComboBox* m_colormapCombo = nullptr;
    QSpinBox* m_rowsSpin = nullptr;
    QSpinBox* m_colsSpin = nullptr;
    QSpinBox* m_offsetSpin = nullptr;
    QSpinBox* m_strideSpin = nullptr;
    QCheckBox* m_logScaleCheck = nullptr;
    QPushButton* m_resetZoomBtn = nullptr;
    QPushButton* m_clearHistoryBtn = nullptr;
    QComboBox* m_rankCombo = nullptr;
    QCheckBox* m_overlayRanksCheck = nullptr;
    QLineEdit* m_rankSubsetEdit = nullptr;
    QCheckBox* m_autoRefreshCheck = nullptr;
    QPushButton* m_refreshBtn = nullptr;
    QPushButton* m_exportCsvBtn = nullptr;
    QPushButton* m_savePngBtn = nullptr;
    QPushButton* m_gnuplotBtn = nullptr;
    QPushButton* m_closeBtn = nullptr;

    ArrayPlotCanvas* m_canvas = nullptr;
    QLabel* m_statsLabel = nullptr;
    QLabel* m_hoverLabel = nullptr;
};

}  // namespace gridlock::ui
