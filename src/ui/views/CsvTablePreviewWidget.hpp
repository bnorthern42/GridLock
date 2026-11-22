/**
 * @file CsvTablePreviewWidget.hpp
 * @brief Virtualized, zero-freeze CSV/TSV table previewer for large tabular data files.
 *
 * Indexes newline byte offsets in a single fast pass over a memory-mapped file and parses
 * only the currently visible rows on demand inside a QAbstractTableModel + QTableView,
 * avoiding upfront QString allocations or syntax-highlighter lockups on multi-megabyte CSVs.
 */

#pragma once

#include <QAbstractTableModel>
#include <QFile>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QLabel;
class QPushButton;
class QTableView;

namespace gridlock::ui {

/**
 * @class CsvVirtualTableModel
 * @brief On-demand virtualized table model backed by byte-offset indexing of a CSV/TSV file.
 */
class CsvVirtualTableModel : public QAbstractTableModel {
    Q_OBJECT

public:
    /**
     * @brief Constructs an empty virtualized CSV/TSV model.
     * @param parent Optional parent QObject.
     */
    explicit CsvVirtualTableModel(QObject* parent = nullptr);

    /** @brief Unmaps and closes the backing file if open. */
    ~CsvVirtualTableModel() override;

    /**
     * @brief Indexes @p filePath in a single fast byte pass and parses only the header row.
     * @param filePath Path to the `.csv` or `.tsv` file on disk.
     * @param delimiter Field separator character (`','` or `'\t'`).
     * @return True if the file was opened and indexed successfully.
     */
    bool loadFile(const QString& filePath, QChar delimiter = QLatin1Char(','));

    /** @brief Unmaps and closes any open file and resets the model. */
    void closeFile();

    /** @brief Returns the number of data rows (excluding the header row). */
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;

    /** @brief Returns the number of parsed columns from the header row. */
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;

    /** @brief Parses and returns the requested cell value on demand. */
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    /** @brief Returns parsed header column names or 1-based vertical row numbers. */
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    /** @brief Returns the total file size in bytes. */
    qint64 fileSizeBytes() const { return m_fileSize; }

    /** @brief Returns the active delimiter character. */
    QChar delimiter() const { return m_delimiter; }

    /** @brief Splits a single raw UTF-8 CSV/TSV line into fields with RFC-4180 quote handling. */
    static QStringList parseLine(const QByteArray& rawLine, QChar delimiter);

private:
    QByteArray readLineBytes(int lineIndex) const;

    mutable QFile m_file;
    const uchar* m_mapped = nullptr;
    qint64 m_fileSize = 0;
    QChar m_delimiter = QLatin1Char(',');
    QVector<qint64> m_lineOffsets;
    QStringList m_headers;
    int m_columnCount = 0;
    mutable int m_cachedRow = -1;
    mutable QStringList m_cachedFields;
};

/**
 * @class CsvTablePreviewWidget
 * @brief Lightweight central-tab previewer for large `.csv`/`.tsv` files with a raw-text fallback button.
 */
class CsvTablePreviewWidget : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief Constructs the CSV preview widget with summary banner and virtualized QTableView.
     * @param parent Optional parent widget.
     */
    explicit CsvTablePreviewWidget(QWidget* parent = nullptr);

    /** @brief Loads and indexes @p filePath into the virtualized table model. */
    bool loadFile(const QString& filePath);

    /** @brief Returns the canonical/absolute path of the loaded file. */
    QString filePath() const { return m_filePath; }

    /** @brief Returns the number of indexed data rows (excluding header). */
    int rowCount() const;

    /** @brief Returns the number of indexed columns. */
    int columnCount() const;

    /** @brief Returns the underlying virtualized QTableView. */
    QTableView* tableView() const { return m_tableView; }

    /** @brief Returns the underlying CsvVirtualTableModel. */
    CsvVirtualTableModel* model() const { return m_model; }

    /** @brief Returns the top summary banner label (`#csvPreviewBanner`). */
    QLabel* bannerLabel() const { return m_bannerLabel; }

    /** @brief Returns the "View Raw Text (First 2 MB)" button (`#csvViewRawTextBtn`). */
    QPushButton* rawTextButton() const { return m_rawTextBtn; }

signals:
    /** @brief Emitted when the user clicks "View Raw Text (First 2 MB)" to open the raw buffer. */
    void openRawTextRequested(const QString& filePath);

private:
    static QString formatFileSize(qint64 bytes);

    QString m_filePath;
    CsvVirtualTableModel* m_model = nullptr;
    QLabel* m_bannerLabel = nullptr;
    QPushButton* m_rawTextBtn = nullptr;
    QTableView* m_tableView = nullptr;
};

}  // namespace gridlock::ui
