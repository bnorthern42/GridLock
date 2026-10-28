/**
 * @file SpackManager.cpp
 * @brief Implementation of the Spack package browser and SLURM output console widget.
 *
 * Builds the search toolbar and read-only monospace console, wires asynchronous
 * HpcBackend signals (`packagesReady`, `slurmJobSubmitted`), and performs local
 * case-insensitive substring filtering over cached `spack find` output lines.
 */

#include "SpackManager.hpp"

#include <algorithm>

#include <QFont>
#include <QScrollBar>

namespace gridlock::ui {

/**
 * @brief Constructs the HPC console UI and attaches the initial HpcBackend.
 */
SpackManager::SpackManager(gridlock::core::HpcBackend* backend, QWidget* parent)
    : QWidget(parent) {
    // Root vertical layout holding the top filter toolbar and bottom console editor.
    auto* vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(4, 4, 4, 4);
    vbox->setSpacing(4);

    // Top search and action toolbar row.
    auto* toolbar = new QHBoxLayout();

    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText(tr("Search packages…"));
    m_filterEdit->setClearButtonEnabled(true);
    m_filterEdit->setToolTip(tr("Filter the spack find output locally (no remote round-trip)."));

    m_fetchButton = new QPushButton(tr("⟳ Refresh Packages"), this);
    m_fetchButton->setToolTip(tr("Run 'spack find' on the remote host."));
    m_fetchButton->setFixedWidth(145);

    m_clearButton = new QPushButton(tr("Clear"), this);
    m_clearButton->setFixedWidth(56);

    m_statusLabel = new QLabel(tr("Idle"), this);
    m_statusLabel->setObjectName("status");

    toolbar->addWidget(m_filterEdit, 1);
    toolbar->addWidget(m_fetchButton);
    toolbar->addWidget(m_clearButton);
    toolbar->addWidget(m_statusLabel);
    vbox->addLayout(toolbar);

    // Read-only monospace output log capped at 5,000 blocks to bound memory usage.
    m_console = new QPlainTextEdit(this);
    m_console->setReadOnly(true);
    m_console->setTextInteractionFlags(Qt::TextBrowserInteraction | Qt::TextSelectableByKeyboard);
    m_console->setMaximumBlockCount(5000);
    QFont mono("Monospace");
    mono.setStyleHint(QFont::TypeWriter);
    mono.setPointSize(10);
    m_console->setFont(mono);
    vbox->addWidget(m_console, 1);

    // Wire toolbar button clicks and live filter text changes.
    connect(m_fetchButton, &QPushButton::clicked, this, &SpackManager::onFetchPackages);
    connect(m_clearButton, &QPushButton::clicked, this, &SpackManager::onClearClicked);
    connect(m_filterEdit, &QLineEdit::textChanged, this, &SpackManager::onFilterChanged);

    setBackend(backend);
}

/**
 * @brief Disconnects any prior backend and binds signal handlers to the new backend.
 */
void SpackManager::setBackend(gridlock::core::HpcBackend* backend) {
    if (m_backend) {
        disconnect(m_backend, nullptr, this, nullptr);
    }
    m_backend = backend;
    if (m_backend) {
        reconnectBackend();
    }
}

/**
 * @brief Connects packagesReady and slurmJobSubmitted signals using Qt::UniqueConnection.
 */
void SpackManager::reconnectBackend() {
    connect(m_backend, &gridlock::core::HpcBackend::packagesReady,
            this, &SpackManager::onPackagesReady, Qt::UniqueConnection);
    connect(m_backend, &gridlock::core::HpcBackend::slurmJobSubmitted,
            this, &SpackManager::onJobSubmitted, Qt::UniqueConnection);
}

/**
 * @brief Appends a prefixed `[HPC]` or `[ERR]` line to the console and scrolls to bottom.
 */
void SpackManager::appendMessage(const QString& msg, bool isError) {
    // Prefix error messages with `[ERR] ` and standard telemetry with `[HPC] `.
    const QString prefix = isError ? "[ERR] " : "[HPC] ";
    m_console->appendPlainText(prefix + msg.trimmed());
    m_console->verticalScrollBar()->setValue(m_console->verticalScrollBar()->maximum());
}

/**
 * @brief Disables the refresh button, updates status to "Fetching…", and invokes fetchPackages().
 */
void SpackManager::onFetchPackages() {
    if (!m_backend) {
        return;
    }
    m_fetchButton->setEnabled(false);
    m_statusLabel->setText(tr("Fetching…"));
    m_console->appendPlainText(tr("[HPC] Running: spack find …"));
    m_backend->fetchPackages();
}

/**
 * @brief Processes the HpcResult from `spack find` and updates the package count badge.
 */
void SpackManager::onPackagesReady(const gridlock::core::HpcResult& result) {
    m_fetchButton->setEnabled(true);

    if (!result.success) {
        m_statusLabel->setText(tr("Error"));
        m_console->appendPlainText("[ERR] spack find failed: " + result.errorOutput.trimmed());
        return;
    }

    // Cache non-empty lines and count package entries (lines not starting with '[').
    m_packageLines = result.output.split('\n', Qt::SkipEmptyParts);
    m_statusLabel->setText(
        tr("%1 packages").arg(
            std::count_if(m_packageLines.begin(), m_packageLines.end(),
                          [](const QString& l) { return !l.startsWith('['); })));
    applyFilter();
}

/**
 * @brief Logs SLURM batch job submission output or error diagnostics.
 */
void SpackManager::onJobSubmitted(const gridlock::core::HpcResult& result) {
    if (result.success) {
        appendMessage(result.output.trimmed());
        emit jobSubmitted(result.output);
    } else {
        appendMessage(result.errorOutput.trimmed(), /*isError=*/true);
    }
}

/**
 * @brief Re-applies the local substring filter whenever the search input changes.
 */
void SpackManager::onFilterChanged(const QString& /*text*/) {
    applyFilter();
}

/**
 * @brief Clears the console buffer and cached Spack package list.
 */
void SpackManager::onClearClicked() {
    m_console->clear();
    m_packageLines.clear();
    m_statusLabel->setText(tr("Idle"));
}

/**
 * @brief Filters m_packageLines against the current search box text and updates m_console.
 */
void SpackManager::applyFilter() {
    const QString filter = m_filterEdit->text().trimmed();
    m_console->clear();

    // Append all package lines matching the case-insensitive filter substring.
    int shown = 0;
    for (const QString& line : m_packageLines) {
        if (filter.isEmpty() || line.contains(filter, Qt::CaseInsensitive)) {
            m_console->appendPlainText(line);
            ++shown;
        }
    }

    // Append a summary footer when a non-empty filter is active.
    if (!filter.isEmpty()) {
        m_console->appendPlainText(
            QString("\n[HPC] Filter '%1' matched %2 / %3 lines")
                .arg(filter)
                .arg(shown)
                .arg(m_packageLines.size()));
    }

    m_console->verticalScrollBar()->setValue(m_console->verticalScrollBar()->maximum());
}

} // namespace gridlock::ui
