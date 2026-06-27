#pragma once

#include <QDialog>
#include <QListWidget>
#include <QStackedWidget>
#include <QPushButton>
#include <QHBoxLayout>
#include <QString>
#include <QSet>

namespace gridlock::ui {

class TutorialDialog : public QDialog {
    Q_OBJECT

public:
    explicit TutorialDialog(QWidget* parent = nullptr);

public:
    static QSet<int> getBreakpointsForFile(const QString& filePath);
    static void injectBreakpoints(const QString& relativePath, const QString& absPath);

signals:
    void launchTutorialRequested(const QString& absoluteFilePath);

private slots:
    void onSelectionChanged(int currentRow);
    void onLaunchClicked();

private:
    void populateList();

    QListWidget* m_listWidget;
    QStackedWidget* m_stackedWidget;
};

} // namespace gridlock::ui
