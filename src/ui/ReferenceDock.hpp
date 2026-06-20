#pragma once
#include <QWidget>

namespace gridlock::ui {

class ReferenceDock : public QWidget {
    Q_OBJECT
public:
    explicit ReferenceDock(QWidget *parent = nullptr);
    ~ReferenceDock() override = default;
};

} // namespace gridlock::ui
