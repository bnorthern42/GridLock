/**
 * @file ArrayPlotExport.cpp
 * @brief Binary buffer decoding, complex/scalar statistics, and CSV/PNG/Gnuplot export for ArrayPlotDialog.
 *
 * Implements typed contiguous/strided memory decoders (Float64, Float32, Int32, Int64,
 * UInt8, Complex64, Complex128), scalar expression parsing, statistical aggregation
 * (min/max/mean/L2/NaN/Inf/mutation diffing), and CSV/PNG/Gnuplot export routines.
 */

#include "ArrayPlotDialog.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <utility>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMessageBox>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>

#include "../../../core/managers/ThemeManager.hpp"

namespace gridlock::ui {

bool ArrayPlotDialog::isComplexType(ElementType type) {
    return type == ElementType::Complex64 || type == ElementType::Complex128;
}

int ArrayPlotDialog::byteSizeOf(ElementType type) {
    switch (type) {
        case ElementType::Float64:    return 8;
        case ElementType::Float32:    return 4;
        case ElementType::Int32:      return 4;
        case ElementType::Int64:      return 8;
        case ElementType::UInt8:      return 1;
        case ElementType::Complex64:  return 8;
        case ElementType::Complex128: return 16;
    }
    return 8;
}

double ArrayPlotDialog::extractComplexComponent(double re, double im, ComplexComponent comp) {
    if (std::isnan(re) || std::isnan(im)) return std::numeric_limits<double>::quiet_NaN();
    if (std::isinf(re) || std::isinf(im)) return std::isinf(re) ? re : im;
    switch (comp) {
        case ComplexComponent::Magnitude: return std::hypot(re, im);
        case ComplexComponent::Phase:     return std::atan2(im == 0.0 ? 0.0 : im, re);
        case ComplexComponent::Real:      return re;
        case ComplexComponent::Imaginary: return im;
    }
    return std::hypot(re, im);
}

QVector<double> ArrayPlotDialog::decodeStridedBytes(
    const QByteArray& rawBytes, ElementType type, ComplexComponent comp,
    int maxCount, int offset, int stride, QVector<QPair<double, double>>* outComplexPairs) {
    if (outComplexPairs) outComplexPairs->clear();
    const int elemSize = byteSizeOf(type);
    const int totalAvailable = static_cast<int>(rawBytes.size()) / elemSize;
    const int safeOffset = qMax(0, offset);
    const int safeStride = qMax(1, stride);
    if (totalAvailable <= safeOffset) return {};

    const int stridedAvailable = (totalAvailable - safeOffset - 1) / safeStride + 1;
    const int count = (maxCount > 0) ? qMin(stridedAvailable, maxCount) : stridedAvailable;
    if (count <= 0) return {};

    auto readAt = [](const char* src, auto tag) -> double {
        using T = decltype(tag);
        T v{};
        std::memcpy(&v, src, sizeof(v));
        return static_cast<double>(v);
    };

    QVector<double> decoded(count);
    if (outComplexPairs && isComplexType(type)) outComplexPairs->resize(count);
    const char* ptr = rawBytes.constData();

    for (int i = 0; i < count; ++i) {
        const char* src = ptr + static_cast<qsizetype>(safeOffset + i * safeStride) * elemSize;
        switch (type) {
            case ElementType::Float64: decoded[i] = readAt(src, double{}); break;
            case ElementType::Float32: decoded[i] = readAt(src, float{}); break;
            case ElementType::Int32:   decoded[i] = readAt(src, int32_t{}); break;
            case ElementType::Int64:   decoded[i] = readAt(src, int64_t{}); break;
            case ElementType::UInt8:   decoded[i] = readAt(src, uint8_t{}); break;
            case ElementType::Complex64: {
                const double re = readAt(src, float{});
                const double im = readAt(src + sizeof(float), float{});
                if (outComplexPairs) (*outComplexPairs)[i] = {re, im};
                decoded[i] = extractComplexComponent(re, im, comp);
                break;
            }
            case ElementType::Complex128: {
                const double re = readAt(src, double{});
                const double im = readAt(src + sizeof(double), double{});
                if (outComplexPairs) (*outComplexPairs)[i] = {re, im};
                decoded[i] = extractComplexComponent(re, im, comp);
                break;
            }
        }
    }
    return decoded;
}

bool ArrayPlotDialog::parseScalarResult(const QString& resultText, double* outVal) {
    if (!outVal) return false;
    QString s = resultText.trimmed();
    if (s.isEmpty()) return false;
    if (const int eq = s.lastIndexOf(QLatin1Char('=')); eq >= 0) s = s.mid(eq + 1).trimmed();
    static const QRegularExpression kCastRe(QStringLiteral(R"(^\([^)]*\)\s*)"));
    s.remove(kCastRe);
    s = s.trimmed();

    const QString lower = s.toLower();
    if (lower == QStringLiteral("nan") || lower == QStringLiteral("-nan")) {
        *outVal = std::numeric_limits<double>::quiet_NaN();
        return true;
    }
    if (lower == QStringLiteral("inf") || lower == QStringLiteral("+inf") || lower == QStringLiteral("infinity")) {
        *outVal = std::numeric_limits<double>::infinity();
        return true;
    }
    if (lower == QStringLiteral("-inf") || lower == QStringLiteral("-infinity")) {
        *outVal = -std::numeric_limits<double>::infinity();
        return true;
    }
    if (lower.startsWith(QStringLiteral("0x"))) {
        bool ok = false;
        const quint64 hv = lower.mid(2).section(QLatin1Char(' '), 0, 0).toULongLong(&ok, 16);
        if (ok) { *outVal = static_cast<double>(hv); return true; }
    }
    bool ok = false;
    if (const double direct = s.toDouble(&ok); ok) { *outVal = direct; return true; }
    static const QRegularExpression kFloatRe(QStringLiteral(R"([-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?)"));
    if (const QRegularExpressionMatch m = kFloatRe.match(s); m.hasMatch()) {
        if (const double v = m.captured(0).toDouble(&ok); ok) { *outVal = v; return true; }
    }
    return false;
}

void ArrayPlotDialog::recomputeComplexProjection() {
    if (m_complexPairs.isEmpty()) return;
    const ComplexComponent comp = complexComponent();
    m_currentSamples.resize(m_complexPairs.size());
    for (int i = 0; i < m_complexPairs.size(); ++i) {
        m_currentSamples[i] = extractComplexComponent(m_complexPairs.at(i).first, m_complexPairs.at(i).second, comp);
    }
    recomputeStats();
    if (m_tensorModel) m_tensorModel->notifyDataRefreshed();
    if (m_canvas) m_canvas->update();
}

bool ArrayPlotDialog::loadFromRawBytes(const QByteArray& rawBytes, const QString& typeName,
                                       int rows, int cols, int offset, int stride) {
    setElementTypeFromString(typeName);
    return loadFromRawBytes(rawBytes, elementType(), rows, cols, offset, stride);
}

bool ArrayPlotDialog::loadFromRawBytes(const QByteArray& rawBytes, ElementType type,
                                       int rows, int cols, int offset, int stride) {
    setElementType(type);
    const int effOffset = (offset >= 0) ? offset : m_sliceOffset;
    const int effStride = (stride > 0) ? stride : m_sliceStride;
    if (offset >= 0) setSliceOffset(effOffset);
    if (stride > 0) setSliceStride(effStride);

    const int effRows = qMax(1, rows);
    int requestedCount = 0;
    if (cols > 0) {
        requestedCount = qMax(1, m_depth) * effRows * cols;
    } else if (m_hasExplicitSliceCount && effRows == 1 && m_depth == 1) {
        requestedCount = qMax(1, m_cols);
    }

    QVector<QPair<double, double>> pairs;
    const QVector<double> decoded = decodeStridedBytes(
        rawBytes, type, complexComponent(), requestedCount, effOffset, effStride,
        isComplexType(type) ? &pairs : nullptr);
    if (decoded.isEmpty()) return false;

    const int effCols = (cols > 0) ? cols : qMax(1, static_cast<int>(decoded.size()) / (effRows * qMax(1, m_depth)));
    setSamples(decoded, effRows, effCols);
    m_complexPairs = std::move(pairs);
    if (m_tensorModel) {
        m_tensorModel->setSourceData(&m_currentSamples, m_complexPairs.isEmpty() ? nullptr : &m_complexPairs);
    }
    return true;
}

void ArrayPlotDialog::setSamples(const QVector<double>& samples, int rows, int cols) {
    if (!m_currentSamples.isEmpty()) m_previousSamples = m_currentSamples;
    m_currentSamples = samples;
    m_complexPairs.clear();
    const int effRows = qMax(1, rows);
    const int effCols = (cols > 0) ? cols : qMax(1, static_cast<int>(samples.size()) / (effRows * qMax(1, m_depth)));
    setDimensions(effRows, effCols);
    if (m_tensorModel) {
        m_tensorModel->setSourceData(&m_currentSamples, nullptr);
        m_tensorModel->setShape(m_cols, m_rows, m_depth);
    }
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

void ArrayPlotDialog::appendScalarSample(double value, int rankId) {
    const int targetRank = (rankId >= 0) ? rankId : m_focusedRank;
    if (targetRank == m_focusedRank) m_scalarHistory.append(value);
    if (isOverlayRanksEnabled() || targetRank != m_focusedRank || !m_rankScalarHistories.isEmpty()) {
        m_rankScalarHistories[targetRank].append(value);
        if (plotMode() == PlotMode::ScalarHistory) m_rankOverlays[targetRank] = m_rankScalarHistories[targetRank];
    }
    if (plotMode() == PlotMode::ScalarHistory) {
        m_previousSamples.clear();
        m_complexPairs.clear();
        m_currentSamples = m_scalarHistory;
        m_rows = 1;
        m_cols = qMax(1, static_cast<int>(m_scalarHistory.size()));
        if (m_tensorModel) {
            m_tensorModel->setSourceData(&m_currentSamples, nullptr);
            m_tensorModel->setShape(m_cols, m_rows, 1);
        }
        recomputeStats();
        if (m_canvas) m_canvas->update();
    }
}

void ArrayPlotDialog::clearScalarHistory() {
    const bool hadScalar = !m_scalarHistory.isEmpty() || !m_rankScalarHistories.isEmpty();
    if (hadScalar || !m_currentSamples.isEmpty()) {
        m_undoSnapshot = {m_scalarHistory, m_currentSamples, m_previousSamples, m_complexPairs,
                          m_rankScalarHistories, m_rankOverlays, m_rows, m_cols, m_depth};
        m_hasUndoSnapshot = true;
        if (m_undoClearBtn) m_undoClearBtn->setEnabled(true);
    }
    m_scalarHistory.clear();
    m_rankScalarHistories.clear();
    if (plotMode() == PlotMode::ScalarHistory || !hadScalar) {
        m_currentSamples.clear();
        m_previousSamples.clear();
        m_complexPairs.clear();
        m_rankOverlays.clear();
        if (m_tensorModel) m_tensorModel->notifyDataRefreshed();
        recomputeStats();
    }
    if (m_canvas) { m_canvas->resetZoom(); m_canvas->update(); }
}

bool ArrayPlotDialog::undoClearHistory() {
    if (!m_hasUndoSnapshot) return false;
    const PlotUndoSnapshot snap = std::move(m_undoSnapshot);
    m_hasUndoSnapshot = false;
    if (m_undoClearBtn) m_undoClearBtn->setEnabled(false);
    m_scalarHistory = snap.scalarHistory; m_rankScalarHistories = snap.rankScalarHistories;
    m_currentSamples = snap.currentSamples; m_previousSamples = snap.previousSamples;
    m_complexPairs = snap.complexPairs; m_rankOverlays = snap.rankOverlays;
    m_rows = snap.rows; m_cols = snap.cols; m_depth = snap.depth;
    if (m_tensorModel) {
        m_tensorModel->setSourceData(&m_currentSamples, m_complexPairs.isEmpty() ? nullptr : &m_complexPairs);
        m_tensorModel->setShape(m_cols, m_rows, m_depth);
    }
    recomputeStats();
    if (m_canvas) m_canvas->update();
    return true;
}

void ArrayPlotDialog::recomputeStats() {
    m_minVal = m_maxVal = m_meanVal = m_l2Norm = 0.0;
    m_nanCount = m_infCount = m_changedElementCount = 0;
    double sum = 0.0, sumSq = 0.0;
    int finiteCount = 0;

    for (int i = 0; i < m_currentSamples.size(); ++i) {
        const double v = m_currentSamples.at(i);
        if (std::isnan(v)) ++m_nanCount;
        else if (std::isinf(v)) ++m_infCount;
        else {
            if (finiteCount == 0 || v < m_minVal) m_minVal = v;
            if (finiteCount == 0 || v > m_maxVal) m_maxVal = v;
            sum += v; sumSq += v * v; ++finiteCount;
        }
        if (!m_previousSamples.isEmpty() && i < m_previousSamples.size()) {
            const double pv = m_previousSamples.at(i);
            if ((std::isnan(v) != std::isnan(pv)) || (std::isinf(v) != std::isinf(pv)) ||
                (!std::isnan(v) && !std::isnan(pv) && std::abs(v - pv) > 1e-12)) ++m_changedElementCount;
        }
    }
    if (finiteCount > 0) { m_meanVal = sum / finiteCount; m_l2Norm = std::sqrt(sumSq); }
    if (!m_statsLabel) return;

    if (plotMode() == PlotMode::ScalarHistory) {
        m_statsLabel->setText(
            QStringLiteral("Stops: %1 | Min: %2 | Max: %3 | Mean: %4 | Latest: %5 | NaN/Inf: %6")
                .arg(m_scalarHistory.size())
                .arg(QString::number(m_minVal, 'g', 6), QString::number(m_maxVal, 'g', 6),
                     QString::number(m_meanVal, 'g', 6),
                     m_scalarHistory.isEmpty() ? QStringLiteral("-") : QString::number(m_scalarHistory.constLast(), 'g', 6))
                .arg(m_nanCount + m_infCount));
    } else {
        m_statsLabel->setText(
            QStringLiteral("Min: %1 | Max: %2 | Mean: %3 | L2: %4 | NaN/Inf: %5 (%6 NaN, %7 Inf) | Changed: %8")
                .arg(QString::number(m_minVal, 'g', 6), QString::number(m_maxVal, 'g', 6),
                     QString::number(m_meanVal, 'g', 6), QString::number(m_l2Norm, 'g', 6))
                .arg(m_nanCount + m_infCount).arg(m_nanCount).arg(m_infCount).arg(m_changedElementCount));
    }
}

QString ArrayPlotDialog::buildGnuplotScript(const QString& dataPath) const {
    QString safeTitle = expression(); safeTitle.replace(QLatin1Char('\''), QLatin1Char(' '));
    QString script; QTextStream sout(&script);
    if (m_rows > 1) {
        sout << "set title 'GridLock 2D Field: " << safeTitle << " (Rank " << m_focusedRank << ")'\nset xlabel 'Column (x)'\nset ylabel 'Row (y)'\n";
        if (isLogScale()) sout << "set logscale zcb\n";
        sout << "set pm3d at bs\nset contour base\nset mouse\nsplot '" << dataPath << "' matrix with pm3d title 'Rank " << m_focusedRank << "'\n";
    } else {
        sout << "set grid\n"; if (isLogScale()) sout << "set logscale y\n";
        sout << "set title 'GridLock Array Plot: " << safeTitle << "'\nplot '" << dataPath << "' using 1:2 with linespoints title 'Rank " << m_focusedRank << "'\n";
    }
    return script;
}

bool ArrayPlotDialog::exportCsv(const QString& filePath) const {
    if (filePath.trimmed().isEmpty() || m_currentSamples.isEmpty()) return false;
    QFile file(filePath); if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream out(&file);
    if (plotMode() == PlotMode::ScalarHistory) {
        out << "stop,value\n";
        for (int i = 0; i < m_currentSamples.size(); ++i) out << i << "," << QString::number(m_currentSamples.at(i), 'g', 12) << "\n";
    } else if (m_rows > 1) {
        out << "row,col,value\n";
        for (int r = 0; r < m_rows; ++r) {
            for (int c = 0; c < m_cols; ++c) {
                out << r << "," << c << "," << QString::number(m_tensorModel ? m_tensorModel->cellValue(r, c) : m_currentSamples.value(r * m_cols + c), 'g', 12) << "\n";
            }
        }
    } else {
        out << "index,value,prev_value,delta\n";
        for (int i = 0; i < m_currentSamples.size(); ++i) {
            const double cur = m_currentSamples.at(i), prev = (i < m_previousSamples.size()) ? m_previousSamples.at(i) : cur;
            out << (m_sliceOffset + i * m_sliceStride) << "," << QString::number(cur, 'g', 12) << "," << QString::number(prev, 'g', 12) << "," << QString::number(cur - prev, 'g', 12) << "\n";
        }
    }
    return true;
}

bool ArrayPlotDialog::savePng(const QString& filePath) {
    return m_canvas && !filePath.trimmed().isEmpty() && m_canvas->grab().save(filePath, "PNG");
}

QString ArrayPlotDialog::resolveGnuplotExecutable(const QString& overridePath) {
    const QString custom = overridePath.trimmed();
    if (!custom.isEmpty()) return (QFileInfo::exists(custom) && QFileInfo(custom).isExecutable()) ? custom : QStandardPaths::findExecutable(custom);
    return QStandardPaths::findExecutable(QStringLiteral("gnuplot"));
}

bool ArrayPlotDialog::openInGnuplot(bool showMessageBox) {
    const bool allowModal = showMessageBox && QGuiApplication::platformName() != QLatin1String("offscreen");
    auto report = [&](const QString& status, const QString& title, const QString& detail, bool isInfo) {
        setHoverSummaryText(status);
        if (allowModal) { if (isInfo) QMessageBox::information(this, title, detail); else QMessageBox::warning(this, title, detail); }
    };
    if (m_currentSamples.isEmpty()) { report(QStringLiteral("Cannot open Gnuplot: no plotted samples available."), tr("Open in Gnuplot"), tr("Plot an array expression first before opening in Gnuplot."), true); return false; }
    const QString bin = resolveGnuplotExecutable(m_gnuplotExecutableOverride);
    if (bin.isEmpty()) { report(QStringLiteral("Gnuplot executable not found in PATH."), tr("Gnuplot Not Found"), tr("Could not find 'gnuplot' on PATH.\n\nInstall it with:\n  sudo pacman -S gnuplot\n  sudo dnf install gnuplot\n  sudo apt install gnuplot\n\nOr export data using Export CSV."), false); return false; }
    const QString dataPath = QDir::temp().filePath(QStringLiteral("gridlock_array_plot.dat"));
    const QString scriptPath = QDir::temp().filePath(QStringLiteral("gridlock_array_plot.gp"));
    QFile df(dataPath);
    if (!df.open(QIODevice::WriteOnly | QIODevice::Text)) { report(QStringLiteral("Failed to write Gnuplot data file."), tr("Gnuplot Error"), tr("Could not write temporary data file: %1").arg(dataPath), false); return false; }
    QTextStream dout(&df);
    if (m_rows > 1) {
        for (int r = 0; r < m_rows; ++r) {
            for (int c = 0; c < m_cols; ++c) {
                if (c > 0) dout << " ";
                dout << QString::number(m_tensorModel ? m_tensorModel->cellValue(r, c) : m_currentSamples.value(r * m_cols + c), 'g', 12);
            }
            dout << "\n";
        }
    } else {
        for (int i = 0; i < m_currentSamples.size(); ++i) dout << ((plotMode() == PlotMode::ScalarHistory) ? i : (m_sliceOffset + i * m_sliceStride)) << " " << QString::number(m_currentSamples.at(i), 'g', 12) << "\n";
    }
    df.close();
    QFile sf(scriptPath);
    if (!sf.open(QIODevice::WriteOnly | QIODevice::Text)) { report(QStringLiteral("Failed to write Gnuplot script."), tr("Gnuplot Error"), tr("Could not write temporary Gnuplot script: %1").arg(scriptPath), false); return false; }
    QTextStream(&sf) << buildGnuplotScript(dataPath); sf.close();
    auto* proc = new QProcess(qApp);
    QPointer<ArrayPlotDialog> self(this);
    auto elapsed = std::make_shared<QElapsedTimer>(); elapsed->start();
    connect(proc, &QProcess::errorOccurred, proc, [proc, self, allowModal](QProcess::ProcessError) {
        const QString err = QString::fromUtf8(proc->readAllStandardError()).trimmed();
        const QString msg = QStringLiteral("Gnuplot launch failed: %1").arg(err.isEmpty() ? proc->errorString() : err);
        if (self) { self->setHoverSummaryText(msg); if (allowModal) QMessageBox::warning(self, QObject::tr("Gnuplot Launch Failed"), msg); }
        proc->deleteLater();
    });
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), proc, [proc, self, elapsed, allowModal](int code, QProcess::ExitStatus st) {
        if ((st != QProcess::NormalExit || code != 0) && elapsed->elapsed() <= 1500) {
            const QString err = QString::fromUtf8(proc->readAllStandardError()).trimmed();
            const QString msg = QStringLiteral("Gnuplot exited (%1): %2").arg(code).arg(err.isEmpty() ? QStringLiteral("unknown error") : err);
            if (self) { self->setHoverSummaryText(msg); if (allowModal) QMessageBox::warning(self, QObject::tr("Gnuplot Error"), msg); }
        }
        proc->deleteLater();
    });
    proc->start(bin, {QStringLiteral("-persist"), scriptPath});
    setHoverSummaryText(QStringLiteral("Launched Gnuplot (%1)").arg(QFileInfo(bin).fileName()));
    return true;
}

