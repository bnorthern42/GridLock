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

namespace {

QString extractSbatchValue(const QString& rawRest) {
    const QString s = rawRest.trimmed();
    if (s.isEmpty()) return QString();
    const QChar q = s[0];
    if (q == QLatin1Char('"') || q == QLatin1Char('\'')) {
        QString out;
        for (int i = 1; i < s.size(); ++i) {
            if (s[i] == QLatin1Char('\\') && q == QLatin1Char('"') && i + 1 < s.size()) {
                out.append(s[++i]);
                continue;
            }
            if (s[i] == q) return out;
            out.append(s[i]);
        }
        return out.trimmed();
    }
    int endIdx = s.size();
    for (int i = 0; i < s.size(); ++i) {
        if (s[i] == QLatin1Char('#') && (i == 0 || s[i - 1].isSpace())) {
            endIdx = i;
            break;
        }
    }
    return s.left(endIdx).trimmed();
}

bool parseSingleSbatchLine(const QString& trimmedLine, QString* keyOut, QString* valOut) {
    if (!trimmedLine.startsWith(QStringLiteral("#SBATCH"))) return false;
    const QString afterTag = trimmedLine.mid(7);
    if (afterTag.isEmpty() || !afterTag[0].isSpace()) return false;
    const QString spec = afterTag.trimmed();
    if (spec.isEmpty()) return false;

    if (spec.startsWith(QStringLiteral("--"))) {
        const QString body = spec.mid(2);
        if (body == QLatin1String("exclusive") || body.startsWith(QStringLiteral("exclusive ")) ||
            body.startsWith(QStringLiteral("exclusive\t")) || body.startsWith(QStringLiteral("exclusive#"))) {
            *keyOut = QStringLiteral("exclusive");
            *valOut = QStringLiteral("true");
            return true;
        }
        int sep = -1;
        for (int i = 0; i < body.size(); ++i) {
            if (body[i] == QLatin1Char('=') || body[i].isSpace()) {
                sep = i;
                break;
            }
        }
        if (sep <= 0) return false;
        *keyOut = body.left(sep).toLower();
        QString rest = body.mid(sep).trimmed();
        if (rest.startsWith(QLatin1Char('='))) rest = rest.mid(1).trimmed();
        *valOut = extractSbatchValue(rest);
        return !keyOut->isEmpty();
    }

    if (spec.startsWith(QLatin1Char('-')) && spec.size() >= 2) {
        const QChar flag = spec[1];
        QString mapped;
        if (flag == QLatin1Char('J')) mapped = QStringLiteral("job-name");
        else if (flag == QLatin1Char('A')) mapped = QStringLiteral("account");
        else if (flag == QLatin1Char('p')) mapped = QStringLiteral("partition");
        else if (flag == QLatin1Char('q')) mapped = QStringLiteral("qos");
        else if (flag == QLatin1Char('N')) mapped = QStringLiteral("nodes");
        else if (flag == QLatin1Char('n')) mapped = QStringLiteral("ntasks");
        else if (flag == QLatin1Char('c')) mapped = QStringLiteral("cpus-per-task");
        else if (flag == QLatin1Char('t')) mapped = QStringLiteral("time");
        else if (flag == QLatin1Char('C')) mapped = QStringLiteral("constraint");
        else if (flag == QLatin1Char('o')) mapped = QStringLiteral("output");
        else if (flag == QLatin1Char('e')) mapped = QStringLiteral("error");
        else if (flag == QLatin1Char('D')) mapped = QStringLiteral("chdir");
        if (mapped.isEmpty()) return false;
        QString rest = spec.mid(2).trimmed();
        if (rest.startsWith(QLatin1Char('='))) rest = rest.mid(1).trimmed();
        *keyOut = mapped;
        *valOut = extractSbatchValue(rest);
        return !valOut->isEmpty();
    }
    return false;
}

QString formatSbatchDirectiveValue(const QString& raw) {
    const QString trimmed = raw.trimmed();
    for (QChar ch : trimmed) {
        if (ch.isSpace() || ch == QLatin1Char('"')) {
            QString escaped = trimmed;
            escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
            escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
            return QStringLiteral("\"%1\"").arg(escaped);
        }
    }
    return trimmed;
}

QString escapeShellDoubleQuoted(const QString& raw) {
    QString out;
    out.reserve(raw.size() + 8);
    for (QChar ch : raw) {
        if (ch == QLatin1Char('\\') || ch == QLatin1Char('"') ||
            ch == QLatin1Char('$') || ch == QLatin1Char('`')) {
            out.append(QLatin1Char('\\'));
        }
        out.append(ch);
    }
    return out;
}

}  // namespace

