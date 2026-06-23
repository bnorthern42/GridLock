#pragma once
#include <QWidget>
#include <QTableWidget>
#include <QJsonArray>
#include "../../RankState.hpp"

namespace gridlock::ui {

class RegisterView : public QWidget {
    Q_OBJECT
public:
    explicit RegisterView(QWidget* parent = nullptr);
    ~RegisterView() override = default;

public slots:
    void updateRegisters(const gridlock::RankState& state);
    void updateRegisters(const QJsonArray& registers);

private:
    QTableWidget* m_table;
    QMap<QString, QString> m_previousRegisters;
};

} // namespace gridlock::ui
