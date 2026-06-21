#include "ReferenceManualWidget.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace gridlock::ui {

ReferenceManualWidget::ReferenceManualWidget(QWidget *parent)
    : QWidget(parent), m_process(new QProcess(this)) {
    auto* mainLayout = new QVBoxLayout(this);

    auto* topLayout = new QHBoxLayout();
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText("Search functions, types, MPI_... (Press Enter)");
    
    m_sectionCombo = new QComboBox(this);
    m_sectionCombo->addItem("All Sections", "");
    m_sectionCombo->addItem("Section 3 (Library/MPI)", "3");
    m_sectionCombo->addItem("Section 2 (Syscalls)", "2");

    m_searchBtn = new QPushButton("Search", this);

    topLayout->addWidget(m_searchEdit);
    topLayout->addWidget(m_sectionCombo);
    topLayout->addWidget(m_searchBtn);

    m_textBrowser = new QTextBrowser(this);
    m_textBrowser->setOpenExternalLinks(true);
    m_textBrowser->setReadOnly(true);

    mainLayout->addLayout(topLayout);
    mainLayout->addWidget(m_textBrowser);

    connect(m_searchBtn, &QPushButton::clicked, this, &ReferenceManualWidget::performSearch);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &ReferenceManualWidget::performSearch);
    connect(m_process, &QProcess::finished, this, &ReferenceManualWidget::onProcessFinished);
}

ReferenceManualWidget::~ReferenceManualWidget() {
    if (m_process->state() == QProcess::Running) {
        m_process->kill();
        m_process->waitForFinished();
    }
}

void ReferenceManualWidget::performSearch() {
    m_currentQuery = m_searchEdit->text().trimmed();
    if (m_currentQuery.isEmpty()) return;

    m_searchBtn->setEnabled(false);
    m_textBrowser->setHtml("<div style='color: #cdd6f4; text-align: center; margin-top: 50px;'><h2>Loading...</h2></div>");

    QStringList args;
    args << "-Thtml";
    QString section = m_sectionCombo->currentData().toString();
    if (!section.isEmpty()) {
        args << section;
    }
    args << m_currentQuery;

    m_process->start("man", args);
}

void ReferenceManualWidget::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    m_searchBtn->setEnabled(true);

    if (exitStatus == QProcess::CrashExit || exitCode != 0) {
        QString errorMsg = QString("<div style='color: #f38ba8; text-align: center; margin-top: 50px;'>"
                                   "<h2>No manual entry found for '%1'</h2></div>").arg(m_currentQuery.toHtmlEscaped());
        m_textBrowser->setHtml(errorMsg);
        return;
    }

    QString htmlOutput = QString::fromUtf8(m_process->readAllStandardOutput());
    
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
        htmlOutput = customCss + htmlOutput; // Fallback
    }

    m_textBrowser->setHtml(htmlOutput);
}

} // namespace gridlock::ui
