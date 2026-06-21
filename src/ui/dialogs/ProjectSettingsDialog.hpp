#pragma once
#include <QDialog>
#include <QLineEdit>

namespace gridlock::ui {

class ProjectSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit ProjectSettingsDialog(QWidget* parent = nullptr);
    void accept() override;

private:
    QLineEdit* m_targetBinary;
    QLineEdit* m_binaryArguments;
    QLineEdit* m_workingDirectory;
};

} // namespace gridlock::ui
