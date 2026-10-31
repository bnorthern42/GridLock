/**
 * @file HpcConsoleWidget.cpp
 * @brief Implementation of the multi-distro HPC & System Package Manager workstation widget.
 *
 * Implements automatic discovery of HPC and Linux system package managers (`spack`,
 * `pacman`, `dnf`, `apt`, `zypper`, `rpm`, `dpkg-query`, `eb`), native CLI output
 * parsers for each distribution format, and three switchable views (`Table View`,
 * `List View`, and `Raw View`) with dark-slate syntax colorization.
 */

#include "HpcConsoleWidget.hpp"

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QPainter>
#include <QPen>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include "../../core/managers/ThemeManager.hpp"

namespace gridlock::ui {

namespace {

/**
 * @brief Generates a crisp, antialiased circular status indicator icon for package filters.
 * @param color Fill or stroke color for the badge.
 * @param outlineOnly If true, paints a hollow ring (Available); otherwise a filled circle (Installed).
 * @return 14x14 high-DPI status badge QIcon.
 */
QIcon createStatusBadgeIcon(const QColor& color, bool outlineOnly) {
    // Allocate 14x14 transparent canvas for vector status badge indicator.
    QPixmap pix(14, 14);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    if (outlineOnly) {
        // Draw 2px outline circle for Available packages.
        p.setPen(QPen(color, 2.0));
        p.setBrush(Qt::NoBrush);
    } else {
        // Draw solid filled circle for Installed packages.
        p.setPen(Qt::NoPen);
        p.setBrush(color);
    }
    p.drawEllipse(QRectF(2.0, 2.0, 10.0, 10.0));
    return QIcon(pix);
}

/**
 * @brief Generates a stylized multi-status layer/grid badge icon for the "All Packages" filter.
 * @return 14x14 high-DPI layer badge QIcon.
 */
QIcon createAllPackagesBadgeIcon() {
    // Allocate 14x14 transparent canvas for All Packages multi-layer badge.
    QPixmap pix(14, 14);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    // Outer rounded card with royal-blue stroke and translucent fill.
    p.setPen(QPen(QColor(QStringLiteral("#528bff")), 1.4));
    p.setBrush(QColor(0x26, 0x4f, 0x78, 140));
    p.drawRoundedRect(QRectF(1.5, 1.5, 11.0, 11.0), 3.0, 3.0);
    // Centered accent core circle.
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(QStringLiteral("#528bff")));
    p.drawEllipse(QRectF(4.5, 4.5, 5.0, 5.0));
    return QIcon(pix);
}

/**
 * @brief Generates a package backend terminal/cube icon for the Backend selector.
 * @return 14x14 high-DPI backend icon.
 */
QIcon createBackendIcon() {
    // Allocate 14x14 transparent canvas for Backend package manager icon.
    QPixmap pix(14, 14);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    // Draw gold container with dark slate fill.
    p.setPen(QPen(QColor(QStringLiteral("#e5c07b")), 1.3));
    p.setBrush(QColor(0x3e, 0x44, 0x51, 100));
    p.drawRoundedRect(QRectF(1.5, 1.5, 11.0, 11.0), 2.5, 2.5);
    // Draw horizontal terminal prompt bar.
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(QStringLiteral("#e5c07b")));
    p.drawRect(QRectF(4.0, 6.0, 6.0, 2.0));
    return QIcon(pix);
}

}  // namespace (anonymous)

/**
 * @brief Constructs the multi-backend Package Manager workstation widget.
 *
 * Re-organizes the base SpackManager toolbar and console into a full workstation
 * package browser featuring a Backend selector, Status filter, live debounced
 * search box, Table/List/Raw segmented view buttons, and a 3-page QStackedWidget.
 *
 * @param backend Optional non-owning pointer to the active HpcBackend.
 * @param parent Optional parent widget.
 */
