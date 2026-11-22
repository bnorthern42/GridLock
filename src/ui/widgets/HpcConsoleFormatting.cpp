/**
 * @file HpcConsoleFormatting.cpp
 * @brief Multi-distro CLI output parsers, Table/List/Raw rendering, and styling for HpcConsoleWidget.
 *
 * Implements native output parsers for `pacman`, `dnf4`/`dnf5`, `rpm`, `apt`/`dpkg`,
 * `zypper`, and `spack`/`eb`, colorized 6-column table and HTML card list rendering,
 * view-mode switching, and dark/light QSS combo-box styling.
 */

#include "HpcConsoleWidget.hpp"

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QRegularExpression>
#include <QSysInfo>
#include <QTableWidgetItem>

#include "../../core/managers/ThemeManager.hpp"

namespace gridlock::ui {

/** @brief Parses native CLI output from a supported package manager into PackageRecord items. */
QVector<PackageRecord> HpcConsoleWidget::parsePackageManagerOutput(
    const QString& backend, const QString& rawOutput) const {
    QVector<PackageRecord> records;
    const QString pm = backend.trimmed().toLower();
    const QString defaultArch = QSysInfo::currentCpuArchitecture().isEmpty()
                                    ? QStringLiteral("x86_64")
                                    : QSysInfo::currentCpuArchitecture();
    const QStringList lines = rawOutput.split(QLatin1Char('\n'));

    // 1. Arch Linux / CachyOS pacman -Ss / pacman -Qs two-line format ("repo/name version [installed]").
    if (pm == QLatin1String("pacman")) {
        static const QRegularExpression pacHeaderRx(
            QStringLiteral("^([a-zA-Z0-9_+-]+)/([a-zA-Z0-9_.+-]+)\\s+([^\\s]+)(.*)$"));
        for (int i = 0; i < lines.size(); ++i) {
            const QRegularExpressionMatch m = pacHeaderRx.match(lines[i]);
            if (m.hasMatch()) {
                PackageRecord rec;
                rec.repository = m.captured(1).trimmed();
                rec.name = m.captured(2).trimmed();
                rec.version = m.captured(3).trimmed();
                rec.arch = defaultArch;
                const QString tail = m.captured(4);
                const bool isLocalRepo = rec.repository.compare(QStringLiteral("local"), Qt::CaseInsensitive) == 0;
                rec.status = (isLocalRepo || tail.contains(QStringLiteral("[installed"), Qt::CaseInsensitive))
                                 ? QStringLiteral("Installed")
                                 : QStringLiteral("Available");
                if (i + 1 < lines.size() && (lines[i + 1].startsWith(QLatin1Char(' ')) ||
                                             lines[i + 1].startsWith(QLatin1Char('\t')))) {
                    rec.description = lines[++i].trimmed();
                }
                records.append(rec);
            }
        }
        return records;
    }

    // 2. Fedora / RHEL DNF4, DNF5, and RPM pipe-delimited query formats.
    if (pm == QLatin1String("dnf") || pm == QLatin1String("rpm")) {
        static const QRegularExpression dnf4Rx(
            QStringLiteral("^\\s*([a-zA-Z0-9_.+-]+)\\.(x86_64|noarch|i686|aarch64|src)\\s*:\\s*(.+)$"));
        static const QRegularExpression dnf5Rx(
            QStringLiteral("^\\s*([a-zA-Z0-9_.+-]+)\\.(x86_64|noarch|i686|aarch64|src)(?:\\s*\\t\\s*|\\s{2,})(.+)$"));
        for (const QString& rawLine : lines) {
            const QString trimmed = rawLine.trimmed();
            if (trimmed.isEmpty() || trimmed.startsWith(QLatin1String("===")) ||
                trimmed.startsWith(QLatin1String("Last metadata")) ||
                trimmed.startsWith(QLatin1String("Matched fields")) ||
                trimmed.startsWith(QLatin1String("Updating and loading")) ||
                trimmed.startsWith(QLatin1String("Repositories loaded"))) {
                continue;
            }
            if (trimmed.contains(QLatin1Char('|'))) {
                const QStringList parts = trimmed.split(QLatin1Char('|'));
                if (parts.size() >= 6) {
                    records.append({parts[0].trimmed(), parts[1].trimmed(), parts[2].trimmed(),
                                    parts[3].trimmed(), parts[4].trimmed(), parts[5].trimmed()});
                    continue;
                }
            }
            QRegularExpressionMatch m = dnf4Rx.match(rawLine);
            if (!m.hasMatch()) m = dnf5Rx.match(rawLine);
            if (m.hasMatch()) {
                records.append({m.captured(1).trimmed(), QStringLiteral("repo"), m.captured(2).trimmed(),
                                QStringLiteral("dnf"), QStringLiteral("Available"), m.captured(3).trimmed()});
            }
        }
        return records;
    }

    // 3. Debian / Ubuntu apt-cache search and dpkg -l / dpkg-query formats.
    if (pm == QLatin1String("apt") || pm == QLatin1String("apt-cache") || pm == QLatin1String("dpkg")) {
        static const QRegularExpression dpkgRx(QStringLiteral("^ii\\s+([^\\s]+)\\s+([^\\s]+)\\s+([^\\s]+)\\s+(.+)$"));
        static const QRegularExpression aptRx(QStringLiteral("^([a-zA-Z0-9_.+-]+)\\s+-\\s+(.+)$"));
        for (const QString& rawLine : lines) {
            const QString line = rawLine.trimmed();
            if (line.isEmpty()) continue;
            if (line.contains(QLatin1Char('|'))) {
                const QStringList parts = line.split(QLatin1Char('|'));
                if (parts.size() >= 6) {
                    records.append({parts[0].trimmed(), parts[1].trimmed(), parts[2].trimmed(),
                                    parts[3].trimmed(), parts[4].trimmed(), parts[5].trimmed()});
                    continue;
                }
            }
            const QRegularExpressionMatch dm = dpkgRx.match(line);
            if (dm.hasMatch()) {
                records.append({dm.captured(1).trimmed(), dm.captured(2).trimmed(), dm.captured(3).trimmed(),
                                QStringLiteral("dpkg"), QStringLiteral("Installed"), dm.captured(4).trimmed()});
                continue;
            }
            const QRegularExpressionMatch am = aptRx.match(line);
            if (am.hasMatch()) {
                records.append({am.captured(1).trimmed(), QStringLiteral("apt"), defaultArch,
                                QStringLiteral("apt"), QStringLiteral("Available"), am.captured(2).trimmed()});
            }
        }
        return records;
    }

    // 4. openSUSE / SLES zypper se -s pipe-delimited table format.
    if (pm == QLatin1String("zypper")) {
        for (const QString& rawLine : lines) {
            if (!rawLine.contains(QLatin1Char('|'))) continue;
            const QStringList cols = rawLine.split(QLatin1Char('|'));
            if (cols.size() < 6) continue;
            const QString sCol = cols[0].trimmed();
            const QString nameCol = cols[1].trimmed();
            if (nameCol.compare(QStringLiteral("Name"), Qt::CaseInsensitive) == 0 ||
                nameCol.startsWith(QLatin1Char('-'))) {
                continue;
            }
            const QString pkgType = cols[2].trimmed();
            const QString status = (sCol == QLatin1String("Installed") || sCol.contains(QLatin1Char('i'), Qt::CaseInsensitive))
                                       ? QStringLiteral("Installed")
                                       : QStringLiteral("Available");
            const QString desc = (pkgType == QLatin1String("package") || pkgType.isEmpty())
                                     ? QStringLiteral("%1 (%2)").arg(nameCol, pkgType.isEmpty() ? QStringLiteral("rpm") : pkgType)
                                     : cols[5].trimmed();
            records.append({nameCol, cols[3].trimmed(), cols[4].trimmed(), cols[5].trimmed(), status, desc});
        }
        return records;
    }

    // 5. Spack / EasyBuild specification lines (`pkg@version%compiler arch=...`).
    static const QRegularExpression spackRx(
        QStringLiteral("^([a-zA-Z0-9_.+-]+)(?:@([a-zA-Z0-9_.+-]+))?(?:%[a-zA-Z0-9_.+-]+)?(?:\\s+arch=([a-zA-Z0-9_+-]+))?"));
    for (const QString& rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('=')) ||
            line.startsWith(QLatin1String("--")) || line.startsWith(QLatin1Char('['))) {
            continue;
        }
        const QRegularExpressionMatch sm = spackRx.match(line);
        if (sm.hasMatch()) {
            const QString repo = pm.isEmpty() ? QStringLiteral("spack") : pm;
            records.append({sm.captured(1).trimmed(),
                            sm.captured(2).isEmpty() ? QStringLiteral("latest") : sm.captured(2).trimmed(),
                            sm.captured(3).isEmpty() ? defaultArch : sm.captured(3).trimmed(),
                            repo,
                            line.contains(QLatin1Char('@')) ? QStringLiteral("Installed") : QStringLiteral("Available"),
                            QStringLiteral("HPC software module (%1)").arg(repo)});
        }
    }
    return records;
}

