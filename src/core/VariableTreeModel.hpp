#pragma once
#include <QAbstractItemModel>
#include <QString>
#include <QList>
#include <QMap>
#include <memory>
#include "VariableNode.hpp"

namespace gridlock {

class GdbRankCoordinator;

class VariableTreeModel : public QAbstractItemModel {
    Q_OBJECT
public:
    explicit VariableTreeModel(GdbRankCoordinator* coordinator, QObject* parent = nullptr);
    ~VariableTreeModel() override;

    QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    bool canFetchMore(const QModelIndex& parent) const override;
    void fetchMore(const QModelIndex& parent) override;

    void loadLocals(int rankId);
    void clear();

public slots:
    void handleGdbOutput(int rankId, const QString& output);

private:
    VariableNode* getNode(const QModelIndex& index) const;
    void parseListVariables(const QString& output);
    void parseVarCreate(const QString& output);
    void parseVarListChildren(const QString& output);

    GdbRankCoordinator* m_coordinator;
    std::unique_ptr<VariableNode> m_rootNode;
    int m_currentRankId;
    int m_updateCounter = 0;

    QList<QString> m_createdVarobjs;
    QMap<QString, VariableNode*> m_varobjToNode;
};

} // namespace gridlock