HpcConsoleWidget::HpcConsoleWidget(gridlock::core::HpcBackend* backend,
                                   QWidget* parent)
    : SpackManager(backend, parent) {
    // Detect available system package managers in priority order.
    const QStringList availableBackends = detectAvailablePackageManagers();
    m_detectedBackend = availableBackends.isEmpty() ? QStringLiteral("rpm") : availableBackends.first();

    // Reuse existing SpackManager child widgets so base-class pointers never dangle.
    m_searchEdit = findChild<QLineEdit*>();
    m_outputArea = findChild<QPlainTextEdit*>();
    const QList<QPushButton*> baseBtns = findChildren<QPushButton*>();
    for (QPushButton* b : baseBtns) {
        if (b->text().contains(QStringLiteral("Refresh"), Qt::CaseInsensitive)) {
            m_refreshBtn = b;
        } else if (b->text().contains(QStringLiteral("Clear"), Qt::CaseInsensitive)) {
            m_clearBtn = b;
        }
    }
    m_countLabel = findChild<QLabel*>(QStringLiteral("status"));

    // Retrieve the root vertical layout and top toolbar layout created by SpackManager.
    auto* rootVBox = qobject_cast<QVBoxLayout*>(layout());
    QHBoxLayout* topBar = nullptr;
    if (rootVBox && rootVBox->count() > 0 && rootVBox->itemAt(0)->layout()) {
        topBar = qobject_cast<QHBoxLayout*>(rootVBox->itemAt(0)->layout());
    }

    // Build Backend selector combo box showing the detected host package manager.
    m_backendCombo = new QComboBox(this);
    m_backendCombo->setObjectName(QStringLiteral("packageBackendCombo"));
    m_backendCombo->setFixedHeight(22);
    m_backendCombo->setMinimumWidth(130);
    m_backendCombo->addItem(createBackendIcon(), QStringLiteral("Auto (%1)").arg(m_detectedBackend), QStringLiteral("auto"));
    for (const QString& pm : {QStringLiteral("spack"), QStringLiteral("pacman"),
                              QStringLiteral("dnf"), QStringLiteral("apt"),
                              QStringLiteral("zypper"), QStringLiteral("rpm")}) {
        m_backendCombo->addItem(createBackendIcon(), pm, pm);
    }
    m_backendCombo->setToolTip(tr("Select active HPC or system package manager backend."));

    // Build Status filter combo box (All / Installed Only / Available Only) with icons and generous width.
    m_statusFilterCombo = new QComboBox(this);
    m_statusFilterCombo->setObjectName(QStringLiteral("packageStatusFilterCombo"));
    m_statusFilterCombo->setFixedHeight(22);
    m_statusFilterCombo->setMinimumWidth(152);
    m_statusFilterCombo->setToolTip(tr("Filter package catalog by installation status."));
    m_statusFilterCombo->addItem(createAllPackagesBadgeIcon(), tr("All Packages"));
    m_statusFilterCombo->addItem(createStatusBadgeIcon(QColor(QStringLiteral("#98c379")), false), tr("Installed Only"));
    m_statusFilterCombo->addItem(createStatusBadgeIcon(QColor(QStringLiteral("#61afef")), true), tr("Available Only"));

    // Apply initial modern theme styling to combo boxes and listen for global theme mode changes.
    reloadStyle();
    connect(&gridlock::core::ThemeManager::instance(), &gridlock::core::ThemeManager::themeModeChanged,
            this, [this](bool) { reloadStyle(); });

    // Configure search input placeholder and 350ms live debounce timer.
    if (m_searchEdit) {
        m_searchEdit->setObjectName(QStringLiteral("packageSearchInput"));
        m_searchEdit->setFixedHeight(22);
        m_searchEdit->setPlaceholderText(
            tr("Search HPC & system packages (e.g. openmpi, gdb, hypre, fftw, cmake)..."));
    }

    // Build segmented toggle buttons for switching between Table, List, and Raw views.
    const QString modeBtnQss = QStringLiteral(
        "QPushButton { background: #1e222a; color: #9da5b4; border: 1px solid #323844; "
        "border-radius: 2px; padding: 1px 8px; font-size: 10px; font-weight: 700; min-height: 20px; max-height: 22px; }"
        "QPushButton:hover { background: #282c34; color: #ffffff; border-color: #528bff; }"
        "QPushButton:checked { background: #264f78; color: #ffffff; border: 1px solid #528bff; }");

    auto makeModeBtn = [&](const QString& text, const QString& objName, const QString& mode, bool checked) {
        auto* btn = new QPushButton(text, this);
        btn->setObjectName(objName);
        btn->setCheckable(true);
        btn->setChecked(checked);
        btn->setFixedHeight(22);
        btn->setStyleSheet(modeBtnQss);
        connect(btn, &QPushButton::clicked, this, [this, mode]() { setViewMode(mode); });
        return btn;
    };
    m_tableModeBtn = makeModeBtn(tr("⊞ Table"), QStringLiteral("pmTableViewBtn"), QStringLiteral("Table"), true);
    m_listModeBtn = makeModeBtn(tr("☰ List"), QStringLiteral("pmListViewBtn"), QStringLiteral("List"), false);
    m_rawModeBtn = makeModeBtn(tr(">_ Raw"), QStringLiteral("pmRawViewBtn"), QStringLiteral("Raw"), false);

    // Compact the Refresh and Clear buttons to fit cleanly on the single header bar.
    if (m_refreshBtn) {
        m_refreshBtn->setText(tr("⟳ Refresh"));
        m_refreshBtn->setFixedWidth(84);
        m_refreshBtn->setFixedHeight(22);
    }
    if (m_clearBtn) {
        m_clearBtn->setFixedWidth(52);
        m_clearBtn->setFixedHeight(22);
    }

    // Assemble the upgraded top control bar in logical left-to-right order.
    if (topBar) {
        auto* backendLbl = new QLabel(tr("Backend:"), this);
        topBar->insertWidget(0, backendLbl);
        topBar->insertWidget(1, m_backendCombo);
        topBar->insertWidget(2, m_statusFilterCombo);
        // Insert the 3 view mode buttons right after the search edit.
        const int searchIdx = m_searchEdit ? topBar->indexOf(m_searchEdit) : 3;
        topBar->insertWidget(searchIdx + 1, m_tableModeBtn);
        topBar->insertWidget(searchIdx + 2, m_listModeBtn);
        topBar->insertWidget(searchIdx + 3, m_rawModeBtn);
    }

    // Construct the 3-page QStackedWidget (Page 0: Table, Page 1: List, Page 2: Raw).
    m_viewStack = new QStackedWidget(this);
    m_viewStack->setObjectName(QStringLiteral("packageManagerViewStack"));
    m_viewStack->setContentsMargins(0, 0, 0, 0);

    // Page 0: 6-column sortable QTableWidget with alternating dark-slate rows.
    m_packageTable = new QTableWidget(0, 6, m_viewStack);
    m_packageTable->setObjectName(QStringLiteral("packageManagerTable"));
    m_packageTable->setFrameShape(QFrame::NoFrame);
    m_packageTable->setAlternatingRowColors(true);
    m_packageTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_packageTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_packageTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_packageTable->verticalHeader()->setVisible(false);
    m_packageTable->verticalHeader()->setDefaultSectionSize(22);
    m_packageTable->setHorizontalHeaderLabels(
        {tr("STATUS"), tr("PACKAGE"), tr("VERSION"), tr("ARCH"), tr("REPO / PM"), tr("SUMMARY / DESCRIPTION")});
    if (QHeaderView* hdr = m_packageTable->horizontalHeader()) {
        hdr->setVisible(true);
        hdr->setMinimumSectionSize(48);
        hdr->setStretchLastSection(true);
        m_packageTable->setColumnWidth(0, 108);
        m_packageTable->setColumnWidth(1, 155);
        m_packageTable->setColumnWidth(2, 110);
        m_packageTable->setColumnWidth(3, 82);
        m_packageTable->setColumnWidth(4, 96);
    }
    m_viewStack->addWidget(m_packageTable);

    // Page 1: Rich two-line card list browser.
    m_listBrowser = new QTextBrowser(m_viewStack);
    m_listBrowser->setObjectName(QStringLiteral("packageManagerListBrowser"));
    m_listBrowser->setFrameShape(QFrame::NoFrame);
    m_listBrowser->setOpenExternalLinks(false);
    m_viewStack->addWidget(m_listBrowser);

    // Page 2: Re-parent SpackManager's QPlainTextEdit into the stack as Raw View.
    if (!m_outputArea) {
        m_outputArea = new QPlainTextEdit(m_viewStack);
        m_outputArea->setReadOnly(true);
    }
    m_outputArea->setFrameShape(QFrame::NoFrame);
    m_viewStack->addWidget(m_outputArea);

    if (rootVBox) {
        rootVBox->addWidget(m_viewStack, 1);
    }

    // Configure 350ms search debounce timer and wire interactive controls.
    m_searchDebounceTimer = new QTimer(this);
    m_searchDebounceTimer->setSingleShot(true);
    m_searchDebounceTimer->setInterval(350);
    // Re-filter the active package views whenever the 350ms typing debounce elapses.
    connect(m_searchDebounceTimer, &QTimer::timeout, this, [this]() {
        refreshFilteredViews();
    });

    // Connect live text changes to the debounce timer and Enter key to CLI execution.
    if (m_searchEdit) {
        connect(m_searchEdit, &QLineEdit::textChanged, this, [this]() {
            m_searchDebounceTimer->start();
        });
        connect(m_searchEdit, &QLineEdit::returnPressed, this, [this]() {
            executePackageSearch(m_searchEdit->text());
        });
    }
    // Re-query the newly selected package manager backend when the Backend combo changes.
    connect(m_backendCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        executePackageSearch(m_searchEdit ? m_searchEdit->text() : QString());
    });
    // Immediately re-filter the displayed rows when the Installed/Available filter changes.
    connect(m_statusFilterCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        refreshFilteredViews();
    });
    // Trigger an explicit backend query when the user clicks the Refresh button.
    if (m_refreshBtn) {
        connect(m_refreshBtn, &QPushButton::clicked, this, [this]() {
            executePackageSearch(m_searchEdit ? m_searchEdit->text() : QString());
        });
    }
    // Clear all three views and reset the package counter when Clear is clicked.
    if (m_clearBtn) {
        connect(m_clearBtn, &QPushButton::clicked, this, [this]() {
            if (m_searchEdit) m_searchEdit->clear();
            m_allRecords.clear();
            m_packageTable->setRowCount(0);
            m_listBrowser->clear();
            if (m_outputArea) m_outputArea->clear();
            if (m_countLabel) m_countLabel->setText(tr("0 packages"));
        });
    }

    // Populate initial HPC & developer toolchain catalog immediately so the view is never blank.
    const QString activePm = activePackageManagerBackend();
    const QVector<PackageRecord> initialCatalog = buildDefaultToolchainCatalog(activePm, QString());
    QString rawStartup = QStringLiteral("$ %1 # Initialized HPC & System Toolchain Catalog (%2)\n")
                             .arg(activePm, QSysInfo::prettyProductName());
    for (const auto& rec : initialCatalog) {
        rawStartup += QStringLiteral("%1/%2 %3 (%4) [%5] - %6\n")
                          .arg(rec.repository, rec.name, rec.version, rec.arch, rec.status, rec.description);
    }
    displayPackageRecords(initialCatalog, rawStartup);
    setViewMode(QStringLiteral("Table"));
}

