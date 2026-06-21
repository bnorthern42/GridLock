#include "ReferenceManualWidget.hpp"
#include "../core/DocsetManager.hpp"
#include <QVBoxLayout>
#include <QFile>

namespace gridlock::ui {

ReferenceManualWidget::ReferenceManualWidget(QWidget *parent)
    : QWidget(parent) {
    auto* mainLayout = new QVBoxLayout(this);

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

    splitter->addWidget(leftWidget);
    splitter->addWidget(m_textBrowser);
    
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 4);

    mainLayout->addWidget(splitter);

    connect(m_searchEdit, &QLineEdit::textChanged, this, &ReferenceManualWidget::performSearch);
    connect(m_resultsList, &QListWidget::currentRowChanged, this, &ReferenceManualWidget::onResultClicked);
}

ReferenceManualWidget::~ReferenceManualWidget() = default;

void ReferenceManualWidget::performSearch() {
    QString query = m_searchEdit->text().trimmed();
    m_resultsList->clear();
    m_searchResults.clear();

    if (query.isEmpty()) return;

    m_searchResults = gridlock::core::DocsetManager::instance().search(query);
    
    for (const auto& result : m_searchResults) {
        m_resultsList->addItem(result.first);
    }
}

void ReferenceManualWidget::onResultClicked(int row) {
    if (row < 0 || row >= m_searchResults.size()) return;

    QString htmlPath = m_searchResults[row].second;
    
    // Strip anchors if any to open the file
    QString filePath = htmlPath;
    int hashIdx = filePath.indexOf('#');
    if (hashIdx != -1) {
        filePath = filePath.left(hashIdx);
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_textBrowser->setHtml("<div style='color: #f38ba8;'><h2>Error loading documentation.</h2></div>");
        return;
    }

    QString htmlOutput = file.readAll();
    
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
    
    if (hashIdx != -1) {
        m_textBrowser->scrollToAnchor(htmlPath.mid(hashIdx + 1));
    }
}

} // namespace gridlock::ui
