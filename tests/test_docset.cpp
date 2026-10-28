/**
 * @file test_docset.cpp
 * @brief Implementation of DocsetManager indexing and search tests.
 *
 * Constructs a mock `.docset` directory with an embedded SQLite `docSet.dsidx` database,
 * querying the index and verifying absolute document path resolution.
 */

#include "test_docset.hpp"

#include <QDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "../src/core/managers/DocsetManager.hpp"

/**
 * @brief Verifies symbol lookup and document path construction against SQLite docsets.
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
}

QTEST_GUILESS_MAIN(TestDocset)
