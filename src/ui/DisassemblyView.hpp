#pragma once
#include <QWidget>
#include <QString>

namespace gridlock::ui {

class DisassemblyView : public QWidget {
    Q_OBJECT
public:
    explicit DisassemblyView(QWidget *parent = nullptr);
    ~DisassemblyView() override = default;

public slots:
    void updateDisassembly(const QString& asmCode) {
        m_asmCode = asmCode;
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_asmCode;
};

} // namespace gridlock::ui
