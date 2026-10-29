/**
 * @file ReferenceManualWidget.cpp
 * @brief Implementation of offline reference manual and documentation viewers.
 *
 * Implements DocsetViewerWidget for searching and rendering Dash/Zeal SQLite
 * docsets, ManPageViewerWidget for querying system man pages via HTML
 * conversion, and ReferenceManualWidget as a unified tabbed container with
 * persistent user preferences.
 */

#include "ReferenceManualWidget.hpp"
#include "../../core/managers/DocsetManager.hpp"

#include <QColor>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QUrl>
#include <QVBoxLayout>

namespace gridlock::ui {

// ============================================================================
// DocsetViewerWidget Implementation
// ============================================================================

/**
 * @brief Constructs the docset viewer with dual-pane search and reading view.
 * @param parent Optional parent widget.
 *
 * Configures an asymmetrical horizontal QSplitter. The left pane hosts a 26px
 * #21252b search header bar and a borderless results list. The right pane hosts
 * a flush QTextBrowser rendering dark-slate HTML documentation.
 */
DocsetViewerWidget::DocsetViewerWidget(QWidget* parent)
    : QWidget(parent) {
    // Top-level zero-margin vertical layout hosting the primary splitter.
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Primary horizontal splitter dividing search results from document view.
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setContentsMargins(0, 0, 0, 0);
    splitter->setHandleWidth(1);

    // Left pane container: borderless results list view beneath the unified ReferenceManualWidget header.
    auto* leftWidget = new QWidget(splitter);
    auto* leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);

    // Docset bundle filter dropdown (All Docsets, discovered Zeal docsets, and Built-in MPI/ABI).
    m_docsetSelector = new QComboBox(this);
    m_docsetSelector->setObjectName(QStringLiteral("docsetBundleSelector"));
    m_docsetSelector->setFixedHeight(22);
    m_docsetSelector->setMinimumWidth(112);

    // Search query input field capped at 22px height.
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setObjectName(QStringLiteral("docsetSearchEdit"));
    m_searchEdit->setPlaceholderText(QStringLiteral("Search Zeal / MPI / C / C++ symbols..."));
    m_searchEdit->setFixedHeight(22);

    // Results list widget populated dynamically by DocsetManager queries.
    m_resultsList = new QListWidget(leftWidget);
    m_resultsList->setObjectName(QStringLiteral("docsetResultsList"));
    m_resultsList->setFrameShape(QFrame::NoFrame);
    m_resultsList->setStyleSheet(QStringLiteral(
        "QListWidget#docsetResultsList { background: #1b1e24; color: #abb2bf; border: none; "
        "border-right: 1px solid #282c34; font-family: 'JetBrains Mono', monospace; font-size: 11px; }"
        "QListWidget#docsetResultsList::item { padding: 3px 8px; min-height: 20px; }"
        "QListWidget#docsetResultsList::item:selected { background: #264f78; color: #ffffff; }"));

    leftLayout->addWidget(m_resultsList, 1);

    // Right pane container: flush HTML text browser with external link handling.
    m_textBrowser = new QTextBrowser(splitter);
    m_textBrowser->setObjectName(QStringLiteral("docsetTextBrowser"));
    m_textBrowser->setFrameShape(QFrame::NoFrame);
    m_textBrowser->setOpenExternalLinks(true);
    m_textBrowser->setReadOnly(true);
    m_textBrowser->setStyleSheet(QStringLiteral(
        "QTextBrowser#docsetTextBrowser { background: #16181d; color: #d7dae0; border: none; }"));

    // Configure high-contrast dark slate palette for document view.
    QPalette pal = m_textBrowser->palette();
    pal.setColor(QPalette::Base, QColor("#16181d"));
    pal.setColor(QPalette::Text, QColor("#d7dae0"));
    pal.setColor(QPalette::Link, QColor("#61afef"));
    m_textBrowser->setPalette(pal);

