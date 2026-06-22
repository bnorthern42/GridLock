#pragma once
#include <QObject>
#include <QtTest>

class TestDapParser : public QObject {
    Q_OBJECT
private slots:
    void testFullMessage();
    void testFragmentedMessage();
    void testMultipleMessages();
};
