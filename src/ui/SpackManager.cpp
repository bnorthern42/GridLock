#include "SpackManager.hpp"
#include <QFont>
#include <QScrollBar>

namespace gridlock::ui {

SpackManager::SpackManager(gridlock::core::HpcBackend *backend, QWidget *parent)
    : QWidget(parent)
{
    // ── Layout ──────────────────────────────────────────────────────────────
    auto *vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(4, 4, 4, 4);
    vbox->setSpacing(4);

    // ── Top toolbar ─────────────────────────────────────────────────────────
    auto *toolbar = new QHBoxLayout();

    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText(tr("Search packages…"));
    m_filterEdit->setClearButtonEnabled(true);
    m_filterEdit->setToolTip(tr(
        "Filter the spack find output locally (no remote round-trip)."));

    m_fetchButton = new QPushButton(tr("⟳ Refresh Packages"), this);
    m_fetchButton->setToolTip(tr("Run 'spack find' on the remote host."));
    m_fetchButton->setFixedWidth(145);

    m_clearButton = new QPushButton(tr("Clear"), this);
    m_clearButton->setFixedWidth(56);

    m_statusLabel = new QLabel(tr("Idle"), this);
    m_statusLabel->setStyleSheet("color: #a6adc8; font-size: 11px;");

    toolbar->addWidget(m_filterEdit, 1);
    toolbar->addWidget(m_fetchButton);
    toolbar->addWidget(m_clearButton);
    toolbar->addWidget(m_statusLabel);
    vbox->addLayout(toolbar);

    // ── Console output ───────────────────────────────────────────────────────
    m_console = new QPlainTextEdit(this);
    m_console->setReadOnly(true);
    m_console->setTextInteractionFlags(
        Qt::TextBrowserInteraction | Qt::TextSelectableByKeyboard);
    m_console->setMaximumBlockCount(5000);   // cap memory usage
    QFont mono("Monospace");
    mono.setStyleHint(QFont::TypeWriter);
    mono.setPointSize(10);
    m_console->setFont(mono);
    vbox->addWidget(m_console, 1);

    // ── Connections ──────────────────────────────────────────────────────────
    connect(m_fetchButton, &QPushButton::clicked,
            this, &SpackManager::onFetchPackages);
    connect(m_clearButton, &QPushButton::clicked,
            this, &SpackManager::onClearClicked);
    connect(m_filterEdit, &QLineEdit::textChanged,
            this, &SpackManager::onFilterChanged);

    setBackend(backend);
}

void SpackManager::setBackend(gridlock::core::HpcBackend *backend)
{
    if (m_backend) {
        disconnect(m_backend, nullptr, this, nullptr);
    }
    m_backend = backend;
    if (m_backend) {
        reconnectBackend();
    }
}

void SpackManager::reconnectBackend()
{
    connect(m_backend, &gridlock::core::HpcBackend::packagesReady,
            this,      &SpackManager::onPackagesReady,
            Qt::UniqueConnection);
    connect(m_backend, &gridlock::core::HpcBackend::slurmJobSubmitted,
            this,      &SpackManager::onJobSubmitted,
            Qt::UniqueConnection);
}

// ── Public interface ─────────────────────────────────────────────────────────

void SpackManager::appendMessage(const QString &msg, bool isError)
{
    const QString line = isError
        ? QString("<span style='color:#f38ba8;'>%1</span>").arg(msg.toHtmlEscaped())
        : msg;
    // QPlainTextEdit doesn't render HTML; use plain text with a visual prefix.
    const QString prefix = isError ? "[ERR] " : "[HPC] ";
    m_console->appendPlainText(prefix + msg.trimmed());
    m_console->verticalScrollBar()->setValue(
        m_console->verticalScrollBar()->maximum());
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void SpackManager::onFetchPackages()
{
    if (!m_backend) return;
    m_fetchButton->setEnabled(false);
    m_statusLabel->setText(tr("Fetching…"));
    m_console->appendPlainText(tr("[HPC] Running: spack find …"));
    m_backend->fetchPackages();
}

void SpackManager::onPackagesReady(const gridlock::core::HpcResult &result)
{
    m_fetchButton->setEnabled(true);

    if (!result.success) {
        m_statusLabel->setText(tr("Error"));
        m_console->appendPlainText(
            "[ERR] spack find failed: " + result.errorOutput.trimmed());
        return;
    }

    // Store lines for local filter
    m_packageLines = result.output.split('\n', Qt::SkipEmptyParts);
    m_statusLabel->setText(
        tr("%1 packages").arg(
            // Count non-bracket lines as "packages"
            std::count_if(m_packageLines.begin(), m_packageLines.end(),
                          [](const QString &l){ return !l.startsWith('['); })));
    applyFilter();
}

void SpackManager::onJobSubmitted(const gridlock::core::HpcResult &result)
{
    if (result.success) {
        appendMessage(result.output.trimmed());
        emit jobSubmitted(result.output);
    } else {
        appendMessage(result.errorOutput.trimmed(), /*isError=*/true);
    }
}

void SpackManager::onFilterChanged(const QString & /*text*/)
{
    applyFilter();
}

void SpackManager::onClearClicked()
{
    m_console->clear();
    m_packageLines.clear();
    m_statusLabel->setText(tr("Idle"));
}

// ── Private ───────────────────────────────────────────────────────────────────

void SpackManager::applyFilter()
{
    const QString filter = m_filterEdit->text().trimmed();
    m_console->clear();

    int shown = 0;
    for (const QString &line : m_packageLines) {
        if (filter.isEmpty() ||
            line.contains(filter, Qt::CaseInsensitive)) {
            m_console->appendPlainText(line);
            ++shown;
        }
    }

    if (!filter.isEmpty()) {
        m_console->appendPlainText(
            QString("\n[HPC] Filter '%1' matched %2 / %3 lines")
                .arg(filter).arg(shown).arg(m_packageLines.size()));
    }

    m_console->verticalScrollBar()->setValue(
        m_console->verticalScrollBar()->maximum());
}

} // namespace gridlock::ui
