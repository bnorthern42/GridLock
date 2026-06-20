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

private:
    std::vector<QString> m_headers;
    std::unordered_map<int, std::unordered_map<QString, QString>> m_varData;
    
    void updateHighlights();
};

} // namespace gridlock::ui
