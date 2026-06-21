#include "test_docset.hpp"
#include "../src/core/managers/DocsetManager.hpp"
#include <QTemporaryDir>
#include <QDir>
#include <QSqlDatabase>
#include <QSqlQuery>

void TestDocset::testSearch() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    
    // Create fake docset structure
    QDir dir(tempDir.path());
    dir.mkdir("Fake.docset");
    dir.cd("Fake.docset");
    dir.mkdir("Contents");
    dir.cd("Contents");
    dir.mkdir("Resources");
    dir.cd("Resources");
    
    // Create SQLite db
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
    manager.setDocsetDirectory(tempDir.path()); // This will call loadDocsets() and attach our fake DB.
    
    auto results = manager.search("cout");
    QCOMPARE(results.size(), 1);
    QCOMPARE(results[0].first, QString("std::cout"));
    // Docset path + /Contents/Resources/Documents/ + rawPath
    QString expectedPath = tempDir.path() + "/Fake.docset/Contents/Resources/Documents/cpp/io/cout.html";
    QCOMPARE(results[0].second, expectedPath);
}

QTEST_GUILESS_MAIN(TestDocset)
