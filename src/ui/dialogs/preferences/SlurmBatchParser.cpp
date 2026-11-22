/**
 * @file SlurmBatchParser.cpp
 * @brief Bidirectional `#SBATCH` directive parser, file loader, and template generator for HpcIntegrationSettingsPage.
 */

#include "HpcSettingsPages.hpp"
#include "PreferencesFormHelpers.hpp"

#include <QFile>
#include <QRegularExpression>
#include <QStringList>

namespace gridlock::ui {

using prefs_detail::setComboTextValue;

/**
 * @brief Loads a SLURM `.sbatch`/`.slurm` file from disk and auto-populates all SLURM fields.
 * @param filePath Path to the batch script on disk.
 * @return True if the file was read and at least one SLURM directive or module was parsed.
 */
bool HpcIntegrationSettingsPage::loadSlurmBatchScriptFile(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    const QString content = QString::fromUtf8(file.readAll());
    m_sbatchFilePathEdit->setText(filePath);
    return parseSlurmBatchScript(content);
}

/**
 * @brief Parses `#SBATCH` directives, `module load` statements, and MPI launcher lines.
 * @param scriptText Raw SLURM batch script contents.
 * @return True if at least one SLURM directive, module load, or launcher was detected.
 */
bool HpcIntegrationSettingsPage::parseSlurmBatchScript(const QString& scriptText) {
    if (scriptText.trimmed().isEmpty()) {
        return false;
    }
    m_templateEdit->setPlainText(scriptText);

    static const QRegularExpression sbatchRx(
        QStringLiteral("^\\s*#SBATCH\\s+(?:--([a-zA-Z0-9_-]+)(?:=|\\s+)(\\S+)|-([JApqNntCo])\\s*(\\S+))"));
    static const QRegularExpression moduleRx(
        QStringLiteral("^\\s*module\\s+load\\s+(.+)$"));
    static const QRegularExpression launcherRx(
        QStringLiteral("^\\s*(srun\\s+--mpi=pmix|srun|mpirun|mpiexec)\\b"));

    QStringList loadedModules;
    bool matchedAny = false;

    const QStringList lines = scriptText.split(QLatin1Char('\n'));
    for (const QString& rawLine : lines) {
        const QString line = rawLine.trimmed();
        const QRegularExpressionMatch sbMatch = sbatchRx.match(line);
        if (sbMatch.hasMatch()) {
            matchedAny = true;
            QString key = sbMatch.captured(1).toLower();
            QString val = sbMatch.captured(2).trimmed();
            if (key.isEmpty()) {
                const QString shortFlag = sbMatch.captured(3);
                val = sbMatch.captured(4).trimmed();
                if (shortFlag == QLatin1String("J")) key = QStringLiteral("job-name");
                else if (shortFlag == QLatin1String("A")) key = QStringLiteral("account");
                else if (shortFlag == QLatin1String("p")) key = QStringLiteral("partition");
                else if (shortFlag == QLatin1String("q")) key = QStringLiteral("qos");
                else if (shortFlag == QLatin1String("N")) key = QStringLiteral("nodes");
                else if (shortFlag == QLatin1String("n")) key = QStringLiteral("ntasks-per-node");
                else if (shortFlag == QLatin1String("t")) key = QStringLiteral("time");
                else if (shortFlag == QLatin1String("C")) key = QStringLiteral("constraint");
                else if (shortFlag == QLatin1String("o")) key = QStringLiteral("output");
            }
            if (val.size() >= 2 && ((val.startsWith(QLatin1Char('"')) && val.endsWith(QLatin1Char('"'))) ||
                                    (val.startsWith(QLatin1Char('\'')) && val.endsWith(QLatin1Char('\''))))) {
                val = val.mid(1, val.size() - 2);
            }

            if (key == QLatin1String("job-name")) {
                m_jobNameEdit->setText(val);
            } else if (key == QLatin1String("account")) {
                m_accountEdit->setText(val);
            } else if (key == QLatin1String("partition")) {
                setComboTextValue(m_partitionCombo, val);
            } else if (key == QLatin1String("qos")) {
                setComboTextValue(m_qosCombo, val);
            } else if (key == QLatin1String("time")) {
                setComboTextValue(m_timeCombo, val);
            } else if (key == QLatin1String("nodes")) {
                m_nodesBox->setValue(qMax(1, val.toInt()));
            } else if (key == QLatin1String("ntasks-per-node") || key == QLatin1String("ntasks")) {
                m_tasksBox->setValue(qMax(1, val.toInt()));
            } else if (key == QLatin1String("cpus-per-task")) {
                m_cpusPerTaskBox->setValue(qMax(1, val.toInt()));
            } else if (key == QLatin1String("mem") || key == QLatin1String("mem-per-cpu")) {
                setComboTextValue(m_memCombo, val);
            } else if (key == QLatin1String("gres") || key == QLatin1String("gpus") ||
                       key == QLatin1String("gpus-per-node")) {
                QString gres = val;
                if (gres.startsWith(QStringLiteral("gpu:"), Qt::CaseInsensitive)) {
                    gres = gres.mid(4);
                }
                const QStringList parts = gres.split(QLatin1Char(':'), Qt::SkipEmptyParts);
                m_gpuCheck->setChecked(true);
                if (parts.size() >= 2) {
                    m_gpuTypeEdit->setText(parts[0]);
                    m_gpusBox->setValue(qMax(1, parts[1].toInt()));
                } else if (parts.size() == 1) {
                    bool ok = false;
                    const int count = parts[0].toInt(&ok);
                    if (ok) {
                        m_gpuTypeEdit->clear();
                        m_gpusBox->setValue(qMax(1, count));
                    } else {
                        m_gpuTypeEdit->setText(parts[0]);
                        m_gpusBox->setValue(1);
                    }
                }
            } else if (key == QLatin1String("constraint")) {
                m_constraintEdit->setText(val);
            } else if (key == QLatin1String("output")) {
                m_outputLogEdit->setText(val);
            } else if (key == QLatin1String("mail-type")) {
                setComboTextValue(m_mailTypeCombo, val);
            } else if (key == QLatin1String("mail-user")) {
                m_mailUserEdit->setText(val);
            }
            continue;
        }

        const QRegularExpressionMatch modMatch = moduleRx.match(line);
        if (modMatch.hasMatch()) {
            matchedAny = true;
            loadedModules.append(modMatch.captured(1).trimmed());
            continue;
        }

        if (!line.startsWith(QLatin1Char('#'))) {
            const QRegularExpressionMatch launchMatch = launcherRx.match(line);
            if (launchMatch.hasMatch()) {
                matchedAny = true;
                setComboTextValue(m_mpiLauncherCombo, launchMatch.captured(1).trimmed());
            }
        }
    }

    if (!loadedModules.isEmpty()) {
        m_modulesEdit->setText(loadedModules.join(QLatin1Char(' ')));
    }
    return matchedAny;
}

/**
 * @brief Generates a complete SLURM batch script from the current 16 form fields.
 * @return Formatted `#SBATCH` batch script string ready for job submission.
 */
QString HpcIntegrationSettingsPage::generateSlurmBatchScript() const {
    QStringList out;
    out << QStringLiteral("#!/bin/bash");
    if (!m_jobNameEdit->text().trimmed().isEmpty()) {
        out << QStringLiteral("#SBATCH --job-name=%1").arg(m_jobNameEdit->text().trimmed());
    }
    if (!m_accountEdit->text().trimmed().isEmpty()) {
        out << QStringLiteral("#SBATCH --account=%1").arg(m_accountEdit->text().trimmed());
    }
    if (!m_partitionCombo->currentText().trimmed().isEmpty()) {
        out << QStringLiteral("#SBATCH --partition=%1").arg(m_partitionCombo->currentText().trimmed());
    }
    const QString qos = m_qosCombo->currentText().trimmed();
    if (!qos.isEmpty() && qos != QStringLiteral("(default)")) {
        out << QStringLiteral("#SBATCH --qos=%1").arg(qos);
    }
    if (!m_timeCombo->currentText().trimmed().isEmpty()) {
        out << QStringLiteral("#SBATCH --time=%1").arg(m_timeCombo->currentText().trimmed());
    }
    out << QStringLiteral("#SBATCH --nodes=%1").arg(m_nodesBox->value());
    out << QStringLiteral("#SBATCH --ntasks-per-node=%1").arg(m_tasksBox->value());
    out << QStringLiteral("#SBATCH --cpus-per-task=%1").arg(m_cpusPerTaskBox->value());
    if (!m_memCombo->currentText().trimmed().isEmpty()) {
        out << QStringLiteral("#SBATCH --mem=%1").arg(m_memCombo->currentText().trimmed().section(QLatin1Char(' '), 0, 0));
    }
    if (m_gpuCheck->isChecked()) {
        const QString gType = m_gpuTypeEdit->text().trimmed();
        out << (gType.isEmpty()
                    ? QStringLiteral("#SBATCH --gres=gpu:%1").arg(m_gpusBox->value())
                    : QStringLiteral("#SBATCH --gres=gpu:%1:%2").arg(gType).arg(m_gpusBox->value()));
    }
    if (!m_constraintEdit->text().trimmed().isEmpty()) {
        out << QStringLiteral("#SBATCH --constraint=%1").arg(m_constraintEdit->text().trimmed());
    }
    if (!m_outputLogEdit->text().trimmed().isEmpty()) {
        out << QStringLiteral("#SBATCH --output=%1").arg(m_outputLogEdit->text().trimmed());
    }
    const QString mailType = m_mailTypeCombo->currentText().trimmed();
    if (!mailType.isEmpty() && mailType != QStringLiteral("NONE")) {
        out << QStringLiteral("#SBATCH --mail-type=%1").arg(mailType);
    }
    if (!m_mailUserEdit->text().trimmed().isEmpty()) {
        out << QStringLiteral("#SBATCH --mail-user=%1").arg(m_mailUserEdit->text().trimmed());
    }
    out << QString();
    if (!m_modulesEdit->text().trimmed().isEmpty()) {
        out << QStringLiteral("module load %1").arg(m_modulesEdit->text().trimmed());
    }
    const QString launcher = m_mpiLauncherCombo->currentText().trimmed().isEmpty()
                                 ? QStringLiteral("srun")
                                 : m_mpiLauncherCombo->currentText().trimmed();
    out << QStringLiteral("%1 {FILE}").arg(launcher);
    return out.join(QLatin1Char('\n'));
}

/**
 * @brief Returns the string value of any of the 16 SLURM fields by key.
 */
QString HpcIntegrationSettingsPage::slurmFieldValue(const QString& key) const {
    const QString k = key.trimmed().toLower();
    if (k == QLatin1String("job_name")) return m_jobNameEdit->text().trimmed();
    if (k == QLatin1String("account")) return m_accountEdit->text().trimmed();
    if (k == QLatin1String("partition")) return m_partitionCombo->currentText().trimmed();
    if (k == QLatin1String("qos")) return m_qosCombo->currentText().trimmed();
    if (k == QLatin1String("time")) return m_timeCombo->currentText().trimmed();
    if (k == QLatin1String("nodes")) return QString::number(m_nodesBox->value());
    if (k == QLatin1String("ntasks_per_node")) return QString::number(m_tasksBox->value());
    if (k == QLatin1String("cpus_per_task")) return QString::number(m_cpusPerTaskBox->value());
    if (k == QLatin1String("mem")) return m_memCombo->currentText().trimmed();
    if (k == QLatin1String("gres_gpu")) {
        if (!m_gpuCheck->isChecked()) return QString();
        const QString gType = m_gpuTypeEdit->text().trimmed();
        return gType.isEmpty() ? QString::number(m_gpusBox->value())
                               : QStringLiteral("%1:%2").arg(gType).arg(m_gpusBox->value());
    }
    if (k == QLatin1String("constraint")) return m_constraintEdit->text().trimmed();
    if (k == QLatin1String("mpi_launcher")) return m_mpiLauncherCombo->currentText().trimmed();
    if (k == QLatin1String("modules")) return m_modulesEdit->text().trimmed();
    if (k == QLatin1String("output")) return m_outputLogEdit->text().trimmed();
    if (k == QLatin1String("mail_type")) return m_mailTypeCombo->currentText().trimmed();
    if (k == QLatin1String("mail_user")) return m_mailUserEdit->text().trimmed();
    return QString();
}

}  // namespace gridlock::ui
