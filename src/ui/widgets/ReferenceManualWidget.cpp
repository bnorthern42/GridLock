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

    // Left pane container: 26px header bar above the borderless results list view.
    auto* leftWidget = new QWidget(splitter);
    auto* leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);

    // Sub-header bar matching the 26px #21252b workstation header strip.
    auto* searchBarContainer = new QWidget(leftWidget);
    searchBarContainer->setObjectName(QStringLiteral("docsetSearchHeader"));
    searchBarContainer->setFixedHeight(26);
    searchBarContainer->setStyleSheet(QStringLiteral(
        "#docsetSearchHeader { background: #21252b; border-bottom: 1px solid #2c313a; }"));
    auto* searchBarLayout = new QHBoxLayout(searchBarContainer);
    searchBarLayout->setContentsMargins(6, 2, 6, 2);
    searchBarLayout->setSpacing(4);

    // Search query input field capped at 22px height.
    m_searchEdit = new QLineEdit(searchBarContainer);
    m_searchEdit->setObjectName(QStringLiteral("docsetSearchEdit"));
    m_searchEdit->setPlaceholderText(QStringLiteral("Filter MPI / ABI topics..."));
    m_searchEdit->setFixedHeight(22);
    searchBarLayout->addWidget(m_searchEdit);

    // Results list widget populated dynamically by DocsetManager queries.
    m_resultsList = new QListWidget(leftWidget);
    m_resultsList->setObjectName(QStringLiteral("docsetResultsList"));
    m_resultsList->setFrameShape(QFrame::NoFrame);
    m_resultsList->setStyleSheet(QStringLiteral(
        "QListWidget#docsetResultsList { background: #1b1e24; color: #abb2bf; border: none; "
        "border-right: 1px solid #282c34; font-family: 'JetBrains Mono', monospace; font-size: 11px; }"
        "QListWidget#docsetResultsList::item { padding: 3px 8px; min-height: 20px; }"
        "QListWidget#docsetResultsList::item:selected { background: #264f78; color: #ffffff; }"));

    leftLayout->addWidget(searchBarContainer);
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
    splitter->setStretchFactor(1, 4);
    splitter->setSizes({210, 790});

    mainLayout->addWidget(splitter, 1);

    // Wire live incremental search query filtering and item selection triggers.
    connect(m_searchEdit, &QLineEdit::textChanged, this,
            &DocsetViewerWidget::performSearch);
    connect(m_resultsList, &QListWidget::currentRowChanged, this,
            &DocsetViewerWidget::onResultClicked);

    // Populate default built-in topics and render MPI_Init reference manual initially.
    performSearch();
    showSymbolDocumentation(QStringLiteral("MPI_Init"));
}

/**
 * @brief Default virtual destructor for DocsetViewerWidget.
 */
DocsetViewerWidget::~DocsetViewerWidget() = default;

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

    const QString html = gridlock::core::DocsetManager::instance().lookupBuiltInManualHtml(normalized);
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
 * @brief Executes an indexed search against active Dash/Zeal docsets or built-in topics.
 *
 * Retrieves search hits matching the current query string from DocsetManager.
 * Clears prior results and populates the list view with symbol tokens and
 * document titles.
 */
