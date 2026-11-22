/**
 * @file DifferentialGrid.cpp
 * @brief Differential multi-rank variable grid implementation.
 *
 * Implements the DifferentialGrid table widget, which tracks watched expressions
 * across all active MPI ranks and detects statistical divergence using majority-vote
 * consensus highlighting.
 */

#include "DifferentialGrid.hpp"
#include "../../core/managers/ThemeManager.hpp"

#include <algorithm>

#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QMenu>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QTextOption>
#include <QVBoxLayout>

namespace gridlock::ui {

namespace {
bool parseLeadingNumber(const QString& raw, double* outVal) {
    static const QRegularExpression kNumRe(QStringLiteral(R"(^\s*([-+]?(?:0x[0-9a-fA-F]+|\d+(?:\.\d+)?(?:[eE][-+]?\d+)?)))"));
    const QRegularExpressionMatch m = kNumRe.match(raw);
    if (!m.hasMatch()) return false;
    const QString tok = m.captured(1);
    bool ok = false;
    const double v = tok.contains(QLatin1String("0x"), Qt::CaseInsensitive) ? static_cast<double>(tok.toLongLong(&ok, 16)) : tok.toDouble(&ok);
    if (ok && outVal) *outVal = v;
    return ok;
}

void showGridFullValueDialog(DifferentialGrid* grid, int row, int col) {
    if (!grid || col < 1 || col >= grid->columnCount()) return;
    const QString colExpr = grid->horizontalHeaderItem(col) ? grid->horizontalHeaderItem(col)->text() : QStringLiteral("Watch");
    QString fullText;
    if (row >= 1 && row < grid->rowCount() && grid->item(row, col)) {
        fullText = grid->item(row, col)->text();
    } else {
        QStringList lines;
        for (int r = 1; r < grid->rowCount(); ++r) {
            const QString rankLbl = grid->verticalHeaderItem(r) ? grid->verticalHeaderItem(r)->text() : QStringLiteral("Rank %1").arg(r - 1);
            const QString val = grid->item(r, col) ? grid->item(r, col)->text() : QStringLiteral("-");
            lines << QStringLiteral("%1: %2").arg(rankLbl, val);
        }
        fullText = lines.join(QLatin1Char('\n'));
    }
    grid->setProperty("lastInspectedFullValue", fullText);
    auto* dlg = grid->findChild<QDialog*>(QStringLiteral("fullValueInspectorDialog"), Qt::FindDirectChildrenOnly);
    if (!dlg) {
        dlg = new QDialog(grid);
        dlg->setObjectName(QStringLiteral("fullValueInspectorDialog"));
        dlg->setModal(false);
        dlg->resize(560, 280);
        auto* v = new QVBoxLayout(dlg);
        v->setContentsMargins(10, 10, 10, 10);
        v->setSpacing(8);
        auto* edit = new QPlainTextEdit(dlg);
        edit->setObjectName(QStringLiteral("fullValueInspectorTextEdit"));
        edit->setReadOnly(true);
        edit->setWordWrapMode(QTextOption::WrapAnywhere);
        edit->setFont(QFont(QStringLiteral("JetBrains Mono"), 10));
        v->addWidget(edit, 1);
        auto* btnRow = new QHBoxLayout();
        btnRow->addStretch();
        auto* copyBtn = new QPushButton(QStringLiteral("Copy to Clipboard"), dlg);
        copyBtn->setObjectName(QStringLiteral("fullValueInspectorCopyBtn"));
        QObject::connect(copyBtn, &QPushButton::clicked, dlg, [edit]() {
            QApplication::clipboard()->setText(edit->toPlainText());
        });
        auto* closeBtn = new QPushButton(QStringLiteral("Close"), dlg);
        QObject::connect(closeBtn, &QPushButton::clicked, dlg, &QDialog::close);
        btnRow->addWidget(copyBtn);
        btnRow->addWidget(closeBtn);
        v->addLayout(btnRow);
    }
    dlg->setWindowTitle(row >= 1 ? QStringLiteral("Full Value — Rank %1: %2").arg(row - 1).arg(colExpr)
                                 : QStringLiteral("Full Values — %1 (All Ranks)").arg(colExpr));
    if (auto* edit = dlg->findChild<QPlainTextEdit*>(QStringLiteral("fullValueInspectorTextEdit"))) {
        edit->setPlainText(fullText);
    }
    dlg->show();
    dlg->raise();
}
}  // namespace

DifferentialGrid::DifferentialGrid(QWidget *parent) : QTableWidget(parent) {
    setFrameShape(QFrame::NoFrame);
    setAlternatingRowColors(true);
    setTextElideMode(Qt::ElideRight);
    horizontalHeader()->setVisible(true);
    horizontalHeader()->setMinimumSectionSize(64);
    horizontalHeader()->setDefaultSectionSize(96);
    horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    horizontalHeader()->setStretchLastSection(true);
    horizontalHeader()->setTextElideMode(Qt::ElideRight);
    verticalHeader()->setDefaultSectionSize(22);

    setContextMenuPolicy(Qt::CustomContextMenu);
    horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        const QModelIndex idx = indexAt(pos);
        showWatchContextMenu(idx.isValid() ? idx.row() : currentRow(),
                             idx.isValid() ? idx.column() : currentColumn(),
                             viewport()->mapToGlobal(pos));
    });
    connect(horizontalHeader(), &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        const int col = horizontalHeader()->logicalIndexAt(pos);
        showWatchContextMenu(-1, col >= 0 ? col : currentColumn(), horizontalHeader()->mapToGlobal(pos));
    });
    connect(this, &QTableWidget::cellDoubleClicked, this, [this](int row, int col) {
        if (row >= 1 && col >= 1) showGridFullValueDialog(this, row, col);
    });

    reloadStyle();
    connect(&gridlock::core::ThemeManager::instance(),
            &gridlock::core::ThemeManager::themeModeChanged,
            this, [this](bool) { reloadStyle(); updateHighlights(); });

    // Seed initial row/col for inline watch input
    setRowCount(1);
    setColumnCount(1);
    setVerticalHeaderItem(0, new QTableWidgetItem("Add Watch ->"));
    setHorizontalHeaderItem(0, new QTableWidgetItem("New Variable"));
    setItem(0, 0, new QTableWidgetItem(""));
    setColumnWidth(0, 140);
    
    // When user types an expression in cell (0, 0), dispatch watchVariableAdded
    connect(this, &QTableWidget::cellChanged, this, [this](int row, int col) {
        if (row == 0 && col == 0) {
            QTableWidgetItem* cell = item(row, col);
            if (!cell) return;
            QString newVarName = cell->text().trimmed();
            if (!newVarName.isEmpty() && newVarName != "...") {
                emit watchVariableAdded(newVarName);
                this->blockSignals(true);
                cell->setText("...");
                this->blockSignals(false);
            }
        }
    });
}

