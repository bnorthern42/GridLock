#include <QtTest>
#include <QRegularExpression>
#include "../src/core/Version.hpp"

class TestVersion : public QObject {
    Q_OBJECT

private slots:
    void testStringFormat() {
        QString ver = gridlock::core::Version::getString();
        QVERIFY(!ver.isEmpty());
        // Verify it matches expected regex "X.Y.Z"
        QRegularExpression rx("^\\d+\\.\\d+\\.\\d+$");
        QVERIFY(rx.match(ver).hasMatch());
    }

    void testParsing() {
        QString ver = gridlock::core::Version::getString();
        QStringList parts = ver.split('.');
        QCOMPARE(gridlock::core::Version::getMajor(), parts[0].toInt());
        QCOMPARE(gridlock::core::Version::getMinor(), parts[1].toInt());
        QCOMPARE(gridlock::core::Version::getPatch(), parts[2].toInt());
    }
};

QTEST_MAIN(TestVersion)
#include "test_version.moc"
