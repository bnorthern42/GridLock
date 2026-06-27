#pragma once

#include <QDialog>
#include <QListView>
#include <QPushButton>
#include <QVBoxLayout>
#include <QString>
#include "TutorialModel.hpp"

namespace gridlock::ui {
class MainWindow;
}

namespace gridlock::ui::tutorial {

class TutorialWizard : public QDialog {
    Q_OBJECT

public:
    explicit TutorialWizard(gridlock::ui::MainWindow* mainWindow, QWidget* parent = nullptr);
    QString getSelectedFilePath() const;

private slots:
    void onLaunchClicked();
    void compileAndLaunch(const QString& sourceFile);

private:
    QListView* m_listView;
    TutorialModel* m_model;
    QPushButton* m_launchBtn;
    gridlock::ui::MainWindow* m_mainWindow;
};

} // namespace gridlock::ui::tutorial