QString DifferentialGrid::emptyPlaceholderText() const {
    return QStringLiteral("No watch expressions — enter a C/C++ expression above and click + Watch");
}

void DifferentialGrid::paintEvent(QPaintEvent* event) {
    QTableWidget::paintEvent(event);
    if (!getWatchExpressions().empty() || !viewport()) return;
    QPainter p(viewport());
    QColor muted = palette().color(QPalette::Text);
    muted.setAlpha(140);
    p.setPen(muted);
    QRect r = viewport()->rect().adjusted(14, isRowHidden(0) ? 12 : 30, -14, -12);
    p.drawText(r, Qt::AlignCenter | Qt::TextWordWrap, emptyPlaceholderText());
}

void DifferentialGrid::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Delete && currentColumn() >= 1) {
        removeWatchColumn(currentColumn());
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Copy)) {
        if (const QTableWidgetItem* it = currentItem(); it && currentRow() >= 1 && currentColumn() >= 1) {
            QApplication::clipboard()->setText(it->text());
            event->accept();
            return;
        }
    }
    QTableWidget::keyPressEvent(event);
}

void DifferentialGrid::showWatchContextMenu(int row, int col, const QPoint& globalPos) {
    const QString colExpr = (col >= 1 && horizontalHeaderItem(col)) ? horizontalHeaderItem(col)->text() : QString();
    const QString cellVal = (row >= 1 && col >= 1 && item(row, col)) ? item(row, col)->text() : QString();
    QMenu menu(this);
    QAction* inspectAct = menu.addAction(QStringLiteral("Inspect Full Value..."));
    inspectAct->setEnabled(col >= 1);
    QAction* copyAct = menu.addAction(QStringLiteral("Copy Value (Ctrl+C)"));
    copyAct->setEnabled(!cellVal.isEmpty());
    QAction* rmColAct = menu.addAction(colExpr.isEmpty() ? QStringLiteral("Remove Watch Column (Del)")
                                                         : QStringLiteral("Remove Watch Column '%1' (Del)").arg(colExpr));
    rmColAct->setEnabled(col >= 1);
    QAction* clearAct = menu.addAction(QStringLiteral("Clear All Watches"));
    clearAct->setEnabled(columnCount() > 1);
    menu.addSeparator();
    QAction* plotAct = menu.addAction(QStringLiteral("Plot 1D/2D Array..."));
    plotAct->setEnabled(!colExpr.isEmpty());
    QAction* graphAct = menu.addAction(QStringLiteral("Graph Pointer / Struct..."));
    graphAct->setEnabled(!colExpr.isEmpty());

    QAction* chosen = menu.exec(globalPos);
    if (chosen == inspectAct && col >= 1) showGridFullValueDialog(this, row, col);
    else if (chosen == copyAct && !cellVal.isEmpty()) QApplication::clipboard()->setText(cellVal);
    else if (chosen == rmColAct && col >= 1) removeWatchColumn(col);
    else if (chosen == clearAct) clearWatches();
    else if (chosen == plotAct && !colExpr.isEmpty()) emit plotArrayRequested(colExpr);
    else if (chosen == graphAct && !colExpr.isEmpty()) emit graphPointerRequested(colExpr, cellVal);
}

