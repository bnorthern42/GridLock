#pragma once
#include <QTableWidget>
#include <QString>
#include <QHash>
#include <vector>

namespace gridlock::ui {

class DifferentialGrid : public QTableWidget {
    Q_OBJECT
public:
    explicit DifferentialGrid(QWidget *parent = nullptr);
    ~DifferentialGrid() override = default;

    void setVariableData(int rankId, const QHash<QString, QString>& variables);

public slots:
    void updateVariableDisplay(int rankId, const QString& varName, const QString& value);

signals:
    void watchVariableAdded(const QString& name);

private:
    void updateHighlights();
};

} // namespace gridlock::ui
