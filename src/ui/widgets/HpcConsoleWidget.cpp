/**
 * @file HpcConsoleWidget.cpp
 * @brief Construction, host package-manager discovery, and local/repository query execution for HpcConsoleWidget.
 *
 * Implements the upgraded multi-backend Package Manager workstation UI, host
 * executable discovery (`spack`, `pacman`, `dnf`, `apt`, `zypper`, `rpm`, `dpkg-query`, `eb`),
 * cached local package queries, and synchronous/asynchronous two-stage repository searches.
 */

#include "HpcConsoleWidget.hpp"

#include <algorithm>

#include <QAbstractItemView>
#include <QColor>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QProcess>
#include <QStandardPaths>
#include <QVBoxLayout>

#include "../../core/managers/ThemeManager.hpp"

namespace gridlock::ui {

namespace {

QIcon createStatusBadgeIcon(const QColor& color, bool outlineOnly) {
    QPixmap pix(14, 14);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    if (outlineOnly) {
        p.setPen(QPen(color, 2.0));
        p.setBrush(Qt::NoBrush);
    } else {
        p.setPen(Qt::NoPen);
        p.setBrush(color);
    }
    p.drawEllipse(QRectF(2.0, 2.0, 10.0, 10.0));
    return QIcon(pix);
}

QIcon createAllPackagesBadgeIcon() {
    QPixmap pix(14, 14);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(QStringLiteral("#528bff")), 1.4));
    p.setBrush(QColor(0x26, 0x4f, 0x78, 140));
    p.drawRoundedRect(QRectF(1.5, 1.5, 11.0, 11.0), 3.0, 3.0);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(QStringLiteral("#528bff")));
    p.drawEllipse(QRectF(4.5, 4.5, 5.0, 5.0));
    return QIcon(pix);
}

QIcon createBackendIcon() {
    QPixmap pix(14, 14);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(QStringLiteral("#e5c07b")), 1.3));
    p.setBrush(QColor(0x3e, 0x44, 0x51, 100));
    p.drawRoundedRect(QRectF(1.5, 1.5, 11.0, 11.0), 2.5, 2.5);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(QStringLiteral("#e5c07b")));
    p.drawRect(QRectF(4.0, 6.0, 6.0, 2.0));
    return QIcon(pix);
}

void resolveRepoSearchCommand(const QString& pm, const QString& q, QString* outProg, QStringList* outArgs) {
    if (pm == QLatin1String("dnf") && !QStandardPaths::findExecutable(QStringLiteral("dnf")).isEmpty()) {
        *outProg = QStringLiteral("dnf"); *outArgs = {QStringLiteral("--quiet"), QStringLiteral("search"), q};
    } else if (pm == QLatin1String("pacman") && !QStandardPaths::findExecutable(QStringLiteral("pacman")).isEmpty()) {
        *outProg = QStringLiteral("pacman"); *outArgs = {QStringLiteral("-Ss"), q};
    } else if (pm == QLatin1String("apt") && !QStandardPaths::findExecutable(QStringLiteral("apt-cache")).isEmpty()) {
        *outProg = QStringLiteral("apt-cache"); *outArgs = {QStringLiteral("search"), q};
    } else if (pm == QLatin1String("zypper") && !QStandardPaths::findExecutable(QStringLiteral("zypper")).isEmpty()) {
        *outProg = QStringLiteral("zypper"); *outArgs = {QStringLiteral("--non-interactive"), QStringLiteral("se"), QStringLiteral("-s"), q};
    } else if (pm == QLatin1String("spack") && !QStandardPaths::findExecutable(QStringLiteral("spack")).isEmpty()) {
        *outProg = QStringLiteral("spack"); *outArgs = {QStringLiteral("list"), QStringLiteral("-d"), q};
    }
}

}  // namespace

