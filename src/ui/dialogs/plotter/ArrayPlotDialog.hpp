/**
 * @file ArrayPlotDialog.hpp
 * @brief Live auto-refreshing 1D/2D scientific array, complex field, multi-rank overlay,
 *        scalar history, and virtualized 2D/3D C/Fortran tensor plotter.
 */

#pragma once

#include <cstdint>

#include <QByteArray>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QPair>
#include <QPushButton>
#include <QSet>
#include <QSpinBox>
#include <QStackedWidget>
#include <QString>
#include <QStringList>
#include <QStringListModel>
#include <QTableView>
#include <QVector>

#include "ArrayPlotCanvas.hpp"
#include "TensorSliceTableModel.hpp"
#include "../../../core/hpc/gdb/LanguageExpressionHelper.hpp"

class IBackendCoordinator;

namespace gridlock::ui {

/**
 * @class ArrayPlotDialog
 * @brief Floating non-modal 1D/2D/3D array, complex spectrum, multi-rank overlay, and tensor visualizer.
 */
class ArrayPlotDialog : public QDialog {
    Q_OBJECT

public:
    /**
     * @enum ElementType
     * @brief Numeric or complex data type used when decoding contiguous raw memory byte buffers.
     */
    enum class ElementType {
        Float64    = 0,  ///< 64-bit IEEE-754 double (`double`).
        Float32    = 1,  ///< 32-bit IEEE-754 float (`float`).
        Int32      = 2,  ///< 32-bit signed integer (`int32_t`).
        Int64      = 3,  ///< 64-bit signed integer (`int64_t`).
        UInt8      = 4,  ///< 8-bit unsigned byte (`uint8_t`).
        Complex64  = 5,  ///< 64-bit interleaved complex float (`std::complex<float>`: `float re, im`).
        Complex128 = 6   ///< 128-bit interleaved complex double (`std::complex<double>`: `double re, im`).
    };

    /**
     * @enum ComplexComponent
     * @brief Scalar projection extracted from `Complex64` / `Complex128` elements.
     */
    enum class ComplexComponent {
        Magnitude = 0,  ///< Euclidean modulus `|z| = hypot(re, im)`.
        Phase     = 1,  ///< Principal phase angle `arg(z) = atan2(im, re)` in radians.
        Real      = 2,  ///< Real part `Re(z)`.
        Imaginary = 3   ///< Imaginary part `Im(z)`.
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

    /** @brief Attaches a debugger backend coordinator and wires memory, scalar eval, and stop notifications. */
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
    /** @brief Returns true when @p type is `Complex64` or `Complex128`. */
    static bool isComplexType(ElementType type);
    /** @brief Returns the byte size (1, 4, 8, or 16) of one element of @p type. */
    static int byteSizeOf(ElementType type);

    /** @brief Sets the active complex component projection (`Magnitude`, `Phase`, `Real`, `Imaginary`). */
    void setComplexComponent(ComplexComponent component);
    /** @brief Returns the active complex component projection. */
    ComplexComponent complexComponent() const;

    /** @brief Sets the visual rendering mode (1D Line, 1D Stem, 2D Heatmap, or Scalar History). */
    void setPlotMode(PlotMode mode);
    /** @brief Returns the active visual rendering mode. */
    PlotMode plotMode() const;

    /** @brief Sets the 2D heatmap colormap. */
    void setColormap(Colormap cm);
    /** @brief Returns the active 2D heatmap colormap. */
    Colormap colormap() const;

    /** @brief Sets the 2D array shape (`rows` = NY, `cols` = NX) and triggers a canvas/table update. */
    void setDimensions(int rows, int cols);
    /** @brief Sets full 3D tensor dimensions (`nx` = Cols, `ny` = Rows, `nz` = Depth). */
    void setTensorDimensions(int nx, int ny, int nz = 1);
    /** @brief Returns the configured row count (`NY`, 1 for 1D plots). */
    int rows() const { return m_rows; }
    /** @brief Returns the configured column count (`NX`). */
    int cols() const { return m_cols; }
    /** @brief Returns the configured 3D depth count (`NZ`, >= 1). */
    int depth() const { return m_depth; }

    /** @brief Sets the active 3D Z-slice index (`[0, depth() - 1]`). */
    void setZSlice(int zSlice);
    /** @brief Returns the active 3D Z-slice index. */
    int zSlice() const;

