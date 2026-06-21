#pragma once
#include <QWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QTextBrowser>
#include <QProcess>

namespace gridlock::ui {

class ReferenceManualWidget : public QWidget {
    Q_OBJECT
public:
    explicit ReferenceManualWidget(QWidget *parent = nullptr);
    ~ReferenceManualWidget() override;

private slots:
    void performSearch();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    QLineEdit* m_searchEdit;
    QComboBox* m_sectionCombo;
    QPushButton* m_searchBtn;
    QTextBrowser* m_textBrowser;
    QProcess* m_process;
    QString m_currentQuery;
};

} // namespace gridlock::ui