bool HpcIntegrationSettingsPage::loadSlurmBatchScriptFile(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    const QString content = QString::fromUtf8(file.readAll());
    m_sbatchFilePathEdit->setText(filePath);
    return parseSlurmBatchScript(content);
}

bool HpcIntegrationSettingsPage::parseSlurmBatchScript(const QString& scriptText) {
    if (scriptText.trimmed().isEmpty()) {
        return false;
    }
    const bool prevUpdating = m_updatingWidgets;
    m_updatingWidgets = true;
    const bool matched = parseDirectivesIntoFields(scriptText, true);
    m_updatingWidgets = prevUpdating;
    return matched;
}

bool HpcIntegrationSettingsPage::parseDirectivesIntoFields(const QString& scriptText, bool updateTemplateBox) {
    if (updateTemplateBox) {
        m_templateEdit->setPlainText(scriptText);
        m_templateManuallyEdited = true;
        setComboTextValue(m_memCombo, QString());
        m_errorLogEdit->clear();
    }
    m_parsedExplicitNtasks = 0;
    m_parsedMemPerCpu.clear();
    m_parsedChdir.clear();
    m_parsedExclusive = false;

    static const QRegularExpression moduleRx(QStringLiteral("^\\s*module\\s+load\\s+(.+)$"));
    static const QRegularExpression launcherRx(QStringLiteral("^\\s*(srun\\s+--mpi=pmix|srun|mpirun|mpiexec)\\b"));

    QStringList loadedModules;
    bool matchedAny = false;
    int parsedNodes = -1;
    int parsedNtasks = -1;
    int parsedNtasksPerNode = -1;

    const QStringList lines = scriptText.split(QLatin1Char('\n'));
    for (const QString& rawLine : lines) {
        const QString line = rawLine.trimmed();
        QString key;
        QString val;
        if (parseSingleSbatchLine(line, &key, &val)) {
            matchedAny = true;
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
                parsedNodes = qMax(1, val.toInt());
            } else if (key == QLatin1String("ntasks-per-node")) {
                parsedNtasksPerNode = qMax(1, val.toInt());
            } else if (key == QLatin1String("ntasks")) {
                parsedNtasks = qMax(1, val.toInt());
            } else if (key == QLatin1String("cpus-per-task")) {
                m_cpusPerTaskBox->setValue(qMax(1, val.toInt()));
            } else if (key == QLatin1String("mem")) {
                m_parsedMemPerCpu.clear();
                setComboTextValue(m_memCombo, val);
            } else if (key == QLatin1String("mem-per-cpu")) {
                m_parsedMemPerCpu = val;
                setComboTextValue(m_memCombo, QString());
            } else if (key == QLatin1String("gres") || key == QLatin1String("gpus") ||
                       key == QLatin1String("gpus-per-node")) {
                QString gres = val;
                if (gres.startsWith(QStringLiteral("gpu:"), Qt::CaseInsensitive)) gres = gres.mid(4);
                const QStringList parts = gres.split(QLatin1Char(':'), Qt::SkipEmptyParts);
                m_gpuCheck->setChecked(true);
                m_gpuTypeEdit->setEnabled(true);
                m_gpusBox->setEnabled(true);
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
            } else if (key == QLatin1String("error")) {
                m_errorLogEdit->setText(val);
            } else if (key == QLatin1String("chdir")) {
                m_parsedChdir = val;
            } else if (key == QLatin1String("exclusive")) {
                m_parsedExclusive = true;
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

    if (parsedNodes > 0) m_nodesBox->setValue(parsedNodes);
    const int effNodes = qMax(1, m_nodesBox->value());
    if (parsedNtasksPerNode > 0) {
        m_tasksBox->setValue(parsedNtasksPerNode);
        m_parsedExplicitNtasks = (parsedNtasks > 0 && parsedNtasks != parsedNtasksPerNode * effNodes) ? parsedNtasks : 0;
    } else if (parsedNtasks > 0) {
        m_parsedExplicitNtasks = parsedNtasks;
        m_tasksBox->setValue(qMax(1, (effNodes > 1) ? ((parsedNtasks + effNodes - 1) / effNodes) : parsedNtasks));
    }

    if (!loadedModules.isEmpty()) {
        m_modulesEdit->setText(loadedModules.join(QLatin1Char(' ')));
    }
    return matchedAny;
}

QString HpcIntegrationSettingsPage::generateSlurmBatchScript() const {
    QStringList out;
    out << QStringLiteral("#!/bin/bash");
    if (!m_jobNameEdit->text().trimmed().isEmpty()) {
        out << QStringLiteral("#SBATCH --job-name=%1").arg(formatSbatchDirectiveValue(m_jobNameEdit->text()));
    }
    if (!m_accountEdit->text().trimmed().isEmpty()) {
        out << QStringLiteral("#SBATCH --account=%1").arg(formatSbatchDirectiveValue(m_accountEdit->text()));
    }
    if (!m_partitionCombo->currentText().trimmed().isEmpty()) {
        out << QStringLiteral("#SBATCH --partition=%1").arg(formatSbatchDirectiveValue(m_partitionCombo->currentText()));
    }
    const QString qos = m_qosCombo->currentText().trimmed();
    if (!qos.isEmpty() && qos != QStringLiteral("(default)")) {
        out << QStringLiteral("#SBATCH --qos=%1").arg(formatSbatchDirectiveValue(qos));
    }
    if (!m_timeCombo->currentText().trimmed().isEmpty()) {
        out << QStringLiteral("#SBATCH --time=%1").arg(m_timeCombo->currentText().trimmed());
    }
    out << QStringLiteral("#SBATCH --nodes=%1").arg(m_nodesBox->value());
    if (m_parsedExplicitNtasks > 0) {
        out << QStringLiteral("#SBATCH --ntasks=%1").arg(m_parsedExplicitNtasks);
    } else {
        out << QStringLiteral("#SBATCH --ntasks-per-node=%1").arg(m_tasksBox->value());
    }
    out << QStringLiteral("#SBATCH --cpus-per-task=%1").arg(m_cpusPerTaskBox->value());

    const QString rawMem = m_memCombo->currentText().trimmed();
    if (!rawMem.isEmpty()) {
        if (rawMem.startsWith(QStringLiteral("per-cpu:"), Qt::CaseInsensitive)) {
            out << QStringLiteral("#SBATCH --mem-per-cpu=%1").arg(rawMem.mid(8).trimmed());
        } else if (rawMem.startsWith(QStringLiteral("--mem-per-cpu="), Qt::CaseInsensitive)) {
            out << QStringLiteral("#SBATCH --mem-per-cpu=%1").arg(rawMem.mid(14).trimmed());
        } else {
            out << QStringLiteral("#SBATCH --mem=%1").arg(rawMem.section(QLatin1Char(' '), 0, 0));
        }
    } else if (!m_parsedMemPerCpu.isEmpty()) {
        out << QStringLiteral("#SBATCH --mem-per-cpu=%1").arg(m_parsedMemPerCpu);
    }

    if (m_parsedExclusive) {
        out << QStringLiteral("#SBATCH --exclusive");
    }
    if (!m_parsedChdir.isEmpty()) {
        out << QStringLiteral("#SBATCH --chdir=%1").arg(formatSbatchDirectiveValue(m_parsedChdir));
    }
    if (m_gpuCheck->isChecked()) {
        const QString gType = m_gpuTypeEdit->text().trimmed();
        out << (gType.isEmpty()
                    ? QStringLiteral("#SBATCH --gres=gpu:%1").arg(m_gpusBox->value())
                    : QStringLiteral("#SBATCH --gres=gpu:%1:%2").arg(gType).arg(m_gpusBox->value()));
    }
    if (!m_constraintEdit->text().trimmed().isEmpty()) {
        out << QStringLiteral("#SBATCH --constraint=%1").arg(formatSbatchDirectiveValue(m_constraintEdit->text()));
    }
    if (!m_outputLogEdit->text().trimmed().isEmpty()) {
        out << QStringLiteral("#SBATCH --output=%1").arg(formatSbatchDirectiveValue(m_outputLogEdit->text()));
    }
    if (!m_errorLogEdit->text().trimmed().isEmpty()) {
        out << QStringLiteral("#SBATCH --error=%1").arg(formatSbatchDirectiveValue(m_errorLogEdit->text()));
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
    out << QStringLiteral("%1 \"{FILE}\"").arg(launcher);
    return out.join(QLatin1Char('\n'));
}

QString HpcIntegrationSettingsPage::substituteTargetBinary(const QString& scriptTemplate,
                                                           const QString& binaryPath,
                                                           const QString& extraArgs) {
    QString script = scriptTemplate;
    const QString quotedBin = QStringLiteral("\"%1\"").arg(escapeShellDoubleQuoted(binaryPath.trimmed()));
    const QString fullTarget = extraArgs.trimmed().isEmpty()
                                   ? quotedBin
                                   : QStringLiteral("%1 %2").arg(quotedBin, extraArgs.trimmed());
    for (const QString& tok : {QStringLiteral("\"{FILE}\""), QStringLiteral("\"%%FILE%%\""),
                               QStringLiteral("'{FILE}'"), QStringLiteral("'%%FILE%%'"),
                               QStringLiteral("{FILE}"), QStringLiteral("%%FILE%%")}) {
        script.replace(tok, fullTarget);
    }
    return script;
}

QString HpcIntegrationSettingsPage::slurmFieldValue(const QString& key) const {
    const QString k = key.trimmed().toLower();
    if (k == QLatin1String("job_name")) return m_jobNameEdit->text().trimmed();
    if (k == QLatin1String("account")) return m_accountEdit->text().trimmed();
    if (k == QLatin1String("partition")) return m_partitionCombo->currentText().trimmed();
    if (k == QLatin1String("qos")) return m_qosCombo->currentText().trimmed();
    if (k == QLatin1String("time")) return m_timeCombo->currentText().trimmed();
    if (k == QLatin1String("nodes")) return QString::number(m_nodesBox->value());
    if (k == QLatin1String("ntasks_per_node")) return QString::number(m_tasksBox->value());
    if (k == QLatin1String("ntasks")) {
        return QString::number(m_parsedExplicitNtasks > 0 ? m_parsedExplicitNtasks
                                                          : (m_nodesBox->value() * m_tasksBox->value()));
    }
    if (k == QLatin1String("cpus_per_task")) return QString::number(m_cpusPerTaskBox->value());
    if (k == QLatin1String("mem")) return m_memCombo->currentText().trimmed();
    if (k == QLatin1String("mem_per_cpu")) return m_parsedMemPerCpu;
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
    if (k == QLatin1String("error")) return m_errorLogEdit->text().trimmed();
    if (k == QLatin1String("chdir")) return m_parsedChdir;
    if (k == QLatin1String("exclusive")) return m_parsedExclusive ? QStringLiteral("true") : QStringLiteral("false");
    if (k == QLatin1String("mail_type")) return m_mailTypeCombo->currentText().trimmed();
    if (k == QLatin1String("mail_user")) return m_mailUserEdit->text().trimmed();
    if (k == QLatin1String("script_path")) return m_sbatchFilePathEdit->text().trimmed();
    if (k == QLatin1String("sbatch_path")) return sbatchPath();
    if (k == QLatin1String("squeue_path")) return squeuePath();
    if (k == QLatin1String("scontrol_path")) return scontrolPath();
    if (k == QLatin1String("spack_root")) return spackRoot();
    if (k == QLatin1String("ssh_host")) return sshHost();
    if (k == QLatin1String("ssh_user")) return sshUser();
    if (k == QLatin1String("ssh_key_path")) return sshKeyPath();
    return QString();
}

void HpcIntegrationSettingsPage::setSlurmFieldValue(const QString& key, const QString& value) {
    const QString k = key.trimmed().toLower();
    if (k == QLatin1String("job_name")) m_jobNameEdit->setText(value);
    else if (k == QLatin1String("account")) m_accountEdit->setText(value);
    else if (k == QLatin1String("partition")) setComboTextValue(m_partitionCombo, value);
    else if (k == QLatin1String("qos")) setComboTextValue(m_qosCombo, value);
    else if (k == QLatin1String("time")) setComboTextValue(m_timeCombo, value);
    else if (k == QLatin1String("nodes")) { m_parsedExplicitNtasks = 0; m_nodesBox->setValue(qMax(1, value.toInt())); }
    else if (k == QLatin1String("ntasks_per_node")) { m_parsedExplicitNtasks = 0; m_tasksBox->setValue(qMax(1, value.toInt())); }
    else if (k == QLatin1String("cpus_per_task")) m_cpusPerTaskBox->setValue(qMax(1, value.toInt()));
    else if (k == QLatin1String("mem")) { m_parsedMemPerCpu.clear(); setComboTextValue(m_memCombo, value); }
    else if (k == QLatin1String("request_gpus")) m_gpuCheck->setChecked(value == QLatin1String("true") || value == QLatin1String("1"));
    else if (k == QLatin1String("gpu_type")) m_gpuTypeEdit->setText(value);
    else if (k == QLatin1String("gpus_per_node")) m_gpusBox->setValue(qMax(1, value.toInt()));
    else if (k == QLatin1String("constraint")) m_constraintEdit->setText(value);
    else if (k == QLatin1String("mpi_launcher")) setComboTextValue(m_mpiLauncherCombo, value);
    else if (k == QLatin1String("modules")) m_modulesEdit->setText(value);
    else if (k == QLatin1String("output")) m_outputLogEdit->setText(value);
    else if (k == QLatin1String("error")) m_errorLogEdit->setText(value);
    else if (k == QLatin1String("mail_type")) setComboTextValue(m_mailTypeCombo, value);
    else if (k == QLatin1String("mail_user")) m_mailUserEdit->setText(value);
    else if (k == QLatin1String("script_path")) m_sbatchFilePathEdit->setText(value);
    else if (k == QLatin1String("sbatch_path")) m_sbatchPathEdit->setText(value);
    else if (k == QLatin1String("squeue_path")) m_squeuePathEdit->setText(value);
    else if (k == QLatin1String("scontrol_path")) m_scontrolPathEdit->setText(value);
    else if (k == QLatin1String("spack_root")) m_spackRootEdit->setText(value);
    else if (k == QLatin1String("ssh_host")) m_sshHostEdit->setText(value);
    else if (k == QLatin1String("ssh_user")) m_sshUserEdit->setText(value);
    else if (k == QLatin1String("ssh_key_path")) m_sshKeyEdit->setText(value);
    syncTemplateFromFieldsIfAuto();
}

}  // namespace gridlock::ui
