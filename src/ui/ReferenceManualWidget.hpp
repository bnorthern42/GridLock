#pragma once
#include <QWidget>
#include <QLineEdit>
#include <QListWidget>
#include <QTextBrowser>
#include <QSplitter>

namespace gridlock::ui {

class ReferenceManualWidget : public QWidget {
    Q_OBJECT
public:
    explicit ReferenceManualWidget(QWidget *parent = nullptr);
    ~ReferenceManualWidget() override;

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

} // namespace gridlock::ui
