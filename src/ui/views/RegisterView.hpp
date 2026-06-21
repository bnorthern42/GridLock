#pragma once
#include <QWidget>
#include <QTableWidget>
#include "../../RankState.hpp"

namespace gridlock::ui {

class RegisterView : public QWidget {
    Q_OBJECT
public:
    explicit RegisterView(QWidget* parent = nullptr);
    ~RegisterView() override = default;

public slots:
    void updateRegisters(const gridlock::RankState& state);

private:
    QTableWidget* m_table;
};

} // namespace gridlock::ui
