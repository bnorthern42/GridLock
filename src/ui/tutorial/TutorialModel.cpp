#include "TutorialModel.hpp"

namespace gridlock {
namespace ui {
namespace tutorial {

TutorialModel::TutorialModel(QObject *parent)
    : QAbstractListModel(parent)
{
    m_tutorials = {
        {"Deadlock Demo", "Intentionally creates an MPI deadlock to demonstrate deadlock detection.", "tutorial/deadlock_demo.c"},
        {"Inspection Demo", "Shows deeply nested structs and arrays to demonstrate the watch grid.", "tutorial/inspection_demo.cpp"},
        {"Register Demo", "Utilizes CPU registers heavily to demonstrate the Register view.", "tutorial/register_demo.cpp"},
        {"MemView Demo", "Allocates contiguous memory and manipulates pointers for MemView.", "tutorial/memview_demo.cpp"},
        {"MemView Diff Demo", "Creates diverging memory states between MPI ranks for Diff Viewer.", "tutorial/memview_diff_demo.cpp"}
    };
}

int TutorialModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid())
        return 0;
    return m_tutorials.size();
}

QVariant TutorialModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_tutorials.size())
        return QVariant();

    const auto &item = m_tutorials[index.row()];

    switch (role) {
    case NameRole:
        return item.name;
    case DescriptionRole:
        return item.description;
    case FilePathRole:
        return item.filePath;
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> TutorialModel::roleNames() const {
    QHash<int, QByteArray> roles;
    roles[NameRole] = "name";
    roles[DescriptionRole] = "description";
    roles[FilePathRole] = "filePath";
    return roles;
}

} // namespace tutorial
} // namespace ui
} // namespace gridlock
