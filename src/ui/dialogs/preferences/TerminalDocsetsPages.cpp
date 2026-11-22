/**
 * @file TerminalDocsetsPages.cpp
 * @brief Implementations of DocsetSettingsPage and TerminalSettingsPage widgets.
 */

#include "TerminalDocsetsPages.hpp"
#include "PreferencesFormHelpers.hpp"
#include "../../utils/FileDialogHelper.hpp"
#include "../../../core/managers/ConfigManager.hpp"
#include "../../../core/managers/DocsetManager.hpp"

#include <QDir>
#include <QFileDialog>
#include <QFrame>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QVBoxLayout>

namespace gridlock::ui {

using prefs_detail::addBrowsePathRow;
using prefs_detail::createStandardPageForm;
using prefs_detail::discoverMonospaceFontFamilies;

// ============================================================================
// DocsetSettingsPage Implementation
// ============================================================================

/**
 * @brief Constructs offline documentation docset management page.
 * @param parent Optional parent widget.
 */
DocsetSettingsPage::DocsetSettingsPage(QWidget* parent)
    : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 16, 20, 24);
    layout->setSpacing(14);

    auto* heading = new QLabel(tr("<b>Offline Docsets</b>"), this);
    heading->setObjectName(QStringLiteral("heading"));
    layout->addWidget(heading);

    auto* separator = new QLabel(this);
    separator->setFixedHeight(1);
    separator->setObjectName(QStringLiteral("separator"));
    layout->addWidget(separator);

    // Directory selection row with Browse dialog and 1-click Zeal auto-detection.
    auto* dirLayout = new QHBoxLayout();
    dirLayout->addWidget(new QLabel(QStringLiteral("Docset Directory:"), this));
    m_dirEdit = new QLineEdit(this);
    dirLayout->addWidget(m_dirEdit);
    auto* browseBtn = new QPushButton(QStringLiteral("Browse..."), this);
    browseBtn->setMinimumWidth(92);
    connect(browseBtn, &QPushButton::clicked, this, [this]() {
        // Resolve default Zeal docsets directory under XDG user data or fallback to Home.
        const QString zealDir = QDir::homePath() + QStringLiteral("/.local/share/Zeal/Zeal/docsets");
        QString startDir = m_dirEdit->text().trimmed();
        if (startDir.isEmpty() || startDir == QStringLiteral("/") || !QDir(startDir).exists()) {
            startDir = QDir(zealDir).exists() ? zealDir : QDir::homePath();
        }
        // Configure non-native directory chooser with pruned quick-jump sidebar URLs and unclipped columns.
        QFileDialog dlg(this, tr("Select Docset Directory"), startDir);
        dlg.setFileMode(QFileDialog::Directory);
        dlg.setOption(QFileDialog::ShowDirsOnly, true);
        FileDialogHelper::configureFileDialog(&dlg, startDir);
        // Persist selected directory path and immediately reload docset indices.
        if (dlg.exec() == QDialog::Accepted && !dlg.selectedFiles().isEmpty()) {
            const QString chosen = dlg.selectedFiles().first();
            m_dirEdit->setText(chosen);
            gridlock::core::DocsetManager::instance().setDocsetDirectory(chosen);
            refreshTable();
        }
    });
    dirLayout->addWidget(browseBtn);

    // One-click button to discover and attach installed Zeal docsets from standard paths.
    auto* detectZealBtn = new QPushButton(QStringLiteral("Auto-Detect Zeal Docsets"), this);
    connect(detectZealBtn, &QPushButton::clicked, this, [this]() {
        const QString zealDir = QDir::homePath() + QStringLiteral("/.local/share/Zeal/Zeal/docsets");
        if (QDir(zealDir).exists()) m_dirEdit->setText(zealDir);
        gridlock::core::DocsetManager::instance().discoverAndLoadSystemDocsets();
        refreshTable();
    });
    dirLayout->addWidget(detectZealBtn);
    layout->addLayout(dirLayout);

    // Heuristic source-tree scanner button recommending relevant HPC/C++ docsets.
    m_autoDetectBtn = new QPushButton(QStringLiteral("Auto-Detect Needs"), this);
    connect(m_autoDetectBtn, &QPushButton::clicked, this, &DocsetSettingsPage::onAutoDetect);
    layout->addWidget(m_autoDetectBtn, 0, Qt::AlignLeft);

    // Configure borderless docsets table with alternating row colors and 48px minimum section size.
    m_table = new QTableWidget(0, 1, this);
    m_table->setFrameShape(QFrame::NoFrame);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->setHorizontalHeaderLabels({QStringLiteral("Installed Docsets")});
    m_table->horizontalHeader()->setMinimumSectionSize(48);
    m_table->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(m_table);

    loadFromSettings();
}

