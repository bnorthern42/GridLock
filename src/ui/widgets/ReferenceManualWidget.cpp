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
#include <QPalette>
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
 * Configures an asymmetrical horizontal QSplitter. The left pane hosts a search
 * query filter bar and a scrollable results list. The right pane hosts a
 * read-only QTextBrowser configured with high-contrast dark theme palette and
 * support for external link handling.
 */
DocsetViewerWidget::DocsetViewerWidget(QWidget* parent)
    : QWidget(parent) {
    // Top-level zero-margin vertical layout hosting the primary splitter.
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // Primary horizontal splitter dividing search results from document view.
    auto* splitter = new QSplitter(Qt::Horizontal, this);

    // Left pane container: search edit bar above the results list view.
    auto* leftWidget = new QWidget(splitter);
    auto* leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 0, 0);

    // Search query input field with intuitive placeholder text.
    m_searchEdit = new QLineEdit(leftWidget);
    m_searchEdit->setPlaceholderText("Search Docsets...");

    // Results list widget populated dynamically by DocsetManager queries.
    m_resultsList = new QListWidget(leftWidget);

    leftLayout->addWidget(m_searchEdit);
    leftLayout->addWidget(m_resultsList);

    // Right pane container: HTML text browser with external link handling.
    m_textBrowser = new QTextBrowser(splitter);
    m_textBrowser->setOpenExternalLinks(true);
    m_textBrowser->setReadOnly(true);

    // Configure high-contrast Catppuccin / dark slate palette for document view.
    QPalette pal = m_textBrowser->palette();
    pal.setColor(QPalette::Base, QColor("#1e1e2e"));
    pal.setColor(QPalette::Text, QColor("#cdd6f4"));
    pal.setColor(QPalette::Link, QColor("#89b4fa"));
    m_textBrowser->setPalette(pal);

    // Add widgets to splitter and allocate generous viewport to documentation.
    splitter->addWidget(leftWidget);
    splitter->addWidget(m_textBrowser);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 4);

    mainLayout->addWidget(splitter);

    // Wire live incremental search query filtering and item selection triggers.
    connect(m_searchEdit, &QLineEdit::textChanged, this,
            &DocsetViewerWidget::performSearch);
    connect(m_resultsList, &QListWidget::currentRowChanged, this,
            &DocsetViewerWidget::onResultClicked);
}

/**
 * @brief Default virtual destructor for DocsetViewerWidget.
 */
DocsetViewerWidget::~DocsetViewerWidget() = default;

/**
 * @brief Executes an indexed search against active Dash/Zeal docsets.
 *
 * Retrieves search hits matching the current query string from DocsetManager.
 * Clears prior results and populates the list view with symbol tokens and
 * document titles.
 */
void DocsetViewerWidget::performSearch() {
    const QString query = m_searchEdit->text().trimmed();
    m_resultsList->clear();
    m_searchResults.clear();

    // Abort early if the search query is completely empty.
    if (query.isEmpty()) {
        return;
    }

    // Dispatch query to the global DocsetManager indexer.
    m_searchResults = gridlock::core::DocsetManager::instance().search(query);

    // Populate the QListWidget with returned symbol names / document titles.
    for (const auto& result : m_searchResults) {
        m_resultsList->addItem(result.first);
    }
}

/**
 * @brief Handles selection of a search result item to load and render HTML.
 * @param row The selected index in the search results list.
 *
 * Extracts the file path and optional anchor tag, decodes percent-encoded URL
 * characters, scrubs hardcoded light background colors from legacy docsets,
 * injects responsive dark mode CSS, and renders the content in the browser.
 */
void DocsetViewerWidget::onResultClicked(int row) {
    if (row < 0 || row >= m_searchResults.size()) {
        return;
    }

    const QString htmlPath = m_searchResults[row].second;

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
        qDebug() << "DocsetViewerWidget: Failed to open file path:" << filePath;
        m_textBrowser->setHtml(
            "<div style='color: #f38ba8;'><h2>Error loading documentation.</h2></div>");
        return;
    }

    QString htmlOutput = file.readAll();

    // Apply regex sanitization to scrub hardcoded light backgrounds from older docsets.
    const auto options = QRegularExpression::CaseInsensitiveOption;
    htmlOutput.replace(
        QRegularExpression("background-color:\\s*(white|#fff|#ffffff);?", options),
        "background-color: #1e1e2e;");
    htmlOutput.replace(
        QRegularExpression("background:\\s*(white|#fff|#ffffff);?", options),
        "background: #1e1e2e;");
    htmlOutput.replace(
        QRegularExpression("color:\\s*(black|#000|#000000);?", options),
        "color: #cdd6f4;");
    htmlOutput.replace(
        QRegularExpression("bgcolor=[\"']?(white|#ffffff)[\"']?", options),
        "bgcolor=\"#1e1e2e\"");

    // Enforce dark canvas styling on <body> and strip incompatible inline style blocks.
    htmlOutput.replace(QRegularExpression("<body[^>]*>", options),
                       "<body style=\"background-color: #1e1e2e; color: #cdd6f4;\">");
    htmlOutput.replace(
        QRegularExpression("<style[^>]*>.*?</style>",
                           QRegularExpression::DotMatchesEverythingOption | options),
        "");

    // Unified dark theme stylesheet override.
    const QString customCss = R"(