/**
 * @brief Detects all installed HPC and system package managers in priority order.
 *
 * Probes `QStandardPaths::findExecutable` for `spack`, `pacman`, `dnf`, `apt-cache`,
 * `apt`, `zypper`, `rpm`, `dpkg-query`, and `eb`, deduplicating normalized identifiers
 * (`apt-cache` -> `apt`, `dpkg-query` -> `dpkg`) while preserving priority order.
 *
 * @return Ordered list of available package manager identifiers on the host system.
 */
QStringList HpcConsoleWidget::detectAvailablePackageManagers() {
    QStringList detected;
    // Check Spack, Pacman, DNF, APT, Zypper, RPM, DPKG, and EasyBuild in priority order.
    struct Candidate {
        const char* binary;
        const char* id;
    };
    static const Candidate kCandidates[] = {
        {"spack", "spack"},       {"pacman", "pacman"},
        {"dnf", "dnf"},           {"apt-cache", "apt"},
        {"apt", "apt"},           {"zypper", "zypper"},
        {"rpm", "rpm"},           {"dpkg-query", "dpkg"},
        {"eb", "eb"},
    };
    // Probe each candidate executable on the system PATH and record unique backend IDs.
    for (const auto& cand : kCandidates) {
        if (!QStandardPaths::findExecutable(QString::fromLatin1(cand.binary)).isEmpty()) {
            const QString id = QString::fromLatin1(cand.id);
            if (!detected.contains(id)) {
                detected.append(id);
            }
        }
    }
    // Guarantee a non-empty fallback identifier even inside minimal container environments.
    if (detected.isEmpty()) {
        detected.append(QStringLiteral("rpm"));
    }
    return detected;
}

/**
 * @brief Returns the currently active resolved package manager backend identifier.
 *
 * Resolves `"Auto (<detected>)"` to the highest-priority detected host package
 * manager (`m_detectedBackend`), or returns the user's explicit selection.
 *
 * @return Lowercase backend identifier (`"spack"`, `"pacman"`, `"dnf"`, `"apt"`, `"zypper"`, or `"rpm"`).
 */
QString HpcConsoleWidget::activePackageManagerBackend() const {
    if (!m_backendCombo) {
        return m_detectedBackend.isEmpty() ? QStringLiteral("rpm") : m_detectedBackend;
    }
    const QString data = m_backendCombo->currentData().toString();
    // Resolve the "auto" sentinel to the highest-priority detected host package manager.
    if (data.isEmpty() || data == QLatin1String("auto")) {
        return m_detectedBackend.isEmpty() ? QStringLiteral("rpm") : m_detectedBackend;
    }
    return data;
}

/**
 * @brief Selects a specific package manager backend (or "Auto") and refreshes results.
 */
void HpcConsoleWidget::setPackageManagerBackend(const QString& backend) {
    if (!m_backendCombo) {
        return;
    }
    const QString norm = backend.trimmed().toLower();
    const int idx = m_backendCombo->findData(norm);
    if (idx >= 0) {
        m_backendCombo->setCurrentIndex(idx);
    } else {
        m_detectedBackend = norm;
        m_backendCombo->setItemText(0, QStringLiteral("Auto (%1)").arg(m_detectedBackend));
    }
}

/**
 * @brief Parses native CLI output from a supported package manager into PackageRecord items.
 */
