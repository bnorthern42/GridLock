/**
 * @file LanguageExpressionHelper.hpp
 * @brief Source-language detection (Fortran vs C/C++), 1-based array slicing,
 *        Fortran GDB type/bounds parsing, and column-major / row-major indexing helpers.
 */

#pragma once

#include <cstdint>

#include <QFileInfo>
#include <QMetaType>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QVector>

namespace gridlock {

/**
 * @enum SourceLanguage
 * @brief Active stack-frame programming language resolved from GDB/MI and source file extensions.
 */
enum class SourceLanguage {
  Unknown = 0,  ///< Unresolved language; defaults to C/C++ 0-based row-major conventions.
  C = 1,        ///< C source frame (0-based, row-major).
  Cpp = 2,      ///< C++ source frame (0-based, row-major).
  Fortran = 3   ///< Fortran (`gfortran` / `ifx`) frame (1-based, column-major).
};

/**
 * @struct FortranArrayDimension
 * @brief Lower and upper inclusive bounds for a single Fortran array dimension.
 */
struct FortranArrayDimension {
  int lowerBound = 1;  ///< Inclusive lower bound (defaults to `1` in Fortran).
  int upperBound = 1;  ///< Inclusive upper bound.

  /** @brief Returns the number of elements along this dimension (`>= 0`). */
  int extent() const {
    return (upperBound >= lowerBound) ? (upperBound - lowerBound + 1) : 0;
  }
};

/**
 * @struct FortranArrayTypeInfo
 * @brief Parsed GDB Fortran array type string (e.g., `real(kind=8) (10,20)` or `integer(kind=4) (0:9,1:5)`).
 */
struct FortranArrayTypeInfo {
  bool isFortranArray = false;              ///< True when @p gdbTypeString matched a Fortran array declaration.
  QString elementType;                      ///< Base Fortran element type (e.g., `"real(kind=8)"`).
  QVector<FortranArrayDimension> dims;      ///< Ordered dimension bounds `(dim1, dim2, ...)`.

