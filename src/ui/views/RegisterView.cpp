/**
 * @file RegisterView.cpp
 * @brief CPU register inspector implementation with mutation tracking.
 *
 * Implements the RegisterView widget displaying architectural CPU registers
 * with color-coded role designations (instruction pointer, stack frames, status flags)
 * and differential mutation detection across stepping events.
 */

#include "RegisterView.hpp"
#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonObject>
#include <QJsonValue>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextOption>
#include <QVBoxLayout>
#include "../../core/managers/ConfigManager.hpp"
#include "../../core/managers/ThemeManager.hpp"

namespace gridlock::ui {

/**
 * @brief Resolves a numeric hardware register index to its standard x86_64 mnemonic.
 *
 * @param idx Register index from the debugger/paged memory backend.
 * @return Register mnemonic (e.g. "rax", "rip", "rsp").
 */
QString RegisterView::x86RegisterNameForIndex(int idx) {
    // Lookup table for standard 64-bit general purpose and segment registers
    static const QMap<int, QString> kRegNames = {
        {0, QStringLiteral("rax")},    {1, QStringLiteral("rbx")},
        {2, QStringLiteral("rcx")},    {3, QStringLiteral("rdx")},
        {4, QStringLiteral("rsi")},    {5, QStringLiteral("rdi")},
        {6, QStringLiteral("rbp")},    {7, QStringLiteral("rsp")},
        {8, QStringLiteral("r8")},     {9, QStringLiteral("r9")},
        {10, QStringLiteral("r10")},   {11, QStringLiteral("r11")},
        {12, QStringLiteral("r12")},   {13, QStringLiteral("r13")},
        {14, QStringLiteral("r14")},   {15, QStringLiteral("r15")},
        {16, QStringLiteral("rip")},   {17, QStringLiteral("eflags")},
        {18, QStringLiteral("cs")},    {19, QStringLiteral("ss")},
        {20, QStringLiteral("ds")},    {21, QStringLiteral("es")},
        {22, QStringLiteral("fs")},    {23, QStringLiteral("gs")}
    };
    return kRegNames.value(idx, QStringLiteral("r%1").arg(idx));
}

/**
 * @brief Normalizes diverse register value representations into a canonical hexadecimal string.
 *
 * Trims extra symbols, strips comments/symbol names, and converts to lowercase hex.
 *
 * @param val Raw register value string (e.g. "0x00401120 <main+0>").
 * @return Canonical hex string (e.g. "0x401120") or lowercase trimmed string.
 */
QString RegisterView::normalizeRegValue(const QString& val) {
    QString trimmed = val.trimmed();
    int spaceIdx = trimmed.indexOf(' ');
    if (spaceIdx > 0) trimmed = trimmed.left(spaceIdx);
    bool ok = false;
    QString hexCandidate = trimmed;
    if (hexCandidate.startsWith("0x", Qt::CaseInsensitive)) hexCandidate = hexCandidate.mid(2);
    uint64_t parsed = hexCandidate.toULongLong(&ok, 16);
    if (ok) return QStringLiteral("0x%1").arg(parsed, 0, 16);
    return trimmed.toLower();
}

/**
 * @brief Constructs a new RegisterView instance.
 *
 * Initializes the two-column QTableWidget ("REG", "VALUE / ROLE"), configures
 * monospace styling, and installs cell double-click navigation and inline edit handlers.
 *
 * @param parent Optional parent widget.
 */
RegisterView::RegisterView(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Compact live search bar filtering register names, hex values, and decimal equivalents.
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setObjectName(QStringLiteral("registerSearchEdit"));
    m_searchEdit->setPlaceholderText(QStringLiteral("Filter registers by name or hex/dec value..."));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setFixedHeight(24);
    layout->addWidget(m_searchEdit);

    // Create borderless monospace table widget with alternating rows, sorting, and Interactive columns.
    m_table = new QTableWidget(this);
    m_table->setObjectName("cutterRegisterTable");
    m_table->setFrameShape(QFrame::NoFrame);
    m_table->setAlternatingRowColors(true);
    m_table->setColumnCount(2);
    m_table->setHorizontalHeaderLabels({"REG", "VALUE / ROLE"});
    m_table->setSortingEnabled(true);
    m_table->horizontalHeader()->setVisible(true);
    m_table->horizontalHeader()->setMinimumSectionSize(48);
    m_table->horizontalHeader()->setSectionsMovable(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_table->setColumnWidth(0, 84);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(20);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setShowGrid(true);

    QFont monoFont("JetBrains Mono", 9);
    monoFont.setStyleHint(QFont::Monospace);
    m_table->setFont(monoFont);

    // Synchronize search bar and table palette with active ThemeManager colors.
    auto syncTheme = [this]() {
        auto& tm = gridlock::core::managers::ThemeManager::instance();
        const auto pal = tm.paletteForTheme(tm.activeThemeId());
        if (m_searchEdit) {
            m_searchEdit->setStyleSheet(QStringLiteral(
                "QLineEdit { background-color: %1; color: %2; border: 1px solid %3; "
                "padding: 1px 6px; font-size: 11px; } QLineEdit:focus { border: 1px solid %4; }")
                .arg(pal.headerHex, pal.textHex, pal.borderHex, pal.accentHex));
        }
        QPalette p = m_table->palette();
        p.setColor(QPalette::Base, QColor(pal.canvasHex));
        p.setColor(QPalette::Text, QColor(pal.textHex));
        m_table->setPalette(p);
    };
    syncTheme();

    // Filter rows live whenever the search query text changes.
    connect(m_searchEdit, &QLineEdit::textChanged, this, &RegisterView::filterRegisters);

    // Double-clicking column 0 seeks to address; double-clicking column 1 starts inline register editing.
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int col) {
        if (col == 0) {
            if (QTableWidgetItem* nameItem = m_table->item(row, 0)) activateRegister(nameItem->text());
        } else if (col == 1 && m_table->item(row, 1)) {
            m_table->editItem(m_table->item(row, 1));
        }
    });