QString DocsetSettingsPage::docsetDirectory() const { return m_dirEdit->text(); }

/**
 * @brief Restores active docset directory path from settings or defaults to ~/.local/share/Zeal.
 */
void DocsetSettingsPage::loadFromSettings() {
    QString configuredDir = gridlock::core::ConfigManager::instance().getDocsetDirectory().trimmed();
    const QString zealDir = QDir::homePath() + QStringLiteral("/.local/share/Zeal/Zeal/docsets");
    if (configuredDir.isEmpty() || configuredDir == QStringLiteral("/")) {
        configuredDir = QDir(zealDir).exists() ? zealDir : QDir::homePath();
    }
    m_dirEdit->setText(configuredDir);
    refreshTable();
}

/**
 * @brief Refreshes the table view with paths of all discovered docset bundles.
 */
void DocsetSettingsPage::refreshTable() {
    m_table->setRowCount(0);
    const auto paths = gridlock::core::DocsetManager::instance().getActiveDocsetPaths();
    for (int i = 0; i < paths.size(); ++i) {
        m_table->insertRow(i);
        m_table->setItem(i, 0, new QTableWidgetItem(paths[i]));
    }
}

/**
 * @brief Runs background heuristic detection of project languages to recommend docsets.
 */
void DocsetSettingsPage::onAutoDetect() {
    m_autoDetectBtn->setText(QStringLiteral("Detecting..."));
    m_autoDetectBtn->setEnabled(false);

    auto* watcher = new QFutureWatcher<QStringList>(this);
    connect(watcher, &QFutureWatcher<QStringList>::finished, this,
            [this, watcher]() {
                const QStringList suggestions = watcher->result();
                m_autoDetectBtn->setText(
                    suggestions.isEmpty() ? QStringLiteral("No extra docsets needed")
                                          : QStringLiteral("Suggested: ") + suggestions.join(QStringLiteral(", ")));
                m_autoDetectBtn->setEnabled(true);
                watcher->deleteLater();
            });
    watcher->setFuture(
        gridlock::core::DocsetManager::instance().suggestDocsets(QDir::currentPath()));
}

// ============================================================================
// TerminalSettingsPage Implementation
// ============================================================================

/**
 * @brief Constructs embedded terminal emulator configuration page.
 * @param parent Optional parent widget.
 */
TerminalSettingsPage::TerminalSettingsPage(QWidget* parent)
    : QWidget(parent) {
    auto* form = createStandardPageForm(this, tr("<b>Terminal</b>"));

    m_shellPathEdit = addBrowsePathRow(
        this, form, tr("Shell Path:"), tr("/bin/bash"),
        tr("Path to the shell executable (e.g. /bin/bash, /bin/zsh). Requires terminal restart."),
        tr("Select Shell Executable"), QStringLiteral("/bin"));

    m_fontFamilyCombo = new QComboBox(this);
    m_fontFamilyCombo->setObjectName(QStringLiteral("terminalFontFamilyCombo"));
    m_fontFamilyCombo->addItems(discoverMonospaceFontFamilies());
    m_fontFamilyCombo->setToolTip(tr("Monospace font for the embedded terminal."));
    form->addRow(tr("Font Family:"), m_fontFamilyCombo);

    m_fontSizeBox = new QSpinBox(this);
    m_fontSizeBox->setRange(6, 72);
    m_fontSizeBox->setToolTip(tr("Font size in points for the terminal."));
    form->addRow(tr("Font Size:"), m_fontSizeBox);

    loadFromSettings();
}

QString TerminalSettingsPage::shellPath() const { return m_shellPathEdit->text().trimmed(); }
QString TerminalSettingsPage::fontFamily() const { return m_fontFamilyCombo->currentText(); }
int TerminalSettingsPage::fontSize() const { return m_fontSizeBox->value(); }

/**
 * @brief Loads terminal font and shell preferences from ConfigManager.
 */
void TerminalSettingsPage::loadFromSettings() {
    const auto ts = gridlock::core::ConfigManager::instance().getTerminalSettings();
    m_shellPathEdit->setText(ts.shellPath);
    const int fIdx = m_fontFamilyCombo->findText(ts.fontFamily);
    if (fIdx >= 0) m_fontFamilyCombo->setCurrentIndex(fIdx);
    else m_fontFamilyCombo->setCurrentText(ts.fontFamily);
    m_fontSizeBox->setValue(ts.fontSize);
}

}  // namespace gridlock::ui