  /** @brief Returns the product of all dimension extents, or `0` if invalid/empty. */
  uint64_t totalElements() const {
    if (!isFortranArray || dims.isEmpty()) return 0u;
    uint64_t total = 1u;
    for (const auto& d : dims) {
      const int ext = d.extent();
      if (ext <= 0) return 0u;
      total *= static_cast<uint64_t>(ext);
    }
    return total;
  }
};

/** @brief Returns a compact human-readable badge label for @p lang. */
inline QString sourceLanguageLabel(SourceLanguage lang) {
  if (lang == SourceLanguage::Fortran) {
    return QStringLiteral("Fortran (1-based, column-major)");
  }
  return QStringLiteral("C/C++ (0-based, row-major)");
}

/** @brief Infers `SourceLanguage` from a source filename's extension (`.f90`, `.F90`, `.for`, `.c`, `.cpp`, etc.). */
inline SourceLanguage detectSourceLanguageFromFile(const QString& filePath) {
  const QString ext = QFileInfo(filePath.trimmed()).suffix().toLower();
  if (ext.isEmpty()) return SourceLanguage::Unknown;
  if (ext == QLatin1String("f") || ext == QLatin1String("for") || ext == QLatin1String("f77") ||
      ext == QLatin1String("f90") || ext == QLatin1String("f95") || ext == QLatin1String("f03") ||
      ext == QLatin1String("f08") || ext == QLatin1String("fpp") || ext == QLatin1String("ftn")) {
    return SourceLanguage::Fortran;
  }
  if (ext == QLatin1String("c")) return SourceLanguage::C;
  if (ext == QLatin1String("cpp") || ext == QLatin1String("cc") || ext == QLatin1String("cxx") ||
      ext == QLatin1String("hpp") || ext == QLatin1String("hh") || ext == QLatin1String("hxx") ||
      ext == QLatin1String("cu") || ext == QLatin1String("cuh")) {
    return SourceLanguage::Cpp;
  }
  return SourceLanguage::Unknown;
}

/**
 * @brief Resolves `SourceLanguage` from GDB/MI `-gdb-show language` output (`value="..."`),
 *        falling back to @p activeFilePath when GDB reports `"auto"`.
 */
inline SourceLanguage detectSourceLanguageFromGdbShow(const QString& gdbShowValue,
                                                      const QString& activeFilePath = QString()) {
  const QString lower = gdbShowValue.trimmed().toLower();
  if (lower.contains(QLatin1String("fortran"))) return SourceLanguage::Fortran;
  if (lower.contains(QLatin1String("c++"))) return SourceLanguage::Cpp;

  // Match explicit "c" or "currently c" without falsely matching "auto" or "objc".
  static const QRegularExpression kExactCRe(
      QStringLiteral(R"((?:^|currently\s+|")c(?:"|$|[\s.;,]))"));
  if (kExactCRe.match(lower).hasMatch()) return SourceLanguage::C;

  if (lower.isEmpty() || lower.contains(QLatin1String("auto")) || lower.contains(QLatin1String("unknown"))) {
    return detectSourceLanguageFromFile(activeFilePath);
  }
  return SourceLanguage::Unknown;
}

/**
 * @brief Parses GDB Fortran array type strings such as `real(kind=8) (10,20)`,
 *        `integer(kind=4) (0:9,1:5)`, `double precision (4, 8)`, or `real*8 (16)`.
 */
inline FortranArrayTypeInfo parseFortranArrayType(const QString& gdbTypeString) {
  FortranArrayTypeInfo info;
  const QString trimmed = gdbTypeString.trimmed();
  if (trimmed.isEmpty() || !trimmed.endsWith(QLatin1Char(')'))) return info;

  // Locate the matching '(' for the trailing dimension list `(...)`.
  int depth = 0;
  int openPos = -1;
  for (int i = trimmed.size() - 1; i >= 0; --i) {
    const QChar c = trimmed.at(i);
    if (c == QLatin1Char(')')) ++depth;
    else if (c == QLatin1Char('(')) {
      if (--depth == 0) {
        openPos = i;
        break;
      }
    }
  }
  if (openPos <= 0) return info;

  const QString elemPart = trimmed.left(openPos).trimmed();
  const QString dimsPart = trimmed.mid(openPos + 1, trimmed.size() - openPos - 2).trimmed();
  if (elemPart.isEmpty() || dimsPart.isEmpty()) return info;

  // Reject C/C++ function/member signatures and kind specifiers like `real(kind=8)` without trailing dims.
  if (dimsPart.contains(QLatin1Char('='))) return info;

  static const QRegularExpression kFortranBaseRe(
      QStringLiteral(R"(^(?:real|integer|complex|logical|character|double\s+precision|type\s*\([^)]+\))(?:\s*(?:\*\s*\d+|\([^)]*\)))?$)"),
      QRegularExpression::CaseInsensitiveOption);
  if (!kFortranBaseRe.match(elemPart).hasMatch()) return info;

  static const QRegularExpression kDimRangeRe(
      QStringLiteral(R"(^\s*(-?\d+)\s*:\s*(-?\d+)\s*$)"));
  static const QRegularExpression kDimUpperRe(QStringLiteral(R"(^\s*(\d+)\s*$)"));

  const QStringList tokens = dimsPart.split(QLatin1Char(','), Qt::SkipEmptyParts);
  if (tokens.isEmpty()) return info;

  QVector<FortranArrayDimension> parsedDims;
  parsedDims.reserve(tokens.size());
  for (const QString& rawTok : tokens) {
    const QString tok = rawTok.trimmed();
    if (const auto rm = kDimRangeRe.match(tok); rm.hasMatch()) {
      parsedDims.append(FortranArrayDimension{rm.captured(1).toInt(), rm.captured(2).toInt()});
    } else if (const auto um = kDimUpperRe.match(tok); um.hasMatch()) {
      const int ub = um.captured(1).toInt();
      if (ub <= 0) return info;
      parsedDims.append(FortranArrayDimension{1, ub});
    } else {
      return info;
    }
  }

  info.isFortranArray = true;
  info.elementType = elemPart;
  info.dims = std::move(parsedDims);
  return info;
}

/**
 * @brief Computes a generalized N-D Fortran column-major 0-based linear offset:
 *        `sum_d (coord_d * prod_{k < d} dim_k)`.
 */
inline uint64_t computeColumnMajorLinearIndex(const QVector<int>& zeroBasedCoords,
                                              const QVector<int>& dims) {
  if (zeroBasedCoords.size() != dims.size() || dims.isEmpty()) return 0u;
  uint64_t linear = 0u;
  uint64_t stride = 1u;
  for (int d = 0; d < dims.size(); ++d) {
    const uint64_t c = static_cast<uint64_t>(qMax(0, zeroBasedCoords.at(d)));
    const uint64_t ext = static_cast<uint64_t>(qMax(1, dims.at(d)));
    linear += c * stride;
    stride *= ext;
  }
  return linear;
}

/**
 * @brief Computes a generalized N-D C/C++ row-major 0-based linear offset:
 *        `sum_d (coord_d * prod_{k > d} dim_k)`.
 */
inline uint64_t computeRowMajorLinearIndex(const QVector<int>& zeroBasedCoords,
                                           const QVector<int>& dims) {
  if (zeroBasedCoords.size() != dims.size() || dims.isEmpty()) return 0u;
  uint64_t linear = 0u;
  uint64_t stride = 1u;
  for (int d = dims.size() - 1; d >= 0; --d) {
    const uint64_t c = static_cast<uint64_t>(qMax(0, zeroBasedCoords.at(d)));
    const uint64_t ext = static_cast<uint64_t>(qMax(1, dims.at(d)));
    linear += c * stride;
    stride *= ext;
  }
  return linear;
}

/**
 * @brief Formats a 1D array slice expression for GDB evaluation according to @p lang.
 *
 * - Fortran: 1-based (or custom @p lowerBound) inclusive slice `sym(start:end)` or `sym(start:end:stride)`.
 * - C / C++: 0-based `sym[offset]@count` when `stride == 1`, or `sym[offset:end:stride]`.
 */
inline QString formatArraySliceExpression(const QString& symbol,
                                          int zeroBasedOffset,
                                          int count,
                                          SourceLanguage lang,
                                          int stride = 1,
                                          int lowerBound = 1) {
  const QString sym = symbol.trimmed();
  const int safeOff = qMax(0, zeroBasedOffset);
  const int safeCnt = qMax(1, count);
  const int safeStr = qMax(1, stride);
  if (sym.isEmpty()) return QString();

  if (lang == SourceLanguage::Fortran) {
    const int start = lowerBound + safeOff;
    const int end = start + (safeCnt - 1) * safeStr;
    if (safeStr > 1) return QStringLiteral("%1(%2:%3:%4)").arg(sym).arg(start).arg(end).arg(safeStr);
    return QStringLiteral("%1(%2:%3)").arg(sym).arg(start).arg(end);
  }
  if (safeStr > 1) {
    const int endExcl = safeOff + safeCnt * safeStr;
    return QStringLiteral("%1[%2:%3:%4]").arg(sym).arg(safeOff).arg(endExcl).arg(safeStr);
  }
  return QStringLiteral("%1[%2]@%3").arg(sym).arg(safeOff).arg(safeCnt);
}

/**
 * @brief Formats a 2D array slice expression (`symbol(r1:r2, c1:c2)` in Fortran).
 */
inline QString format2DArraySliceExpression(const QString& symbol,
                                            int zeroBasedRowOffset,
                                            int rowCount,
                                            int zeroBasedColOffset,
                                            int colCount,
                                            SourceLanguage lang,
                                            int rowLowerBound = 1,
                                            int colLowerBound = 1) {
  const QString sym = symbol.trimmed();
  const int r0 = qMax(0, zeroBasedRowOffset);
  const int rCnt = qMax(1, rowCount);
  const int c0 = qMax(0, zeroBasedColOffset);
  const int cCnt = qMax(1, colCount);
  if (sym.isEmpty()) return QString();

  if (lang == SourceLanguage::Fortran) {
    const int r1 = rowLowerBound + r0;
    const int r2 = r1 + rCnt - 1;
    const int c1 = colLowerBound + c0;
    const int c2 = c1 + cCnt - 1;
    return QStringLiteral("%1(%2:%3, %4:%5)").arg(sym).arg(r1).arg(r2).arg(c1).arg(c2);
  }
  return QStringLiteral("%1[%2][%3]@%4").arg(sym).arg(r0).arg(c0).arg(cCnt);
}

/**
 * @brief Formats the base-address expression passed to `-data-read-memory-bytes`.
 *
 * Avoids C-style `&(arr)[0]` subscripting in Fortran frames where GDB expects `&(arr)`.
 */
inline QString formatArrayBaseAddressExpression(const QString& baseExpr, SourceLanguage lang) {
  const QString trimmed = baseExpr.trimmed();
  if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('&')) ||
      trimmed.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
    return trimmed;
  }
  if (lang == SourceLanguage::Fortran) {
    return QStringLiteral("&(%1)").arg(trimmed);
  }
  return QStringLiteral("&(%1)[0]").arg(trimmed);
}