    // Commit inline cell edits on column 1 and emit registerEditRequested.
    connect(m_table, &QTableWidget::cellChanged, this, [this](int row, int col) {
        if (m_updatingTable || col != 1) return;
        const QString regName = m_table->item(row, 0) ? m_table->item(row, 0)->text().trimmed().toLower() : QString();
        const QString rawVal = m_table->item(row, 1) ? m_table->item(row, 1)->text().trimmed() : QString();
        if (regName.isEmpty() || rawVal.isEmpty()) return;
        setRegisterValue(regName, rawVal);
    });

    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QTableWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        const QModelIndex idx = m_table->indexAt(pos);
        if (!idx.isValid()) return;
        const int row = idx.row();
        const QString regName = m_table->item(row, 0) ? m_table->item(row, 0)->text().trimmed() : QString();
        const QString fullVal = m_table->item(row, 1) ? m_table->item(row, 1)->text().trimmed() : QString();
        const QString regVal = fullVal.section(QLatin1Char(' '), 0, 0).trimmed();
        if (regVal.isEmpty()) return;
        QMenu menu(this);
        QAction* editAct = menu.addAction(QStringLiteral("Edit Register Value..."));
        QAction* inspectAct = menu.addAction(QStringLiteral("Inspect Full Value..."));
        menu.addSeparator();
        QAction* calcAct = menu.addAction(QStringLiteral("Open Register in Programmer's Calculator"));
        QAction* seekAct = menu.addAction(QStringLiteral("Seek to Register Address"));
        QAction* chosen = menu.exec(m_table->viewport()->mapToGlobal(pos));
        if (chosen == editAct && m_table->item(row, 1)) {
            m_table->editItem(m_table->item(row, 1));
        } else if (chosen == inspectAct) {
            inspectFullValue(QStringLiteral("$%1 (Rank %2)").arg(regName).arg(m_focusedRank), fullVal);
        } else if (chosen == calcAct) {
            emit sendToCalculatorRequested(regVal);
            if (QWidget* win = window()) {
                QMetaObject::invokeMethod(win, "openProgrammerCalculatorWithText", Q_ARG(QString, regVal));
            }
        } else if (chosen == seekAct) {
            activateRegister(regName);
        }
    });

    layout->addWidget(m_table, 1);
    clearRegisters();

    connect(&gridlock::core::managers::ThemeManager::instance(),
            &gridlock::core::managers::ThemeManager::themeModeChanged, this, [this, syncTheme](bool) {
                syncTheme();
                if (!m_currentRegisters.isEmpty()) updateRegisters(m_currentRegisters);
            });
}

