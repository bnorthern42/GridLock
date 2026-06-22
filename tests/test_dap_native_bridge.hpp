#pragma once

#include <QObject>

class TestDapNativeBridge : public QObject {
    Q_OBJECT

private slots:
    void testPidExtraction();
    void testRoutingLogic();
};
