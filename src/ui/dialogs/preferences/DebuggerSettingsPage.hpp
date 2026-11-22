/**
 * @file DebuggerSettingsPage.hpp
 * @brief Declaration of the Debugger settings page widget.
 */

#pragma once

#include <QCheckBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QString>
#include <QWidget>

namespace gridlock::ui {

/**
 * @class DebuggerSettingsPage
 * @brief Configuration panel for GDB, DAP adapter, Clangd LSP, and MPI launcher flags.
 */
class DebuggerSettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit DebuggerSettingsPage(QWidget* parent = nullptr);

    QString clangdPath() const;
    QString dapAdapterPath() const;
    QString gdbPath() const;
    QString mpiExecutable() const;
    QString mpiArgs() const;
    int defaultRanks() const;
    bool trapFpe() const;

    void loadFromSettings();

private:
    QLineEdit* m_clangdPathEdit = nullptr;
    QLineEdit* m_dapAdapterEdit = nullptr;
    QLineEdit* m_gdbPathEdit = nullptr;
    QLineEdit* m_mpiExecEdit = nullptr;
    QLineEdit* m_mpiArgsEdit = nullptr;
    QSpinBox* m_rankBox = nullptr;
    QCheckBox* m_trapFpeCheck = nullptr;
};

}  // namespace gridlock::ui