    // Add widgets to splitter and allocate generous viewport to documentation.
    splitter->addWidget(leftWidget);
    splitter->addWidget(m_textBrowser);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);
    splitter->setSizes({260, 740});

    mainLayout->addWidget(splitter, 1);

    // Wire live incremental search query filtering, docset selector changes, and item selection triggers.
    connect(m_searchEdit, &QLineEdit::textChanged, this,
            &DocsetViewerWidget::performSearch);
    connect(m_docsetSelector, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        performSearch();
    });
    connect(m_resultsList, &QListWidget::currentRowChanged, this,
            &DocsetViewerWidget::onResultClicked);

    // Discover installed Zeal docsets, populate selector and results list, and render MPI_Init initially.
    refreshDocsets();
    showSymbolDocumentation(QStringLiteral("MPI_Init"));
}

/**
 * @brief Default virtual destructor for DocsetViewerWidget.
 */
DocsetViewerWidget::~DocsetViewerWidget() = default;

/**
 * @brief Re-discovers installed Zeal docset bundles on disk and repopulates the selector combo box.
 */
void DocsetViewerWidget::refreshDocsets() {
    auto& mgr = gridlock::core::DocsetManager::instance();
    if (mgr.loadedDocsetNames().isEmpty()) {
        mgr.discoverAndLoadSystemDocsets();
    }
    const QStringList zealNames = mgr.loadedDocsetNames();

    if (m_docsetSelector) {
        m_docsetSelector->blockSignals(true);
        m_docsetSelector->clear();
        m_docsetSelector->addItem(
            QStringLiteral("All Docsets (%1)").arg(zealNames.size() + 1),
            QStringLiteral("All Docsets"));
        for (const QString& dsName : zealNames) {
            m_docsetSelector->addItem(dsName, dsName);
        }
        m_docsetSelector->addItem(QStringLiteral("Built-in MPI/ABI"),
                                  QStringLiteral("Built-in MPI/ABI"));
        m_docsetSelector->blockSignals(false);
    }
    performSearch();
}

/**
 * @brief Returns the list of loaded Zeal docset bundle names (plus Built-in MPI/ABI).
 * @return Non-empty list of active docset names.
 */
QStringList DocsetViewerWidget::loadedDocsetNames() const {
    QStringList names = gridlock::core::DocsetManager::instance().loadedDocsetNames();
    names.append(QStringLiteral("Built-in MPI/ABI"));
    return names;
}

/**
 * @brief Looks up and renders formatted HTML documentation for the specified symbol.
 * @param rawSymbol Symbol identifier (e.g., "MPI_Init@plt" or "MPI_Send").
 * @return True if documentation was resolved and rendered; false if symbol is empty.
 */
bool DocsetViewerWidget::showSymbolDocumentation(const QString& rawSymbol) {
    const QString normalized = gridlock::core::DocsetManager::normalizeSymbolName(rawSymbol);
    if (normalized.isEmpty()) {
        return false;
    }

    auto& mgr = gridlock::core::DocsetManager::instance();
    // Check if an installed Zeal docset has an exact symbol match (unless it is a built-in MPI/ABI topic).
    if (!mgr.builtInTopics().contains(normalized, Qt::CaseInsensitive)) {
        const auto zealHits = mgr.search(normalized);
        for (const auto& hit : zealHits) {
            if (hit.first.compare(normalized, Qt::CaseInsensitive) == 0 &&
                !hit.second.startsWith(QStringLiteral("builtin://"))) {
                const QString zealHtml = mgr.getEntryHtmlForPath(hit.second);
                if (!zealHtml.isEmpty()) {
                    m_textBrowser->setHtml(zealHtml);
                    return true;
                }
            }
        }
    }

    const QString html = mgr.lookupBuiltInManualHtml(normalized);
    if (html.isEmpty()) {
        return false;
    }

    m_textBrowser->setHtml(html);

    // Highlight matching item in the left-hand topic list if present.
    m_resultsList->blockSignals(true);
    for (int i = 0; i < m_resultsList->count(); ++i) {
        if (m_resultsList->item(i)->text().compare(normalized, Qt::CaseInsensitive) == 0) {
            m_resultsList->setCurrentRow(i);
            break;
        }
    }
    m_resultsList->blockSignals(false);
    return true;
}

/**
 * @brief Executes an indexed search against active Dash/Zeal docsets and built-in topics.
 *
 * Retrieves search hits matching the current query string and selected docset filter
 * from DocsetManager, prepending matching built-in MPI/ABI topics when "All Docsets"
 * is active so both MPI reference cards and installed Zeal docsets are immediately visible.
 */
