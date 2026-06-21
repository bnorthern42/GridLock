#include "ReferenceManualWidget.hpp"
#include "../../core/managers/DocsetManager.hpp"
#include <QVBoxLayout>
#include <QFile>
#include <QSettings>
#include <QUrl>
#include <QDebug>
#include <QFileInfo>
#include <QPalette>
#include <QColor>
#include <QRegularExpression>

namespace gridlock::ui {

// --- DocsetViewerWidget ---
DocsetViewerWidget::DocsetViewerWidget(QWidget *parent)
    : QWidget(parent) {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    QSplitter* splitter = new QSplitter(Qt::Horizontal, this);

    QWidget* leftWidget = new QWidget(splitter);
    auto* leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 0, 0);

    m_searchEdit = new QLineEdit(leftWidget);
    m_searchEdit->setPlaceholderText("Search Docsets...");
    
    m_resultsList = new QListWidget(leftWidget);

    leftLayout->addWidget(m_searchEdit);
    leftLayout->addWidget(m_resultsList);

    m_textBrowser = new QTextBrowser(splitter);
    m_textBrowser->setOpenExternalLinks(true);
    m_textBrowser->setReadOnly(true);

    QPalette pal = m_textBrowser->palette();
    pal.setColor(QPalette::Base, QColor("#1e1e2e"));
    pal.setColor(QPalette::Text, QColor("#cdd6f4"));
    pal.setColor(QPalette::Link, QColor("#89b4fa"));
    m_textBrowser->setPalette(pal);

    splitter->addWidget(leftWidget);
    splitter->addWidget(m_textBrowser);
    
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 4);

    mainLayout->addWidget(splitter);

    connect(m_searchEdit, &QLineEdit::textChanged, this, &DocsetViewerWidget::performSearch);
    connect(m_resultsList, &QListWidget::currentRowChanged, this, &DocsetViewerWidget::onResultClicked);
}

DocsetViewerWidget::~DocsetViewerWidget() = default;

void DocsetViewerWidget::performSearch() {
    QString query = m_searchEdit->text().trimmed();
    m_resultsList->clear();
    m_searchResults.clear();

    if (query.isEmpty()) return;

    m_searchResults = gridlock::core::DocsetManager::instance().search(query);
    
    for (const auto& result : m_searchResults) {
        m_resultsList->addItem(result.first);
    }
}

void DocsetViewerWidget::onResultClicked(int row) {
    if (row < 0 || row >= m_searchResults.size()) return;

    QString htmlPath = m_searchResults[row].second;
    
    // Strip anchors if any to open the file
    QString filePath = htmlPath;
    QString anchorString;
    int hashIdx = filePath.indexOf('#');
    if (hashIdx != -1) {
        anchorString = filePath.mid(hashIdx + 1);
        filePath = filePath.left(hashIdx);
    }
    
    // Decode percent-encoded paths
    filePath = QUrl::fromPercentEncoding(filePath.toUtf8());

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "DocsetViewerWidget: Failed to open absolute file path:" << filePath;
        m_textBrowser->setHtml("<div style='color: #f38ba8;'><h2>Error loading documentation.</h2></div>");
        return;
    }

    QString htmlOutput = file.readAll();
    
    // "Scorched Earth" regex scrubbers
    QRegularExpression::PatternOptions options = QRegularExpression::CaseInsensitiveOption;
    htmlOutput.replace(QRegularExpression("background-color:\\s*(white|#fff|#ffffff);?", options), "background-color: #1e1e2e;");
    htmlOutput.replace(QRegularExpression("background:\\s*(white|#fff|#ffffff);?", options), "background: #1e1e2e;");
    htmlOutput.replace(QRegularExpression("color:\\s*(black|#000|#000000);?", options), "color: #cdd6f4;");
    htmlOutput.replace(QRegularExpression("bgcolor=[\"']?(white|#ffffff)[\"']?", options), "bgcolor=\"#1e1e2e\"");
    
    htmlOutput.replace(QRegularExpression("<body[^>]*>", options), "<body style=\"background-color: #1e1e2e; color: #cdd6f4;\">");
    htmlOutput.replace(QRegularExpression("<style[^>]*>.*?</style>", QRegularExpression::DotMatchesEverythingOption | options), "");
    
    QString customCss = R"(
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

    if (htmlOutput.contains("</head>")) {
        htmlOutput.replace("</head>", customCss + "</head>");
    } else {
        htmlOutput = customCss + htmlOutput;
    }

    QFileInfo fileInfo(filePath);
    QString absoluteDirPath = fileInfo.absolutePath();
    // Use QUrl::fromLocalFile to ensure spaces and special chars are correctly converted into a file:// URL
    m_textBrowser->document()->setBaseUrl(QUrl::fromLocalFile(absoluteDirPath + "/"));

    m_textBrowser->setHtml(htmlOutput);
    
    if (!anchorString.isEmpty()) {
        m_textBrowser->scrollToAnchor(anchorString);
    }
}

