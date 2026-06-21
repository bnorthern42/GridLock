#pragma once
#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QList>
#include <QPair>
#include <QStringList>

namespace gridlock::core {

class DocsetManager : public QObject {
    Q_OBJECT
public:
    static DocsetManager& instance() {
        static DocsetManager instance;
        return instance;
    }

    void setDocsetDirectory(const QString& path);
    QString getDocsetDirectory() const;
    void loadDocsets();
    
    // Suggests docsets to download based on file contents
    QStringList suggestDocsets(const QString& projectRoot);

    // Returns a list of <Keyword, AbsoluteHTMLPath> pairs
    QList<QPair<QString, QString>> search(const QString& query);

    // Returns the active docset absolute paths
    QStringList getActiveDocsetPaths() const;

private:
    DocsetManager(QObject* parent = nullptr);
    ~DocsetManager() override;

    QString m_docsetDir;
    QSqlDatabase m_db;
    QStringList m_activeDocsetPaths;
};

} // namespace gridlock::core
