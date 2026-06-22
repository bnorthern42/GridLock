#pragma once
#include <QObject>
#include <QtTest>

class TestDapEvaluate : public QObject {
    Q_OBJECT
private slots:
    void testEvaluateRequest();
    void testEvaluateResponse();
};
