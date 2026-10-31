#pragma once

/**
 * @file DifferentialGrid.hpp
 * @brief Multi-rank variable differential inspection grid table.
 *
 * Implements DifferentialGrid, a 2D table mapping MPI ranks (rows) against
 * watched variables or expressions (columns), highlighting divergence across ranks.
 */

#include <QTableWidget>
#include <QString>
#include <QHash>
#include <vector>

namespace gridlock::ui {

/**
 * @class DifferentialGrid
 * @brief Table widget visualizing variable evaluations across all cluster ranks.
 *
 * Rows represent MPI ranks, columns represent watched variable expressions.
 * Cells where values diverge from the modal consensus or rank 0 baseline are
 * highlighted in distinct differential alert colors.
 */
class DifferentialGrid : public QTableWidget {
    Q_OBJECT

public:
    /**
     * @brief Constructs a new DifferentialGrid widget.
     * @param parent Optional parent widget.
     */
    explicit DifferentialGrid(QWidget *parent = nullptr);

    /**
     * @brief Destructor.
     */
    ~DifferentialGrid() override = default;

    /**
     * @brief Sets all variable key-value pairs for a specific rank row.
     * @param rankId Zero-based rank index.
     * @param variables Hash of variable names to string values.
     */
    void setVariableData(int rankId, const QHash<QString, QString>& variables);

    /**
     * @brief Returns whether horizontal header columns are configured to stretch evenly across the dock width.
     * @return True if QHeaderView::Stretch is active on the horizontal header; false otherwise.
     */
    bool horizontalHeaderStretchEnabled() const;

    /**
     * @brief Returns active table canvas background color according to the theme.
     * @return Resolved QColor corresponding to active theme canvas base.
     */
    QColor activeCanvasBackgroundColor() const;

    /**
     * @brief Reloads widget styling according to the active theme palette.
     */
    Q_INVOKABLE void reloadStyle();


public slots:
    /**
     * @brief Updates or creates an individual cell for a given rank and variable.
     * @param rankId Rank index (row).
     * @param varName Variable identifier (column).
     * @param value Evaluated value string.
     */
    void updateVariableDisplay(int rankId, const QString& varName, const QString& value);

    /**
     * @brief Adds a new variable watch column if not already present.
     * @param name Variable identifier or expression.
     */
    void addVariableColumn(const QString& name);

    /**
     * @brief Retrieves all active watch column expression strings.
     * @return Vector of expression strings.
     */
    std::vector<std::string> getWatchExpressions() const;

    /**
     * @brief Clears all watched columns and row data.
     */
    void clearWatches();

signals:
    /**
     * @brief Emitted when a new variable is added to the watch table.
     * @param name Name of the watched variable.
     */
    void watchVariableAdded(const QString& name);

private:
    /**
     * @brief Scans column cells to detect divergence across ranks and updates cell colors.
     */
    void updateHighlights();
};

}  // namespace gridlock::ui

