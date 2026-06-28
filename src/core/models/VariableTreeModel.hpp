#pragma once
#include <QAbstractItemModel>
#include <QString>
#include <QList>
#include <QMap>
#include <QJsonArray>
#include <memory>
#include "VariableNode.hpp"

class IBackendCoordinator;

namespace gridlock {

class VariableTreeModel : public QAbstractItemModel {
    Q_OBJECT
public:
    explicit VariableTreeModel(IBackendCoordinator* coordinator, QObject* parent = nullptr);
    ~VariableTreeModel() override;

    QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    bool hasChildren(const QModelIndex& parent = QModelIndex()) const override;

    bool canFetchMore(const QModelIndex& parent) const override;
    void fetchMore(const QModelIndex& parent) override;

    void loadLocals(int rankId);
    void clear();

public slots:
    void handleGdbOutput(int rankId, const QString& output);
    void onLocalsUpdated(int rankId, int parentVarRef, const QJsonArray& variables);

private:
    VariableNode* getNode(const QModelIndex& index) const;
    void parseListVariables(const QString& output);
    void parseVarCreate(const QString& output);
    void parseVarListChildren(const QString& output);
    void storePreviousValues(VariableNode* node);

    IBackendCoordinator* m_coordinator;
    std::unique_ptr<VariableNode> m_rootNode;
    int m_currentRankId;
    int m_updateCounter = 0;

    QList<QString> m_createdVarobjs;
    QMap<QString, VariableNode*> m_varobjToNode;
    
    int m_evalCounter = 10000;
    QMap<int, QString> m_evalTokenToVarobj;
    QMap<QString, QString> m_previousValues;
};

} // namespace gridlock
