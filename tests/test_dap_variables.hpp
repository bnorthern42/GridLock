#pragma once
#include <QObject>
#include <QtTest>

class TestDapVariables : public QObject {
    Q_OBJECT
private slots:
    void testScopesFetch();
    void testVariablesFetch();
    void testVariableTreePopulation();
};
