/**
 * @file TensorSliceTableModel.cpp
 * @brief Implementation of TensorSliceTableModel for zero-copy 2D/3D tensor inspection.
 */

#include "TensorSliceTableModel.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QBrush>
#include <QColor>
#include <QFont>

namespace gridlock::ui {

TensorSliceTableModel::TensorSliceTableModel(QObject* parent)
    : QAbstractTableModel(parent) {}

void TensorSliceTableModel::setSourceData(const QVector<double>* samples,
                                          const QVector<QPair<double, double>>* complexPairs) {
    beginResetModel();
    m_samples = samples;
    m_complexPairs = complexPairs;
    endResetModel();
}

void TensorSliceTableModel::setShape(int nx, int ny, int nz) {
    const int safeNx = qMax(0, nx);
    const int safeNy = qMax(0, ny);
    const int safeNz = qMax(1, nz);
    const int clampedZ = std::clamp(m_zSlice, 0, safeNz - 1);
    if (m_nx == safeNx && m_ny == safeNy && m_nz == safeNz && m_zSlice == clampedZ) return;

    beginResetModel();
    m_nx = safeNx;
    m_ny = safeNy;
    m_nz = safeNz;
    m_zSlice = clampedZ;
    endResetModel();
}

void TensorSliceTableModel::setZSlice(int zSlice) {
    const int clamped = std::clamp(zSlice, 0, qMax(0, m_nz - 1));
    if (m_zSlice == clamped) return;
    m_zSlice = clamped;
    notifyDataRefreshed();
}

void TensorSliceTableModel::setMemoryOrder(MemoryOrder order) {
    if (m_memoryOrder == order) return;
    m_memoryOrder = order;
    notifyDataRefreshed();
}

qsizetype TensorSliceTableModel::computeLinearIndex(int x, int y, int z, int nx, int ny,
                                                    MemoryOrder order) {
    const qsizetype lx = qMax(0, x);
    const qsizetype ly = qMax(0, y);
    const qsizetype lz = qMax(0, z);
    const qsizetype lnx = qMax(1, nx);
    const qsizetype lny = qMax(1, ny);
    if (order == MemoryOrder::FortranColMajor) {
        return (lz * lnx + lx) * lny + ly;
    }
    return (lz * lny + ly) * lnx + lx;
}

qsizetype TensorSliceTableModel::linearIndexForCell(int row, int col) const {
    return computeLinearIndex(col, row, m_zSlice, m_nx, m_ny, m_memoryOrder);
}

double TensorSliceTableModel::cellValue(int row, int col) const {
    if (!m_samples || row < 0 || row >= m_ny || col < 0 || col >= m_nx) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const qsizetype idx = linearIndexForCell(row, col);
    if (idx < 0 || idx >= m_samples->size()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return m_samples->at(idx);
}

void TensorSliceTableModel::notifyDataRefreshed() {
    if (m_ny <= 0 || m_nx <= 0) return;
    emit dataChanged(index(0, 0), index(m_ny - 1, m_nx - 1));
}

int TensorSliceTableModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return m_ny;
}

int TensorSliceTableModel::columnCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return m_nx;
}

QVariant TensorSliceTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role != Qt::DisplayRole) return {};
    if (orientation == Qt::Horizontal) {
        return QStringLiteral("[%1]").arg(section);
    }
    return QStringLiteral("[%1]").arg(section);
}

QVariant TensorSliceTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || !m_samples) return {};
    const int r = index.row();
    const int c = index.column();
    if (r < 0 || r >= m_ny || c < 0 || c >= m_nx) return {};

    const qsizetype linIdx = linearIndexForCell(r, c);
    const bool inBounds = (linIdx >= 0 && linIdx < m_samples->size());
    if (!inBounds) {
        if (role == Qt::DisplayRole) return QStringLiteral("-");
        if (role == Qt::ToolTipRole) {
            return QStringLiteral("[%1] (z=%2, y=%3, x=%4): out of buffer bounds")
                .arg(linIdx).arg(m_zSlice).arg(r).arg(c);
        }
        return {};
    }

    const double val = m_samples->at(linIdx);
    const bool isAnom = std::isnan(val) || std::isinf(val);

    switch (role) {
        case Qt::DisplayRole:
            return QString::number(val, 'g', 7);
        case Qt::EditRole:
        case Qt::UserRole:
            return val;
        case Qt::TextAlignmentRole:
            return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
        case Qt::ForegroundRole:
            if (isAnom) return QBrush(QColor(QStringLiteral("#ffffff")));
            return {};
        case Qt::BackgroundRole:
            if (isAnom) return QBrush(QColor(QStringLiteral("#be185d")));
            return {};
        case Qt::FontRole:
            if (isAnom) {
                QFont f(QStringLiteral("JetBrains Mono"), 10);
                f.setBold(true);
                return f;
            }
            return {};
        case Qt::ToolTipRole: {
            const QString orderTag = (m_memoryOrder == MemoryOrder::FortranColMajor)
                                         ? QStringLiteral("Fortran Col-Major")
                                         : QStringLiteral("C Row-Major");
            QString tip = QStringLiteral("[%1] (z=%2, y=%3, x=%4, %5) = %6")
                              .arg(linIdx)
                              .arg(m_zSlice)
                              .arg(r)
                              .arg(c)
                              .arg(orderTag, QString::number(val, 'g', 12));
            if (m_complexPairs && linIdx < m_complexPairs->size()) {
                const auto [re, im] = m_complexPairs->at(linIdx);
                tip += QStringLiteral("\nComplex: Re = %1, Im = %2")
                           .arg(QString::number(re, 'g', 10), QString::number(im, 'g', 10));
            }
            return tip;
        }
        default:
            return {};
    }
}

}  // namespace gridlock::ui
