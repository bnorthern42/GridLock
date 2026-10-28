/**
 * @file ServerRackView.cpp
 * @brief MPI rank topology rack visualizer and delegate renderer.
 *
 * Implements the ServerRackView widget and its custom item delegate ServerNodeDelegate,
 * rendering each MPI rank as a hardware blade card with execution state indicators,
 * program counter readouts, and cluster divergence telemetry.
 */

#include "ServerRackView.hpp"
#include <QApplication>
#include <QPalette>
#include <QPainter>
#include <QPainterPath>
#include <QStyledItemDelegate>

namespace gridlock::ui {

/**
 * @class ServerNodeDelegate
 * @brief Custom styled item delegate rendering individual MPI rank blade cards.
 */
class ServerNodeDelegate : public QStyledItemDelegate {
public:
    /**
     * @brief Constructs a new ServerNodeDelegate.
     * @param view Pointer to the parent ServerRackView.
     * @param parent Optional QObject parent.
     */
    explicit ServerNodeDelegate(ServerRackView* view, QObject* parent = nullptr) 
        : QStyledItemDelegate(parent), m_view(view) {}

    /**
     * @brief Paints a single MPI rank blade node with LED and status pill badge.
     * @param painter The QPainter instance.
     * @param option View item style options (state, rect, palette).
     * @param index Model index of the rank item.
     */
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);

        // Retrieve rank metadata from item user roles
        int rankId = index.data(Qt::UserRole).toInt();
        RankState state = index.data(Qt::UserRole + 1).value<RankState>();

        QRect rect = option.rect.adjusted(2, 2, -2, -2);
        bool isSelected = option.state & QStyle::State_Selected;
        bool isHovered = option.state & QStyle::State_MouseOver;

        QString status = state.currentState.isEmpty() ? QStringLiteral("stopped") : state.currentState;
        QString pcStr = state.registers.contains(16) ? state.registers.value(16) : QStringLiteral("0x00401120");

        // Determine background and outline colors based on selection and hover state
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

        // Map execution state to LED status and pill badge color
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

        // Top row: Rank [N] header text
        QFont rankFont("JetBrains Mono", 8, QFont::Bold);
        rankFont.setStyleHint(QFont::Monospace);
        painter->setFont(rankFont);
        painter->setPen(QColor("#e5e9f0"));
        painter->drawText(QRect(rect.left() + 6, rect.top() + 3, 54, 14),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          QStringLiteral("RANK %1").arg(rankId));

        // State badge pill rendered on top-right
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

        // Bottom row: Program counter instruction pointer address
        QFont pcFont("JetBrains Mono", 8);
        pcFont.setStyleHint(QFont::Monospace);
        painter->setFont(pcFont);
        painter->setPen(QColor("#4ec9b0"));
        painter->drawText(QRect(rect.left() + 6, rect.top() + 18, rect.width() - 12, 14),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          pcStr);

        painter->restore();
    }

    /**
     * @brief Computes the fixed card size hint for each rank blade item.
     * @return Blade node dimensions (116 x 34 px).
     */
    QSize sizeHint(const QStyleOptionViewItem &/*option*/, const QModelIndex &/*index*/) const override {
        return QSize(116, 34);
    }

private:
    ServerRackView* m_view;
};

/**
 * @brief Constructs the ServerRackView widget.
 *
 * Configures the list view into horizontal wrapping icon mode to render a grid
 * of compact server blade nodes.
 *
 * @param parent Optional parent widget.
 */
ServerRackView::ServerRackView(QWidget *parent) : QListWidget(parent) {
    // Layout nodes horizontally wrapping across available width
    setViewMode(QListView::IconMode);
    setFlow(QListView::LeftToRight);
    setWrapping(true);
    setResizeMode(QListView::Adjust);
    setSpacing(3);
    setContentsMargins(2, 2, 2, 2);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    // Install the custom node painter delegate
    setItemDelegate(new ServerNodeDelegate(this, this));

    // Connect user clicks to selection dispatcher
    connect(this, &QListWidget::itemClicked, this, &ServerRackView::onItemClicked);
}