    /** @brief Sets C row-major or Fortran column-major tensor indexing order. */
    void setMemoryOrder(TensorSliceTableModel::MemoryOrder order);
    /** @brief Returns the active C row-major or Fortran column-major tensor indexing order. */
    TensorSliceTableModel::MemoryOrder memoryOrder() const;

    /** @brief Updates indexing conventions (1-based column-major for Fortran vs 0-based row-major for C/C++). */
    void setSourceLanguage(gridlock::SourceLanguage lang);
    /** @brief Returns the active frame source language applied to the plotter. */
    gridlock::SourceLanguage sourceLanguage() const { return m_activeLanguage; }
    /** @brief Returns the current language/indexing badge text shown in the dialog. */
    QString languageBadgeText() const { return m_languageBadge ? m_languageBadge->text() : QString(); }

    /** @brief Switches between the graphical Chart view (`false`) and virtualized Tensor Table (`true`). */
    void setTensorTableActive(bool active);
    /** @brief Returns true when the virtualized 2D/3D Tensor Table view is active. */
    bool isTensorTableActive() const;
    /** @brief Returns the underlying zero-copy `TensorSliceTableModel`. */
    TensorSliceTableModel* tensorTableModel() const { return m_tensorModel; }
    /** @brief Returns the virtualized `QTableView` displaying the active tensor Z-slice. */
    QTableView* tensorTableView() const { return m_tensorTableView; }

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

    /** @brief Decodes raw bytes (including `Complex64`/`Complex128` and strided slices) into double samples. */
    bool loadFromRawBytes(const QByteArray& rawBytes, ElementType type, int rows = 1, int cols = -1,
                          int offset = -1, int stride = -1);
    /** @brief Convenience overload decoding @p rawBytes using a C++ type string. */
    bool loadFromRawBytes(const QByteArray& rawBytes, const QString& typeName, int rows = 1, int cols = -1,
                          int offset = -1, int stride = -1);

    /** @brief Loads pre-decoded scalar values directly, preserving the previous stop's samples for mutation comparison. */
    void setSamples(const QVector<double>& samples, int rows = 1, int cols = -1);
    /** @brief Stores an additional rank's 1D sample curve for multi-rank halo/ghost-cell overlay comparison. */
    void setRankOverlaySamples(int rankId, const QVector<double>& samples);
    /** @brief Clears any secondary multi-rank overlay curves. */
    void clearRankOverlays();

    /** @brief Appends a single scalar measurement for Scalar History mode across debugger stops. */
    void appendScalarSample(double value, int rankId = -1);
    /** @brief Returns the accumulated primary scalar convergence history samples. */
    const QVector<double>& scalarHistory() const { return m_scalarHistory; }
    /** @brief Clears all accumulated scalar time-series history samples (primary and per-rank). */
    void clearScalarHistory();
    /** @brief Returns true when a cleared history/plot snapshot is available to restore. */
    bool canUndoClear() const { return m_hasUndoSnapshot; }
    /** @brief Restores the most recently cleared scalar history and plot sample buffers. */
    bool undoClearHistory();
    /** @brief Convenience alias forwarding to undoClearHistory(). */
    bool undoClear() { return undoClearHistory(); }

    /** @brief Returns the currently plotted sample values. */
    const QVector<double>& currentSamples() const { return m_currentSamples; }
    /** @brief Returns the decoded `(Re, Im)` complex pairs when `Complex64`/`Complex128` is active. */
    const QVector<QPair<double, double>>& complexPairs() const { return m_complexPairs; }
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

    /** @brief Builds the gnuplot script (`splot ... matrix with pm3d` for 2D, `plot ... with linespoints` for 1D). */
    QString buildGnuplotScript(const QString& dataPath) const;

    /** @brief Exports the active 1D, 2D, or Scalar History dataset to CSV. */
    bool exportCsv(const QString& filePath) const;
    /** @brief Renders the plot canvas to a PNG image file on disk. */
    bool savePng(const QString& filePath);
    /** @brief Resolves the `gnuplot` executable path (or @p overridePath when provided). */
    static QString resolveGnuplotExecutable(const QString& overridePath = QString());
    /** @brief Overrides the `gnuplot` executable path (primarily for testing). */
    void setGnuplotExecutableOverride(const QString& path) { m_gnuplotExecutableOverride = path; }
    /** @brief Pipes the active 1D or 2D dataset to an external `gnuplot -persist` process with preflight/stderr checks. */
    bool openInGnuplot(bool showMessageBox = true);