void DocsetViewerWidget::performSearch() {
    const QString query = m_searchEdit ? m_searchEdit->text().trimmed() : QString();
    const QString docsetFilter =
        (m_docsetSelector && m_docsetSelector->currentIndex() >= 0)
            ? m_docsetSelector->currentData().toString()
            : QStringLiteral("All Docsets");

    m_resultsList->clear();
    m_searchResults.clear();

    auto& mgr = gridlock::core::DocsetManager::instance();
    const bool includeBuiltIn =
        docsetFilter.isEmpty() ||
        docsetFilter.startsWith(QStringLiteral("All Docsets"), Qt::CaseInsensitive) ||
        docsetFilter.compare(QStringLiteral("Built-in MPI/ABI"), Qt::CaseInsensitive) == 0;

    // Include built-in MPI/ABI topics first when browsing All Docsets or Built-in MPI/ABI.
    if (includeBuiltIn) {
        for (const QString& topic : mgr.builtInTopics()) {
            if (query.isEmpty() || topic.contains(query, Qt::CaseInsensitive)) {
                m_searchResults.append(qMakePair(topic, QStringLiteral("builtin://") + topic));
            }
        }
    }

    // Append matching entries from installed Zeal SQLite docsets.
    if (docsetFilter.compare(QStringLiteral("Built-in MPI/ABI"), Qt::CaseInsensitive) != 0) {
        const auto zealResults = mgr.search(query, docsetFilter);
        for (const auto& entry : zealResults) {
            if (!entry.second.startsWith(QStringLiteral("builtin://"))) {
                m_searchResults.append(entry);
            }
        }
    }

    // Populate the QListWidget with returned symbol names / document titles.
    for (const auto& result : m_searchResults) {
        m_resultsList->addItem(result.first);
    }
}

/**
 * @brief Handles selection of a search result item to load and render HTML.
 * @param row The selected index in the search results list.
 */
void DocsetViewerWidget::onResultClicked(int row) {
    if (row < 0 || row >= m_searchResults.size()) {
        return;
    }

    const QString symbolName = m_searchResults[row].first;
    const QString htmlPath = m_searchResults[row].second;

    // Render built-in topic documentation directly when using builtin:// URI scheme.
    if (htmlPath.startsWith(QStringLiteral("builtin://"))) {
        showSymbolDocumentation(symbolName);
        return;
    }

    // Extract optional #anchor for scrolling and load styled dark-slate HTML via DocsetManager.
    QString anchorString;
    const int hashIdx = htmlPath.indexOf(QLatin1Char('#'));
    if (hashIdx != -1) {
        anchorString = htmlPath.mid(hashIdx + 1);
    }

    const QString htmlOutput = gridlock::core::DocsetManager::instance().getEntryHtmlForPath(htmlPath);
    if (htmlOutput.isEmpty()) {
        showSymbolDocumentation(symbolName);
        return;
    }

    // Set base URL to local directory so relative images and CSS resources resolve.
    QString cleanFilePath = htmlPath;
    cleanFilePath.remove(QRegularExpression(QStringLiteral("<[^>]+>")));
    if (hashIdx != -1) {
        cleanFilePath = cleanFilePath.left(cleanFilePath.indexOf(QLatin1Char('#')));
    }
    cleanFilePath = QUrl::fromPercentEncoding(cleanFilePath.toUtf8());
    const QFileInfo fileInfo(cleanFilePath);
    m_textBrowser->document()->setBaseUrl(QUrl::fromLocalFile(fileInfo.absolutePath() + QStringLiteral("/")));

    m_textBrowser->setHtml(htmlOutput);

    if (!anchorString.isEmpty()) {
        m_textBrowser->scrollToAnchor(anchorString);
    }
}

// ============================================================================
// ManPageViewerWidget Implementation
// ============================================================================

/**
 * @brief Constructs the man page viewer with command search and HTML reader.
 * @param parent Optional parent widget.
 */