bool DifferentialGrid::horizontalHeaderStretchEnabled() const {
    return horizontalHeader() != nullptr &&
           horizontalHeader()->sectionResizeMode(0) == QHeaderView::Stretch;
}

void DifferentialGrid::setVariableData(int rankId, const QHash<QString, QString>& variables) {
    for (auto it = variables.constBegin(); it != variables.constEnd(); ++it) {
        updateVariableDisplay(rankId, it.key(), it.value());
    }
}

void DifferentialGrid::addVariableColumn(const QString& name) {
    horizontalHeader()->setVisible(true);
    verticalHeader()->setVisible(true);
    int col = -1;
    for (int c = 0; c < columnCount(); ++c) {
        if (horizontalHeaderItem(c) && horizontalHeaderItem(c)->text() == name) { col = c; break; }
    }
    if (col == -1) {
        col = columnCount();
        setColumnCount(col + 1);
        auto* hdrItem = new QTableWidgetItem(name);
        hdrItem->setToolTip(name);
        setHorizontalHeaderItem(col, hdrItem);
        for (int r = 0; r < rowCount(); ++r) setItem(r, col, new QTableWidgetItem("-"));
    }
    if (viewport()) viewport()->update();
}

bool DifferentialGrid::removeWatchColumn(int col) {
    if (col < 1 || col >= columnCount()) return false;
    const QString removedName = horizontalHeaderItem(col) ? horizontalHeaderItem(col)->text() : QString();
    removeColumn(col);
    if (columnCount() <= 1 && isColumnHidden(0)) {
        horizontalHeader()->setVisible(false);
        verticalHeader()->setVisible(false);
    }
    if (!removedName.isEmpty()) emit watchVariableRemoved(removedName);
    if (viewport()) viewport()->update();
    return true;
}