void ArrayPlotDialog::setHoverSummaryText(const QString& text) { if (m_hoverLabel) m_hoverLabel->setText(text); }

void ArrayPlotDialog::reloadStyle() {
    const auto pal = gridlock::core::ThemeManager::paletteForTheme(gridlock::core::ThemeManager::instance().activeThemeId());
    const QString bg = pal.isDark ? QStringLiteral("#141821") : QStringLiteral("#f8fafc"), inputBg = pal.isDark ? QStringLiteral("#0d1117") : QStringLiteral("#ffffff"), btnBg = pal.isDark ? QStringLiteral("#252b38") : QStringLiteral("#e2e8f0"), muted = pal.isDark ? QStringLiteral("#94a3b8") : QStringLiteral("#475569");
    setStyleSheet(QStringLiteral("QDialog#ArrayPlotDialog { background: %1; color: %2; } QLineEdit, QComboBox, QSpinBox, QTableView { background: %3; color: %2; border: 1px solid %4; border-radius: 3px; padding: 2px 6px; font-family: 'JetBrains Mono', monospace; font-size: 11px; } QHeaderView::section { background: %5; color: %2; border: 1px solid %4; padding: 2px 4px; font-family: 'JetBrains Mono', monospace; font-size: 10px; } QPushButton { background: %5; color: %2; border: 1px solid %4; border-radius: 3px; padding: 2px 8px; font-size: 11px; } QPushButton:checked { border: 1px solid %6; color: %6; font-weight: bold; } QPushButton:hover { border: 1px solid %6; } QPushButton:disabled { color: %7; } QLabel#arrayPlotStatsLabel { color: %6; font-family: 'JetBrains Mono', monospace; font-size: 10px; font-weight: bold; } QLabel#arrayPlotHoverLabel { color: %7; font-family: 'JetBrains Mono', monospace; font-size: 10px; }").arg(bg, pal.textHex, inputBg, pal.borderHex, btnBg, pal.accentHex, muted));
    if (m_canvas) m_canvas->update();
}

void ArrayPlotDialog::setCompletionCandidates(const QStringList& candidates) {
    QStringList deduped; QSet<QString> seen;
    auto push = [&](const QString& raw) { const QString s = raw.trimmed(); if (!s.isEmpty() && !seen.contains(s)) { seen.insert(s); deduped.append(s); } };
    for (const QString& c : candidates) push(c);
    if (m_exprEdit && !m_exprEdit->text().trimmed().isEmpty()) push(m_exprEdit->text().trimmed());
    m_completionCandidates = deduped;
    if (m_completerModel) m_completerModel->setStringList(m_completionCandidates);
}

}  // namespace gridlock::ui