/** @brief Constructs the multi-backend Package Manager workstation widget. */
HpcConsoleWidget::HpcConsoleWidget(gridlock::core::HpcBackend* backend, QWidget* parent)
    : SpackManager(backend, parent) {
    const QStringList availableBackends = detectAvailablePackageManagers();
    m_detectedBackend = availableBackends.isEmpty() ? QStringLiteral("rpm") : availableBackends.first();

    m_searchEdit = findChild<QLineEdit*>();
    m_outputArea = findChild<QPlainTextEdit*>();
    for (QPushButton* b : findChildren<QPushButton*>()) {
        if (b->text().contains(QStringLiteral("Refresh"), Qt::CaseInsensitive)) m_refreshBtn = b;
        else if (b->text().contains(QStringLiteral("Clear"), Qt::CaseInsensitive)) m_clearBtn = b;
    }
    m_countLabel = findChild<QLabel*>(QStringLiteral("status"));

    auto* rootVBox = qobject_cast<QVBoxLayout*>(layout());
    QHBoxLayout* topBar = (rootVBox && rootVBox->count() > 0 && rootVBox->itemAt(0)->layout())
                              ? qobject_cast<QHBoxLayout*>(rootVBox->itemAt(0)->layout())
                              : nullptr;

    m_backendCombo = new QComboBox(this);
    m_backendCombo->setObjectName(QStringLiteral("packageBackendCombo"));
    m_backendCombo->setFixedHeight(22);
    m_backendCombo->setMinimumWidth(130);
    m_backendCombo->addItem(createBackendIcon(), QStringLiteral("Auto (%1)").arg(m_detectedBackend), QStringLiteral("auto"));
    for (const QString& pm : {QStringLiteral("spack"), QStringLiteral("pacman"), QStringLiteral("dnf"),
                              QStringLiteral("apt"), QStringLiteral("zypper"), QStringLiteral("rpm")}) {
        m_backendCombo->addItem(createBackendIcon(), pm, pm);
    }
    m_backendCombo->setToolTip(tr("Select active HPC or system package manager backend."));

    m_statusFilterCombo = new QComboBox(this);
    m_statusFilterCombo->setObjectName(QStringLiteral("packageStatusFilterCombo"));
    m_statusFilterCombo->setFixedHeight(22);
    m_statusFilterCombo->setMinimumWidth(152);
    m_statusFilterCombo->setToolTip(tr("Filter package catalog by installation status."));
    m_statusFilterCombo->addItem(createAllPackagesBadgeIcon(), tr("All Packages"));
    m_statusFilterCombo->addItem(createStatusBadgeIcon(QColor(QStringLiteral("#98c379")), false), tr("Installed Only"));
    m_statusFilterCombo->addItem(createStatusBadgeIcon(QColor(QStringLiteral("#61afef")), true), tr("Available Only"));

    reloadStyle();
    connect(&gridlock::core::ThemeManager::instance(), &gridlock::core::ThemeManager::themeModeChanged,
            this, [this](bool) { reloadStyle(); });

    if (m_searchEdit) {
        m_searchEdit->setObjectName(QStringLiteral("packageSearchInput"));
        m_searchEdit->setFixedHeight(22);
        m_searchEdit->setPlaceholderText(tr("Search HPC & system packages (e.g. openmpi, gdb, hypre, fftw, cmake)..."));
    }

    const QString modeBtnQss = QStringLiteral(
        "QPushButton { background: #1e222a; color: #9da5b4; border: 1px solid #323844; "
        "border-radius: 2px; padding: 1px 8px; font-size: 10px; font-weight: 700; min-height: 20px; max-height: 22px; }"
        "QPushButton:hover { background: #282c34; color: #ffffff; border-color: #528bff; }"
        "QPushButton:checked { background: #264f78; color: #ffffff; border: 1px solid #528bff; }");

    auto makeModeBtn = [&](const QString& text, const QString& objName, const QString& mode, bool checked) {
        auto* btn = new QPushButton(text, this);
        btn->setObjectName(objName); btn->setCheckable(true); btn->setChecked(checked); btn->setFixedHeight(22); btn->setStyleSheet(modeBtnQss);
        connect(btn, &QPushButton::clicked, this, [this, mode]() { setViewMode(mode); });
        return btn;
    };
    m_tableModeBtn = makeModeBtn(tr("⊞ Table"), QStringLiteral("pmTableViewBtn"), QStringLiteral("Table"), true);
    m_listModeBtn = makeModeBtn(tr("☰ List"), QStringLiteral("pmListViewBtn"), QStringLiteral("List"), false);
    m_rawModeBtn = makeModeBtn(tr(">_ Raw"), QStringLiteral("pmRawViewBtn"), QStringLiteral("Raw"), false);

    if (m_refreshBtn) { m_refreshBtn->setText(tr("⟳ Refresh")); m_refreshBtn->setFixedWidth(84); m_refreshBtn->setFixedHeight(22); }
    if (m_clearBtn) { m_clearBtn->setFixedWidth(52); m_clearBtn->setFixedHeight(22); }

    if (topBar) {
        auto* backendLbl = new QLabel(tr("Backend:"), this);
        topBar->insertWidget(0, backendLbl);
        topBar->insertWidget(1, m_backendCombo);
        topBar->insertWidget(2, m_statusFilterCombo);
        const int searchIdx = m_searchEdit ? topBar->indexOf(m_searchEdit) : 3;
        topBar->insertWidget(searchIdx + 1, m_tableModeBtn);
        topBar->insertWidget(searchIdx + 2, m_listModeBtn);
        topBar->insertWidget(searchIdx + 3, m_rawModeBtn);
    }

    m_viewStack = new QStackedWidget(this);
    m_viewStack->setObjectName(QStringLiteral("packageManagerViewStack"));
    m_viewStack->setContentsMargins(0, 0, 0, 0);

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
        hdr->setVisible(true); hdr->setMinimumSectionSize(48); hdr->setStretchLastSection(true);
        m_packageTable->setColumnWidth(0, 108); m_packageTable->setColumnWidth(1, 155);
        m_packageTable->setColumnWidth(2, 110); m_packageTable->setColumnWidth(3, 82); m_packageTable->setColumnWidth(4, 96);
    }
    m_viewStack->addWidget(m_packageTable);

    m_listBrowser = new QTextBrowser(m_viewStack);
    m_listBrowser->setObjectName(QStringLiteral("packageManagerListBrowser"));
    m_listBrowser->setFrameShape(QFrame::NoFrame);
    m_listBrowser->setOpenExternalLinks(false);
    m_viewStack->addWidget(m_listBrowser);

    if (!m_outputArea) { m_outputArea = new QPlainTextEdit(m_viewStack); m_outputArea->setReadOnly(true); }
    m_outputArea->setFrameShape(QFrame::NoFrame);
    m_viewStack->addWidget(m_outputArea);

    if (rootVBox) rootVBox->addWidget(m_viewStack, 1);

    m_searchProcess = new QProcess(this);
    m_searchDebounceTimer = new QTimer(this);
    m_searchDebounceTimer->setSingleShot(true);
    m_searchDebounceTimer->setInterval(300);
    connect(m_searchDebounceTimer, &QTimer::timeout, this, [this]() {
        executePackageSearch(m_searchEdit ? m_searchEdit->text() : QString());
    });

    if (m_searchEdit) {
        connect(m_searchEdit, &QLineEdit::textEdited, this, [this]() { m_searchDebounceTimer->start(); });
        connect(m_searchEdit, &QLineEdit::returnPressed, this, [this]() {
            m_searchDebounceTimer->stop();
            executePackageSearch(m_searchEdit->text());
        });
    }
    connect(m_backendCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        executePackageSearch(m_searchEdit ? m_searchEdit->text() : QString());
    });
    connect(m_statusFilterCombo, &QComboBox::currentIndexChanged, this, [this](int) { refreshFilteredViews(); });
    if (m_refreshBtn) {
        connect(m_refreshBtn, &QPushButton::clicked, this, [this]() {
            executePackageSearch(m_searchEdit ? m_searchEdit->text() : QString());
        });
    }
    if (m_clearBtn) {
        connect(m_clearBtn, &QPushButton::clicked, this, [this]() {
            if (m_searchEdit) m_searchEdit->clear();
            m_allRecords.clear();
            m_packageTable->setRowCount(0);
            m_listBrowser->clear();
            if (m_outputArea) m_outputArea->clear();
            m_visibleCount = 0;
            if (m_countLabel) m_countLabel->setText(tr("0 packages"));
        });
    }

    if (m_countLabel) m_countLabel->setText(tr("0 packages"));
    setViewMode(QStringLiteral("Table"));
}

