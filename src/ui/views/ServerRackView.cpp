#include "ServerRackView.hpp"
#include <QApplication>
#include <QPalette>
#include <QPainter>
#include <QPainterPath>
#include <QStyledItemDelegate>

namespace gridlock::ui {

class ServerNodeDelegate : public QStyledItemDelegate {
public:
    explicit ServerNodeDelegate(ServerRackView* view, QObject* parent = nullptr) 
        : QStyledItemDelegate(parent), m_view(view) {}

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);

        int rankId = index.data(Qt::UserRole).toInt();
        RankState state = index.data(Qt::UserRole + 1).value<RankState>();

        QRect rect = option.rect.adjusted(2, 2, -2, -2);
        bool isSelected = option.state & QStyle::State_Selected;
        bool isHovered = option.state & QStyle::State_MouseOver;

        QString status = state.currentState.isEmpty() ? QStringLiteral("stopped") : state.currentState;
        QString pcStr = state.registers.contains(16) ? state.registers.value(16) : QStringLiteral("0x00401120");

        QColor bgColor = isSelected ? QColor("#22303c") : (isHovered ? QColor("#1f242c") : QColor("#181b20"));
        QColor borderColor = isSelected ? QColor("#009688") : (isHovered ? QColor("#569cd6") : QColor("#2d323b"));

        QPainterPath path;
        path.addRoundedRect(rect, 3, 3);
        painter->fillPath(path, bgColor);

        QPen borderPen(borderColor);
        borderPen.setWidth(isSelected ? 2 : 1);
        painter->setPen(borderPen);
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(path);

        // Status LED & Badge color
        QColor ledColor;
        QString badgeText;
        if (status == "running") {
            ledColor = QColor("#2ecc71");
            badgeText = QStringLiteral("RUN");
        } else if (status == "stopped" || status == "paused") {
            ledColor = QColor("#f1c40f");
            badgeText = QStringLiteral("BRK");
        } else {
            ledColor = QColor("#7f8c8d");
            badgeText = QStringLiteral("IDLE");
        }

        // Top row: Rank [N] + LED + State Badge
        QFont rankFont("JetBrains Mono", 8, QFont::Bold);
        rankFont.setStyleHint(QFont::Monospace);
        painter->setFont(rankFont);
        painter->setPen(QColor("#e5e9f0"));
        painter->drawText(QRect(rect.left() + 6, rect.top() + 3, 54, 14),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          QStringLiteral("RANK %1").arg(rankId));

        // State badge pill on top-right
        QRect badgeRect(rect.right() - 38, rect.top() + 3, 32, 13);
        QPainterPath badgePath;
        badgePath.addRoundedRect(badgeRect, 2, 2);
        QColor badgeBg = ledColor;
        badgeBg.setAlpha(45);
        painter->fillPath(badgePath, badgeBg);
        painter->setPen(ledColor);
        QFont badgeFont("JetBrains Mono", 7, QFont::Bold);
        badgeFont.setStyleHint(QFont::Monospace);
        painter->setFont(badgeFont);
        painter->drawText(badgeRect, Qt::AlignCenter, badgeText);

        // Bottom row: Instruction pointer address
        QFont pcFont("JetBrains Mono", 8);
        pcFont.setStyleHint(QFont::Monospace);
        painter->setFont(pcFont);
        painter->setPen(QColor("#4ec9b0"));
        painter->drawText(QRect(rect.left() + 6, rect.top() + 18, rect.width() - 12, 14),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          pcStr);

        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &/*option*/, const QModelIndex &/*index*/) const override {
        return QSize(116, 34);
    }

private:
    ServerRackView* m_view;
};

ServerRackView::ServerRackView(QWidget *parent) : QListWidget(parent) {
    setViewMode(QListView::IconMode);
    setFlow(QListView::LeftToRight);
    setWrapping(true);
    setResizeMode(QListView::Adjust);
    setSpacing(3);
    setContentsMargins(2, 2, 2, 2);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    setItemDelegate(new ServerNodeDelegate(this, this));

    connect(this, &QListWidget::itemClicked, this, &ServerRackView::onItemClicked);

    // Seed an 8-rank MPI cluster matrix by default for immediate visual inspection
    resetRanks(8);
}

void ServerRackView::resetRanks(int count) {
    m_rankCount = count;
    m_states.clear();
    clear();

    for (int i = 0; i < count; ++i) {
        RankState defaultState;
        defaultState.currentState = (i == 0) ? QStringLiteral("stopped") : QStringLiteral("running");
        defaultState.registers[16] = (i == 0) ? QStringLiteral("0x00401120") : QStringLiteral("0x00401155");
        defaultState.totalRuntimeMs = 12 + i * 3;
        m_states[i] = defaultState;

        QListWidgetItem* item = new QListWidgetItem(this);
        item->setData(Qt::UserRole, i);
        item->setData(Qt::UserRole + 1, QVariant::fromValue(defaultState));
        addItem(item);
    }

    if (count > 0) {
        setCurrentRow(0);
    }
}

void ServerRackView::updateRankState(int rank, const RankState& state) {
    m_states[rank] = state;
    if (rank >= 0 && rank < count()) {
        QListWidgetItem* item = this->item(rank);
        item->setData(Qt::UserRole + 1, QVariant::fromValue(state));
        viewport()->update();
    }
}

void ServerRackView::onItemClicked(QListWidgetItem* item) {
    if (item) {
        int rankId = item->data(Qt::UserRole).toInt();
        emit rankSelected(rankId);
    }
}

} // namespace gridlock::ui
