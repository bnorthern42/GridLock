/**
 * @file HpcSettingsPages.hpp
 * @brief Declarations of HpcSettingsPage and HpcIntegrationSettingsPage widgets.
 */

#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QString>
#include <QTextEdit>
#include <QWidget>

namespace gridlock::ui {

/**
 * @class HpcSettingsPage
 * @brief Configuration panel for cluster hostfiles, environment variables, and node pinning.
 */
class HpcSettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit HpcSettingsPage(QWidget* parent = nullptr);

    QString hostsFile() const;
    QString envVars() const;
    bool strictAffinity() const;
    bool enableSlurmMenu() const;

    void loadFromSettings();

private:
    QLineEdit* m_hostsFileEdit = nullptr;
    QTextEdit* m_envVarsEdit = nullptr;
    QCheckBox* m_strictAffinityBox = nullptr;
    QCheckBox* m_enableSlurmMenuBox = nullptr;
    QLabel* m_slurmWarningLabel = nullptr;
};

/**
 * @class HpcIntegrationSettingsPage
 * @brief Configuration panel for remote SSH connections, SLURM templates, and Spack roots.
 */
class HpcIntegrationSettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit HpcIntegrationSettingsPage(QWidget* parent = nullptr);

    // SSH settings
    QString sshHost() const;
    QString sshUser() const;
    QString sshKeyPath() const;

    // SLURM batch settings
    QString slurmTemplate() const;
    QString slurmPartition() const;
    int slurmNodes() const;
    int slurmTasks() const;
    bool requestGpus() const;
    int gpusPerNode() const;

    // SLURM batch script auto-detection and directive accessors
    bool parseSlurmBatchScript(const QString& scriptText);
    bool loadSlurmBatchScriptFile(const QString& filePath);
    QString generateSlurmBatchScript() const;
    QString slurmFieldValue(const QString& key) const;

    // Spack environment
    QString spackRoot() const;

    void loadFromSettings();

private:
    QLineEdit* m_sshHostEdit = nullptr;
    QLineEdit* m_sshUserEdit = nullptr;
    QLineEdit* m_sshKeyEdit = nullptr;

    // 16-Directive SLURM Batch Configuration controls
    QLineEdit* m_sbatchFilePathEdit = nullptr;
    QLineEdit* m_jobNameEdit = nullptr;
    QLineEdit* m_accountEdit = nullptr;
    QComboBox* m_partitionCombo = nullptr;
    QComboBox* m_qosCombo = nullptr;
    QComboBox* m_timeCombo = nullptr;
    QSpinBox* m_nodesBox = nullptr;
    QSpinBox* m_tasksBox = nullptr;
    QSpinBox* m_cpusPerTaskBox = nullptr;
    QComboBox* m_memCombo = nullptr;
    QCheckBox* m_gpuCheck = nullptr;
    QLineEdit* m_gpuTypeEdit = nullptr;
    QSpinBox* m_gpusBox = nullptr;
    QLineEdit* m_constraintEdit = nullptr;
    QComboBox* m_mpiLauncherCombo = nullptr;
    QLineEdit* m_modulesEdit = nullptr;
    QLineEdit* m_outputLogEdit = nullptr;
    QComboBox* m_mailTypeCombo = nullptr;
    QLineEdit* m_mailUserEdit = nullptr;
    QPlainTextEdit* m_templateEdit = nullptr;
    QLineEdit* m_spackRootEdit = nullptr;
};

}  // namespace gridlock::ui
