/**
 * @file test_version.cpp
 * @brief Unit tests for semantic version string formatting and numeric field parsing.
 *
 * Validates regular expression matching of semantic versioning formats and checks
 * individual major, minor, and patch integer components in Version.
 */

#include <QObject>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QtTest/QtTest>

#include "../src/core/Version.hpp"

/**
 * @class TestVersion
 * @brief Test suite validating semantic version parsing and format integrity.
 */
class TestVersion : public QObject {
    Q_OBJECT

private slots:
    /**
     * @brief Verifies that version string conforms to major.minor.patch regex.
     */
    void testStringFormat() {
        QString ver = gridlock::core::Version::getString();
        // Check non-empty string.
        QVERIFY(!ver.isEmpty());
        // Verify it matches expected regex "X.Y.Z"
        QRegularExpression rx("^\\d+\\.\\d+\\.\\d+$");
        QVERIFY(rx.match(ver).hasMatch());
    }

    /**
     * @brief Verifies numeric breakdown of major, minor, and patch values.
     */
    void testParsing() {
        QString ver = gridlock::core::Version::getString();
        QStringList parts = ver.split('.');
        // Assert individual version numbers match split components.
        QCOMPARE(gridlock::core::Version::getMajor(), parts[0].toInt());
        QCOMPARE(gridlock::core::Version::getMinor(), parts[1].toInt());
        QCOMPARE(gridlock::core::Version::getPatch(), parts[2].toInt());
    }
};

QTEST_MAIN(TestVersion)
#include "test_version.moc"
