#pragma once
#include <QObject>
#include <QtTest>

class TestDapStacktrace : public QObject {
    Q_OBJECT
private slots:
    void testStackTraceRequest();
    void testStackTraceResponseParsing();
};