void HpcConsoleWidget::displayPackageRecords(const QVector<PackageRecord>& records, const QString& rawOutput) {
    m_allRecords = records;
    if (!rawOutput.isEmpty()) m_lastRawOutput = rawOutput;
    if (m_outputArea) m_outputArea->setPlainText(m_lastRawOutput);
    refreshFilteredViews();
}

/** @brief Re-renders m_packageTable and m_listBrowser applying active status and search filters. */
void HpcConsoleWidget::refreshFilteredViews() {
    if (!m_packageTable || !m_listBrowser) return;
    const QString query = m_searchEdit ? m_searchEdit->text().trimmed() : QString();
    const int statusMode = m_statusFilterCombo ? m_statusFilterCombo->currentIndex() : 0;

    QVector<PackageRecord> filtered;
    for (const auto& rec : m_allRecords) {
        if (statusMode == 1 && rec.status.compare(QStringLiteral("Installed"), Qt::CaseInsensitive) != 0) continue;
        if (statusMode == 2 && rec.status.compare(QStringLiteral("Available"), Qt::CaseInsensitive) != 0) continue;
        if (!query.isEmpty() && !rec.name.contains(query, Qt::CaseInsensitive) &&
            !rec.description.contains(query, Qt::CaseInsensitive) &&
            !rec.repository.contains(query, Qt::CaseInsensitive)) {
            continue;
        }
        filtered.append(rec);
    }

    m_packageTable->setSortingEnabled(false);
    m_packageTable->setRowCount(filtered.size());
    m_visibleCount = filtered.size();
    QFont boldFont = m_packageTable->font();
    boldFont.setBold(true);

    auto makeCell = [&](const QString& text, const char* hexColor, bool bold = false) {
        auto* item = new QTableWidgetItem(text);
        item->setForeground(QBrush(QColor(hexColor)));
        if (bold) item->setFont(boldFont);
        return item;
    };

    for (int r = 0; r < filtered.size(); ++r) {
        const PackageRecord& rec = filtered[r];
        const bool isInstalled = rec.status.compare(QStringLiteral("Installed"), Qt::CaseInsensitive) == 0;
        m_packageTable->setItem(r, 0, makeCell(isInstalled ? QStringLiteral("● INSTALLED") : QStringLiteral("○ AVAILABLE"),
                                               isInstalled ? "#98c379" : "#61afef", true));
        m_packageTable->setItem(r, 1, makeCell(rec.name, "#e5c07b", true));
        m_packageTable->setItem(r, 2, makeCell(rec.version, "#56b6c2"));
        m_packageTable->setItem(r, 3, makeCell(rec.arch, "#c678dd"));
        m_packageTable->setItem(r, 4, makeCell(rec.repository, "#4ec9b0"));
        m_packageTable->setItem(r, 5, makeCell(rec.description, "#dcdfe4"));
    }

    QString html = QStringLiteral("<div style='font-family:\"JetBrains Mono\",monospace; font-size:11px; color:#d7dae0;'>");
    for (const auto& rec : filtered) {
        const bool isInstalled = rec.status.compare(QStringLiteral("Installed"), Qt::CaseInsensitive) == 0;
        html += QStringLiteral(
                    "<div style='padding:5px 8px; border-bottom:1px solid #282c34;'>"
                    "<div><span style='color:#c678dd;'>[%1]</span> <b style='color:#e5c07b;'>%2</b> "
                    "<span style='color:#56b6c2;'>%3</span> <span style='color:#8b949e;'>(%4)</span> "
                    "<span style='color:%5; font-weight:700;'>[%6]</span></div>"
                    "<div style='color:#abb2bf; padding-left:14px; margin-top:2px;'>%7</div></div>")
                    .arg(rec.repository.toHtmlEscaped(), rec.name.toHtmlEscaped(),
                         rec.version.toHtmlEscaped(), rec.arch.toHtmlEscaped(),
                         isInstalled ? QStringLiteral("#98c379") : QStringLiteral("#61afef"),
                         isInstalled ? QStringLiteral("INSTALLED") : QStringLiteral("AVAILABLE"),
                         rec.description.toHtmlEscaped());
    }
    html += QStringLiteral("</div>");
    m_listBrowser->setHtml(html);

    if (m_countLabel) m_countLabel->setText(tr("%1 packages").arg(m_visibleCount));
}