/** @brief Detects all installed HPC and system package managers in priority order. */
QStringList HpcConsoleWidget::detectAvailablePackageManagers() {
    static QStringList s_detected;
    if (!s_detected.isEmpty()) return s_detected;
    struct Candidate { const char* binary; const char* id; };
    static const Candidate kCandidates[] = {
        {"spack", "spack"}, {"pacman", "pacman"}, {"dnf", "dnf"},
        {"apt-cache", "apt"}, {"apt", "apt"}, {"zypper", "zypper"},
        {"rpm", "rpm"}, {"dpkg-query", "dpkg"}, {"eb", "eb"},
    };
    for (const auto& cand : kCandidates) {
        if (!QStandardPaths::findExecutable(QString::fromLatin1(cand.binary)).isEmpty()) {
            const QString id = QString::fromLatin1(cand.id);
            if (!s_detected.contains(id)) s_detected.append(id);
        }
    }
    if (s_detected.isEmpty()) s_detected.append(QStringLiteral("rpm"));
    return s_detected;
}

QString HpcConsoleWidget::activePackageManagerBackend() const {
    if (!m_backendCombo) return m_detectedBackend.isEmpty() ? QStringLiteral("rpm") : m_detectedBackend;
    const QString data = m_backendCombo->currentData().toString();
    if (data.isEmpty() || data == QLatin1String("auto")) {
        return m_detectedBackend.isEmpty() ? QStringLiteral("rpm") : m_detectedBackend;
    }
    return data;
}