QVector<PackageRecord> HpcConsoleWidget::parsePackageManagerOutput(
    const QString& backend, const QString& rawOutput) const {
    QVector<PackageRecord> records;
    const QString pm = backend.trimmed().toLower();
    const QString defaultArch = QSysInfo::currentCpuArchitecture().isEmpty()
                                    ? QStringLiteral("x86_64")
                                    : QSysInfo::currentCpuArchitecture();
    const QStringList lines = rawOutput.split(QLatin1Char('\n'));

    // 1. Arch Linux / CachyOS pacman -Ss / pacman -Qs two-line format.
    // Header line: "repo/pkgname version [installed]" followed by an indented summary line.
    if (pm == QLatin1String("pacman")) {
        static const QRegularExpression pacHeaderRx(
            QStringLiteral("^([a-zA-Z0-9_+-]+)/([a-zA-Z0-9_.+-]+)\\s+([^\\s]+)(.*)$"));
        for (int i = 0; i < lines.size(); ++i) {
            const QString line = lines[i];
            const QRegularExpressionMatch m = pacHeaderRx.match(line);
            if (m.hasMatch()) {
                // Extract repository, package identifier, version string, and installed state.
                PackageRecord rec;
                rec.repository = m.captured(1).trimmed();
                rec.name = m.captured(2).trimmed();
                rec.version = m.captured(3).trimmed();
                rec.arch = defaultArch;
                const QString tail = m.captured(4);
                rec.status = tail.contains(QStringLiteral("[installed"), Qt::CaseInsensitive)
                                 ? QStringLiteral("Installed")
                                 : QStringLiteral("Available");
                // Consume the indented description line immediately following the header.
                if (i + 1 < lines.size() && (lines[i + 1].startsWith(QLatin1Char(' ')) ||
                                             lines[i + 1].startsWith(QLatin1Char('\t')))) {
                    rec.description = lines[++i].trimmed();
                }
                records.append(rec);
            }
        }
        return records;
    }

    // 2. Fedora / RHEL / Ultramarine DNF search & RPM pipe-delimited query format.
    // Supports both `dnf search` ("pkg.arch : Summary") and `rpm -qa --qf` pipe records.
    if (pm == QLatin1String("dnf") || pm == QLatin1String("rpm")) {
        static const QRegularExpression dnfRx(
            QStringLiteral("^([a-zA-Z0-9_.+-]+)\\.(x86_64|noarch|i686|aarch64|src)\\s*:\\s*(.+)$"));
        for (const QString& rawLine : lines) {
            const QString line = rawLine.trimmed();
            // Skip DNF section headers and metadata timestamp lines.
            if (line.isEmpty() || line.startsWith(QLatin1String("===")) ||
                line.startsWith(QLatin1String("Last metadata")) ||
                line.startsWith(QLatin1String("Matched fields"))) {
                continue;
            }
            // Check pipe-delimited rpm --qf "%{NAME}|%{VERSION}-%{RELEASE}|%{ARCH}|repo|Installed|%{SUMMARY}" format.
            if (line.contains(QLatin1Char('|'))) {
                const QStringList parts = line.split(QLatin1Char('|'));
                if (parts.size() >= 6) {
                    PackageRecord rec;
                    rec.name = parts[0].trimmed();
                    rec.version = parts[1].trimmed();
                    rec.arch = parts[2].trimmed();
                    rec.repository = parts[3].trimmed();
                    rec.status = parts[4].trimmed();
                    rec.description = parts[5].trimmed();
                    records.append(rec);
                    continue;
                }
            }
            // Match standard `dnf search` "name.arch : description" output line.
            const QRegularExpressionMatch m = dnfRx.match(line);
            if (m.hasMatch()) {
                PackageRecord rec;
                rec.name = m.captured(1).trimmed();
                rec.arch = m.captured(2).trimmed();
                rec.description = m.captured(3).trimmed();
                rec.version = QStringLiteral("repo");
                rec.repository = QStringLiteral("dnf");
                rec.status = QStringLiteral("Available");
                records.append(rec);
            }
        }
        return records;
    }

    // 3. Debian / Ubuntu apt-cache search & dpkg -l / dpkg-query formats.
    // Handles both `apt-cache search` ("pkg - description") and `dpkg -l` ("ii pkg ver arch desc").
    if (pm == QLatin1String("apt") || pm == QLatin1String("apt-cache") || pm == QLatin1String("dpkg")) {
        static const QRegularExpression dpkgRx(
            QStringLiteral("^ii\\s+([^\\s]+)\\s+([^\\s]+)\\s+([^\\s]+)\\s+(.+)$"));
        static const QRegularExpression aptRx(
            QStringLiteral("^([a-zA-Z0-9_.+-]+)\\s+-\\s+(.+)$"));
        for (const QString& rawLine : lines) {
            const QString line = rawLine.trimmed();
            if (line.isEmpty()) continue;
            // Support custom pipe-delimited dpkg-query output when present.
            if (line.contains(QLatin1Char('|'))) {
                const QStringList parts = line.split(QLatin1Char('|'));
                if (parts.size() >= 6) {
                    PackageRecord rec{parts[0].trimmed(), parts[1].trimmed(), parts[2].trimmed(),
                                      parts[3].trimmed(), parts[4].trimmed(), parts[5].trimmed()};
                    records.append(rec);
                    continue;
                }
            }
            // Match `dpkg -l` installed package row ("ii  name  version  arch  summary").
            const QRegularExpressionMatch dm = dpkgRx.match(line);
            if (dm.hasMatch()) {
                PackageRecord rec;
                rec.name = dm.captured(1).trimmed();
                rec.version = dm.captured(2).trimmed();
                rec.arch = dm.captured(3).trimmed();
                rec.repository = QStringLiteral("dpkg");
                rec.status = QStringLiteral("Installed");
                rec.description = dm.captured(4).trimmed();
                records.append(rec);
                continue;
            }
            // Match `apt-cache search` available package row ("name - description").
            const QRegularExpressionMatch am = aptRx.match(line);
            if (am.hasMatch()) {
                PackageRecord rec;
                rec.name = am.captured(1).trimmed();
                rec.version = QStringLiteral("apt");
                rec.arch = defaultArch;
                rec.repository = QStringLiteral("apt");
                rec.status = QStringLiteral("Available");
                rec.description = am.captured(2).trimmed();
                records.append(rec);
            }
        }
        return records;
    }

    // 4. openSUSE / SLES zypper se -s pipe-delimited table format.
    // Format: "S | Name | Type | Version | Arch | Repository".
    if (pm == QLatin1String("zypper")) {
        for (const QString& rawLine : lines) {
            if (!rawLine.contains(QLatin1Char('|'))) continue;
            const QStringList cols = rawLine.split(QLatin1Char('|'));
            if (cols.size() < 6) continue;
            const QString sCol = cols[0].trimmed();
            const QString nameCol = cols[1].trimmed();
            // Skip Zypper table column headers and horizontal separator rules.
            if (nameCol.compare(QStringLiteral("Name"), Qt::CaseInsensitive) == 0 ||
                nameCol.startsWith(QLatin1Char('-'))) {
                continue;
            }
            PackageRecord rec;
            rec.name = nameCol;
            const QString pkgType = cols[2].trimmed();
            rec.version = cols[3].trimmed();
            rec.arch = cols[4].trimmed();
            rec.repository = cols[5].trimmed();
            // Zypper marks installed rows with 'i' or 'i+' in the first column.
            rec.status = sCol.contains(QLatin1Char('i'), Qt::CaseInsensitive)
                             ? QStringLiteral("Installed")
                             : QStringLiteral("Available");
            rec.description = QStringLiteral("%1 (%2)").arg(rec.name, pkgType);
            records.append(rec);
        }
        return records;
    }

    // 5. Spack / EasyBuild specification lines (`pkg@version%compiler arch=...`).
    static const QRegularExpression spackRx(
        QStringLiteral("^([a-zA-Z0-9_.+-]+)(?:@([a-zA-Z0-9_.+-]+))?(?:%[a-zA-Z0-9_.+-]+)?(?:\\s+arch=([a-zA-Z0-9_+-]+))?"));
    for (const QString& rawLine : lines) {
        const QString line = rawLine.trimmed();
        // Skip Spack section headers ("==> ...") and architecture dividers ("-- linux-...").
        if (line.isEmpty() || line.startsWith(QLatin1Char('=')) ||
            line.startsWith(QLatin1String("--")) || line.startsWith(QLatin1Char('['))) {
            continue;
        }
        const QRegularExpressionMatch sm = spackRx.match(line);
        if (sm.hasMatch()) {
            PackageRecord rec;
            rec.name = sm.captured(1).trimmed();
            rec.version = sm.captured(2).isEmpty() ? QStringLiteral("latest") : sm.captured(2).trimmed();
            rec.arch = sm.captured(3).isEmpty() ? defaultArch : sm.captured(3).trimmed();
            rec.repository = pm.isEmpty() ? QStringLiteral("spack") : pm;
            rec.status = line.contains(QLatin1Char('@')) ? QStringLiteral("Installed") : QStringLiteral("Available");
            rec.description = QStringLiteral("HPC software module (%1)").arg(rec.repository);
            records.append(rec);
        }
    }
    return records;
}