bool DifferentialGrid::removeWatchExpression(const QString& name) {
    for (int c = 1; c < columnCount(); ++c) {
        if (horizontalHeaderItem(c) && horizontalHeaderItem(c)->text() == name) return removeWatchColumn(c);
    }
    return false;
}

std::vector<std::string> DifferentialGrid::getWatchExpressions() const {
    std::vector<std::string> watches;
    for (int c = 1; c < columnCount(); ++c) {
        if (auto* item = horizontalHeaderItem(c)) watches.push_back(item->text().toStdString());
    }
    return watches;
}

void DifferentialGrid::clearWatches() {
    const auto prior = getWatchExpressions();
    setColumnCount(1);
    setHorizontalHeaderItem(0, new QTableWidgetItem("New Variable"));
    setRowCount(1);
    setItem(0, 0, new QTableWidgetItem(""));
    if (isColumnHidden(0)) {
        horizontalHeader()->setVisible(false);
        verticalHeader()->setVisible(false);
    }
    for (const auto& w : prior) emit watchVariableRemoved(QString::fromStdString(w));
    if (viewport()) viewport()->update();
}

void DifferentialGrid::updateVariableDisplay(int rankId, const QString& varName, const QString& value) {
    horizontalHeader()->setVisible(true);
    verticalHeader()->setVisible(true);
    int targetRow = rankId + 1;
    int col = -1;
    for (int c = 0; c < columnCount(); ++c) {
        if (horizontalHeaderItem(c) && horizontalHeaderItem(c)->text() == varName) { col = c; break; }
    }
    if (col == -1) {
        col = columnCount();
        setColumnCount(col + 1);
        auto* hdrItem = new QTableWidgetItem(varName);
        hdrItem->setToolTip(varName);
        setHorizontalHeaderItem(col, hdrItem);
        for (int r = 0; r < rowCount(); ++r) setItem(r, col, new QTableWidgetItem("-"));
        QTableWidgetItem* inputCell = item(0, 0);
        if (inputCell && inputCell->text() == "...") {
            blockSignals(true);
            inputCell->setText("");
            blockSignals(false);
        }
    }
    if (targetRow >= rowCount()) {
        int oldRow = rowCount();
        setRowCount(targetRow + 1);
        for (int r = oldRow; r <= targetRow; ++r) {
            setVerticalHeaderItem(r, new QTableWidgetItem(QString("Rank %1").arg(r - 1)));
            for (int c = 0; c < columnCount(); ++c) setItem(r, c, new QTableWidgetItem("-"));
        }
    }
    QTableWidgetItem* cellItem = item(targetRow, col);
    if (!cellItem) { cellItem = new QTableWidgetItem(value); setItem(targetRow, col, cellItem); }
    else cellItem->setText(value);

    QFont font = cellItem->font();
    font.setItalic(value == "<Out of Scope>");
    cellItem->setFont(font);
    updateHighlights();
}