void HpcConsoleWidget::setViewMode(const QString& mode) {
    const QString norm = mode.trimmed();
    if (norm.compare(QStringLiteral("List"), Qt::CaseInsensitive) == 0) {
        m_currentViewMode = QStringLiteral("List");
        if (m_viewStack) m_viewStack->setCurrentIndex(1);
    } else if (norm.compare(QStringLiteral("Raw"), Qt::CaseInsensitive) == 0) {
        m_currentViewMode = QStringLiteral("Raw");
        if (m_viewStack) m_viewStack->setCurrentIndex(2);
    } else {
        m_currentViewMode = QStringLiteral("Table");
        if (m_viewStack) m_viewStack->setCurrentIndex(0);
    }
    if (m_tableModeBtn) m_tableModeBtn->setChecked(m_currentViewMode == QStringLiteral("Table"));
    if (m_listModeBtn) m_listModeBtn->setChecked(m_currentViewMode == QStringLiteral("List"));
    if (m_rawModeBtn) m_rawModeBtn->setChecked(m_currentViewMode == QStringLiteral("Raw"));
}

int HpcConsoleWidget::packageTableRowCount() const {
    return m_packageTable ? m_packageTable->rowCount() : 0;
}

int HpcConsoleWidget::visiblePackageCount() const {
    return m_packageTable ? m_packageTable->rowCount() : m_visibleCount;
}

