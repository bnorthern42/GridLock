#include "DocsetManager.hpp"
#include "ConfigManager.hpp"
#include <QSqlQuery>
#include <QSqlError>
#include <QDir>
#include <QDirIterator>
#include <QDebug>
#include <QFile>

namespace gridlock::core {

DocsetManager::DocsetManager(QObject* parent) : QObject(parent) {
    m_db = QSqlDatabase::addDatabase("QSQLITE", "DocsetDB");
    m_docsetDir = ConfigManager::instance().getDocsetDirectory();
    if (m_docsetDir.isEmpty()) {
        m_docsetDir = QDir::homePath() + "/.local/share/Zeal/Zeal/docsets";
    }
    loadDocsets();
}

DocsetManager::~DocsetManager() {
    if (m_db.isOpen()) {
        m_db.close();
    }
}

void DocsetManager::setDocsetDirectory(const QString& path) {
    m_docsetDir = path;
    ConfigManager::instance().setDocsetDirectory(path);
    loadDocsets();
}

QString DocsetManager::getDocsetDirectory() const {
    return m_docsetDir;
}

void DocsetManager::loadDocsets() {
    if (m_db.isOpen()) m_db.close();
    m_activeDocsetPaths.clear();

    QDir dir(m_docsetDir);
    if (!dir.exists()) return;

    m_db.setDatabaseName(":memory:"); // Dummy main db
    if (!m_db.open()) return;

    QStringList filters;
    filters << "*.docset";
    QFileInfoList docsets = dir.entryInfoList(filters, QDir::Dirs | QDir::NoDotAndDotDot);
    
    int count = 0;
    for (const auto& ds : docsets) {
        QString dbPath = ds.absoluteFilePath() + "/Contents/Resources/docSet.dsidx";
        if (QFile::exists(dbPath)) {
            QString attachQuery = QString("ATTACH DATABASE '%1' AS db%2").arg(dbPath).arg(count);
            QSqlQuery q(m_db);
            if (q.exec(attachQuery)) {
                m_activeDocsetPaths.append(ds.absoluteFilePath());
                count++;
                if (count >= 10) break; // SQLite default limit for attached DBs
            }
        }
    }
}

QList<QPair<QString, QString>> DocsetManager::search(const QString& query) {
    QList<QPair<QString, QString>> results;
    if (!m_db.isOpen() || m_activeDocsetPaths.isEmpty()) return results;

    for (int i = 0; i < m_activeDocsetPaths.size(); ++i) {
        QSqlQuery q(m_db);
        QString sql = QString("SELECT name, path FROM db%1.searchIndex WHERE name LIKE '%%2%' LIMIT 50;").arg(i).arg(query);
        if (q.exec(sql)) {
            while (q.next()) {
                QString name = q.value(0).toString();
                // Docset paths can include anchors, so we preserve the full path
                QString path = m_activeDocsetPaths[i] + "/Contents/Resources/Documents/" + q.value(1).toString();
                results.append(qMakePair(name, path));
                if (results.size() >= 50) return results;
            }
        }
    }
    return results;
}

QStringList DocsetManager::suggestDocsets(const QString& projectRoot) {
    QStringList suggestions;
    QDirIterator it(projectRoot, QStringList() << "*.c" << "*.cpp" << "*.h" << "*.hpp", QDir::Files, QDirIterator::Subdirectories);
    
    bool wantsMpi = false;
    bool wantsCpp = false;
    
    while (it.hasNext()) {
        QFile file(it.next());
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QString content = file.readAll();
            if (content.contains("<mpi.h>")) wantsMpi = true;
            if (content.contains("<vector>") || content.contains("<iostream>")) wantsCpp = true;
        }
        if (wantsMpi && wantsCpp) break;
    }
    
    if (wantsMpi) suggestions << "OpenMPI";
    if (wantsCpp) suggestions << "C++";
    
    return suggestions;
}

QStringList DocsetManager::getActiveDocsetPaths() const {
    return m_activeDocsetPaths;
}

} // namespace gridlock::core