void DifferentialGrid::updateHighlights() {
    const bool isDark = gridlock::core::ThemeManager::instance().isDarkMode();
    const QColor divFg = isDark ? QColor(0x24, 0x0b, 0x0e) : QColor(0x6e, 0x11, 0x16);
    for (int col = 1; col < columnCount(); ++col) {
        const QString varName = horizontalHeaderItem(col) ? horizontalHeaderItem(col)->text() : QString();
        std::unordered_map<QString, int> counts;
        for (int row = 1; row < rowCount(); ++row) {
            QTableWidgetItem* it = item(row, col);
            if (it && it->text() != "-") counts[it->text()]++;
        }
        QString majorityStr;
        int maxCount = 0;
        for (const auto& kv : counts) {
            if (kv.second > maxCount) { maxCount = kv.second; majorityStr = kv.first; }
        }
        double rank0Num = 0.0;
        const QString rank0Str = (rowCount() > 1 && item(1, col)) ? item(1, col)->text() : QString();
        const bool hasRank0Num = (rank0Str != QLatin1String("-")) && parseLeadingNumber(rank0Str, &rank0Num);

        for (int row = 1; row < rowCount(); ++row) {
            QTableWidgetItem* it = item(row, col);
            if (!it) continue;
            const QString val = it->text();
            const bool divergent = (val != QLatin1String("-") && val != majorityStr && counts.size() > 1);
            if (divergent) {
                it->setBackground(QColor(255, 200, 200));
                it->setForeground(divFg);
            } else {
                it->setBackground(QBrush());
                it->setForeground(val == QLatin1String("<Out of Scope>") ? QBrush(Qt::gray) : QBrush());
            }
            QString tip = QStringLiteral("Rank %1 — %2 = %3").arg(row - 1).arg(varName, val);
            double curNum = 0.0;
            if (row > 1 && hasRank0Num && val != QLatin1String("-") && parseLeadingNumber(val, &curNum)) {
                const double delta = curNum - rank0Num;
                tip += QStringLiteral("\nΔ vs Rank 0 (%1): %2%3").arg(rank0Str, delta >= 0.0 ? QStringLiteral("+") : QString()).arg(delta);
            }
            if (!majorityStr.isEmpty() && counts.size() > 1) {
                tip += divergent ? QStringLiteral("\nConsensus: %1 (DIVERGENT)").arg(majorityStr)
                                 : QStringLiteral("\nMatches consensus (%1)").arg(majorityStr);
            }
            it->setToolTip(tip);
        }
    }
}

/**
 * @brief Returns active table canvas background color according to the theme.
 */
QColor DifferentialGrid::activeCanvasBackgroundColor() const {
    return palette().color(QPalette::Base);
}

/**
 * @brief Reloads widget styling according to the active theme palette.
 */
void DifferentialGrid::reloadStyle() {
    auto& tm = gridlock::core::ThemeManager::instance();
    const auto pal = tm.paletteForTheme(tm.activeThemeId());
    const QColor canvasColor(pal.canvasHex);
    const QColor headerColor(pal.headerHex);
    const QColor textColor(pal.textHex);
    const QColor altColor = pal.isDark ? QColor(0x18, 0x1b, 0x20) : QColor(0xf5, 0xef, 0xdc);

    QPalette p = palette();
    p.setColor(QPalette::Base, canvasColor);
    p.setColor(QPalette::AlternateBase, altColor);
    p.setColor(QPalette::Window, canvasColor);
    p.setColor(QPalette::Button, headerColor);
    p.setColor(QPalette::Text, textColor);
    p.setColor(QPalette::WindowText, textColor);
    setPalette(p);

    if (viewport()) {
        viewport()->setAutoFillBackground(true);
        viewport()->setPalette(p);
    }
    if (verticalHeader()) {
        verticalHeader()->setAutoFillBackground(true);
        verticalHeader()->setPalette(p);
        if (verticalHeader()->viewport()) {
            verticalHeader()->viewport()->setAutoFillBackground(true);
            verticalHeader()->viewport()->setPalette(p);
        }
    }
    if (horizontalHeader()) {
        horizontalHeader()->setAutoFillBackground(true);
        horizontalHeader()->setPalette(p);
    }

    setStyleSheet(QStringLiteral(
        "QTableWidget, QTableView { background-color: %1; alternate-background-color: %2; "
        "color: %3; border: none; gridline-color: %4; } "
        "QHeaderView, QHeaderView:vertical, QHeaderView:horizontal { background-color: %1; border: none; } "
        "QHeaderView::section { background-color: %5; color: %3; padding: 2px 6px; "
        "height: 20px; min-height: 20px; max-height: 22px; border: 0px; "
        "border-right: 1px solid %4; border-bottom: 1px solid %4; font-size: 10px; font-weight: 600; } "
        "QTableCornerButton::section { background-color: %5; border: 0px; border-right: 1px solid %4; border-bottom: 1px solid %4; }")
        .arg(pal.canvasHex, altColor.name(), pal.textHex, pal.borderHex, pal.headerHex));

    updateHighlights();
    update();
}

}  // namespace gridlock::ui

