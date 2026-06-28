#pragma once
#include <QObject>
#include <QtTest/QtTest>

class TestLspCoordinator : public QObject {
    Q_OBJECT
private slots:
    void testMessageFraming();
    void testLspParser_data();
    void testLspParser();
    void testFragmentedStream();
    void testLspHoverParsing();
};
