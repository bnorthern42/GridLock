/**
 * @file HpcSettingsPages.hpp
 * @brief Declarations of HpcSettingsPage and HpcIntegrationSettingsPage widgets.
 */

#pragma once

#include "../../../core/managers/ConfigManager.hpp"

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
 * @brief Configuration panel for remote SSH connections, SLURM templates, tool paths, and Spack roots.
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
    QString jobName() const;
    QString account() const;
    QString qos() const;
    QString wallTime() const;
    int cpusPerTask() const;
    QString memory() const;
    QString gpuType() const;
    QString constraint() const;
    QString mpiLauncher() const;
    QString modules() const;
    QString outputLog() const;
    QString errorLog() const;
    QString mailType() const;
    QString mailUser() const;
    QString scriptPath() const;
    QString sbatchPath() const;
    QString squeuePath() const;
    QString scontrolPath() const;
    gridlock::core::SlurmSettings toSlurmSettings() const;

    // SLURM batch script auto-detection and directive accessors
    bool parseSlurmBatchScript(const QString& scriptText);
    bool loadSlurmBatchScriptFile(const QString& filePath);
    QString generateSlurmBatchScript() const;
    QString slurmFieldValue(const QString& key) const;
    void setSlurmFieldValue(const QString& key, const QString& value);
    static QString substituteTargetBinary(const QString& scriptTemplate,
                                          const QString& binaryPath,
                                          const QString& extraArgs = QString());

    // Spack environment
    QString spackRoot() const;

    void loadFromSettings();

private:
    void syncTemplateFromFieldsIfAuto();
    bool parseDirectivesIntoFields(const QString& scriptText, bool updateTemplateBox);

    QLineEdit* m_sshHostEdit = nullptr;
    QLineEdit* m_sshUserEdit = nullptr;
    QLineEdit* m_sshKeyEdit = nullptr;

    // SLURM Batch Configuration & CLI path controls
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
    QLineEdit* m_errorLogEdit = nullptr;
    QComboBox* m_mailTypeCombo = nullptr;
    QLineEdit* m_mailUserEdit = nullptr;
    QLineEdit* m_sbatchPathEdit = nullptr;
    QLineEdit* m_squeuePathEdit = nullptr;
    QLineEdit* m_scontrolPathEdit = nullptr;
    QPlainTextEdit* m_templateEdit = nullptr;
    QLineEdit* m_spackRootEdit = nullptr;

    // Preserved extra directives from imported scripts for lossless round-tripping
    int m_parsedExplicitNtasks = 0;
    QString m_parsedMemPerCpu;
    QString m_parsedChdir;
    bool m_parsedExclusive = false;
    bool m_templateManuallyEdited = false;
    bool m_updatingWidgets = false;
};

}  // namespace gridlock::ui
