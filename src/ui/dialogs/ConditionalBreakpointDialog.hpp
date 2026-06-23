#pragma once
#include <QDialog>
#include <QLineEdit>

namespace gridlock::ui {

class ConditionalBreakpointDialog : public QDialog {
    Q_OBJECT
public:
    explicit ConditionalBreakpointDialog(const QString& currentCondition = "", QWidget* parent = nullptr);

    QString condition() const;

private:
    QLineEdit* m_conditionEdit;
};

} // namespace gridlock::ui
