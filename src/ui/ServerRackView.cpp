#include "ServerRackView.hpp"
#include "ThemeManager.hpp"
#include <QPainter>
#include <QPainterPath>
#include <QStyledItemDelegate>

namespace gridlock::ui {

class ServerNodeDelegate : public QStyledItemDelegate {
public:
    explicit ServerNodeDelegate(ServerRackView* view, QObject* parent = nullptr) 
        : QStyledItemDelegate(parent), m_view(view) {}

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        painter->setRenderHint(QPainter::Antialiasing);

        int rankId = index.data(Qt::UserRole).toInt();
        RankState state = index.data(Qt::UserRole + 1).value<RankState>();

        QRect rect = option.rect.adjusted(5, 5, -5, -5);
        QPainterPath path;
        path.addRoundedRect(rect, 5, 5);

        bool isSelected = option.state & QStyle::State_Selected;
        bool isHovered = option.state & QStyle::State_MouseOver;

        // Background
        if (isHovered) {
            painter->setBrush(QColor(ThemeManager::SURFACE1));
        } else {
            painter->setBrush(QColor(ThemeManager::SURFACE0));
        }

        // Border
        QPen borderPen(isSelected ? QColor(ThemeManager::MAUVE) : QColor(ThemeManager::SURFACE1));
        borderPen.setWidth(isSelected ? 2 : 1);
        painter->setPen(borderPen);
        painter->drawPath(path);

        // Text
        QString status = state.currentState.isEmpty() ? "offline" : state.currentState;
        qint64 runTime = state.totalRuntimeMs;
        if (status == "running" && state.executionTimer.isValid()) {
            runTime += state.executionTimer.elapsed();
        }

        QFont monoFont("monospace", 10, QFont::Bold);
        monoFont.setStyleHint(QFont::Monospace);
        painter->setFont(monoFont);
        painter->setPen(QColor(ThemeManager::TEXT));
        QString titleStr = QString("R[%1] %2ms").arg(rankId).arg(runTime);
        painter->drawText(rect.left() + 15, rect.center().y() + 4, titleStr);

        // LED
        QColor ledColor;
        if (status == "running") {
            ledColor = QColor(ThemeManager::GREEN);
        } else if (status == "stopped") {
            ledColor = QColor(ThemeManager::YELLOW);
        } else {
            ledColor = QColor(ThemeManager::SURFACE1);
        }

        painter->setBrush(ledColor);
        painter->setPen(Qt::NoPen);
        painter->drawEllipse(rect.right() - 20, rect.center().y() - 5, 10, 10);
    }

    QSize sizeHint(const QStyleOptionViewItem &/*option*/, const QModelIndex &/*index*/) const override {
        return QSize(160, 45); // width, height per node
    }

private:
    ServerRackView* m_view;
};

ServerRackView::ServerRackView(QWidget *parent) : QListWidget(parent) {
    setViewMode(QListView::IconMode);
    setFlow(QListView::LeftToRight);
    setWrapping(true);
    setResizeMode(QListView::Adjust);
    setSpacing(5);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    
    // Apply Mantle background color as requested
    setStyleSheet(QString("QListWidget { background-color: #181825; border: none; }"));

    setItemDelegate(new ServerNodeDelegate(this, this));

    connect(this, &QListWidget::itemClicked, this, &ServerRackView::onItemClicked);
}

void ServerRackView::resetRanks(int count) {
    m_rankCount = count;
    m_states.clear();
    clear();

    for (int i = 0; i < count; ++i) {
        QListWidgetItem* item = new QListWidgetItem(this);
        item->setData(Qt::UserRole, i);
        item->setData(Qt::UserRole + 1, QVariant::fromValue(RankState{}));
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
    }
}

void ServerRackView::onItemClicked(QListWidgetItem* item) {
    if (item) {
        int rankId = item->data(Qt::UserRole).toInt();
        emit rankSelected(rankId);
    }
}

} // namespace gridlock::ui