<style>
    html, body, div, p, table, tr, td, th, ul, li {
        background-color: #1e1e2e !important;
        background: none !important; 
        color: #cdd6f4 !important;
    }
    a, a:visited, a:hover {
        color: #89b4fa !important;
        text-decoration: none !important;
    }
    pre, code, .mw-code {
        background-color: #181825 !important;
        border: 1px solid #313244 !important;
        color: #cba6f7 !important;
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
 *
 * Allocates a child QProcess instance to execute the system `man` utility,
 * sets up a search box wired to return-key triggers, and initializes an
 * asynchronous reader text browser.
 */
ManPageViewerWidget::ManPageViewerWidget(QWidget* parent)
    : QWidget(parent), m_process(new QProcess(this)) {
    // Outer vertical layout.
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // Command / function search bar.
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText("Search Man Pages (e.g., printf)...");

    // Read-only HTML documentation viewport.
    m_textBrowser = new QTextBrowser(this);
    m_textBrowser->setOpenExternalLinks(true);
    m_textBrowser->setReadOnly(true);

    mainLayout->addWidget(m_searchEdit);
    mainLayout->addWidget(m_textBrowser);

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
 *
 * Terminates any in-flight man command process, displays an in-progress status
 * banner, and launches `man -Thtml <query>`.
 */
void ManPageViewerWidget::performSearch() {
    const QString query = m_searchEdit->text().trimmed();
    if (query.isEmpty()) {
        return;
    }

    // Display temporary progress message while groff/man renders HTML.
    m_textBrowser->setHtml(
        "<div style='color: #cdd6f4;'>Searching man pages for: " +
        query.toHtmlEscaped() + "...</div>");

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
 *
 * If `man` fails or crashes, displays the standard error output in a red warning
 * box. Otherwise, formats the HTML with monospace JetBrains Mono blocks,
 * dark theme styling, and renders it in the browser.
 */
void ManPageViewerWidget::onProcessFinished(int exitCode,
                                           QProcess::ExitStatus exitStatus) {
    if (exitStatus == QProcess::CrashExit || exitCode != 0) {
        const QString errorOutput = m_process->readAllStandardError();
        m_textBrowser->setHtml(
            "<div style='color: #f38ba8;'><h2>Error loading man page.</h2><p>" +
            errorOutput.toHtmlEscaped() + "</p></div>");
        return;
    }

    QString htmlOutput = m_process->readAllStandardOutput();

    // Stylized dark-mode stylesheet customized for groff/mandoc HTML output.
    const QString customCss = R"(
<style>
    body {
        background-color: #1e1e2e;
        color: #cdd6f4;
        font-family: sans-serif;
        padding: 20px;
    }
    h1, h2, h3, h4 {
        color: #89b4fa;
        font-weight: bold;
    }
    a {
        color: #cba6f7;
        text-decoration: none;
    }
    a:hover {
        text-decoration: underline;
    }
    pre, code, tt {
        background-color: #181825;
        border-left: 3px solid #89b4fa;
        font-family: 'JetBrains Mono', monospace;
        padding: 4px;
        display: block;
        margin: 10px 0;
        white-space: pre-wrap;
    }
    b, strong {
        color: #f5c2e7;
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
 * Assembles the Docsets and Man Pages tabs into a flat document-mode
 * QTabWidget, restores the user's previously active tab index from QSettings,
 * and attaches a change listener to persist tab selections across sessions.
 */
ReferenceManualWidget::ReferenceManualWidget(QWidget* parent)
    : QWidget(parent) {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // Document mode tab container for seamless border integration.
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setDocumentMode(true);

    // Instantiate tab subviews.
    m_tabWidget->addTab(new DocsetViewerWidget(m_tabWidget), "Docsets");
    m_tabWidget->addTab(new ManPageViewerWidget(m_tabWidget), "Man Pages");

    mainLayout->addWidget(m_tabWidget);

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

}  // namespace gridlock::ui
