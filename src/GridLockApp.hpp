#pragma once
#include <QObject>

namespace gridlock {

class GridLockApp : public QObject {
    Q_OBJECT
public:
    explicit GridLockApp(QObject *parent = nullptr);
    ~GridLockApp() override = default;
};

} // namespace gridlock
