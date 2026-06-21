#pragma once
#include <QWidget>
#include <QLineEdit>
#include <QListWidget>
#include <QTextBrowser>
#include <QSplitter>
#include <QTabWidget>
#include <QProcess>

namespace gridlock::ui {

class DocsetViewerWidget : public QWidget {
    Q_OBJECT
public:
    explicit DocsetViewerWidget(QWidget *parent = nullptr);
    ~DocsetViewerWidget() override;

private slots:
    void performSearch();
    void onResultClicked(int row);

private:
    QLineEdit* m_searchEdit;
    QListWidget* m_resultsList;
    QTextBrowser* m_textBrowser;
    
    // Store pairs of <Keyword, AbsoluteHTMLPath> for easy access
    QList<QPair<QString, QString>> m_searchResults;
};

class ManPageViewerWidget : public QWidget {
    Q_OBJECT
public:
    explicit ManPageViewerWidget(QWidget *parent = nullptr);
    ~ManPageViewerWidget() override;

private slots:
    void performSearch();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    QLineEdit* m_searchEdit;
    QTextBrowser* m_textBrowser;
    QProcess* m_process;
};

class ReferenceManualWidget : public QWidget {
    Q_OBJECT
public:
    explicit ReferenceManualWidget(QWidget *parent = nullptr);
    ~ReferenceManualWidget() override;

private:
    QTabWidget* m_tabWidget;
};

} // namespace gridlock::ui
