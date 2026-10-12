#pragma once

#include <QObject>

class TestStrideSecurity : public QObject {
    Q_OBJECT

private slots:
    void testValidStride();
    void testOutOfBoundsStride();
    void testOverflowStride();
    void testSafeToRenderValid();
    void testSafeToRenderInvalid();
    void testBaseAddressOverflow();
    void testNullNode();
    void testRenderCountOverflow();
    void testExactBoundary();
    void testZeroBytes();
};
