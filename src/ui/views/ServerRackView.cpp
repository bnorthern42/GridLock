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
        QColor borderColor = isSelected ? QColor("#528bff") : (isHovered ? QColor("#569cd6") : QColor("#2d323b"));

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
}

uint64_t ServerRackView::extractPcFromState(int rankId, const RankState& state) {
    auto tryParseHex = [](QString raw, uint64_t* outVal) -> bool {
        raw = raw.trimmed();
        if (raw.isEmpty()) {
            return false;
        }
        const int spaceIdx = raw.indexOf(' ');
        if (spaceIdx > 0) {
            raw = raw.left(spaceIdx);
        }
        if (raw.startsWith("0x", Qt::CaseInsensitive)) {
            raw = raw.mid(2);
        }
        bool ok = false;
        const uint64_t val = raw.toULongLong(&ok, 16);
        if (ok && val != 0ULL) {
            *outVal = val;
            return true;
        }
        return false;
    };

    uint64_t parsed = 0ULL;
    if (state.registers.contains(16) && tryParseHex(state.registers.value(16), &parsed)) {
        return parsed;
    }
    if (!state.currentFunction.isEmpty() && tryParseHex(state.currentFunction, &parsed)) {
        return parsed;
    }
    return (rankId == 0) ? 0x00401120ULL : 0x00401155ULL;
}

void ServerRackView::resetRanks(int count) {
    QList<RankState> initialRanks;
    initialRanks.reserve(count);
    for (int i = 0; i < count; ++i) {
        RankState defaultState;
        defaultState.rankId = i;
        defaultState.currentState = (i == 0) ? QStringLiteral("stopped") : QStringLiteral("running");
        defaultState.registers[16] = (i == 0) ? QStringLiteral("0x00401120") : QStringLiteral("0x00401155");
        defaultState.totalRuntimeMs = 12 + i * 3;
        initialRanks.append(defaultState);
    }
    updateRanks(initialRanks);
}

void ServerRackView::updateRanks(const QList<RankState>& ranks) {
    m_rankCount = ranks.size();
    m_selectedRank = 0;
    m_states.clear();
    clear();

    for (int i = 0; i < ranks.size(); ++i) {
        RankState st = ranks[i];
        const int rankId = (st.rankId >= 0 && st.rankId != 0) ? st.rankId : i;
        st.rankId = rankId;
        if (!st.registers.contains(16) || st.registers.value(16).trimmed().isEmpty()) {
            const uint64_t pc = extractPcFromState(rankId, st);
            st.registers[16] = QStringLiteral("0x%1").arg(pc, 8, 16, QChar('0'));
        }
        m_states[rankId] = st;

        auto* item = new QListWidgetItem(this);
        item->setData(Qt::UserRole, rankId);
        item->setData(Qt::UserRole + 1, QVariant::fromValue(st));
        addItem(item);
    }

    if (count() > 0) {
        setCurrentRow(0);
    }
    viewport()->update();
    emit ranksUpdated(rankPcAddresses());
}

void ServerRackView::updateRankState(int rank, const RankState& state) {
    if (rank < 0) {
        return;
    }
    RankState normalized = state;
    normalized.rankId = rank;
    if (!normalized.registers.contains(16) || normalized.registers.value(16).trimmed().isEmpty()) {
        const uint64_t pc = extractPcFromState(rank, normalized);
        normalized.registers[16] = QStringLiteral("0x%1").arg(pc, 8, 16, QChar('0'));
    }
    m_states[rank] = normalized;

    while (count() <= rank) {
        const int idx = count();
        RankState placeholder;
        placeholder.rankId = idx;
        placeholder.currentState = QStringLiteral("stopped");
        placeholder.registers[16] = (idx == 0) ? QStringLiteral("0x00401120") : QStringLiteral("0x00401155");
        if (m_states.find(idx) == m_states.end()) {
            m_states[idx] = placeholder;
        }
        auto* item = new QListWidgetItem(this);
        item->setData(Qt::UserRole, idx);
        item->setData(Qt::UserRole + 1, QVariant::fromValue(m_states[idx]));
        addItem(item);
    }
    m_rankCount = count();
    if (QListWidgetItem* item = this->item(rank)) {
        item->setData(Qt::UserRole + 1, QVariant::fromValue(normalized));
        viewport()->update();
    }
    emit ranksUpdated(rankPcAddresses());
}

uint64_t ServerRackView::rankPcAddress(int rankId) const {
    auto it = m_states.find(rankId);
    if (it == m_states.end()) {
        return 0ULL;
    }
    return extractPcFromState(rankId, it->second);
}

QMap<int, uint64_t> ServerRackView::rankPcAddresses() const {
    QMap<int, uint64_t> pcs;
    for (const auto& [rankId, st] : m_states) {
        pcs.insert(rankId, extractPcFromState(rankId, st));
    }
    return pcs;
}

bool ServerRackView::selectRank(int rankId) {
    if (rankId < 0 || rankId >= count()) {
        return false;
    }
    m_selectedRank = rankId;
    setCurrentRow(rankId);
    viewport()->update();

    const uint64_t pcAddr = rankPcAddress(rankId);
    emit rankSelected(rankId, pcAddr);
    return true;
}

void ServerRackView::onItemClicked(QListWidgetItem* item) {
    if (item) {
        int rankId = item->data(Qt::UserRole).toInt();
        selectRank(rankId);
    }
}

} // namespace gridlock::ui
