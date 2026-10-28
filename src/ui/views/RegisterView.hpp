#pragma once

/**
 * @file RegisterView.hpp
 * @brief CPU register inspector widget displaying hardware state and mutations.
 *
 * Provides a tabular view of architecture registers (e.g., x86_64 general purpose,
 * instruction pointer, stack pointer, and flags), tracking mutations between steps
 * and providing interactive navigation when double-clicking or activating addresses.
 */

#include <QJsonArray>
#include <QMap>
#include <QSet>
#include <QString>
#include <QTableWidget>
#include <QWidget>
#include <cstdint>
#include "../../RankState.hpp"

namespace gridlock::ui {

/**
 * @class RegisterView
 * @brief Tabular visualizer for CPU registers across debugged MPI ranks.
 *
 * Displays register names and their current values in hexadecimal format.
 * Mutated values are visually highlighted in red, and registers holding memory
 * addresses can be activated to dispatch address seeks to disassembly or hex views.
 */
class RegisterView : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief Constructs a new RegisterView widget.
     * @param parent Optional parent QWidget.
     */
    explicit RegisterView(QWidget* parent = nullptr);

    /**
     * @brief Destructor.
     */
    ~RegisterView() override = default;

    /**
     * @brief Returns the count of registers modified during the last step.
     * @return Number of modified registers.
     */
    int changedRegisterCount() const { return m_modifiedRegisters.size(); }

    /**
     * @brief Checks if a specific register was modified during the last execution step.
     * @param name Register identifier (e.g. "RAX", "RIP").
     * @return True if register mutated, false otherwise.
     */
    bool isRegisterModified(const QString& name) const;

    /**
     * @brief Simulates clicking or activating a register row by name.
     * @param name Register identifier.
     * @return True if register was found and activated, false otherwise.
     */
    bool activateRegister(const QString& name);

signals:
    /**
     * @brief Emitted when a register holding a valid memory pointer is activated.
     * @param address 64-bit target address.
     */
    void addressActivated(uint64_t address);

public slots:
    /**
     * @brief Updates register table from rank state telemetry.
     * @param state The current RankState snapshot.
     */
    void updateRegisters(const gridlock::RankState& state);

    /**
     * @brief Updates register table from a JSON array of register values.
     * @param registers Array containing register objects or value strings.
     */
    void updateRegisters(const QJsonArray& registers);

    /**
     * @brief Updates register table from a key-value mapping.
     * @param registers Map of register names to hexadecimal value strings.
     */
    void updateRegisters(const QMap<QString, QString>& registers);

private:
    /**
     * @brief Populates the QTableWidget with ordered name/value pairs.
     * @param orderedRegs List of register name and value pairs.
     */
    void applyRegisterMap(const QList<QPair<QString, QString>>& orderedRegs);

    /**
     * @brief Formats a raw register string into canonical hex format.
     * @param val Raw value string.
     * @return Canonical hexadecimal representation (e.g. "0x0000000000401120").
     */
    static QString normalizeRegValue(const QString& val);

    /**
     * @brief Maps standard x86 register index to its conventional name.
     * @param idx Numeric index.
     * @return Conventional register name.
     */
    static QString x86RegisterNameForIndex(int idx);

    /// Monospace tabular register display widget.
    QTableWidget* m_table = nullptr;

    /// Previous register snapshot used for delta/mutation comparison.
    QMap<QString, QString> m_previousRegisters;

    /// Current register state.
    QMap<QString, QString> m_currentRegisters;

    /// Set of register names whose values changed in the most recent update.
    QSet<QString> m_modifiedRegisters;
};

}  // namespace gridlock::ui