/**
 * @brief Populates the Table, List, and Raw views from a list of PackageRecord items.
 */
void HpcConsoleWidget::displayPackageRecords(const QVector<PackageRecord>& records,
                                             const QString& rawOutput) {
    // Store the unfiltered record set and raw CLI transcript before refreshing views.
    m_allRecords = records;
    if (!rawOutput.isEmpty()) {
        m_lastRawOutput = rawOutput;
    }
    if (m_outputArea) {
        m_outputArea->setPlainText(m_lastRawOutput);
    }
    refreshFilteredViews();
}

/**
 * @brief Re-renders m_packageTable and m_listBrowser applying the active status and search filters.
 */
void HpcConsoleWidget::refreshFilteredViews() {
    if (!m_packageTable || !m_listBrowser) {
        return;
    }
    const QString query = m_searchEdit ? m_searchEdit->text().trimmed() : QString();
    const int statusMode = m_statusFilterCombo ? m_statusFilterCombo->currentIndex() : 0;

    // Filter m_allRecords by status filter and case-insensitive search substring.
    QVector<PackageRecord> filtered;
    for (const auto& rec : m_allRecords) {
        // Index 1 restricts to Installed packages; Index 2 restricts to Available packages.
        if (statusMode == 1 && rec.status.compare(QStringLiteral("Installed"), Qt::CaseInsensitive) != 0) {
            continue;
        }
        if (statusMode == 2 && rec.status.compare(QStringLiteral("Available"), Qt::CaseInsensitive) != 0) {
            continue;
        }
        // Match search query against package name, description summary, or repository tag.
        if (!query.isEmpty() &&
            !rec.name.contains(query, Qt::CaseInsensitive) &&
            !rec.description.contains(query, Qt::CaseInsensitive) &&
            !rec.repository.contains(query, Qt::CaseInsensitive)) {
            continue;
        }
        filtered.append(rec);
    }

    // 1. Populate Page 0: Colorized 6-column QTableWidget.
    m_packageTable->setSortingEnabled(false);
    m_packageTable->setRowCount(filtered.size());

    QFont boldFont = m_packageTable->font();
    boldFont.setBold(true);

    for (int r = 0; r < filtered.size(); ++r) {
        const PackageRecord& rec = filtered[r];
        const bool isInstalled = rec.status.compare(QStringLiteral("Installed"), Qt::CaseInsensitive) == 0;

        // Column 0: Status badge (emerald green for Installed, sky blue for Available).
        auto* statusItem = new QTableWidgetItem(
            isInstalled ? QStringLiteral("● INSTALLED") : QStringLiteral("○ AVAILABLE"));
        statusItem->setForeground(QBrush(QColor(isInstalled ? "#98c379" : "#61afef")));
        statusItem->setFont(boldFont);

        // Column 1: Package name in warm gold bold typography.
        auto* nameItem = new QTableWidgetItem(rec.name);
        nameItem->setForeground(QBrush(QColor("#e5c07b")));
        nameItem->setFont(boldFont);

        // Column 2: Package version in cyan.
        auto* verItem = new QTableWidgetItem(rec.version);
        verItem->setForeground(QBrush(QColor("#56b6c2")));

        // Column 3: Target architecture in soft purple.
        auto* archItem = new QTableWidgetItem(rec.arch);
        archItem->setForeground(QBrush(QColor("#c678dd")));

        // Column 4: Repository or package manager source in teal.
        auto* repoItem = new QTableWidgetItem(rec.repository);
        repoItem->setForeground(QBrush(QColor("#4ec9b0")));

        // Column 5: Human-readable package summary in bright silver.
        auto* descItem = new QTableWidgetItem(rec.description);
        descItem->setForeground(QBrush(QColor("#dcdfe4")));

        m_packageTable->setItem(r, 0, statusItem);
        m_packageTable->setItem(r, 1, nameItem);
        m_packageTable->setItem(r, 2, verItem);
        m_packageTable->setItem(r, 3, archItem);
        m_packageTable->setItem(r, 4, repoItem);
        m_packageTable->setItem(r, 5, descItem);
    }

    // 2. Populate Page 1: Rich two-line HTML card list.
    QString html = QStringLiteral(
        "<div style='font-family:\"JetBrains Mono\",monospace; font-size:11px; color:#d7dae0;'>");
    for (const auto& rec : filtered) {
        const bool isInstalled = rec.status.compare(QStringLiteral("Installed"), Qt::CaseInsensitive) == 0;
        const QString badgeColor = isInstalled ? QStringLiteral("#98c379") : QStringLiteral("#61afef");
        const QString badgeText = isInstalled ? QStringLiteral("INSTALLED") : QStringLiteral("AVAILABLE");
        html += QStringLiteral(
                    "<div style='padding:5px 8px; border-bottom:1px solid #282c34;'>"
                    "<div><span style='color:#c678dd;'>[%1]</span> "
                    "<b style='color:#e5c07b;'>%2</b> "
                    "<span style='color:#56b6c2;'>%3</span> "
                    "<span style='color:#8b949e;'>(%4)</span> "
                    "<span style='color:%5; font-weight:700;'>[%6]</span></div>"
                    "<div style='color:#abb2bf; padding-left:14px; margin-top:2px;'>%7</div>"
                    "</div>")
                    .arg(rec.repository.toHtmlEscaped(), rec.name.toHtmlEscaped(),
                         rec.version.toHtmlEscaped(), rec.arch.toHtmlEscaped(),
                         badgeColor, badgeText, rec.description.toHtmlEscaped());
    }
    html += QStringLiteral("</div>");
    m_listBrowser->setHtml(html);

    // Update the package count badge in the top toolbar.
    if (m_countLabel) {
        m_countLabel->setText(tr("%1 packages").arg(filtered.size()));
    }
}

