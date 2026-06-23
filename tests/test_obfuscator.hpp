#pragma once
#include <QObject>

class TestObfuscator : public QObject {
    Q_OBJECT

private slots:
    void testConstantSizePadding();
    void testEmptyFlush();
};
