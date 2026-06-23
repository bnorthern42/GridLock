#pragma once
#include <QObject>

class TestSanitizer : public QObject {
    Q_OBJECT

private slots:
    void testValidPayload();
    void testDepthExceeded();
    void testTruncation();
    void testControlCharacterEscaping();
};
