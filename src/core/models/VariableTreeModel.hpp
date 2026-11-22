/**
 * @file VariableTreeModel.hpp
 * @brief Qt item model presenting hierarchical local variables and structs per MPI rank.
 *
 * Implements lazy tree expansion (`canFetchMore`/`fetchMore`) over both GDB/MI
 * (`-var-create` / `-var-list-children`) and LLDB-DAP (`scopes` / `variables`
 * requests), highlighting modified variable values across step transitions.
 */

#pragma once

#include <memory>

#include <QAbstractItemModel>
#include <QJsonArray>
#include <QList>
#include <QMap>
#include <QSet>
#include <QString>
#include <QVariantList>

#include "VariableNode.hpp"

class IBackendCoordinator;

namespace gridlock {

/**
 * @brief Three-column (Name, Value, Type) hierarchical tree model for stack frame variables.
 */
class VariableTreeModel : public QAbstractItemModel {
    Q_OBJECT
public:
    /**
     * @brief Constructs the variable tree model and connects to backend coordinator signals.
     * @param coordinator Active GDB/MI or LLDB-DAP backend coordinator.
     * @param parent Optional owning QObject parent.
     */
    explicit VariableTreeModel(IBackendCoordinator* coordinator, QObject* parent = nullptr);

    /** @brief Destroys the model and releases any active GDB variable objects. */
    ~VariableTreeModel() override;

    /** @brief Returns the model index for the given row, column, and parent index. */
    QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;

    /** @brief Returns the parent model index of the specified child index. */
    QModelIndex parent(const QModelIndex& child) const override;

    /** @brief Returns the number of child rows under the given parent index. */
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;

    /** @brief Returns the fixed column count (3: Name, Value, Type). */
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;

    /** @brief Returns display text, edit text, monospace font, alignment, or diff highlight brush for index. */
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    /** @brief Enables `Qt::ItemIsEditable` on scalar/enum/pointer leaf value cells (column 1). */
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    /** @brief Commits an inline variable value edit and emits `variableEditRequested(rank, fullExpression, newValue)`. */
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;

    /** @brief Returns horizontal column header titles ("Name", "Value", "Type"). */
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    /** @brief Returns true if the parent node has loaded or unloaded child fields. */
    bool hasChildren(const QModelIndex& parent = QModelIndex()) const override;

    /** @brief Returns true if the node has remote children that have not yet been fetched. */
    bool canFetchMore(const QModelIndex& parent) const override;

    /** @brief Requests child fields from GDB/MI or LLDB-DAP when the user expands a node. */
    void fetchMore(const QModelIndex& parent) override;

    /** @brief Returns the full C/C++ expression path for @p index (e.g., `"Iconfig.mesh.dims"`). */
    QString expressionPath(const QModelIndex& index) const;

    /**
     * @brief Snapshots current values and queries top-level local variables for rankId.
     * @param rankId Zero-based MPI rank index to inspect.
     */
    void loadLocals(int rankId);

    /** @brief Deletes backend GDB varobjs and resets the tree model to an empty root. */
    void clear();

signals:
    /** @brief Emitted when a leaf variable value is modified via `setData()`. */
    void variableEditRequested(int rankId, const QString& fullExpression, const QString& newValue);

public slots:
    /**
     * @brief Parses asynchronous GDB/MI variable responses for the active MPI rank.
     * @param rankId Originating MPI rank index.
     * @param output Raw GDB/MI result record line.
     */
    void handleGdbOutput(int rankId, const QString& output);

    /**
     * @brief Populates root or child VariableNode entries from a DAP variables response.
     * @param rankId Originating MPI rank index.
     * @param parentVarRef DAP variablesReference of the parent container (0 for top-level).
     * @param variables JSON array of DAP Variable objects.
     */
    void onLocalsUpdated(int rankId, int parentVarRef, const QJsonArray& variables);

    /**
     * @brief Populates lazy child entries resolved via `IBackendCoordinator::requestVariableChildren`.
     */
    void onVariableChildrenUpdated(int rankId,
                                   const QString& parentExpression,
                                   const QVariantList& children);

    /**
     * @brief Filters a DAP scopes response and requests variables for the "Locals" scope.
     * @param rankId Originating MPI rank index.
     * @param scopes JSON array of DAP Scope descriptors.
     */
    void onScopesUpdated(int rankId, const QJsonArray& scopes);

private:
    /** @brief Extracts the VariableNode pointer from a QModelIndex, defaulting to m_rootNode. */
    VariableNode* getNode(const QModelIndex& index) const;

    /** @brief Builds the QModelIndex for an internal VariableNode pointer. */
    QModelIndex getIndexForNode(VariableNode* node) const;

    /** @brief Builds the full C++ member/array expression path for @p node. */
    QString buildExpressionPath(const VariableNode* node) const;

    /** @brief Recursively locates a VariableNode matching @p expr under @p root. */
    VariableNode* findNodeByExpr(VariableNode* root, const QString& expr) const;

    /** @brief Returns true when @p node represents a `{...}` composite or container with children. */
    static bool isCompositeVariable(const VariableNode* node);

    /** @brief Parses `-stack-list-variables` output and issues `-var-create` per local symbol. */
    void parseListVariables(const QString& output);

    /** @brief Parses `-var-create` output and appends a new top-level VariableNode. */
    void parseVarCreate(const QString& output);

    /** @brief Parses `-var-list-children` output and inserts child rows under the parent varobj. */
    void parseVarListChildren(const QString& output);

    /** @brief Recursively records current scalar values into m_previousValues before a refresh. */
    void storePreviousValues(VariableNode* node);

    IBackendCoordinator* m_coordinator = nullptr;
    std::unique_ptr<VariableNode> m_rootNode;
    int m_currentRankId = -1;
    int m_updateCounter = 0;

    QList<QString> m_createdVarobjs;
    QMap<QString, VariableNode*> m_varobjToNode;
    QSet<VariableNode*> m_pendingChildFetches;

    int m_evalCounter = 10000;
    QMap<int, QString> m_evalTokenToVarobj;
    QMap<QString, QString> m_previousValues;
};

} // namespace gridlock