void HpcConsoleWidget::setPackageManagerBackend(const QString& backend) {
    if (!m_backendCombo) return;
    const QString norm = backend.trimmed().toLower();
    const int idx = m_backendCombo->findData(norm);
    if (idx >= 0) m_backendCombo->setCurrentIndex(idx);
    else {
        m_detectedBackend = norm;
        m_backendCombo->setItemText(0, QStringLiteral("Auto (%1)").arg(m_detectedBackend));
    }
}

/** @brief Queries the local installed package database (`rpm -qa`, `dpkg-query -W`, `pacman -Qs`). */
QVector<PackageRecord> HpcConsoleWidget::queryLocalInstalledPackages(
    const QString& backend, const QString& filterQuery, QString* outRawLog) const {
    const QString pm = backend.trimmed().toLower(), q = filterQuery.trimmed();
    QString prog, parseTag = pm; QStringList args;
    if ((pm == QLatin1String("dnf") || pm == QLatin1String("rpm") || pm == QLatin1String("zypper")) &&
        !QStandardPaths::findExecutable(QStringLiteral("rpm")).isEmpty()) {
        prog = QStringLiteral("rpm"); parseTag = QStringLiteral("rpm");
        args = {QStringLiteral("-qa"), QStringLiteral("--qf"), QStringLiteral("%{NAME}|%{VERSION}-%{RELEASE}|%{ARCH}|installed|Installed|%{SUMMARY}\n")};
    } else if ((pm == QLatin1String("apt") || pm == QLatin1String("dpkg")) &&
               !QStandardPaths::findExecutable(QStringLiteral("dpkg-query")).isEmpty()) {
        prog = QStringLiteral("dpkg-query"); parseTag = QStringLiteral("dpkg");
        args = {QStringLiteral("-W"), QStringLiteral("-f=${Package}|${Version}|${Architecture}|installed|Installed|${binary:Summary}\n")};
    } else if (pm == QLatin1String("pacman") && !QStandardPaths::findExecutable(QStringLiteral("pacman")).isEmpty()) {
        prog = QStringLiteral("pacman"); parseTag = QStringLiteral("pacman");
        args = q.isEmpty() ? QStringList{QStringLiteral("-Qs")} : QStringList{QStringLiteral("-Qs"), q};
    }
    QVector<PackageRecord> matched;
    if (prog.isEmpty()) return matched;

    static QString s_cachedLocalProg;
    static QVector<PackageRecord> s_cachedLocalRecords;
    QVector<PackageRecord> allLocal;
    if (prog != QLatin1String("pacman") && s_cachedLocalProg == prog && !s_cachedLocalRecords.isEmpty()) {
        allLocal = s_cachedLocalRecords;
    } else {
        QProcess proc; proc.start(prog, args);
        if (proc.waitForFinished(2500) && proc.exitStatus() == QProcess::NormalExit) {
            allLocal = parsePackageManagerOutput(parseTag, QString::fromUtf8(proc.readAllStandardOutput()));
            std::sort(allLocal.begin(), allLocal.end(), [](const PackageRecord& a, const PackageRecord& b) {
                return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
            });
            if (prog != QLatin1String("pacman") && !allLocal.isEmpty()) { s_cachedLocalProg = prog; s_cachedLocalRecords = allLocal; }
        }
    }

    QString rawBuilder = QStringLiteral("$ %1 %2\n").arg(prog, args.join(QLatin1Char(' ')));
    for (const auto& rec : allLocal) {
        if (!q.isEmpty() && !rec.name.contains(q, Qt::CaseInsensitive) && !rec.description.contains(q, Qt::CaseInsensitive)) continue;
        matched.append(rec);
        rawBuilder += QStringLiteral("%1|%2|%3|%4|%5|%6\n").arg(rec.name, rec.version, rec.arch, rec.repository, rec.status, rec.description);
        if (q.isEmpty() && matched.size() >= 12) break;
    }
    if (outRawLog) *outRawLog = rawBuilder;
    return matched;
}

