#include "RegisterView.hpp"
#include <QFont>
#include <QHeaderView>
#include <QJsonObject>
#include <QJsonValue>
#include <QVBoxLayout>
#include "../../core/managers/ConfigManager.hpp"

namespace gridlock::ui {

QString RegisterView::x86RegisterNameForIndex(int idx) {
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

QString RegisterView::normalizeRegValue(const QString& val) {
    QString trimmed = val.trimmed();
    int spaceIdx = trimmed.indexOf(' ');
    if (spaceIdx > 0) {
        trimmed = trimmed.left(spaceIdx);
    }
    bool ok = false;
    QString hexCandidate = trimmed;
    if (hexCandidate.startsWith("0x", Qt::CaseInsensitive)) {
        hexCandidate = hexCandidate.mid(2);
    }
    uint64_t parsed = hexCandidate.toULongLong(&ok, 16);
    if (ok) {
        return QStringLiteral("0x%1").arg(parsed, 0, 16);
    }
    return trimmed.toLower();
}

RegisterView::RegisterView(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_table = new QTableWidget(this);
    m_table->setObjectName("cutterRegisterTable");
    m_table->setColumnCount(2);
    m_table->setHorizontalHeaderLabels({"REG", "VALUE / ROLE"});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setDefaultSectionSize(76);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(20);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setShowGrid(true);

    QFont monoFont("JetBrains Mono", 9);
    monoFont.setStyleHint(QFont::Monospace);
    m_table->setFont(monoFont);

    QPalette p = m_table->palette();
    p.setColor(QPalette::Base, QColor(gridlock::core::ConfigManager::instance().getSourceBackground()));
    p.setColor(QPalette::Text, QColor(gridlock::core::ConfigManager::instance().getSourceText()));
    m_table->setPalette(p);

    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int /*col*/) {
        if (QTableWidgetItem* nameItem = m_table->item(row, 0)) {
            activateRegister(nameItem->text());
        }
    });

    layout->addWidget(m_table);

    // Seed with a realistic x86_64 MPI startup register state
    const QList<QPair<QString, QString>> seedRegs = {
        {QStringLiteral("rip"), QStringLiteral("0x00401120 <main+0>")},
        {QStringLiteral("rsp"), QStringLiteral("0x7fffffffe480")},
        {QStringLiteral("rbp"), QStringLiteral("0x7fffffffe4b0")},
        {QStringLiteral("rax"), QStringLiteral("0x0000000000000000")},
        {QStringLiteral("rbx"), QStringLiteral("0x0000000000000000")},
        {QStringLiteral("rcx"), QStringLiteral("0x00007ffff7dc45a0")},
        {QStringLiteral("rdx"), QStringLiteral("0x00007fffffffe5b8")},
        {QStringLiteral("rdi"), QStringLiteral("0x0000000000000001")},
        {QStringLiteral("rsi"), QStringLiteral("0x00007fffffffe5a8")},
        {QStringLiteral("r8"),  QStringLiteral("0x00007ffff7fb1400")},
        {QStringLiteral("r9"),  QStringLiteral("0x00007ffff7fc9040")},
        {QStringLiteral("eflags"), QStringLiteral("0x00000246 [ PF ZF IF ]")}
    };
    applyRegisterMap(seedRegs);
    // Reset diff baseline after seeding so the first live/test update establishes the baseline
    m_previousRegisters.clear();
    m_modifiedRegisters.clear();
}

bool RegisterView::isRegisterModified(const QString& name) const {
    return m_modifiedRegisters.contains(name.trimmed().toLower());
}

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

void RegisterView::applyRegisterMap(const QList<QPair<QString, QString>>& orderedRegs) {
    if (orderedRegs.isEmpty()) {
        return;
    }

    const bool hasBaseline = !m_previousRegisters.isEmpty();
    m_modifiedRegisters.clear();
    m_currentRegisters.clear();

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

        QTableWidgetItem* valItem = m_table->item(row, 1);
        if (!valItem) {
            valItem = new QTableWidgetItem(regVal);
            m_table->setItem(row, 1, valItem);
        } else {
            valItem->setText(regVal);
        }

        // Role-based Cutter color classification
        QColor roleColor("#d4d7dc");
        if (key == QStringLiteral("rip") || key == QStringLiteral("eip") || key == QStringLiteral("pc")) {
            roleColor = QColor("#4ec9b0"); // Instruction pointer: Cutter teal
        } else if (key == QStringLiteral("rsp") || key == QStringLiteral("rbp") ||
                   key == QStringLiteral("esp") || key == QStringLiteral("ebp")) {
            roleColor = QColor("#569cd6"); // Stack frame pointers: Cutter blue
        } else if (key.contains(QStringLiteral("flags"))) {
            roleColor = QColor("#dcdcaa"); // Status flags: Cutter gold
        }

        if (changed) {
            nameItem->setForeground(QColor("#f44747"));
            valItem->setForeground(QColor("#f44747"));
            nameItem->setBackground(QColor(0x2d, 0x1b, 0x1e));
            valItem->setBackground(QColor(0x2d, 0x1b, 0x1e));
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
}

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

void RegisterView::updateRegisters(const QMap<QString, QString>& registers) {
    if (registers.isEmpty()) {
        return;
    }

    QList<QPair<QString, QString>> ordered;
    // Place rip, rsp, rbp first if present for Cutter ergonomics
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

} // namespace gridlock::ui
