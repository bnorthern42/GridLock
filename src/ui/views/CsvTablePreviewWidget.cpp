/**
 * @file CsvTablePreviewWidget.cpp
 * @brief Implementation of CsvVirtualTableModel and CsvTablePreviewWidget.
 */

#include "CsvTablePreviewWidget.hpp"

#include <cstring>

#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QTableView>
#include <QVBoxLayout>

namespace gridlock::ui {

CsvVirtualTableModel::CsvVirtualTableModel(QObject* parent) : QAbstractTableModel(parent) {}

CsvVirtualTableModel::~CsvVirtualTableModel() { closeFile(); }

void CsvVirtualTableModel::closeFile() {
    if (m_mapped && m_file.isOpen()) {
        m_file.unmap(const_cast<uchar*>(m_mapped));
    }
    m_mapped = nullptr;
    if (m_file.isOpen()) m_file.close();
    m_fileSize = 0;
    m_lineOffsets.clear();
    m_headers.clear();
    m_columnCount = 0;
    m_cachedRow = -1;
    m_cachedFields.clear();
}

bool CsvVirtualTableModel::loadFile(const QString& filePath, QChar delimiter) {
    beginResetModel();
    closeFile();
    m_delimiter = delimiter;

    m_file.setFileName(filePath);
    if (!m_file.open(QIODevice::ReadOnly)) {
        endResetModel();
        return false;
    }

    m_fileSize = m_file.size();
    if (m_fileSize <= 0) {
        endResetModel();
        return true;
    }

    m_mapped = m_file.map(0, m_fileSize);
    m_lineOffsets.reserve(static_cast<qsizetype>(qMin<qint64>(m_fileSize / 32 + 64, 2000000)));
    m_lineOffsets.append(0);

    if (m_mapped) {
        const char* base = reinterpret_cast<const char*>(m_mapped);
        qint64 pos = 0;
        while (pos < m_fileSize) {
            const void* found = std::memchr(base + pos, '\n', static_cast<size_t>(m_fileSize - pos));
            if (!found) break;
            const qint64 nextOffset = static_cast<const char*>(found) - base + 1;
            if (nextOffset < m_fileSize) m_lineOffsets.append(nextOffset);
            pos = nextOffset;
        }
    } else {
        constexpr qint64 kChunk = 256 * 1024;
        QByteArray buf;
        buf.resize(static_cast<qsizetype>(kChunk));
        qint64 offset = 0;
        while (offset < m_fileSize) {
            const qint64 n = m_file.read(buf.data(), kChunk);
            if (n <= 0) break;
            qint64 local = 0;
            while (local < n) {
                const void* found = std::memchr(buf.constData() + local, '\n', static_cast<size_t>(n - local));
                if (!found) break;
                const qint64 nextLocal = static_cast<const char*>(found) - buf.constData() + 1;
                if (offset + nextLocal < m_fileSize) m_lineOffsets.append(offset + nextLocal);
                local = nextLocal;
            }
            offset += n;
        }
    }

    // Parse only line 0 for column headers.
    m_headers = parseLine(readLineBytes(0), m_delimiter);
    m_columnCount = qMax(1, static_cast<int>(m_headers.size()));
    endResetModel();
    return true;
}

QByteArray CsvVirtualTableModel::readLineBytes(int lineIndex) const {
    if (lineIndex < 0 || lineIndex >= m_lineOffsets.size()) return {};
    const qint64 start = m_lineOffsets[lineIndex];
    const qint64 end = (lineIndex + 1 < m_lineOffsets.size()) ? m_lineOffsets[lineIndex + 1] : m_fileSize;
    qint64 len = end - start;
    if (len <= 0) return {};

    QByteArray raw;
    if (m_mapped) {
        const char* ptr = reinterpret_cast<const char*>(m_mapped) + start;
        while (len > 0 && (ptr[len - 1] == '\n' || ptr[len - 1] == '\r')) --len;
        raw = QByteArray(ptr, static_cast<qsizetype>(len));
    } else if (m_file.isOpen()) {
        if (!m_file.seek(start)) return {};
        raw = m_file.read(len);
        while (!raw.isEmpty() && (raw.endsWith('\n') || raw.endsWith('\r'))) raw.chop(1);
    }
    return raw;
}

QStringList CsvVirtualTableModel::parseLine(const QByteArray& rawLine, QChar delimiter) {
    QStringList fields;
    const QString line = QString::fromUtf8(rawLine);
    const qsizetype n = line.size();
    QString cur;
    cur.reserve(32);
    bool inQuotes = false;

    for (qsizetype i = 0; i < n; ++i) {
        const QChar c = line.at(i);
        if (inQuotes) {
            if (c == QLatin1Char('"')) {
                if (i + 1 < n && line.at(i + 1) == QLatin1Char('"')) {
                    cur.append(QLatin1Char('"'));
                    ++i;
                } else {
                    inQuotes = false;
                }
            } else {
                cur.append(c);
            }
        } else if (c == QLatin1Char('"') && cur.isEmpty()) {
            inQuotes = true;
        } else if (c == delimiter) {
            fields.append(cur);
            cur.clear();
        } else {
            cur.append(c);
        }
    }
    fields.append(cur);
    return fields;
}

int CsvVirtualTableModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid() || m_lineOffsets.size() <= 1) return 0;
    return static_cast<int>(m_lineOffsets.size() - 1);
}

int CsvVirtualTableModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : m_columnCount;
}

QVariant CsvVirtualTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || (role != Qt::DisplayRole && role != Qt::ToolTipRole)) return {};
    const int row = index.row();
    const int col = index.column();
    if (row < 0 || row >= rowCount() || col < 0 || col >= m_columnCount) return {};

    if (m_cachedRow != row) {
        m_cachedFields = parseLine(readLineBytes(row + 1), m_delimiter);
        m_cachedRow = row;
    }
    return (col < m_cachedFields.size()) ? m_cachedFields.at(col) : QString();
}

QVariant CsvVirtualTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role != Qt::DisplayRole) return {};
    if (orientation == Qt::Horizontal) {
        if (section >= 0 && section < m_headers.size() && !m_headers.at(section).isEmpty()) {
            return m_headers.at(section);
        }
        return QStringLiteral("Col %1").arg(section + 1);
    }
    return section + 1;
}

CsvTablePreviewWidget::CsvTablePreviewWidget(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("CsvTablePreviewWidget"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* bannerBar = new QWidget(this);
    bannerBar->setObjectName(QStringLiteral("csvPreviewBannerBar"));
    auto* bannerLayout = new QHBoxLayout(bannerBar);
    bannerLayout->setContentsMargins(10, 6, 10, 6);
    bannerLayout->setSpacing(10);

    m_bannerLabel = new QLabel(tr("CSV Preview"), bannerBar);
    m_bannerLabel->setObjectName(QStringLiteral("csvPreviewBanner"));
    bannerLayout->addWidget(m_bannerLabel, 1);

    m_rawTextBtn = new QPushButton(tr("View Raw Text (First 2 MB)"), bannerBar);
    m_rawTextBtn->setObjectName(QStringLiteral("csvViewRawTextBtn"));
    m_rawTextBtn->setToolTip(tr("Open the first 2 MB of this file in the raw text editor (highlighting disabled)"));
    connect(m_rawTextBtn, &QPushButton::clicked, this, [this]() {
        if (!m_filePath.isEmpty()) emit openRawTextRequested(m_filePath);
    });
    bannerLayout->addWidget(m_rawTextBtn);
    layout->addWidget(bannerBar);

    m_model = new CsvVirtualTableModel(this);
    m_tableView = new QTableView(this);
    m_tableView->setObjectName(QStringLiteral("csvPreviewTableView"));
    m_tableView->setModel(m_model);
    m_tableView->setAlternatingRowColors(true);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tableView->setWordWrap(false);
    m_tableView->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_tableView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_tableView->horizontalHeader()->setDefaultSectionSize(130);
    m_tableView->horizontalHeader()->setStretchLastSection(true);
    m_tableView->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_tableView->verticalHeader()->setDefaultSectionSize(24);
    layout->addWidget(m_tableView, 1);
}

bool CsvTablePreviewWidget::loadFile(const QString& filePath) {
    const QFileInfo fi(filePath);
    m_filePath = !fi.canonicalFilePath().isEmpty() ? fi.canonicalFilePath() : fi.absoluteFilePath();
    const QChar delim = fi.suffix().compare(QStringLiteral("tsv"), Qt::CaseInsensitive) == 0
                            ? QLatin1Char('\t')
                            : QLatin1Char(',');
    if (!m_model->loadFile(m_filePath, delim)) return false;

    const QLocale loc;
    m_bannerLabel->setText(tr("Virtualized %1 Preview  |  %2  |  %3 rows  |  %4 columns")
                               .arg(delim == QLatin1Char('\t') ? QStringLiteral("TSV") : QStringLiteral("CSV"),
                                    formatFileSize(m_model->fileSizeBytes()),
                                    loc.toString(m_model->rowCount()),
                                    loc.toString(m_model->columnCount())));
    return true;
}

int CsvTablePreviewWidget::rowCount() const { return m_model ? m_model->rowCount() : 0; }
int CsvTablePreviewWidget::columnCount() const { return m_model ? m_model->columnCount() : 0; }

QString CsvTablePreviewWidget::formatFileSize(qint64 bytes) {
    if (bytes >= 1024LL * 1024LL * 1024LL) {
        return QStringLiteral("%1 GB").arg(static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
    }
    if (bytes >= 1024LL * 1024LL) {
        return QStringLiteral("%1 MB").arg(static_cast<double>(bytes) / (1024.0 * 1024.0), 0, 'f', 2);
    }
    if (bytes >= 1024LL) {
        return QStringLiteral("%1 KB").arg(static_cast<double>(bytes) / 1024.0, 0, 'f', 1);
    }
    return QStringLiteral("%1 B").arg(bytes);
}

}  // namespace gridlock::ui