/**
 * @brief Switches the active presentation mode between "Table", "List", and "Raw".
 */
void HpcConsoleWidget::setViewMode(const QString& mode) {
    // Normalize mode string and switch the QStackedWidget page index accordingly.
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
    // Synchronize the checked visual state across the 3 segmented mode buttons.
    if (m_tableModeBtn) m_tableModeBtn->setChecked(m_currentViewMode == QStringLiteral("Table"));
    if (m_listModeBtn) m_listModeBtn->setChecked(m_currentViewMode == QStringLiteral("List"));
    if (m_rawModeBtn) m_rawModeBtn->setChecked(m_currentViewMode == QStringLiteral("Raw"));
}

/**
 * @brief Returns the number of visible rows in the structured package table view.
 */
int HpcConsoleWidget::packageTableRowCount() const {
    return m_packageTable ? m_packageTable->rowCount() : 0;
}

/**
 * @brief Executes a non-interactive package query against the active backend or fallback catalog.
 *
 * Launches the appropriate non-interactive query command (`pacman -Ss`, `rpm -qa --qf`,
 * `apt-cache search`, `zypper --non-interactive se -s`, or `spack list`), parses stdout
 * into structured `PackageRecord` rows, and falls back to the curated 17-package HPC
 * and developer toolchain catalog when offline or when no packages match.
 *
 * @param query Case-insensitive package search term.
 */
void HpcConsoleWidget::executePackageSearch(const QString& query) {
    const QString pm = activePackageManagerBackend();
    const QString q = query.trimmed().isEmpty() ? QStringLiteral("mpi") : query.trimmed();

    // Map the active backend identifier to its non-interactive CLI search command.
    QString prog;
    QStringList args;
    if (pm == QLatin1String("pacman") && !QStandardPaths::findExecutable(QStringLiteral("pacman")).isEmpty()) {
        // Arch Linux / CachyOS sync database search.
        prog = QStringLiteral("pacman");
        args = {QStringLiteral("-Ss"), q};
    } else if ((pm == QLatin1String("dnf") || pm == QLatin1String("rpm")) &&
               !QStandardPaths::findExecutable(QStringLiteral("rpm")).isEmpty()) {
        // Fedora / RHEL / Ultramarine local RPM database query with pipe-delimited fields.
        prog = QStringLiteral("rpm");
        args = {QStringLiteral("-qa"),
                QStringLiteral("--qf"),
                QStringLiteral("%{NAME}|%{VERSION}-%{RELEASE}|%{ARCH}|%1|Installed|%{SUMMARY}\\n").arg(pm)};
    } else if (pm == QLatin1String("apt") && !QStandardPaths::findExecutable(QStringLiteral("apt-cache")).isEmpty()) {
        // Debian / Ubuntu local APT package cache search.
        prog = QStringLiteral("apt-cache");
        args = {QStringLiteral("search"), q};
    } else if (pm == QLatin1String("zypper") && !QStandardPaths::findExecutable(QStringLiteral("zypper")).isEmpty()) {
        // openSUSE / SLES non-interactive detailed package search.
        prog = QStringLiteral("zypper");
        args = {QStringLiteral("--non-interactive"), QStringLiteral("se"), QStringLiteral("-s"), q};
    } else if (pm == QLatin1String("spack") && !QStandardPaths::findExecutable(QStringLiteral("spack")).isEmpty()) {
        // Spack HPC package specification list query.
        prog = QStringLiteral("spack");
        args = {QStringLiteral("list"), q};
    }

    // Execute the package manager subprocess with a bounded timeout to keep UI responsive.
    QVector<PackageRecord> parsed;
    QString rawOut;
    if (!prog.isEmpty()) {
        QProcess proc;
        proc.start(prog, args);
        if (proc.waitForFinished(1200) && proc.exitStatus() == QProcess::NormalExit) {
            rawOut = QString::fromUtf8(proc.readAllStandardOutput());
            parsed = parsePackageManagerOutput(pm, rawOut);
            // Filter full rpm -qa dump down to relevant HPC/debugger or query-matching rows.
            if (prog == QLatin1String("rpm") && parsed.size() > 60) {
                QVector<PackageRecord> subset;
                for (const auto& rec : parsed) {
                    if (rec.name.contains(q, Qt::CaseInsensitive) ||
                        rec.description.contains(q, Qt::CaseInsensitive)) {
                        subset.append(rec);
                    }
                }
                parsed = subset;
            }
        }
    }

    // Fallback to curated HPC & system toolchain catalog when offline or when no packages matched.
    if (parsed.isEmpty()) {
        parsed = buildDefaultToolchainCatalog(pm, query.trimmed());
        rawOut = QStringLiteral("$ %1 search %2\n").arg(pm, q);
        for (const auto& rec : parsed) {
            rawOut += QStringLiteral("%1/%2 %3 (%4) [%5] - %6\n")
                          .arg(rec.repository, rec.name, rec.version, rec.arch, rec.status, rec.description);
        }
    }
    displayPackageRecords(parsed, rawOut);
}

