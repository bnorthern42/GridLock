#pragma once
#include <QObject>
#include <QtTest>

class TestMainWindowUI : public QObject {
    Q_OBJECT

private slots:
    void testSourceFileLoading();
    void testMainLayoutStructure();
    void testSourceCodeViewportMargins();
    void testServerRackStateUpdate();
};