bool RegisterView::setRegisterValue(const QString& regName, const QString& newValue) {
    QString key = regName.trimmed().toLower();
    if (key.startsWith(QLatin1Char('$'))) key = key.mid(1).trimmed();
    const QString cleanVal = newValue.trimmed().section(QLatin1Char(' '), 0, 0).trimmed();
    if (key.isEmpty() || cleanVal.isEmpty()) return false;
    m_previousRegisters.insert(key, normalizeRegValue(m_currentRegisters.value(key)));
    m_currentRegisters.insert(key, cleanVal);
    m_modifiedRegisters.insert(key);
    if (m_table && !m_updatingTable) {
        m_updatingTable = true;
        for (int r = 0; r < m_table->rowCount(); ++r) {
            if (m_table->item(r, 0) && m_table->item(r, 0)->text().compare(key, Qt::CaseInsensitive) == 0) {
                if (m_table->item(r, 1)) m_table->item(r, 1)->setText(cleanVal);
                break;
            }
        }
        m_updatingTable = false;
    }
    emit registerEditRequested(m_focusedRank, key, cleanVal);
    return true;
}

QDialog* RegisterView::inspectFullValue(const QString& title, const QString& fullText) {
    m_lastInspectedFullValue = fullText;
    auto* dlg = findChild<QDialog*>(QStringLiteral("fullValueInspectorDialog"));
    if (!dlg) {
        dlg = new QDialog(this);
        dlg->setObjectName(QStringLiteral("fullValueInspectorDialog"));
        dlg->resize(520, 240);
        auto* v = new QVBoxLayout(dlg);
        v->setContentsMargins(10, 10, 10, 10);
        v->setSpacing(8);
        auto* te = new QPlainTextEdit(dlg);
        te->setObjectName(QStringLiteral("fullValueInspectorTextEdit"));
        te->setReadOnly(true);
        te->setWordWrapMode(QTextOption::WrapAnywhere);
        QFont mf(QStringLiteral("JetBrains Mono"), 10);
        mf.setStyleHint(QFont::Monospace);
        te->setFont(mf);
        v->addWidget(te, 1);
        auto* hb = new QHBoxLayout();
        hb->addStretch(1);
        auto* copyBtn = new QPushButton(QStringLiteral("Copy to Clipboard"), dlg);
        copyBtn->setObjectName(QStringLiteral("fullValueInspectorCopyBtn"));
        auto* closeBtn = new QPushButton(QStringLiteral("Close"), dlg);
        closeBtn->setObjectName(QStringLiteral("fullValueInspectorCloseBtn"));
        connect(copyBtn, &QPushButton::clicked, dlg, [te]() { QApplication::clipboard()->setText(te->toPlainText()); });
        connect(closeBtn, &QPushButton::clicked, dlg, &QDialog::close);
        hb->addWidget(copyBtn);
        hb->addWidget(closeBtn);
        v->addLayout(hb);
    }
    dlg->setWindowTitle(QStringLiteral("Inspect Full Value — %1").arg(title));
    if (auto* te = dlg->findChild<QPlainTextEdit*>(QStringLiteral("fullValueInspectorTextEdit"))) te->setPlainText(fullText);
    dlg->show();
    return dlg;
}

/**
 * @brief Checks if a named register underwent mutation in the last update.
 *
 * @param name Register identifier (e.g., "rax", "rip").
 * @return True if register mutated, false otherwise.
 */
bool RegisterView::isRegisterModified(const QString& name) const {
    return m_modifiedRegisters.contains(name.trimmed().toLower());
}

/**
 * @brief Activates a register by parsing its address and emitting addressActivated.
 *
 * @param name Register identifier.
 * @return True if register contained a valid 64-bit hex address; false otherwise.
 */
bool RegisterView::activateRegister(const QString& name) {
    const QString key = name.trimmed().toLower();
    if (!m_currentRegisters.contains(key)) {
        return false;
    }
    bool ok = false;
    QString valStr = m_currentRegisters.value(key).trimmed();
    if (valStr.startsWith("0x", Qt::CaseInsensitive)) {
        valStr = valStr.mid(2);
    }
    const int spaceIdx = valStr.indexOf(' ');
    if (spaceIdx > 0) {
        valStr = valStr.left(spaceIdx);
    }
    const uint64_t addr = valStr.toULongLong(&ok, 16);
    if (!ok) {
        return false;
    }
    emit addressActivated(addr);
    return true;
}

/**
 * @brief Updates table items from an ordered list of register names and values.
 */