void HpcConsoleWidget::ensurePackagesLoaded() {
    if (m_packagesLoaded) return;
    m_packagesLoaded = true;
    const QString pm = activePackageManagerBackend().trimmed().toLower();
    QString prog, parseTag = pm; QStringList args;
    if ((pm == QLatin1String("dnf") || pm == QLatin1String("rpm") || pm == QLatin1String("zypper")) &&
        !QStandardPaths::findExecutable(QStringLiteral("rpm")).isEmpty()) {
        prog = QStringLiteral("rpm"); parseTag = QStringLiteral("rpm");
        args = {QStringLiteral("-qa"), QStringLiteral("--qf"), QStringLiteral("%{NAME}|%{VERSION}-%{RELEASE}|%{ARCH}|installed|Installed|%{SUMMARY}\n")};
    } else if ((pm == QLatin1String("apt") || pm == QLatin1String("dpkg")) &&
               !QStandardPaths::findExecutable(QStringLiteral("dpkg-query")).isEmpty()) {
        prog = QStringLiteral("dpkg-query"); parseTag = QStringLiteral("dpkg");
        args = {QStringLiteral("-W"), QStringLiteral("-f=${Package}|${Version}|${Architecture}|installed|Installed|${binary:Summary}\n")};
    } else if (pm == QLatin1String("pacman") && !QStandardPaths::findExecutable(QStringLiteral("pacman")).isEmpty()) {
        prog = QStringLiteral("pacman"); parseTag = QStringLiteral("pacman"); args = {QStringLiteral("-Qs")};
    }
    if (prog.isEmpty()) return;
    if (m_allRecords.isEmpty()) {
        displayPackageRecords({{QStringLiteral("Loading installed packages..."), QStringLiteral("..."),
                                QStringLiteral("host"), pm, QStringLiteral("Installed"),
                                QStringLiteral("Querying local package database in background...")}},
                              QStringLiteral("$ %1 %2\nLoading installed packages...\n").arg(prog, args.join(QLatin1Char(' '))));
    }
    auto* proc = new QProcess(this);
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc, prog, args, parseTag](int, QProcess::ExitStatus status) {
                if (status == QProcess::NormalExit && (m_searchEdit == nullptr || m_searchEdit->text().trimmed().isEmpty())) {
                    QVector<PackageRecord> allLocal = parsePackageManagerOutput(parseTag, QString::fromUtf8(proc->readAllStandardOutput()));
                    std::sort(allLocal.begin(), allLocal.end(), [](const PackageRecord& a, const PackageRecord& b) {
                        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
                    });
                    QVector<PackageRecord> matched;
                    QString rawBuilder = QStringLiteral("$ %1 %2\n").arg(prog, args.join(QLatin1Char(' ')));
                    for (const auto& rec : allLocal) {
                        matched.append(rec);
                        rawBuilder += QStringLiteral("%1|%2|%3|%4|%5|%6\n")
                                          .arg(rec.name, rec.version, rec.arch, rec.repository, rec.status, rec.description);
                        if (matched.size() >= 12) break;
                    }
                    displayPackageRecords(matched, rawBuilder);
                }
                proc->deleteLater();
            });
    proc->start(prog, args);
}

void HpcConsoleWidget::showEvent(QShowEvent* event) {
    SpackManager::showEvent(event);
    ensurePackagesLoaded();
}