void DocsetViewerWidget::performSearch() {
    const QString query = m_searchEdit->text().trimmed();
    m_resultsList->clear();
    m_searchResults.clear();

    // Dispatch query to the global DocsetManager indexer (falls back to built-in topics).
    m_searchResults = gridlock::core::DocsetManager::instance().search(query);
    if (m_searchResults.isEmpty()) {
        for (const QString& topic : gridlock::core::DocsetManager::instance().builtInTopics()) {
            if (query.isEmpty() || topic.contains(query, Qt::CaseInsensitive)) {
                m_searchResults.append(qMakePair(topic, QStringLiteral("builtin://") + topic));
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

    // Separate fragment anchor (e.g. #method_name) from physical file path.
    QString filePath = htmlPath;
    QString anchorString;
    const int hashIdx = filePath.indexOf('#');
    if (hashIdx != -1) {
        anchorString = filePath.mid(hashIdx + 1);
        filePath = filePath.left(hashIdx);
    }

    // Decode URL percent-encoded characters (e.g., %20 -> space).
    filePath = QUrl::fromPercentEncoding(filePath.toUtf8());

    // Attempt to open the local HTML file on disk.
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        showSymbolDocumentation(symbolName);
        return;
    }

    QString htmlOutput = file.readAll();

    // Apply regex sanitization to scrub hardcoded light backgrounds from older docsets.
    const auto options = QRegularExpression::CaseInsensitiveOption;
    htmlOutput.replace(
        QRegularExpression("background-color:\\s*(white|#fff|#ffffff);?", options),
        "background-color: #16181d;");
    htmlOutput.replace(
        QRegularExpression("background:\\s*(white|#fff|#ffffff);?", options),
        "background: #16181d;");
    htmlOutput.replace(
        QRegularExpression("color:\\s*(black|#000|#000000);?", options),
        "color: #d7dae0;");
    htmlOutput.replace(
        QRegularExpression("bgcolor=[\"']?(white|#ffffff)[\"']?", options),
        "bgcolor=\"#16181d\"");

    // Enforce dark canvas styling on <body> and strip incompatible inline style blocks.
    htmlOutput.replace(QRegularExpression("<body[^>]*>", options),
                       "<body style=\"background-color: #16181d; color: #d7dae0;\">");
    htmlOutput.replace(
        QRegularExpression("<style[^>]*>.*?</style>",
                           QRegularExpression::DotMatchesEverythingOption | options),
        "");

    // Unified dark theme stylesheet override.
    const QString customCss = R"(
<style>
    html, body, div, p, table, tr, td, th, ul, li {
        background-color: #16181d !important;
        background: none !important; 
        color: #d7dae0 !important;
    }
    a, a:visited, a:hover {
        color: #61afef !important;
        text-decoration: none !important;
    }
    pre, code, .mw-code {
        background-color: #1e222a !important;
        border: 1px solid #2c313a !important;
        color: #e5c07b !important;
    }
</style>
)";

    // Inject custom CSS into <head> if present, or prepend directly.
    if (htmlOutput.contains("</head>")) {
        htmlOutput.replace("</head>", customCss + "</head>");
    } else {
        htmlOutput = customCss + htmlOutput;
    }

    // Set base URL to local directory so relative images and resources resolve.
    const QFileInfo fileInfo(filePath);
    const QString absoluteDirPath = fileInfo.absolutePath();
    m_textBrowser->document()->setBaseUrl(
        QUrl::fromLocalFile(absoluteDirPath + "/"));

    m_textBrowser->setHtml(htmlOutput);

    // Scroll to the targeted anchor if one was specified in the index URL.
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
    // Outer vertical layout with zero margins.
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // 26px #21252b header bar containing the man page search box.
    auto* headerBar = new QWidget(this);
    headerBar->setObjectName(QStringLiteral("manPageHeaderBar"));
    headerBar->setFixedHeight(26);
    headerBar->setStyleSheet(QStringLiteral(
        "#manPageHeaderBar { background: #21252b; border-bottom: 1px solid #2c313a; }"));
    auto* headerLayout = new QHBoxLayout(headerBar);
    headerLayout->setContentsMargins(6, 2, 6, 2);
    headerLayout->setSpacing(6);

    // Command / function search bar capped at 22px height.
    m_searchEdit = new QLineEdit(headerBar);
    m_searchEdit->setObjectName(QStringLiteral("manPageSearchEdit"));
    m_searchEdit->setPlaceholderText(QStringLiteral("Search Man Pages (e.g., MPI_Init, printf)..."));
    m_searchEdit->setFixedHeight(22);
    headerLayout->addWidget(m_searchEdit, 1);

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

    mainLayout->addWidget(headerBar);
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
 * @brief Constructs the composite reference manual widget.
 * @param parent Optional parent widget.
 *
 * Builds a unified 26px #21252b header bar with quick-jump topic pill buttons
 * (MPI_Init, MPI_Send, MPI_Recv, MPI_Barrier, MPI_Allreduce, MPI_Finalize,
 * gdb-mi, x86_64-abi) above the flush document-mode Docsets / Man Pages tab view.
 */
ReferenceManualWidget::ReferenceManualWidget(QWidget* parent)
    : QWidget(parent) {
    // Configure zero-margin vertical layout for flush edge-to-edge docking.
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Unified 26px #21252b top control bar with quick-jump topic pills.
    auto* headerBar = new QWidget(this);
    headerBar->setObjectName(QStringLiteral("refManualHeaderBar"));
    headerBar->setFixedHeight(26);
    headerBar->setStyleSheet(QStringLiteral(
        "#refManualHeaderBar { background: #21252b; border-bottom: 1px solid #2c313a; }"
        "#refManualHeaderBar QLabel { color: #abb2bf; font-size: 10px; font-weight: 600; }"
        "#refManualHeaderBar QPushButton { min-height: 18px; max-height: 20px; padding: 1px 6px; "
        "background: #252932; color: #d7dae0; border: 1px solid #323844; border-radius: 2px; "
        "font-family: 'JetBrains Mono', monospace; font-size: 10px; font-weight: 600; }"
        "#refManualHeaderBar QPushButton:hover { background: #2c3444; border: 1px solid #528bff; color: #ffffff; }"));

    // Arrange quick-jump topic buttons inside the 26px control strip.
    auto* headerLayout = new QHBoxLayout(headerBar);
    headerLayout->setContentsMargins(6, 2, 6, 2);
    headerLayout->setSpacing(4);

    // Label indicating F1 context-sensitive documentation lookup support.
    auto* badgeLabel = new QLabel(QStringLiteral("TOPICS (F1):"), headerBar);
    headerLayout->addWidget(badgeLabel);

    // Curated list of core MPI routines, GDB/MI protocol, and x86_64 System V ABI topics.
    const QStringList quickTopics = {
        QStringLiteral("MPI_Init"),
        QStringLiteral("MPI_Send"),
        QStringLiteral("MPI_Recv"),
        QStringLiteral("MPI_Barrier"),
        QStringLiteral("MPI_Allreduce"),
        QStringLiteral("MPI_Finalize"),
        QStringLiteral("gdb-mi"),
        QStringLiteral("x86_64-abi")
    };
    for (const QString& topic : quickTopics) {
        // Create compact 20px pill button for each built-in reference topic.
        auto* btn = new QPushButton(topic, headerBar);
        btn->setFixedHeight(20);
        connect(btn, &QPushButton::clicked, this, [this, topic]() {
            lookupSymbol(topic);
        });
        headerLayout->addWidget(btn);
    }
    headerLayout->addStretch(1);

    // Document mode tab container for seamless border integration.
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setDocumentMode(true);
    m_tabWidget->setContentsMargins(0, 0, 0, 0);

    // Instantiate tab subviews for Dash/Zeal Docsets and groff Man Pages.
    m_docsetViewer = new DocsetViewerWidget(m_tabWidget);
    m_manPageViewer = new ManPageViewerWidget(m_tabWidget);
    m_tabWidget->addTab(m_docsetViewer, QStringLiteral("Docsets"));
    m_tabWidget->addTab(m_manPageViewer, QStringLiteral("Man Pages"));

    mainLayout->addWidget(headerBar);
    mainLayout->addWidget(m_tabWidget, 1);

    // Restore last visited tab from persistent application settings.
    QSettings settings("GridLock", "Debugger");
    const int savedIndex = settings.value("ui/reference_manual_tab", 0).toInt();
    if (savedIndex >= 0 && savedIndex < m_tabWidget->count()) {
        m_tabWidget->setCurrentIndex(savedIndex);
    }

    // Persist tab changes dynamically to user settings.
    connect(m_tabWidget, &QTabWidget::currentChanged, this, [](int index) {
        QSettings settings("GridLock", "Debugger");
        settings.setValue("ui/reference_manual_tab", index);
    });
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
    // Ensure the Docsets sub-tab is active when displaying symbol manual cards.
    if (m_tabWidget) {
        m_tabWidget->setCurrentIndex(0);
    }
    return m_docsetViewer->showSymbolDocumentation(rawSymbol);
}

}  // namespace gridlock::ui
