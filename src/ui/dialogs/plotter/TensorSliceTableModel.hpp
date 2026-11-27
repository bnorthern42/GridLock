/**
 * @file TensorSliceTableModel.hpp
 * @brief Zero-copy virtualized QAbstractTableModel for 2D/3D C row-major and Fortran column-major tensors.
 *
 * Exposes an `[Nx, Ny, Nz]` slice view over `ArrayPlotDialog`'s contiguous sample buffer
 * without per-cell widget allocations, supporting 100,000+ elements at 60 FPS, complex
 * `(Re, Im)` hover tooltips, and magenta IEEE-754 `NaN`/`Inf` anomaly highlighting.
 */

#pragma once

#include <QAbstractTableModel>
#include <QPair>
#include <QString>
#include <QVariant>
#include <QVector>

namespace gridlock::ui {

/**
 * @class TensorSliceTableModel
 * @brief Virtualized 2D Z-slice table model over 2D/3D C row-major or Fortran column-major buffers.
 */
class TensorSliceTableModel : public QAbstractTableModel {
    Q_OBJECT

public:
    /**
     * @enum MemoryOrder
     * @brief Multi-dimensional array storage layout used to map `(x, y, z)` to a linear index.
     */
    enum class MemoryOrder {
        CRowMajor      = 0,  ///< C / C++ row-major: `idx = (z * Ny + y) * Nx + x`.
        FortranColMajor = 1  ///< Fortran column-major: `idx = (z * Nx + x) * Ny + y`.
    };

    /**
     * @brief Constructs an empty tensor slice table model.
     * @param parent Optional owning QObject.
     */
    explicit TensorSliceTableModel(QObject* parent = nullptr);

    /** @brief Virtual destructor. */
    ~TensorSliceTableModel() override = default;

    /**
     * @brief Binds non-owning pointers to the active scalar sample vector and optional complex `(Re, Im)` pairs.
     * @param samples Pointer to decoded scalar values (owned by `ArrayPlotDialog`).
     * @param complexPairs Optional pointer to raw `(Re, Im)` pairs when Complex64/Complex128 is active.
     */
    void setSourceData(const QVector<double>* samples,
                       const QVector<QPair<double, double>>* complexPairs = nullptr);

    /**
     * @brief Configures the 3D tensor shape `(Nx = cols, Ny = rows, Nz = depth)` and clamps `zSlice`.
     */
    void setShape(int nx, int ny, int nz = 1);

    /** @brief Returns the X dimension (`Cols`, `Nx`). */
    int nx() const { return m_nx; }
    /** @brief Returns the Y dimension (`Rows`, `Ny`). */
    int ny() const { return m_ny; }
    /** @brief Returns the Z dimension (`Depth`, `Nz`). */
    int nz() const { return m_nz; }

    /** @brief Selects the active 0-based Z-slice (`[0, Nz - 1]`). */
    void setZSlice(int zSlice);
    /** @brief Returns the active 0-based Z-slice index. */
    int zSlice() const { return m_zSlice; }

    /** @brief Sets C row-major or Fortran column-major indexing order. */
    void setMemoryOrder(MemoryOrder order);
    /** @brief Returns the active memory layout order. */
    MemoryOrder memoryOrder() const { return m_memoryOrder; }

    /** @brief Sets the displayed coordinate index base (`0` for C/C++, `1` for Fortran). */
    void setIndexBase(int base);
    /** @brief Returns the active displayed coordinate index base (`0` or `1`). */
    int indexBase() const { return m_indexBase; }

    /**
     * @brief Computes the 0-based linear element offset for `(x, y, z)` under `order`.
     */
    static qsizetype computeLinearIndex(int x, int y, int z, int nx, int ny, MemoryOrder order);

    /** @brief Computes the 0-based linear element offset for `(col = x, row = y)` on the active Z-slice. */
    qsizetype linearIndexForCell(int row, int col) const;

    /** @brief Returns the numeric value at `(row = y, col = x)` on the active Z-slice, or `NaN` if out of bounds. */
    double cellValue(int row, int col) const;

    /** @brief Notifies attached views that underlying sample values were refreshed. */
    void notifyDataRefreshed();

    /** @brief Returns `Ny` (number of rows in the active 2D slice). */
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;

    /** @brief Returns `Nx` (number of columns in the active 2D slice). */
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;

    /** @brief Returns `[x]` horizontal and `[y]` vertical coordinate headers. */
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    /** @brief Formats cell text, magenta `NaN`/`Inf` badges, alignment, and `[linear_idx]` tooltips. */
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

private:
    const QVector<double>* m_samples = nullptr;
    const QVector<QPair<double, double>>* m_complexPairs = nullptr;
    int m_nx = 0;
    int m_ny = 0;
    int m_nz = 1;
    int m_zSlice = 0;
    int m_indexBase = 0;
    MemoryOrder m_memoryOrder = MemoryOrder::CRowMajor;
};

}  // namespace gridlock::ui