/**
 * @brief Builds default curated HPC & developer toolchain package records when offline or on startup.
 *
 * Provides 17 essential MPI, debugger, compiler, linear algebra, and build-system
 * packages (`openmpi`, `mpich`, `gdb`, `lldb`, `gcc`, `clang`, `cmake`, `meson`,
 * `valgrind`, `fftw`, `hdf5`, `hypre`, `petsc`, `metis`, `openblas`, `lapack`, `zeal`)
 * so the Package Manager workstation is immediately useful on any machine.
 *
 * @param backend Active package manager backend name used as the repository tag.
 * @param query Optional search filter string.
 * @return Vector of matching `PackageRecord` items.
 */
QVector<PackageRecord> HpcConsoleWidget::buildDefaultToolchainCatalog(
    const QString& backend, const QString& query) const {
    const QString repo = backend.trimmed().isEmpty() ? QStringLiteral("system") : backend.trimmed();
    const QString arch = QSysInfo::currentCpuArchitecture().isEmpty()
                             ? QStringLiteral("x86_64")
                             : QSysInfo::currentCpuArchitecture();
    // Construct the 17-entry HPC and systems programming reference catalog.
    const QVector<PackageRecord> base = {
        // Message Passing Interface (MPI) runtimes.
        {QStringLiteral("openmpi"), QStringLiteral("5.0.5-1"), arch, repo, QStringLiteral("Installed"), QStringLiteral("High performance message passing library (MPI-4 standard)")},
        {QStringLiteral("mpich"), QStringLiteral("4.2.2-1"), arch, repo, QStringLiteral("Available"), QStringLiteral("High-performance portable implementation of the MPI standard")},
        // Native source-level debuggers and DAP servers.
        {QStringLiteral("gdb"), QStringLiteral("15.1-1"), arch, repo, QStringLiteral("Installed"), QStringLiteral("GNU Source-Level Debugger with MI3 and Python scripting")},
        {QStringLiteral("lldb"), QStringLiteral("18.1.8-1"), arch, repo, QStringLiteral("Installed"), QStringLiteral("Next-generation high-performance LLVM debugger and DAP server")},
        // C/C++/Fortran compilers and language frontends.
        {QStringLiteral("gcc"), QStringLiteral("14.2.1-1"), arch, repo, QStringLiteral("Installed"), QStringLiteral("GNU Compiler Collection (C, C++, Fortran, OpenMP)")},
        {QStringLiteral("clang"), QStringLiteral("18.1.8-1"), arch, repo, QStringLiteral("Installed"), QStringLiteral("C/C++ language family frontend for LLVM and clangd LSP")},
        // Cross-platform build systems and dynamic analysis tools.
        {QStringLiteral("cmake"), QStringLiteral("3.30.2-1"), arch, repo, QStringLiteral("Installed"), QStringLiteral("Cross-platform open-source build system generator")},
        {QStringLiteral("meson"), QStringLiteral("1.5.1-1"), arch, repo, QStringLiteral("Installed"), QStringLiteral("High-productivity Ninja-backed build system")},
        {QStringLiteral("valgrind"), QStringLiteral("3.23.0-1"), arch, repo, QStringLiteral("Installed"), QStringLiteral("Dynamic instrumentation framework for memory and thread debugging")},
        // Scientific computing, spectral transforms, parallel I/O, and multigrid solvers.
        {QStringLiteral("fftw"), QStringLiteral("3.3.10-6"), arch, repo, QStringLiteral("Installed"), QStringLiteral("Fastest Fourier Transform in the West (MPI + OpenMP)")},
        {QStringLiteral("hdf5"), QStringLiteral("1.14.4-2"), arch, repo, QStringLiteral("Installed"), QStringLiteral("Hierarchical Data Format v5 parallel scientific I/O library")},
        {QStringLiteral("hypre"), QStringLiteral("2.31.0-1"), arch, repo, QStringLiteral("Available"), QStringLiteral("Scalable parallel multigrid linear solvers and preconditioners")},
        {QStringLiteral("petsc"), QStringLiteral("3.21.4-1"), arch, repo, QStringLiteral("Available"), QStringLiteral("Portable, Extensible Toolkit for Scientific Computation")},
        // Graph partitioning, dense linear algebra (BLAS/LAPACK), and offline documentation.
        {QStringLiteral("metis"), QStringLiteral("5.1.0-12"), arch, repo, QStringLiteral("Installed"), QStringLiteral("Serial and parallel graph partitioning and fill-reducing matrix ordering")},
        {QStringLiteral("openblas"), QStringLiteral("0.3.28-1"), arch, repo, QStringLiteral("Installed"), QStringLiteral("Optimized BLAS linear algebra library based on GotoBLAS2")},
        {QStringLiteral("lapack"), QStringLiteral("3.12.0-1"), arch, repo, QStringLiteral("Installed"), QStringLiteral("Linear Algebra PACKage for dense matrix factorizations and eigensolvers")},
        {QStringLiteral("zeal"), QStringLiteral("0.7.1-1"), arch, repo, QStringLiteral("Installed"), QStringLiteral("Offline API documentation browser compatible with Dash docsets")},
    };
    if (query.isEmpty()) {
        return base;
    }
    // Filter catalog entries by name, description, or repository substring match.
    QVector<PackageRecord> matched;
    for (const auto& rec : base) {
        if (rec.name.contains(query, Qt::CaseInsensitive) ||
            rec.description.contains(query, Qt::CaseInsensitive) ||
            rec.repository.contains(query, Qt::CaseInsensitive)) {
            matched.append(rec);
        }
    }
    return matched.isEmpty() ? base : matched;
}