void RegisterView::applyRegisterMap(const QList<QPair<QString, QString>>& orderedRegs) {
    if (orderedRegs.isEmpty()) {
        return;
    }
    m_updatingTable = true;

    const bool isDark = gridlock::core::managers::ThemeManager::instance().isDarkMode();
    QPalette regPal = m_table->palette();
    regPal.setColor(QPalette::Base, isDark ? QColor("#16181d") : QColor("#ffffff"));
    regPal.setColor(QPalette::Text, isDark ? QColor("#d7dae0") : QColor("#1f2328"));
    m_table->setPalette(regPal);

    const bool hasBaseline = !m_previousRegisters.isEmpty();
    m_modifiedRegisters.clear();
    m_currentRegisters.clear();

    const bool wasSorting = m_table->isSortingEnabled();
    m_table->setSortingEnabled(false);

    if (m_table->rowCount() != orderedRegs.size()) {
        m_table->setRowCount(orderedRegs.size());
    }

    QFont regularFont = m_table->font();
    QFont boldFont = regularFont;
    boldFont.setBold(true);

    for (int row = 0; row < orderedRegs.size(); ++row) {
        const QString rawName = orderedRegs[row].first.trimmed();
        const QString key = rawName.toLower();
        const QString regVal = orderedRegs[row].second.trimmed();
        const QString normVal = normalizeRegValue(regVal);

        bool changed = false;
        if (hasBaseline && m_previousRegisters.contains(key) &&
            m_previousRegisters.value(key) != normVal) {
            changed = true;
            m_modifiedRegisters.insert(key);
        }

        m_previousRegisters.insert(key, normVal);
        m_currentRegisters.insert(key, regVal);

        QTableWidgetItem* nameItem = m_table->item(row, 0);
        if (!nameItem) {
            nameItem = new QTableWidgetItem(key);
            m_table->setItem(row, 0, nameItem);
        } else {
            nameItem->setText(key);
        }
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);

        QTableWidgetItem* valItem = m_table->item(row, 1);
        if (!valItem) {
            valItem = new QTableWidgetItem(regVal);
            m_table->setItem(row, 1, valItem);
        } else {
            valItem->setText(regVal);
        }
        valItem->setFlags(valItem->flags() | Qt::ItemIsEditable);

        QColor roleColor = isDark ? QColor("#d4d7dc") : QColor("#1f2328");
        if (key == QStringLiteral("rip") || key == QStringLiteral("eip") || key == QStringLiteral("pc")) {
            roleColor = isDark ? QColor("#4ec9b0") : QColor("#116329");
        } else if (key == QStringLiteral("rsp") || key == QStringLiteral("rbp") ||
                   key == QStringLiteral("esp") || key == QStringLiteral("ebp")) {
            roleColor = isDark ? QColor("#569cd6") : QColor("#0550ae");
        } else if (key.contains(QStringLiteral("flags"))) {
            roleColor = isDark ? QColor("#dcdcaa") : QColor("#953800");
        }

        if (changed) {
            const QColor diffFg = isDark ? QColor("#f44747") : QColor("#cf222e");
            const QColor diffBg = isDark ? QColor(0x2d, 0x1b, 0x1e) : QColor(0xff, 0xeb, 0xe9);
            nameItem->setForeground(diffFg);
            valItem->setForeground(diffFg);
            nameItem->setBackground(diffBg);
            valItem->setBackground(diffBg);
            nameItem->setFont(boldFont);
            valItem->setFont(boldFont);
        } else {
            nameItem->setForeground(roleColor);
            valItem->setForeground(roleColor);
            nameItem->setBackground(Qt::NoBrush);
            valItem->setBackground(Qt::NoBrush);
            nameItem->setFont(regularFont);
            valItem->setFont(regularFont);
        }
    }

    m_table->setSortingEnabled(wasSorting);
    m_updatingTable = false;
    filterRegisters(m_searchEdit ? m_searchEdit->text() : QString());
}

/**
 * @brief Filters visible register rows against @p filterText (name, hex, or decimal value).
 */
void RegisterView::filterRegisters(const QString& filterText) {
    if (!m_table) {
        return;
    }
    const QString q = filterText.trimmed();
    for (int r = 0; r < m_table->rowCount(); ++r) {
        if (q.isEmpty()) {
            m_table->setRowHidden(r, false);
            continue;
        }
        const QString regName = m_table->item(r, 0) ? m_table->item(r, 0)->text() : QString();
        const QString regVal = m_table->item(r, 1) ? m_table->item(r, 1)->text() : QString();
        bool match = regName.contains(q, Qt::CaseInsensitive) || regVal.contains(q, Qt::CaseInsensitive);
        if (!match && !regVal.isEmpty()) {
            // Also check unsigned decimal value when the register holds a hex address/integer.
            const QString normHex = normalizeRegValue(regVal);
            bool ok = false;
            const qulonglong decVal = normHex.startsWith(QLatin1String("0x"))
                                          ? normHex.mid(2).toULongLong(&ok, 16)
                                          : normHex.toULongLong(&ok, 16);
            if (ok && QString::number(decVal).contains(q)) {
                match = true;
            }
        }
        m_table->setRowHidden(r, !match);
    }
}