ManPageViewerWidget::ManPageViewerWidget(QWidget* parent)
    : QWidget(parent), m_process(new QProcess(this)) {
    // Outer vertical layout with zero margins beneath the unified ReferenceManualWidget header.
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Command / function search bar capped at 22px height (hosted in ReferenceManualWidget's single 26px bar).
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setObjectName(QStringLiteral("manPageSearchEdit"));
    m_searchEdit->setPlaceholderText(QStringLiteral("Search Man Pages (e.g., MPI_Init, printf)..."));
    m_searchEdit->setFixedHeight(22);

    // Read-only HTML documentation viewport with borderless dark-slate canvas.
    m_textBrowser = new QTextBrowser(this);
    m_textBrowser->setObjectName(QStringLiteral("manPageTextBrowser"));
    m_textBrowser->setFrameShape(QFrame::NoFrame);
    m_textBrowser->setOpenExternalLinks(true);
    m_textBrowser->setReadOnly(true);
    m_textBrowser->setStyleSheet(QStringLiteral(
        "QTextBrowser#manPageTextBrowser { background: #16181d; color: #d7dae0; border: none; }"));
    m_textBrowser->setHtml(
        gridlock::core::DocsetManager::instance().lookupBuiltInManualHtml(QStringLiteral("MPI_Init")));

    mainLayout->addWidget(m_textBrowser, 1);

    // Connect user return press to query dispatch and process finish to renderer.
    connect(m_searchEdit, &QLineEdit::returnPressed, this,
            &ManPageViewerWidget::performSearch);
    connect(m_process,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            &ManPageViewerWidget::onProcessFinished);
}

/**
 * @brief Default virtual destructor for ManPageViewerWidget.
 */
ManPageViewerWidget::~ManPageViewerWidget() = default;

/**
 * @brief Spawns the system `man` utility with `-Thtml` flag asynchronously.
 */
void ManPageViewerWidget::performSearch() {
    const QString query = m_searchEdit->text().trimmed();
    if (query.isEmpty()) {
        return;
    }

    // Terminate any currently running man lookup process before initiating new one.
    if (m_process->state() == QProcess::Running) {
        m_process->kill();
        m_process->waitForFinished();
    }

    // Launch man with HTML groff translation flag.
    m_process->start("man", QStringList() << "-Thtml" << query);
}

/**
 * @brief Handles process completion to render generated man page HTML.
 * @param exitCode Exit code returned by `man`.
 * @param exitStatus Exit status enum (NormalExit or CrashExit).
 */
void ManPageViewerWidget::onProcessFinished(int exitCode,
                                            QProcess::ExitStatus exitStatus) {
    if (exitStatus == QProcess::CrashExit || exitCode != 0) {
        // Fall back to built-in dark-slate reference card if system man page is not installed.
        const QString fallbackHtml =
            gridlock::core::DocsetManager::instance().lookupBuiltInManualHtml(m_searchEdit->text());
        if (!fallbackHtml.isEmpty()) {
            m_textBrowser->setHtml(fallbackHtml);
            return;
        }
        const QString errorOutput = m_process->readAllStandardError();
        m_textBrowser->setHtml(
            "<div style='color: #e06c75;'><h2>Error loading man page.</h2><p>" +
            errorOutput.toHtmlEscaped() + "</p></div>");
        return;
    }

    QString htmlOutput = m_process->readAllStandardOutput();

    // Stylized dark-mode stylesheet customized for groff/mandoc HTML output.
    const QString customCss = R"(
<style>
    body {
        background-color: #16181d;
        color: #d7dae0;
        font-family: sans-serif;
        padding: 12px 16px;
    }
    h1, h2, h3, h4 {
        color: #61afef;
        font-weight: bold;
    }
    a {
        color: #c678dd;
        text-decoration: none;
    }
    pre, code, tt {
        background-color: #1e222a;
        border-left: 3px solid #528bff;
        font-family: 'JetBrains Mono', monospace;
        padding: 6px;
        display: block;
        margin: 8px 0;
        white-space: pre-wrap;
    }
    b, strong {
        color: #e5c07b;
    }
</style>
)";

    // Inject stylesheet into <head> or prepend.
    if (htmlOutput.contains("</head>")) {
        htmlOutput.replace("</head>", customCss + "</head>");
    } else {
        htmlOutput = customCss + htmlOutput;
    }

    m_textBrowser->setHtml(htmlOutput);
}

// ============================================================================
// ReferenceManualWidget Implementation
// ============================================================================

