#pragma once
#include <QJsonArray>
#include <QMap>
#include <QSet>
#include <QString>
#include <QTableWidget>
#include <QWidget>
#include <cstdint>
#include "../../RankState.hpp"

namespace gridlock::ui {

class RegisterView : public QWidget {
    Q_OBJECT
public:
    explicit RegisterView(QWidget* parent = nullptr);
    ~RegisterView() override = default;

    int changedRegisterCount() const { return m_modifiedRegisters.size(); }
    bool isRegisterModified(const QString& name) const;
    bool activateRegister(const QString& name);

signals:
    void addressActivated(uint64_t address);

public slots:
    void updateRegisters(const gridlock::RankState& state);
    void updateRegisters(const QJsonArray& registers);
    void updateRegisters(const QMap<QString, QString>& registers);

private:
    void applyRegisterMap(const QList<QPair<QString, QString>>& orderedRegs);
    static QString normalizeRegValue(const QString& val);
    static QString x86RegisterNameForIndex(int idx);

    QTableWidget* m_table = nullptr;
    QMap<QString, QString> m_previousRegisters;
    QMap<QString, QString> m_currentRegisters;
    QSet<QString> m_modifiedRegisters;
};

} // namespace gridlock::ui
