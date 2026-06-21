#pragma once
#include <QObject>
#include <QtTest>

class TestDocset : public QObject {
    Q_OBJECT
private slots:
    void testSearch();
};