/**
 * @brief Constructs the composite reference manual widget with a single 26px control strip.
 * @param parent Optional parent widget.
 *
 * Consolidates the [DOCSETS | MAN PAGES] mode toggle buttons, Docset Selector combo box,
 * Quick Topic (F1) selector, and symbol/man-page search input into one sleek 26px
 * #21252b horizontal header bar above the split index/reader view.
 */
ReferenceManualWidget::ReferenceManualWidget(QWidget* parent)
    : QWidget(parent) {
    // Configure zero-margin vertical layout for flush edge-to-edge docking.
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Instantiate stacked subviews for Dash/Zeal Docsets and groff Man Pages first so
    // their selector/search controls can be mounted directly in the single 26px header bar.
    m_stackedWidget = new QStackedWidget(this);
    m_stackedWidget->setContentsMargins(0, 0, 0, 0);
    m_docsetViewer = new DocsetViewerWidget(m_stackedWidget);
    m_manPageViewer = new ManPageViewerWidget(m_stackedWidget);
    m_stackedWidget->addWidget(m_docsetViewer);
    m_stackedWidget->addWidget(m_manPageViewer);

    // Single 26px #21252b top control bar housing mode toggles, docset selector, quick topics, and search input.
    m_headerBar = new QWidget(this);
    m_headerBar->setObjectName(QStringLiteral("refManualHeaderBar"));
    m_headerBar->setFixedHeight(26);
    m_headerBar->setStyleSheet(QStringLiteral(
        "#refManualHeaderBar { background: #21252b; border-bottom: 1px solid #2c313a; }"
        "#refManualHeaderBar QLabel { color: #abb2bf; font-size: 10px; font-weight: 600; }"
        "#refManualHeaderBar QPushButton { min-height: 18px; max-height: 20px; padding: 1px 8px; "
        "background: #252932; color: #abb2bf; border: 1px solid #323844; border-radius: 2px; "
        "font-family: 'JetBrains Mono', monospace; font-size: 10px; font-weight: 600; }"
        "#refManualHeaderBar QPushButton:hover { background: #2c323d; border: 1px solid #528bff; color: #ffffff; }"
        "#refManualHeaderBar QPushButton:checked { background: #2c3444; border: 1px solid #528bff; color: #ffffff; }"));

    auto* headerLayout = new QHBoxLayout(m_headerBar);
    headerLayout->setContentsMargins(6, 2, 6, 2);
    headerLayout->setSpacing(6);

    // Mode toggle buttons: DOCSETS | MAN PAGES.
    m_docsetsModeBtn = new QPushButton(QStringLiteral("DOCSETS"), m_headerBar);
    m_docsetsModeBtn->setObjectName(QStringLiteral("refDocsetsModeBtn"));
    m_docsetsModeBtn->setCheckable(true);
    m_docsetsModeBtn->setChecked(true);
    m_docsetsModeBtn->setFixedHeight(20);
    headerLayout->addWidget(m_docsetsModeBtn);

    m_manPagesModeBtn = new QPushButton(QStringLiteral("MAN PAGES"), m_headerBar);
    m_manPagesModeBtn->setObjectName(QStringLiteral("refManPagesModeBtn"));
    m_manPagesModeBtn->setCheckable(true);
    m_manPagesModeBtn->setChecked(false);
    m_manPagesModeBtn->setFixedHeight(20);
    headerLayout->addWidget(m_manPagesModeBtn);

    // Mount the Docset Selector combo box on the same 26px row.
    if (m_docsetViewer->docsetSelector()) {
        m_docsetViewer->docsetSelector()->setParent(m_headerBar);
        headerLayout->addWidget(m_docsetViewer->docsetSelector());
    }

    // Compact Quick Topic (F1) selector for immediate jump to core MPI and ABI manual cards.
    m_quickTopicCombo = new QComboBox(m_headerBar);
    m_quickTopicCombo->setObjectName(QStringLiteral("refQuickTopicCombo"));
    m_quickTopicCombo->setFixedHeight(22);
    m_quickTopicCombo->setMinimumWidth(132);
    m_quickTopicCombo->addItems({
        QStringLiteral("Quick Topic (F1): MPI_Init"),
        QStringLiteral("MPI_Send"),
        QStringLiteral("MPI_Recv"),
        QStringLiteral("MPI_Barrier"),
        QStringLiteral("MPI_Allreduce"),
        QStringLiteral("MPI_Finalize"),
        QStringLiteral("gdb-mi"),
        QStringLiteral("x86_64-abi")
    });
    connect(m_quickTopicCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
                if (!m_quickTopicCombo || idx < 0) {
                    return;
                }
                QString topic = m_quickTopicCombo->itemText(idx);
                if (topic.contains(QLatin1Char(':'))) {
                    topic = topic.section(QLatin1Char(':'), 1).trimmed();
                }
                lookupSymbol(topic);
            });
    headerLayout->addWidget(m_quickTopicCombo);

    // Mount both search inputs in the unified header bar and toggle visibility by active mode.
    if (m_docsetViewer->searchEdit()) {
        m_docsetViewer->searchEdit()->setParent(m_headerBar);
        headerLayout->addWidget(m_docsetViewer->searchEdit(), 1);
    }
    if (m_manPageViewer->searchEdit()) {
        m_manPageViewer->searchEdit()->setParent(m_headerBar);
        m_manPageViewer->searchEdit()->setVisible(false);
        headerLayout->addWidget(m_manPageViewer->searchEdit(), 1);
    }

    mainLayout->addWidget(m_headerBar);
    mainLayout->addWidget(m_stackedWidget, 1);

    // Helper lambda switching between DOCSETS (0) and MAN PAGES (1) modes.
    auto setModeIndex = [this](int index) {
        const bool isDocsets = (index == 0);
        m_docsetsModeBtn->setChecked(isDocsets);
        m_manPagesModeBtn->setChecked(!isDocsets);
        m_stackedWidget->setCurrentIndex(isDocsets ? 0 : 1);
        if (m_docsetViewer && m_docsetViewer->docsetSelector()) {
            m_docsetViewer->docsetSelector()->setVisible(isDocsets);
        }
        if (m_docsetViewer && m_docsetViewer->searchEdit()) {
            m_docsetViewer->searchEdit()->setVisible(isDocsets);
        }
        if (m_manPageViewer && m_manPageViewer->searchEdit()) {
            m_manPageViewer->searchEdit()->setVisible(!isDocsets);
        }
        QSettings settings("GridLock", "Debugger");
        settings.setValue("ui/reference_manual_tab", isDocsets ? 0 : 1);
    };

    connect(m_docsetsModeBtn, &QPushButton::clicked, this, [setModeIndex]() { setModeIndex(0); });
    connect(m_manPagesModeBtn, &QPushButton::clicked, this, [setModeIndex]() { setModeIndex(1); });

    // Restore last visited mode from persistent application settings.
    QSettings settings("GridLock", "Debugger");
    const int savedIndex = settings.value("ui/reference_manual_tab", 0).toInt();
    setModeIndex(savedIndex == 1 ? 1 : 0);
}

