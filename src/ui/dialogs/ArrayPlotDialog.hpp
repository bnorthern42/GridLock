/**
 * @file ArrayPlotDialog.hpp
 * @brief DDD-inspired live auto-refreshing 1D/2D scientific array and field plotter.
 *
 * Implements ArrayPlotDialog and ArrayPlotCanvas as an independent floating Qt::Window
 * tool supporting 1D Line, 1D Bar/Stem, and 2D Heatmap visualization of raw C/C++ arrays,
 * pointers, and std::vector buffers across MPI ranks. Automatically re-evaluates on
 * debugger stop events (*stopped), highlights element deltas against the previous stop,
 * flags IEEE-754 NaN and +/-Inf anomalies, and exports CSV, PNG, or gnuplot scripts.
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
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPushButton>
#include <QSpinBox>
#include <QString>
#include <QVector>
#include <QWidget>

class IBackendCoordinator;

namespace gridlock::ui {

class ArrayPlotDialog;

/**
 * @class ArrayPlotCanvas
 * @brief Custom anti-aliased QPainter canvas rendering 1D line/stem curves, ghost stop overlays, and 2D colormapped heatmaps.
 */
class ArrayPlotCanvas : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief Constructs the custom scientific plot canvas widget.
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

protected:
    /** @brief Renders the active 1D line/stem plot or 2D heatmap with axes, colorbar, and NaN/Inf markers. */
    void paintEvent(QPaintEvent* event) override;

    /** @brief Tracks cursor position for hover crosshairs and `[row, col] = val (delta)` readout. */
    void mouseMoveEvent(QMouseEvent* event) override;

    /** @brief Clears the hover crosshair when the cursor leaves the plot canvas. */
    void leaveEvent(QEvent* event) override;

private:
    ArrayPlotDialog* m_owner = nullptr;
    QPoint m_hoverPos{-1, -1};
};

/**
 * @class ArrayPlotDialog
 * @brief Floating non-modal 1D/2D array and field visualizer with automatic stop-event refresh and anomaly detection.
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
        Line1D    = 0,  ///< 1D anti-aliased polyline chart with previous-stop ghost curve.
        Stem1D    = 1,  ///< 1D discrete bar / stem chart.
        Heatmap2D = 2   ///< 2D Rows x Cols colormapped matrix with colorbar legend.
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
     * @brief Attaches a debugger backend coordinator and wires memory/stop notifications.
     * @param coordinator Active IBackendCoordinator instance.
     */
    void setCoordinator(::IBackendCoordinator* coordinator);

    /** @brief Sets the C/C++ array or pointer expression (e.g., "u", "grid.data()", "&vec[0]"). */
    void setExpression(const QString& expr);
    /** @brief Returns the active C/C++ array or pointer expression. */
    QString expression() const;

    /** @brief Sets the element data type enum used to decode raw byte buffers. */
    void setElementType(ElementType type);
    /** @brief Infers and selects the element data type from a C/C++ type declaration string (e.g., "double *", "std::vector<float>"). */
    void setElementTypeFromString(const QString& cType);
    /** @brief Returns the currently selected element data type. */
    ElementType elementType() const;
    /** @brief Returns the byte size (1, 4, or 8) of one element of @p type. */
    static int byteSizeOf(ElementType type);

    /** @brief Sets the visual rendering mode (1D Line, 1D Stem, or 2D Heatmap). */
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

    /** @brief Sets the focused MPI rank index. */
    void setFocusedRank(int rankId);
    /** @brief Returns the focused MPI rank index. */
    int focusedRank() const { return m_focusedRank; }

    /** @brief Returns whether automatic re-evaluation on debugger `*stopped` events is enabled. */
    bool isAutoRefreshEnabled() const;
    /** @brief Enables or disables automatic re-evaluation on debugger `*stopped` events. */
    void setAutoRefreshEnabled(bool enabled);

    /**
     * @brief Decodes a contiguous raw memory buffer into double-precision plot samples, preserving the prior snapshot for delta tracking.
     * @param rawBytes Contiguous raw bytes in host little-endian order.
     * @param type Element data type used to decode @p rawBytes.
     * @param rows Number of rows (`NY`, >= 1).
     * @param cols Number of columns (`NX`; if <= 0, inferred from `rawBytes.size() / sizeof(type)`).
     * @return True if at least one sample was decoded.
     */
    bool loadFromRawBytes(const QByteArray& rawBytes, ElementType type, int rows = 1, int cols = -1);

    /**
     * @brief Convenience overload decoding @p rawBytes using a C++ type string ("double", "float", "int32_t", "int64_t", "uint8_t").
     */
    bool loadFromRawBytes(const QByteArray& rawBytes, const QString& typeName, int rows = 1, int cols = -1);

    /**
     * @brief Loads pre-decoded scalar values directly, preserving the previous stop's samples for mutation comparison.
     * @param samples Contiguous row-major scalar values.
     * @param rows Number of rows (`NY`, >= 1).
     * @param cols Number of columns (`NX`; if <= 0, set to `samples.size() / rows`).
     */
    void setSamples(const QVector<double>& samples, int rows = 1, int cols = -1);

    /**
     * @brief Stores an additional rank's 1D sample curve for multi-rank halo/ghost-cell overlay comparison.
     * @param rankId MPI rank index.
     * @param samples 1D sample vector for @p rankId.
     */
    void setRankOverlaySamples(int rankId, const QVector<double>& samples);

    /** @brief Clears any secondary multi-rank overlay curves. */
    void clearRankOverlays();

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

    /**
     * @brief Exports the active 1D or 2D sample series (including previous values and deltas in 1D) to a CSV file on disk.
     * @param filePath Destination CSV file path.
     * @return True if the file was written and is non-empty.
     */
    bool exportCsv(const QString& filePath) const;

    /**
     * @brief Renders the plot canvas to a PNG image file on disk.
     * @param filePath Destination PNG file path.
     * @return True if the PNG image was saved.
     */
    bool savePng(const QString& filePath);

    /**
     * @brief Pipes the active dataset to an external `gnuplot -persist` process if `gnuplot` is installed on PATH.
     * @return True if the gnuplot script was written (and launched when gnuplot binary is present).
     */
    bool openInGnuplot() const;

    /** @brief Updates the hover readout bar at the bottom of the dialog. */
    void setHoverSummaryText(const QString& text);

    /** @brief Reloads dialog colors and widget styling from the active ThemeManager theme. */
    void reloadStyle();

public slots:
    /**
     * @brief Requests a fresh memory read for the configured expression from the active debugger backend.
     */
    void refreshPlot();

    /**
     * @brief Invoked automatically when MPI rank @p rankId hits a breakpoint/step stop or becomes the focused rank.
     * @param rankId Zero-based MPI rank index.
     */
    void onTargetStopped(int rankId);

private:
    /** @brief Recomputes Min, Max, Mean, L2 Norm, NaN/Inf counts, and Changed Elements against `m_previousSamples`. */
    void recomputeStats();

    ::IBackendCoordinator* m_coordinator = nullptr;
    int m_focusedRank = 0;
    int m_rows = 1;
    int m_cols = 64;
    int m_stopRefreshCount = 0;
    bool m_awaitingCoordinatorRead = false;

    QVector<double> m_currentSamples;
    QVector<double> m_previousSamples;
    QMap<int, QVector<double>> m_rankOverlays;

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
    QComboBox* m_rankCombo = nullptr;
    QCheckBox* m_overlayRanksCheck = nullptr;
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