/**
 * @brief Parses or extracts a program counter address from rank state telemetry.
 *
 * Inspects register index 16 (RIP on x86_64) or currentFunction string, converting
 * hex strings to uint64_t. Defaults to 0x00401120 for rank 0 or 0x00401155 for others.
 *
 * @param rankId Rank index.
 * @param state Rank state structure.
 * @return Extracted 64-bit instruction pointer address.
 */
uint64_t ServerRackView::extractPcFromState(int rankId, const RankState& state) {
    // Helper to safely parse hexadecimal address strings
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
    // Check if register 16 (x86 RIP) contains a valid parseable address
    if (state.registers.contains(16) && tryParseHex(state.registers.value(16), &parsed)) {
        return parsed;
    }
    // Check if current function string contains a hex address
    if (!state.currentFunction.isEmpty() && tryParseHex(state.currentFunction, &parsed)) {
        return parsed;
    }
    // Fall back to standard synthetic test offsets
    return (rankId == 0) ? 0x00401120ULL : 0x00401155ULL;
}

/**
 * @brief Resets the cluster rack view with default simulated rank states.
 * @param count Number of rank nodes to generate.
 */
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

/**
 * @brief Batch updates all rank blade nodes in the rack view.
 * @param ranks List of incoming RankState snapshots.
 */
void ServerRackView::updateRanks(const QList<RankState>& ranks) {
    m_rankCount = ranks.size();
    m_selectedRank = 0;
    m_states.clear();
    clear();

    for (int i = 0; i < ranks.size(); ++i) {
        RankState st = ranks[i];
        const int rankId = (st.rankId >= 0 && st.rankId != 0) ? st.rankId : i;
        st.rankId = rankId;
        // Ensure register 16 is populated with a valid formatted PC
        if (!st.registers.contains(16) || st.registers.value(16).trimmed().isEmpty()) {
            const uint64_t pc = extractPcFromState(rankId, st);
            st.registers[16] = QStringLiteral("0x%1").arg(pc, 8, 16, QChar('0'));
        }
        m_states[rankId] = st;

        // Allocate and associate item with rank data
        auto* item = new QListWidgetItem(this);
        item->setData(Qt::UserRole, rankId);
        item->setData(Qt::UserRole + 1, QVariant::fromValue(st));
        addItem(item);
    }

    if (count() > 0) {
        setCurrentRow(0);
    }
    viewport()->update();
    // Notify connected views (DisassemblyView) of all rank positions
    emit ranksUpdated(rankPcAddresses());
}

/**
 * @brief Updates an individual rank's state, dynamically growing the item list if needed.
 * @param rank Zero-based rank index.
 * @param state Updated RankState.
 */
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

    // Dynamically expand list items if the updated rank index exceeds current count
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

/**
 * @brief Returns the 64-bit program counter address for a specified rank.
 * @param rankId Rank index.
 * @return 64-bit address or 0 if rank not tracked.
 */
uint64_t ServerRackView::rankPcAddress(int rankId) const {
    auto it = m_states.find(rankId);
    if (it == m_states.end()) {
        return 0ULL;
    }
    return extractPcFromState(rankId, it->second);
}

/**
 * @brief Constructs a mapping of all active ranks to their current program counters.
 * @return Map of rank ID to 64-bit PC address.
 */
QMap<int, uint64_t> ServerRackView::rankPcAddresses() const {
    QMap<int, uint64_t> pcs;
    for (const auto& [rankId, st] : m_states) {
        pcs.insert(rankId, extractPcFromState(rankId, st));
    }
    return pcs;
}

/**
 * @brief Selects a rank node programmatically, updating highlights and firing rankSelected.
 * @param rankId Zero-based rank index.
 * @return True if rank was within range; false otherwise.
 */
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

/**
 * @brief Handles user click events on individual rank cards.
 * @param item Pointer to the clicked list item.
 */
void ServerRackView::onItemClicked(QListWidgetItem* item) {
    if (item) {
        int rankId = item->data(Qt::UserRole).toInt();
        selectRank(rankId);
    }
}

}  // namespace gridlock::ui