/**
 * @brief Default virtual destructor for ReferenceManualWidget.
 */
ReferenceManualWidget::~ReferenceManualWidget() = default;

/**
 * @brief Resolves and displays symbol documentation in the active Docset browser.
 * @param rawSymbol Raw symbol or topic name (strips @plt/@got suffixes automatically).
 * @return True if documentation was rendered; false if symbol is empty.
 */
bool ReferenceManualWidget::lookupSymbol(const QString& rawSymbol) {
    if (!m_docsetViewer) {
        return false;
    }
    // Activate the Docsets mode when displaying symbol manual cards.
    if (m_stackedWidget && m_stackedWidget->currentIndex() != 0) {
        m_docsetsModeBtn->click();
    }
    return m_docsetViewer->showSymbolDocumentation(rawSymbol);
}

/**
 * @brief Returns the list of loaded Zeal docset bundle names available in the viewer.
 * @return Non-empty list of discovered Zeal docsets (plus Built-in MPI/ABI).
 */
QStringList ReferenceManualWidget::loadedDocsetNames() const {
    return m_docsetViewer ? m_docsetViewer->loadedDocsetNames() : QStringList();
}

/**
 * @brief Returns the pixel height of the consolidated top header bar (<= 26px).
 * @return Header bar height in pixels.
 */
int ReferenceManualWidget::headerBarHeight() const {
    return m_headerBar ? m_headerBar->height() : 0;
}

}  // namespace gridlock::ui