QVector<PackageRecord> HpcConsoleWidget::mergePackageRecords(
    const QVector<PackageRecord>& installed, const QVector<PackageRecord>& repoRecords) {
    QVector<PackageRecord> merged = installed;
    for (const auto& repoRec : repoRecords) {
        bool exists = false;
        for (auto& instRec : merged) {
            if (instRec.name.compare(repoRec.name, Qt::CaseInsensitive) == 0 &&
                (instRec.arch.compare(repoRec.arch, Qt::CaseInsensitive) == 0 ||
                 instRec.arch.isEmpty() || repoRec.arch.isEmpty())) {
                if (instRec.description.isEmpty() && !repoRec.description.isEmpty()) instRec.description = repoRec.description;
                exists = true;
                break;
            }
        }
        if (!exists) merged.append(repoRec);
    }
    return merged;
}

int HpcConsoleWidget::searchPackagesSynchronously(const QString& query, int timeoutMs) {
    m_packagesLoaded = true;
    const QString pm = activePackageManagerBackend(), q = query.trimmed();
    if (m_searchEdit && m_searchEdit->text() != q) {
        m_searchEdit->blockSignals(true); m_searchEdit->setText(q); m_searchEdit->blockSignals(false);
    }
    QString rawLocal;
    const QVector<PackageRecord> localMatches = queryLocalInstalledPackages(pm, q, &rawLocal);
    const int statusMode = m_statusFilterCombo ? m_statusFilterCombo->currentIndex() : 0;
    if (q.isEmpty() || statusMode == 1) {
        displayPackageRecords(localMatches, rawLocal);
        return visiblePackageCount();
    }
    QString prog; QStringList args;
    resolveRepoSearchCommand(pm, q, &prog, &args);
    QVector<PackageRecord> repoMatches; QString rawRepo;
    if (!prog.isEmpty()) {
        QProcess proc; proc.start(prog, args);
        if (!proc.waitForFinished(timeoutMs) && proc.state() != QProcess::NotRunning) {
            proc.kill(); proc.waitForFinished(500);
        }
        rawRepo = QString::fromUtf8(proc.readAllStandardOutput());
        repoMatches = parsePackageManagerOutput(pm, rawRepo);
    }
    const QVector<PackageRecord> merged = mergePackageRecords(localMatches, repoMatches);
    const QString combinedRaw = rawLocal + (rawRepo.isEmpty() ? QString() : (QStringLiteral("\n$ %1 %2\n").arg(prog, args.join(QLatin1Char(' '))) + rawRepo));
    displayPackageRecords(merged, combinedRaw);
    return visiblePackageCount();
}

void HpcConsoleWidget::executePackageSearch(const QString& query) {
    m_packagesLoaded = true;
    const QString pm = activePackageManagerBackend(), q = query.trimmed();
    QString rawLocal;
    const QVector<PackageRecord> localMatches = queryLocalInstalledPackages(pm, q, &rawLocal);
    displayPackageRecords(localMatches, rawLocal);
    const int statusMode = m_statusFilterCombo ? m_statusFilterCombo->currentIndex() : 0;
    if (q.isEmpty() || statusMode == 1 || !m_searchProcess) return;

    if (m_searchProcess->state() != QProcess::NotRunning) {
        m_searchProcess->disconnect(this); m_searchProcess->kill(); m_searchProcess->waitForFinished(500);
    }
    m_searchProcess->disconnect();
    QString prog; QStringList args;
    resolveRepoSearchCommand(pm, q, &prog, &args);
    if (prog.isEmpty()) return;

    connect(m_searchProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, pm, localMatches, rawLocal, prog, args](int, QProcess::ExitStatus) {
                const QString rawRepo = QString::fromUtf8(m_searchProcess->readAllStandardOutput());
                const QVector<PackageRecord> repoMatches = parsePackageManagerOutput(pm, rawRepo);
                const QVector<PackageRecord> merged = mergePackageRecords(localMatches, repoMatches);
                const QString combined = rawLocal + QStringLiteral("\n$ %1 %2\n").arg(prog, args.join(QLatin1Char(' '))) + rawRepo;
                displayPackageRecords(merged, combined);
            });
    m_searchProcess->start(prog, args);
}

HpcConsoleWidget::~HpcConsoleWidget() {
    if (m_searchDebounceTimer) m_searchDebounceTimer->stop();
    for (QProcess* proc : findChildren<QProcess*>()) {
        if (proc && proc->state() != QProcess::NotRunning) {
            proc->disconnect(this); proc->kill(); proc->waitForFinished(500);
        }
    }
}

}  // namespace gridlock::ui