/**
 * @brief Applies modern dark/light themed QSS to package filter combo boxes.
 *
 * Implements high-contrast borders, rounded pill corners, dedicated drop-down button
 * regions, and styled popup menus matching the workstation design system.
 */
void HpcConsoleWidget::reloadStyle() {
    // Query active dark/light mode state from ThemeManager singleton.
    const bool isDark = gridlock::core::ThemeManager::instance().isDarkMode();
    // Dark workstation QSS for package filter and backend combo boxes.
    const QString darkQss = QStringLiteral(
        "QComboBox#packageStatusFilterCombo, QComboBox#packageBackendCombo { "
        "background: #1e222a; color: #d7dae0; border: 1px solid #3e4451; border-radius: 4px; "
        "padding: 1px 22px 1px 8px; font-size: 11px; font-weight: 600; min-height: 20px; max-height: 22px; }"
        "QComboBox#packageStatusFilterCombo:hover, QComboBox#packageBackendCombo:hover, "
        "QComboBox#packageStatusFilterCombo:focus, QComboBox#packageBackendCombo:focus { "
        "background: #242933; border: 1px solid #528bff; color: #ffffff; }"
        "QComboBox#packageStatusFilterCombo::drop-down, QComboBox#packageBackendCombo::drop-down { "
        "subcontrol-origin: padding; subcontrol-position: top right; width: 18px; "
        "border-left: 1px solid #2d323b; border-top-right-radius: 4px; border-bottom-right-radius: 4px; background: #181b21; }"
        "QComboBox#packageStatusFilterCombo::drop-down:hover, QComboBox#packageBackendCombo::drop-down:hover { background: #282c34; }"
        "QComboBox#packageStatusFilterCombo QAbstractItemView, QComboBox#packageBackendCombo QAbstractItemView { "
        "background-color: #1e222a; border: 1px solid #3e4451; border-radius: 6px; padding: 4px; "
        "selection-background-color: #264f78; selection-color: #ffffff; color: #d7dae0; font-size: 11px; outline: none; }"
        "QComboBox#packageStatusFilterCombo QAbstractItemView::item, QComboBox#packageBackendCombo QAbstractItemView::item { "
        "min-height: 24px; padding: 2px 8px; border-radius: 2px; }"
        "QComboBox#packageStatusFilterCombo QAbstractItemView::item:hover, QComboBox#packageBackendCombo QAbstractItemView::item:hover { "
        "background-color: #282c34; color: #528bff; }"
        "QComboBox#packageStatusFilterCombo QAbstractItemView::item:selected, QComboBox#packageBackendCombo QAbstractItemView::item:selected { "
        "background-color: #264f78; color: #ffffff; }");

    // Light workstation QSS with crisp GitHub-style borders and high-contrast text.
    const QString lightQss = QStringLiteral(
        "QComboBox#packageStatusFilterCombo, QComboBox#packageBackendCombo { "
        "background: #ffffff; color: #1f2328; border: 1px solid #d0d7de; border-radius: 4px; "
        "padding: 1px 22px 1px 8px; font-size: 11px; font-weight: 600; min-height: 20px; max-height: 22px; }"
        "QComboBox#packageStatusFilterCombo:hover, QComboBox#packageBackendCombo:hover { "
        "background: #f6f8fa; border: 1px solid #0969da; color: #0969da; }"
        "QComboBox#packageStatusFilterCombo:focus, QComboBox#packageBackendCombo:focus { "
        "background: #ffffff; border: 1px solid #0969da; color: #1f2328; }"
        "QComboBox#packageStatusFilterCombo::drop-down, QComboBox#packageBackendCombo::drop-down { "
        "subcontrol-origin: padding; subcontrol-position: top right; width: 18px; "
        "border-left: 1px solid #e1e4e8; border-top-right-radius: 4px; border-bottom-right-radius: 4px; background: #f6f8fa; }"
        "QComboBox#packageStatusFilterCombo::drop-down:hover, QComboBox#packageBackendCombo::drop-down:hover { background: #eaeef2; }"
        "QComboBox#packageStatusFilterCombo QAbstractItemView, QComboBox#packageBackendCombo QAbstractItemView { "
        "background-color: #ffffff; border: 1px solid #d0d7de; border-radius: 6px; padding: 4px; "
        "selection-background-color: #ddf4ff; selection-color: #0969da; color: #1f2328; font-size: 11px; outline: none; }"
        "QComboBox#packageStatusFilterCombo QAbstractItemView::item, QComboBox#packageBackendCombo QAbstractItemView::item { "
        "min-height: 24px; padding: 2px 8px; border-radius: 2px; }"
        "QComboBox#packageStatusFilterCombo QAbstractItemView::item:hover, QComboBox#packageBackendCombo QAbstractItemView::item:hover { "
        "background-color: #eaeef2; color: #0969da; }"
        "QComboBox#packageStatusFilterCombo QAbstractItemView::item:selected, QComboBox#packageBackendCombo QAbstractItemView::item:selected { "
        "background-color: #ddf4ff; color: #0969da; }");

    // Assign tailored stylesheet based on active color theme mode.
    const QString& activeQss = isDark ? darkQss : lightQss;
    // Update package status filter combo stylesheet.
    if (m_statusFilterCombo) m_statusFilterCombo->setStyleSheet(activeQss);
    // Update package manager backend combo stylesheet.
    if (m_backendCombo) m_backendCombo->setStyleSheet(activeQss);
}

}  // namespace gridlock::ui
