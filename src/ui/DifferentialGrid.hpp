#pragma once
#include <QTableWidget>
#include <QString>
#include <unordered_map>
#include <vector>

namespace gridlock::ui {

class DifferentialGrid : public QTableWidget {
    Q_OBJECT
public:
    explicit DifferentialGrid(QWidget *parent = nullptr);
    ~DifferentialGrid() override = default;

    void setVariableData(int rank, const QString& varName, const QString& value);
    void setVariableData(int rank, const std::unordered_map<QString, QString>& variables);
    void updateVariable(int rankId, const QString& name, const QString& value);

signals:
    void watchVariableAdded(const QString& name);

private:
    std::vector<QString> m_watchVariables;
    int m_numRanks = 4;
    
    void updateHighlights();
    void addEmptyWatchRow();
};

} // namespace gridlock::ui
