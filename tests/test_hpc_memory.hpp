#pragma once

#include <QObject>

class TestHpcMemory : public QObject {
    Q_OBJECT

private slots:
    void testDirectRead();
};