/**
 * @brief Slot to update registers from a RankState telemetry snapshot.
 * @param state RankState struct containing integer-keyed register map.
 */
void RegisterView::updateRegisters(const gridlock::RankState& state) {
    if (state.registers.isEmpty()) {
        return;
    }

    QList<QPair<QString, QString>> ordered;
    for (auto it = state.registers.constBegin(); it != state.registers.constEnd(); ++it) {
        ordered.append({x86RegisterNameForIndex(it.key()), it.value()});
    }
    applyRegisterMap(ordered);
}

/**
 * @brief Slot to update registers from a QJsonArray of register objects.
 * @param registers JSON array of objects with "name" and "value" keys.
 */
void RegisterView::updateRegisters(const QJsonArray& registers) {
    if (registers.isEmpty()) {
        return;
    }

    QList<QPair<QString, QString>> ordered;
    ordered.reserve(registers.size());
    for (int row = 0; row < registers.size(); ++row) {
        QJsonObject reg = registers[row].toObject();
        ordered.append({reg["name"].toString(), reg["value"].toString()});
    }
    applyRegisterMap(ordered);
}

/**
 * @brief Slot to update registers from a string-keyed QMap.
 *
 * Orders critical architectural registers (RIP, RSP, RBP) first for optimal inspection ergonomics.
 *
 * @param registers Map of register names to string values.
 */
void RegisterView::updateRegisters(const QMap<QString, QString>& registers) {
    if (registers.isEmpty()) {
        return;
    }

    QList<QPair<QString, QString>> ordered;
    // Place rip, rsp, rbp first if present for inspection ergonomics
    const QStringList priorityKeys = {QStringLiteral("rip"), QStringLiteral("rsp"), QStringLiteral("rbp")};
    QSet<QString> seen;
    for (const QString& pk : priorityKeys) {
        for (auto it = registers.constBegin(); it != registers.constEnd(); ++it) {
            if (it.key().compare(pk, Qt::CaseInsensitive) == 0) {
                ordered.append({it.key().toLower(), it.value()});
                seen.insert(it.key().toLower());
            }
        }
    }
    for (auto it = registers.constBegin(); it != registers.constEnd(); ++it) {
        const QString k = it.key().toLower();
        if (!seen.contains(k)) {
            ordered.append({k, it.value()});
            seen.insert(k);
        }
    }
    applyRegisterMap(ordered);
}

void RegisterView::clearRegisters() {
    // Reset all cached register snapshots and clear table rows for idle state.
    m_previousRegisters.clear();
    m_currentRegisters.clear();
    m_modifiedRegisters.clear();
    if (m_table) {
        m_table->setRowCount(0);
    }
}

void RegisterView::seedPreviewRegisters() {
    // Populate realistic baseline x86_64 registers exclusively for preview fixtures.
    const QMap<QString, QString> seedRegs = {
        {QStringLiteral("rax"), QStringLiteral("0x0000000000000000")},
        {QStringLiteral("rbx"), QStringLiteral("0x0000000000000001")},
        {QStringLiteral("rcx"), QStringLiteral("0x00007ffff7dc45a0")},
        {QStringLiteral("rdx"), QStringLiteral("0x00007fffffffe598")},
        {QStringLiteral("rsi"), QStringLiteral("0x00007fffffffe588")},
        {QStringLiteral("rdi"), QStringLiteral("0x0000000000000001")},
        {QStringLiteral("rbp"), QStringLiteral("0x00007fffffffe4a0")},
        {QStringLiteral("rsp"), QStringLiteral("0x00007fffffffe480")},
        {QStringLiteral("rip"), QStringLiteral("0x00401120 <main+0>")},
        {QStringLiteral("eflags"), QStringLiteral("0x00000246 [ PF ZF IF ]")},
        {QStringLiteral("r8"), QStringLiteral("0x0000000000000000")},
        {QStringLiteral("r9"), QStringLiteral("0x00007ffff7fe3b80")}};
    m_previousRegisters.clear();
    m_modifiedRegisters.clear();
    updateRegisters(seedRegs);
}

}  // namespace gridlock::ui