/**
 * @brief Parses Fortran 1-based inclusive slice syntax `arr(start:end[:stride])` or
 *        2D `mat(r1:r2, c1:c2)` into 0-based internal offsets and element counts.
 */
inline bool parseFortranSliceSuffix(const QString& expression,
                                    QString* outBase,
                                    int* outOffset0,
                                    int* outCount,
                                    int* outStride,
                                    int* outRows = nullptr,
                                    int* outCols = nullptr) {
  const QString trimmed = expression.trimmed();
  if (trimmed.isEmpty() || !trimmed.endsWith(QLatin1Char(')'))) return false;
  const int openIdx = trimmed.lastIndexOf(QLatin1Char('('));
  if (openIdx <= 0) return false;
  const QString base = trimmed.left(openIdx).trimmed();
  const QString inside = trimmed.mid(openIdx + 1, trimmed.size() - openIdx - 2).trimmed();
  if (base.isEmpty() || !inside.contains(QLatin1Char(':'))) return false;

  static const QRegularExpression k1DRe(
      QStringLiteral(R"(^\s*(\d+)\s*:\s*(\d+)(?:\s*:\s*(\d+))?\s*$)"));
  if (const auto m = k1DRe.match(inside); m.hasMatch()) {
    const int start1 = qMax(1, m.captured(1).toInt());
    const int end1 = qMax(start1, m.captured(2).toInt());
    const int stride = m.captured(3).isEmpty() ? 1 : qMax(1, m.captured(3).toInt());
    if (outBase) *outBase = base;
    if (outOffset0) *outOffset0 = start1 - 1;
    if (outStride) *outStride = stride;
    if (outCount) *outCount = (end1 - start1) / stride + 1;
    if (outRows) *outRows = 0;
    if (outCols) *outCols = 0;
    return true;
  }

  static const QRegularExpression k2DRe(
      QStringLiteral(R"(^\s*(\d+)\s*:\s*(\d+)\s*,\s*(\d+)\s*:\s*(\d+)\s*$)"));
  if (const auto m2 = k2DRe.match(inside); m2.hasMatch()) {
    const int r1 = qMax(1, m2.captured(1).toInt());
    const int r2 = qMax(r1, m2.captured(2).toInt());
    const int c1 = qMax(1, m2.captured(3).toInt());
    const int c2 = qMax(c1, m2.captured(4).toInt());
    const int nRows = r2 - r1 + 1;
    const int nCols = c2 - c1 + 1;
    if (outBase) *outBase = base;
    if (outOffset0) *outOffset0 = 0;
    if (outStride) *outStride = 1;
    if (outCount) *outCount = nCols;
    if (outRows) *outRows = nRows;
    if (outCols) *outCols = nCols;
    return true;
  }
  return false;
}

}  // namespace gridlock

Q_DECLARE_METATYPE(gridlock::SourceLanguage)