// --- ManPageViewerWidget ---
ManPageViewerWidget::ManPageViewerWidget(QWidget *parent)
    : QWidget(parent), m_process(new QProcess(this)) {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText("Search Man Pages (e.g., printf)...");

    m_textBrowser = new QTextBrowser(this);
    m_textBrowser->setOpenExternalLinks(true);
    m_textBrowser->setReadOnly(true);

    mainLayout->addWidget(m_searchEdit);
    mainLayout->addWidget(m_textBrowser);

    connect(m_searchEdit, &QLineEdit::returnPressed, this, &ManPageViewerWidget::performSearch);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &ManPageViewerWidget::onProcessFinished);
}

ManPageViewerWidget::~ManPageViewerWidget() = default;

void ManPageViewerWidget::performSearch() {
    QString query = m_searchEdit->text().trimmed();
    if (query.isEmpty()) return;

    m_textBrowser->setHtml("<div style='color: #cdd6f4;'>Searching man pages for: " + query.toHtmlEscaped() + "...</div>");

    if (m_process->state() == QProcess::Running) {
        m_process->kill();
        m_process->waitForFinished();
    }

    m_process->start("man", QStringList() << "-Thtml" << query);
}

void ManPageViewerWidget::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    if (exitStatus == QProcess::CrashExit || exitCode != 0) {
        QString errorOutput = m_process->readAllStandardError();
        m_textBrowser->setHtml("<div style='color: #f38ba8;'><h2>Error loading man page.</h2><p>" + errorOutput.toHtmlEscaped() + "</p></div>");
        return;
    }

    QString htmlOutput = m_process->readAllStandardOutput();

    QString customCss = R"(
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

    if (htmlOutput.contains("</head>")) {
        htmlOutput.replace("</head>", customCss + "</head>");
    } else {
        htmlOutput = customCss + htmlOutput;
    }

    m_textBrowser->setHtml(htmlOutput);
}

// --- ReferenceManualWidget ---
ReferenceManualWidget::ReferenceManualWidget(QWidget *parent)
    : QWidget(parent) {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setDocumentMode(true); // Gives a flat, modern appearance

    m_tabWidget->addTab(new DocsetViewerWidget(m_tabWidget), "Docsets");
    m_tabWidget->addTab(new ManPageViewerWidget(m_tabWidget), "Man Pages");

    mainLayout->addWidget(m_tabWidget);

    QSettings settings("GridLock", "Debugger");
    int savedIndex = settings.value("ui/reference_manual_tab", 0).toInt();
    if (savedIndex >= 0 && savedIndex < m_tabWidget->count()) {
        m_tabWidget->setCurrentIndex(savedIndex);
    }

    connect(m_tabWidget, &QTabWidget::currentChanged, this, [](int index) {
        QSettings settings("GridLock", "Debugger");
        settings.setValue("ui/reference_manual_tab", index);
    });
}

ReferenceManualWidget::~ReferenceManualWidget() = default;

} // namespace gridlock::ui