/** @brief Applies modern dark/light themed QSS to package filter combo boxes. */
void HpcConsoleWidget::reloadStyle() {
    const bool isDark = gridlock::core::ThemeManager::instance().isDarkMode();
    const QString darkQss = QStringLiteral(
        "QComboBox#packageStatusFilterCombo, QComboBox#packageBackendCombo { background: #1e222a; color: #d7dae0; border: 1px solid #3e4451; border-radius: 4px; padding: 1px 22px 1px 8px; font-size: 11px; font-weight: 600; min-height: 20px; max-height: 22px; }"
        "QComboBox#packageStatusFilterCombo:hover, QComboBox#packageBackendCombo:hover, QComboBox#packageStatusFilterCombo:focus, QComboBox#packageBackendCombo:focus { background: #242933; border: 1px solid #528bff; color: #ffffff; }"
        "QComboBox#packageStatusFilterCombo::drop-down, QComboBox#packageBackendCombo::drop-down { subcontrol-origin: padding; subcontrol-position: top right; width: 18px; border-left: 1px solid #2d323b; border-top-right-radius: 4px; border-bottom-right-radius: 4px; background: #181b21; }"
        "QComboBox#packageStatusFilterCombo::drop-down:hover, QComboBox#packageBackendCombo::drop-down:hover { background: #282c34; }"
        "QComboBox#packageStatusFilterCombo QAbstractItemView, QComboBox#packageBackendCombo QAbstractItemView { background-color: #1e222a; border: 1px solid #3e4451; border-radius: 6px; padding: 4px; selection-background-color: #264f78; selection-color: #ffffff; color: #d7dae0; font-size: 11px; outline: none; }"
        "QComboBox#packageStatusFilterCombo QAbstractItemView::item, QComboBox#packageBackendCombo QAbstractItemView::item { min-height: 24px; padding: 2px 8px; border-radius: 2px; }"
        "QComboBox#packageStatusFilterCombo QAbstractItemView::item:hover, QComboBox#packageBackendCombo QAbstractItemView::item:hover { background-color: #282c34; color: #528bff; }"
        "QComboBox#packageStatusFilterCombo QAbstractItemView::item:selected, QComboBox#packageBackendCombo QAbstractItemView::item:selected { background-color: #264f78; color: #ffffff; }");
    const QString lightQss = QStringLiteral(
        "QComboBox#packageStatusFilterCombo, QComboBox#packageBackendCombo { background: #ffffff; color: #1f2328; border: 1px solid #d0d7de; border-radius: 4px; padding: 1px 22px 1px 8px; font-size: 11px; font-weight: 600; min-height: 20px; max-height: 22px; }"
        "QComboBox#packageStatusFilterCombo:hover, QComboBox#packageBackendCombo:hover { background: #f6f8fa; border: 1px solid #0969da; color: #0969da; }"
        "QComboBox#packageStatusFilterCombo:focus, QComboBox#packageBackendCombo:focus { background: #ffffff; border: 1px solid #0969da; color: #1f2328; }"
        "QComboBox#packageStatusFilterCombo::drop-down, QComboBox#packageBackendCombo::drop-down { subcontrol-origin: padding; subcontrol-position: top right; width: 18px; border-left: 1px solid #e1e4e8; border-top-right-radius: 4px; border-bottom-right-radius: 4px; background: #f6f8fa; }"
        "QComboBox#packageStatusFilterCombo::drop-down:hover, QComboBox#packageBackendCombo::drop-down:hover { background: #eaeef2; }"
        "QComboBox#packageStatusFilterCombo QAbstractItemView, QComboBox#packageBackendCombo QAbstractItemView { background-color: #ffffff; border: 1px solid #d0d7de; border-radius: 6px; padding: 4px; selection-background-color: #ddf4ff; selection-color: #0969da; color: #1f2328; font-size: 11px; outline: none; }"
        "QComboBox#packageStatusFilterCombo QAbstractItemView::item, QComboBox#packageBackendCombo QAbstractItemView::item { min-height: 24px; padding: 2px 8px; border-radius: 2px; }"
        "QComboBox#packageStatusFilterCombo QAbstractItemView::item:hover, QComboBox#packageBackendCombo QAbstractItemView::item:hover { background-color: #eaeef2; color: #0969da; }"
        "QComboBox#packageStatusFilterCombo QAbstractItemView::item:selected, QComboBox#packageBackendCombo QAbstractItemView::item:selected { background-color: #ddf4ff; color: #0969da; }");
    const QString& activeQss = isDark ? darkQss : lightQss;
    if (m_statusFilterCombo) m_statusFilterCombo->setStyleSheet(activeQss);
    if (m_backendCombo) m_backendCombo->setStyleSheet(activeQss);
}

}  // namespace gridlock::ui
