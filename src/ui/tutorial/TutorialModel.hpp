#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QList>

namespace gridlock {
namespace ui {
namespace tutorial {

struct TutorialItem {
    QString name;
    QString description;
    QString filePath;
};

class TutorialModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles {
        NameRole = Qt::DisplayRole,
        FilePathRole = Qt::UserRole,
        DescriptionRole = Qt::UserRole + 1
    };

    explicit TutorialModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    QList<TutorialItem> m_tutorials;
};

} // namespace tutorial
} // namespace ui
} // namespace gridlock