    /** @brief Updates the hover/status readout bar at the bottom of the dialog. */
    void setHoverSummaryText(const QString& text);
    /** @brief Returns the current text displayed in the plotter status strip. */
    QString statusText() const { return m_hoverLabel ? m_hoverLabel->text() : QString(); }
    /** @brief Reloads dialog colors and widget styling from the active ThemeManager theme. */
    void reloadStyle();

    /** @brief Populates live variable, struct field, and symbol autocomplete candidates on the expression box. */
    void setCompletionCandidates(const QStringList& candidates);
    /** @brief Returns the active expression autocomplete candidate list. */
    QStringList completionCandidates() const { return m_completionCandidates; }
    /** @brief Returns the QCompleter attached to the expression input box. */
    QCompleter* expressionCompleter() const { return m_completer; }

signals:
    /** @brief Emitted when multi-rank 1D overlay refresh requests memory slices across selected MPI ranks. */
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
    static double extractComplexComponent(double re, double im, ComplexComponent comp);
    static QVector<double> decodeStridedBytes(const QByteArray& rawBytes, ElementType type,
                                              ComplexComponent comp, int maxCount, int offset, int stride,
                                              QVector<QPair<double, double>>* outComplexPairs = nullptr);
    static bool parseScalarResult(const QString& resultText, double* outVal);
    ArraySliceSpec syncSliceControlsFromExpression();
    void recomputeComplexProjection();
    void recomputeStats();

    ::IBackendCoordinator* m_coordinator = nullptr;
    gridlock::SourceLanguage m_activeLanguage = gridlock::SourceLanguage::Unknown;
    int m_focusedRank = 0;
    int m_rows = 1;
    int m_cols = 64;
    int m_depth = 1;
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
    QVector<QPair<double, double>> m_complexPairs;
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

    struct PlotUndoSnapshot {
        QVector<double> scalarHistory, currentSamples, previousSamples;
        QVector<QPair<double, double>> complexPairs;
        QMap<int, QVector<double>> rankScalarHistories, rankOverlays;
        int rows = 1, cols = 1, depth = 1;
    };
    bool m_hasUndoSnapshot = false;
    PlotUndoSnapshot m_undoSnapshot;

    QLineEdit* m_exprEdit = nullptr;
    QCompleter* m_completer = nullptr;
    QStringListModel* m_completerModel = nullptr;
    QStringList m_completionCandidates;
    QComboBox* m_typeCombo = nullptr;
    QComboBox* m_componentCombo = nullptr;
    QComboBox* m_modeCombo = nullptr;
    QComboBox* m_colormapCombo = nullptr;
    QSpinBox* m_rowsSpin = nullptr;
    QSpinBox* m_colsSpin = nullptr;
    QSpinBox* m_depthSpin = nullptr;
    QSpinBox* m_zSliceSpin = nullptr;
    QComboBox* m_memoryOrderCombo = nullptr;
    QSpinBox* m_offsetSpin = nullptr;
    QSpinBox* m_strideSpin = nullptr;
    QCheckBox* m_logScaleCheck = nullptr;
    QPushButton* m_resetZoomBtn = nullptr;
    QPushButton* m_clearHistoryBtn = nullptr;
    QPushButton* m_undoClearBtn = nullptr;
    QPushButton* m_chartViewBtn = nullptr;
    QPushButton* m_tableViewBtn = nullptr;
    QComboBox* m_rankCombo = nullptr;
    QCheckBox* m_overlayRanksCheck = nullptr;
    QLineEdit* m_rankSubsetEdit = nullptr;
    QCheckBox* m_autoRefreshCheck = nullptr;
    QPushButton* m_refreshBtn = nullptr;
    QPushButton* m_exportCsvBtn = nullptr;
    QPushButton* m_savePngBtn = nullptr;
    QPushButton* m_gnuplotBtn = nullptr;
    QPushButton* m_closeBtn = nullptr;

    QStackedWidget* m_viewStack = nullptr;
    ArrayPlotCanvas* m_canvas = nullptr;
    TensorSliceTableModel* m_tensorModel = nullptr;
    QTableView* m_tensorTableView = nullptr;
    QLabel* m_statsLabel = nullptr;
    QLabel* m_languageBadge = nullptr;
    QLabel* m_hoverLabel = nullptr;
    QString m_gnuplotExecutableOverride;
};

}  // namespace gridlock::ui
