#pragma once
#include <QWidget>
#include <unordered_map>
#include <QString>
#include "../RankState.hpp"

namespace gridlock::ui {

class ServerRackView : public QWidget {
    Q_OBJECT
public:
    explicit ServerRackView(QWidget *parent = nullptr);
    ~ServerRackView() override = default;

    void updateRankState(int rank, const RankState& state) {
        m_states[rank] = state;
        update();
    }

signals:
    void rankSelected(int rankId);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    std::unordered_map<int, RankState> m_states;
    int m_hoveredRank = -1;
    int m_selectedRank = 0;
};

} // namespace gridlock::ui
