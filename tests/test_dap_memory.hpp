#pragma once
#include <QObject>
#include <QtTest>

class TestDapMemory : public QObject {
    Q_OBJECT
private slots:
    void testRegistersFetch();
    void testMemoryRequest();
    void testMemoryResponse();
};
