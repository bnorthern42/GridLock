#pragma once
#include <QObject>

class TestMemoryExport : public QObject {
    Q_OBJECT
private slots:
    void testExportToBinary();
    void testExportToCsvInt32();
    void testExportToCsvFloat();
    void testExportToCsvDouble();
    void testExportBoundsCheck();
};
