/**
 * @file test_docset.cpp
 * @brief Implementation of DocsetManager indexing and search tests.
 *
 * Constructs a mock `.docset` directory with an embedded SQLite `docSet.dsidx` database,
 * querying the index and verifying absolute document path resolution.
 */

#include "test_docset.hpp"

#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "../src/core/managers/DocsetManager.hpp"

namespace gridlock::tests {

/**
 * @brief Verifies Zeal .docset bundle auto-discovery, SQLite searchIndex lookup,
 *        fragment/anchor stripping (#top), and dark-slate HTML reader stylesheet injection.
 */
void testZealDocsetAutoDiscoveryAndHtmlLoading() {
    QTemporaryDir tempZealDir;
    QVERIFY(tempZealDir.isValid());

    // Create a realistic Zeal .docset bundle: TempZeal/C_Test.docset/Contents/Resources/Documents/c/io/printf.html
    QDir rootDir(tempZealDir.path());
    QVERIFY(rootDir.mkpath(QStringLiteral("C_Test.docset/Contents/Resources/Documents/c/io")));

    const QString dsidxPath = rootDir.filePath(QStringLiteral("C_Test.docset/Contents/Resources/docSet.dsidx"));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("zeal_c_test_setup"));
        db.setDatabaseName(dsidxPath);
        QVERIFY(db.open());
        QSqlQuery q(db);
        QVERIFY(q.exec(QStringLiteral("CREATE TABLE searchIndex(id INTEGER PRIMARY KEY, name TEXT, type TEXT, path TEXT);")));
        QVERIFY(q.exec(QStringLiteral(
            "INSERT INTO searchIndex(name, type, path) VALUES('printf', 'func', '<dash_entry_name=printf>c/io/printf.html#top');")));
        db.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("zeal_c_test_setup"));

    // Write actual HTML documentation file on disk inside Documents/c/io/printf.html.
    const QString htmlFilePath = rootDir.filePath(
        QStringLiteral("C_Test.docset/Contents/Resources/Documents/c/io/printf.html"));
    {
        QFile htmlFile(htmlFilePath);
        QVERIFY(htmlFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text));
        htmlFile.write("<html><body><h1>printf</h1><p>Formatted output conversion</p></body></html>");
        htmlFile.close();
    }

    auto& manager = gridlock::core::DocsetManager::instance();
    manager.setDocsetDirectory(tempZealDir.path());

    // Verify loadedDocsetNames() discovers "C_Test".
    const QStringList docsetNames = manager.loadedDocsetNames();
    QVERIFY2(docsetNames.contains(QStringLiteral("C_Test")),
             qPrintable(QString("Expected 'C_Test' in loadedDocsetNames(), got: %1").arg(docsetNames.join(", "))));

    // Verify search("printf") resolves the entry and getEntryHtmlForPath loads the actual HTML file
    // (stripping <dash_entry_...> and #top) with dark-slate CSS injected.
    const auto results = manager.search(QStringLiteral("printf"));
    QVERIFY(!results.isEmpty());
    QCOMPARE(results.first().first, QStringLiteral("printf"));

    const QString renderedHtml = manager.getEntryHtmlForPath(results.first().second);
    QVERIFY2(renderedHtml.contains(QStringLiteral("Formatted output conversion")),
             "getEntryHtmlForPath must load real HTML content from disk after stripping #top anchor");
    QVERIFY2(renderedHtml.contains(QStringLiteral("#16181d"), Qt::CaseInsensitive),
             "getEntryHtmlForPath must inject dark-slate #16181d reader stylesheet");
}

}  // namespace gridlock::tests

/**
 * @brief Verifies symbol lookup, Zeal docset auto-discovery, and HTML document loading against SQLite docsets.
 */
void TestDocset::testSearch() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    // Create fake docset structure mimicking Dash/Zeal format.
    QDir dir(tempDir.path());
    dir.mkdir("Fake.docset");
    dir.cd("Fake.docset");
    dir.mkdir("Contents");
    dir.cd("Contents");
    dir.mkdir("Resources");
    dir.cd("Resources");

    // Create SQLite db index.
    QString dbPath = dir.absoluteFilePath("docSet.dsidx");
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "test_setup_db");
        db.setDatabaseName(dbPath);
        QVERIFY(db.open());
        QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TABLE searchIndex(id INTEGER PRIMARY KEY, name TEXT, type TEXT, path TEXT);"));
        QVERIFY(q.exec("INSERT INTO searchIndex(name, type, path) VALUES('std::cout', 'Object', 'cpp/io/cout.html');"));
        db.close();
    }
    QSqlDatabase::removeDatabase("test_setup_db");

    auto& manager = gridlock::core::DocsetManager::instance();
    // This will call loadDocsets() and attach our fake DB.
    manager.setDocsetDirectory(tempDir.path());

    // Search for 'cout' and assert returned documentation path matches.
    auto results = manager.search("cout");
    QCOMPARE(results.size(), 1);
    QCOMPARE(results[0].first, QString("std::cout"));

    // Expected docset path + /Contents/Resources/Documents/ + rawPath
    QString expectedPath = tempDir.path() + "/Fake.docset/Contents/Resources/Documents/cpp/io/cout.html";
    QCOMPARE(results[0].second, expectedPath);

    // Run Zeal docset bundle discovery and HTML reader verification.
    gridlock::tests::testZealDocsetAutoDiscoveryAndHtmlLoading();
}

QTEST_GUILESS_MAIN(TestDocset)
