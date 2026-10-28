/**
 * @file VariableTreeModel.cpp
 * @brief Implementation of the dual-protocol (GDB/MI & LLDB-DAP) hierarchical variable model.
 *
 * Manages lazy tree population for local variables, struct members, and pointers
 * across MPI ranks. Tracks previous step values to highlight mutated variables
 * in the Variables dock and automatically resolves aggregate struct addresses.
 */

#include "VariableTreeModel.hpp"

#include <functional>

#include <QColor>
#include <QFont>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>

#include "../hpc/DapCoordinator.hpp"
#include "../hpc/GdbRankCoordinator.hpp"
#include "../hpc/IBackendCoordinator.hpp"

namespace gridlock {

/**
 * @brief Constructs a VariableTreeModel and attaches signal listeners to the active backend.
 * @param coordinator Active GDB/MI or LLDB-DAP backend coordinator.
 * @param parent Optional owning QObject parent.
 */
VariableTreeModel::VariableTreeModel(IBackendCoordinator* coordinator, QObject* parent)
    : QAbstractItemModel(parent), m_coordinator(coordinator), m_currentRankId(-1) {
    // Initialize the invisible root container node.
    m_rootNode = std::make_unique<VariableNode>();
    if (m_coordinator) {
        // Connect GDB/MI raw output stream when backed by GdbRankCoordinator.
        if (auto* gdb = dynamic_cast<gridlock::GdbRankCoordinator*>(m_coordinator)) {
            connect(gdb, &gridlock::GdbRankCoordinator::gdbOutputReceived,
                    this, &VariableTreeModel::handleGdbOutput);
        }
        // Connect structured scopes, variables, and stop-location signals when backed by DapCoordinator.
        if (auto* dap = dynamic_cast<DapCoordinator*>(m_coordinator)) {
            connect(dap, &DapCoordinator::localsUpdated, this, &VariableTreeModel::onLocalsUpdated);
            connect(dap, &DapCoordinator::scopesUpdated, this, &VariableTreeModel::onScopesUpdated);
            connect(dap, &DapCoordinator::locationChanged, this, [this](int rankId, const QString&, int) {
                if (m_currentRankId == rankId || m_currentRankId == -1) {
                    loadLocals(rankId);
                }
            });
        }
    }
}

/**
 * @brief Destroys the model after deleting any live backend variable objects.
 */
VariableTreeModel::~VariableTreeModel() {
    // Release any live backend variable objects before destruction.
    clear();
}

/**
 * @brief Creates a QModelIndex for the specified child row and column under parent.
 */
QModelIndex VariableTreeModel::index(int row, int column, const QModelIndex& parent) const {
    if (!hasIndex(row, column, parent)) {
        return QModelIndex();
    }
    // Look up the parent VariableNode and bind the child pointer into the QModelIndex internalPointer.
    VariableNode* parentNode = getNode(parent);
    if (row >= 0 && row < static_cast<int>(parentNode->children.size())) {
        return createIndex(row, column, parentNode->children[row].get());
    }
    return QModelIndex();
}

/**
 * @brief Computes the parent QModelIndex for a given child index in the variable hierarchy.
 */
QModelIndex VariableTreeModel::parent(const QModelIndex& child) const {
    if (!child.isValid()) {
        return QModelIndex();
    }
    VariableNode* childNode = getNode(child);
    VariableNode* parentNode = childNode->parent;
    // Top-level variables have m_rootNode as their parent and report an invalid QModelIndex.
    if (parentNode == m_rootNode.get() || !parentNode) {
        return QModelIndex();
    }

    // Locate parentNode's row index within its own parent (grandParent) children vector.
    VariableNode* grandParent = parentNode->parent;
    int row = 0;
    for (size_t i = 0; i < grandParent->children.size(); ++i) {
        if (grandParent->children[i].get() == parentNode) {
            row = static_cast<int>(i);
            break;
        }
    }
    return createIndex(row, 0, parentNode);
}

/**
 * @brief Returns the number of direct child variables under the specified parent node.
 */
int VariableTreeModel::rowCount(const QModelIndex& parent) const {
    VariableNode* node = getNode(parent);
    return static_cast<int>(node->children.size());
}

/**
 * @brief Returns true if the node has either already-loaded children or pending remote children.
 */
bool VariableTreeModel::hasChildren(const QModelIndex& parent) const {
    VariableNode* node = getNode(parent);
    if (!node) {
        return false;
    }
    // Report true if the backend indicated child fields exist even before lazy fetch completes.
    if (node->numChildren > 0) {
        return true;
    }
    return !node->children.empty();
}

/**
 * @brief Returns the fixed number of columns (3: Name, Value, Type).
 */
int VariableTreeModel::columnCount(const QModelIndex& /*parent*/) const {
    // Column 0: Name, Column 1: Value, Column 2: Type.
    return 3;
}

/**
 * @brief Supplies display strings, monospace fonts, alignment, and mutation highlight colors.
 */
QVariant VariableTreeModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid()) {
        return QVariant();
    }
    VariableNode* node = getNode(index);

    if (role == Qt::DisplayRole) {
        // Map columns 0, 1, 2 to variable name, value, and C/C++ type string.
        if (index.column() == 0) return node->name;
        if (index.column() == 1) return node->value;
        if (index.column() == 2) return node->type;
    } else if (role == Qt::FontRole) {
        // Render value and type columns in a fixed-width monospace font for alignment.
        if (index.column() == 1 || index.column() == 2) {
            QFont font("JetBrains Mono");
            font.setStyleHint(QFont::Monospace);
            return font;
        }
    } else if (role == Qt::TextAlignmentRole) {
        // Right-align the type column for clean visual separation from values.
        if (index.column() == 2) {
            return QVariant(Qt::AlignRight | Qt::AlignVCenter);
        }
    } else if (role == Qt::BackgroundRole) {
        // Highlight rows whose value changed since the previous step in soft coral red.
        if (m_previousValues.contains(node->name) && m_previousValues[node->name] != node->value) {
            return QColor("#f38ba8");
        }
    } else if (role == Qt::ForegroundRole) {
        // Use dark foreground text when the coral mutation highlight background is active.
        if (m_previousValues.contains(node->name) && m_previousValues[node->name] != node->value) {
            return QColor("#1e1e2e");
        }
    }
    return QVariant();
}

/**
 * @brief Returns horizontal column header labels for the Variables tree view.
 */
QVariant VariableTreeModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        if (section == 0) return "Name";
        if (section == 1) return "Value";
        if (section == 2) return "Type";
    }
    return QVariant();
}

/**
 * @brief Determines whether expanding parent requires an asynchronous backend child query.
 */
bool VariableTreeModel::canFetchMore(const QModelIndex& parent) const {
    // Allow lazy child expansion when the backend reported children that are not yet loaded.
    VariableNode* node = getNode(parent);
    return node && (node->numChildren > 0 || node->variablesReference > 0) && !node->childrenLoaded;
}

/**
 * @brief Issues an asynchronous `-var-list-children` or DAP `variables` request for parent.
 */
void VariableTreeModel::fetchMore(const QModelIndex& parent) {
    if (!parent.isValid()) {
        return;
    }
    VariableNode* node = getNode(parent);
    if (node && (node->numChildren > 0 || node->variablesReference > 0) && !node->childrenLoaded) {
        // Mark childrenLoaded immediately to prevent duplicate asynchronous fetch requests.
        node->childrenLoaded = true;
        if (auto* gdb = dynamic_cast<gridlock::GdbRankCoordinator*>(m_coordinator)) {
            gdb->writeCmd(m_currentRankId,
                          QString("-var-list-children --all-values %1\n").arg(node->varobjName));
        } else if (auto* dap = dynamic_cast<DapCoordinator*>(m_coordinator)) {
            if (node->variablesReference > 0) {
                dap->requestVariables(m_currentRankId, node->variablesReference);
            }
        }
    }
}

/**
 * @brief Snapshots current variable values, clears stale varobjs, and requests frame locals.
 */
void VariableTreeModel::loadLocals(int rankId) {
    // Snapshot prior scalar values so mutated variables can be highlighted after reload.
    storePreviousValues(m_rootNode.get());
    clear();
    m_currentRankId = rankId;
    if (auto* gdb = dynamic_cast<gridlock::GdbRankCoordinator*>(m_coordinator)) {
        // Request all local and argument variables in the selected rank's current frame.
        gdb->sendCommand(rankId, "-stack-list-variables --all-values");
    } else if (auto* dap = dynamic_cast<DapCoordinator*>(m_coordinator)) {
        // For DAP, query scopes for the active frame or request a stack trace if no frame is cached.
        const int activeFrame = dap->getActiveFrameId(rankId);
        if (activeFrame >= 0) {
            dap->requestScopes(activeFrame, rankId);
        } else {
            dap->requestStackTrace(rankId);
        }
    }
}

/**
 * @brief Populates top-level or nested child VariableNodes from a DAP `variables` response.
 */
void VariableTreeModel::onLocalsUpdated(int rankId, int parentVarRef, const QJsonArray& variables) {
    // Ignore updates belonging to non-focused MPI ranks.
    if (m_currentRankId != -1 && rankId != m_currentRankId) return;
    if (m_currentRankId == -1) m_currentRankId = rankId;

    // Depth-first search helper to locate an existing parent VariableNode by its DAP variablesReference.
    std::function<VariableNode*(VariableNode*)> findNodeByRef = [&](VariableNode* n) -> VariableNode* {
        if (!n) return nullptr;
        if (n->variablesReference == parentVarRef) return n;
        for (const auto& child : n->children) {
            if (auto* found = findNodeByRef(child.get())) return found;
        }
        return nullptr;
    };

    VariableNode* targetParent = nullptr;
    if (parentVarRef > 0) {
        targetParent = findNodeByRef(m_rootNode.get());
    }

    // Reconstruct the QModelIndex for an internal VariableNode so beginInsertRows notifies views accurately.
    std::function<QModelIndex(VariableNode*)> getIndex = [&](VariableNode* n) -> QModelIndex {
        if (n == m_rootNode.get() || !n) return QModelIndex();
        if (n->parent == m_rootNode.get()) {
            for (size_t i = 0; i < m_rootNode->children.size(); ++i) {
                if (m_rootNode->children[i].get() == n) return createIndex(static_cast<int>(i), 0, n);
            }
        } else {
            const QModelIndex pIdx = getIndex(n->parent);
            if (!pIdx.isValid()) return QModelIndex();
            for (size_t i = 0; i < n->parent->children.size(); ++i) {
                if (n->parent->children[i].get() == n) return createIndex(static_cast<int>(i), 0, n);
            }
        }
        return QModelIndex();
    };

    QModelIndex parentIdx;
    if (!targetParent) {
        // Top-level "Locals" scope update: reset the entire root collection.
        beginResetModel();
        m_rootNode->children.clear();
        m_rootNode->numChildren = 0;
        targetParent = m_rootNode.get();
    } else {
        // Nested struct/array expansion: insert rows under the existing targetParent index.
        if (targetParent->childrenLoaded) return;
        parentIdx = getIndex(targetParent);
        if (!variables.isEmpty()) {
            beginInsertRows(parentIdx, 0, variables.size() - 1);
        }
    }

    // Materialize a VariableNode for each JSON entry returned in the DAP `variables` array.
    for (const QJsonValue& val : variables) {
        const QJsonObject var = val.toObject();
        const QString name = var["name"].toString();
        const QString value = var["value"].toString();
        const QString type = var["type"].toString();
        const int varRef = var["variablesReference"].toInt(0);

        auto node = std::make_unique<VariableNode>(name, type, value, "", 0, varRef, targetParent);
        if (varRef > 0) {
            // Mark non-zero variablesReference nodes as expandable in QTreeView.
            node->numChildren = 1;
        }
        targetParent->children.push_back(std::move(node));
    }

    if (targetParent == m_rootNode.get()) {
        m_rootNode->numChildren = static_cast<int>(m_rootNode->children.size());
        endResetModel();
    } else {
        targetParent->numChildren = static_cast<int>(targetParent->children.size());
        targetParent->childrenLoaded = true;
        if (!variables.isEmpty()) {
            endInsertRows();
        }
    }
}

/**
 * @brief Filters a DAP `scopes` response and requests variables for the "Locals" scope.
 */
void VariableTreeModel::onScopesUpdated(int rankId, const QJsonArray& scopes) {
    if (m_currentRankId != -1 && rankId != m_currentRankId) return;

    // Inspect the frame's scopes and automatically fetch variables for the "Locals" scope.
    for (const QJsonValue& val : scopes) {
        const QJsonObject scope = val.toObject();
        if (scope["name"].toString() == "Locals") {
            const int varRef = scope["variablesReference"].toInt();
            if (auto* dap = dynamic_cast<DapCoordinator*>(m_coordinator)) {
                dap->requestVariables(rankId, varRef);
            }
            break;
        }
    }
}

/**
 * @brief Clears active GDB variable objects and resets the tree model to an empty state.
 */
void VariableTreeModel::clear() {
    beginResetModel();
    // Delete all previously created GDB variable objects on the active rank to avoid leaks.
    if (m_coordinator && m_currentRankId != -1) {
        for (const QString& varobj : m_createdVarobjs) {
            if (auto* gdb = dynamic_cast<gridlock::GdbRankCoordinator*>(m_coordinator)) {
                gdb->writeCmd(m_currentRankId, QString("-var-delete %1\n").arg(varobj));
            }
        }
    }
    // Reset lookup maps and allocate a fresh empty root node.
    m_createdVarobjs.clear();
    m_varobjToNode.clear();
    m_rootNode = std::make_unique<VariableNode>();
    endResetModel();
}

/**
 * @brief Routes asynchronous GDB/MI variable and expression responses to specialized parsers.
 * @param rankId Zero-based MPI rank index emitting the output line.
 * @param output Raw single-line GDB/MI result record.
 */
void VariableTreeModel::handleGdbOutput(int rankId, const QString& output) {
    // Ignore GDB/MI records originating from ranks other than the currently inspected rank.
    if (rankId != m_currentRankId) return;

    // Dispatch GDB/MI result records to the appropriate variable-object parser.
    if (output.startsWith("^done,variables=[")) {
        parseListVariables(output);
    } else if (output.startsWith("^done,name=\"var_")) {
        parseVarCreate(output);
    } else if (output.startsWith("^done,numchild=") && output.contains("children=[")) {
        parseVarListChildren(output);
    } else {
        // Match tokenized address evaluation responses (`<token>^done,value="0x..."`) for aggregates.
        static const QRegularExpression evalRe("^(\\d+)\\^done,value=\"([^\"]+)\"");
        const QRegularExpressionMatch match = evalRe.match(output);
        if (match.hasMatch()) {
            const int token = match.captured(1).toInt();
            if (m_evalTokenToVarobj.contains(token)) {
                const QString varobjName = m_evalTokenToVarobj[token];
                if (m_varobjToNode.contains(varobjName)) {
                    VariableNode* node = m_varobjToNode[varobjName];
                    QString result = match.captured(2);
                    // Extract the leading hexadecimal address token prior to any symbol annotation.
                    if (result.contains(" ")) {
                        result = result.split(" ").first();
                    }
                    // Append the resolved base address (`{...} @ 0x7ff...`) to composite value summaries.
                    if (!node->value.contains("@") && node->value.startsWith("{")) {
                        node->value = QString("%1 @ %2").arg(node->value, result);
                        emit layoutChanged();
                    }
                }
                // Retire the one-shot evaluation token once resolved.
                m_evalTokenToVarobj.remove(token);
            }
        }
    }
}

/**
 * @brief Parses `-stack-list-variables` output and creates a GDB varobj for each local symbol.
 * @param output Raw GDB/MI `^done,variables=[...]` record.
 */
void VariableTreeModel::parseListVariables(const QString& output) {
    // Increment generation counter so varobj handles remain unique across step transitions.
    m_updateCounter++;
    static const QRegularExpression re("\\{name=\"([^\"]+)\"");
    QRegularExpressionMatchIterator i = re.globalMatch(output);
    while (i.hasNext()) {
        const QRegularExpressionMatch match = i.next();
        const QString name = match.captured(1);
        // Construct a deterministic varobj identifier `var_<gen>_<symbol>` for the floating frame (`*`).
        const QString varName = QString("var_%1_%2").arg(m_updateCounter).arg(name);
        if (auto* gdb = dynamic_cast<gridlock::GdbRankCoordinator*>(m_coordinator)) {
            gdb->writeCmd(m_currentRankId, QString("-var-create %1 * %2\n").arg(varName, name));
        }
    }
}

/**
 * @brief Parses `-var-create` result records and inserts top-level VariableNode rows.
 * @param output Raw GDB/MI `^done,name="var_...",numchild="...",value="...",type="..."` record.
 */
void VariableTreeModel::parseVarCreate(const QString& output) {
    // Compiled regular expressions for extracting GDB/MI varobj attributes.
    static const QRegularExpression nameRe("name=\"([^\"]+)\"");
    static const QRegularExpression numRe("numchild=\"(\\d+)\"");
    static const QRegularExpression typeRe("type=\"([^\"]+)\"");
    static const QRegularExpression valRe("value=\"([^\"]*)\"");

    if (!nameRe.match(output).hasMatch()) return;

    // Only process variable objects created by VariableTreeModel (`var_` prefix).
    const QString varobjName = nameRe.match(output).captured(1);
    if (!varobjName.startsWith("var_")) return;

    const int numChildren = numRe.match(output).hasMatch() ? numRe.match(output).captured(1).toInt() : 0;
    const QString type = typeRe.match(output).hasMatch() ? typeRe.match(output).captured(1) : "";
    const QString value = valRe.match(output).hasMatch() ? valRe.match(output).captured(1) : "";

    // Strip the internal `var_<counter>_` prefix to recover the user-visible C/C++ symbol name.
    QString name = varobjName;
    if (name.startsWith("var_")) {
        const int firstUnderscore = name.indexOf('_');
        const int secondUnderscore = name.indexOf('_', firstUnderscore + 1);
        if (secondUnderscore != -1) {
            name = name.mid(secondUnderscore + 1);
        }
    }

    // Append the new variable row under the invisible root node.
    const int newRow = static_cast<int>(m_rootNode->children.size());
    beginInsertRows(QModelIndex(), newRow, newRow);

    auto* node = new VariableNode(name, type, value, varobjName, numChildren, 0, m_rootNode.get());
    m_rootNode->children.push_back(std::unique_ptr<VariableNode>(node));
    m_varobjToNode[varobjName] = node;
    m_createdVarobjs.append(varobjName);

    // For structs, classes, pointers, and MPI handles, asynchronously query `&symbol` for memory inspection.
    if (value == "{...}" || type.contains("struct") || type.contains("class") ||
        type.contains("*") || type == "MPI_Status" || type.contains("MPI_")) {
        m_evalCounter++;
        m_evalTokenToVarobj[m_evalCounter] = varobjName;
        if (auto* gdb = dynamic_cast<gridlock::GdbRankCoordinator*>(m_coordinator)) {
            gdb->writeCmd(m_currentRankId,
                          QString("%1-data-evaluate-expression &%2\n").arg(m_evalCounter).arg(name));
        }
    }

    endInsertRows();
}

/**
 * @brief Parses `-var-list-children` output and populates child nodes under the expanded parent varobj.
 * @param output Raw GDB/MI `^done,numchild="...",children=[child={...},...]` record.
 */
void VariableTreeModel::parseVarListChildren(const QString& output) {
    static const QRegularExpression childBlockRe("child=\\{([^}]+)\\}");
    QRegularExpressionMatchIterator it = childBlockRe.globalMatch(output);
    if (!it.hasNext()) return;

    // Inspect the first child's varobj path (e.g. `var_1_pkt.public.id`) to resolve the parent varobj.
    const QString firstChildBlock = it.peekNext().captured(1);
    static const QRegularExpression nameRe("name=\"([^\"]+)\"");
    const QRegularExpressionMatch nameMatch = nameRe.match(firstChildBlock);
    if (!nameMatch.hasMatch()) return;

    const QString firstChildName = nameMatch.captured(1);
    const int lastDotIndex = firstChildName.lastIndexOf('.');
    if (lastDotIndex == -1) return;

    // Look up the parent VariableNode from its dotted prefix and guard against duplicate population.
    const QString parentVarobj = firstChildName.left(lastDotIndex);
    if (!m_varobjToNode.contains(parentVarobj)) return;

    VariableNode* parentNode = m_varobjToNode[parentVarobj];
    if (parentNode->childrenLoaded) return;

    // Reconstruct the parent QModelIndex so Qt tree views receive row insertion notifications.
    std::function<QModelIndex(VariableNode*)> getIndex = [&](VariableNode* n) -> QModelIndex {
        if (n == m_rootNode.get() || !n) return QModelIndex();
        if (n->parent == m_rootNode.get()) {
            for (size_t i = 0; i < m_rootNode->children.size(); ++i) {
                if (m_rootNode->children[i].get() == n) return createIndex(static_cast<int>(i), 0, n);
            }
        } else {
            const QModelIndex pIdx = getIndex(n->parent);
            if (!pIdx.isValid()) return QModelIndex();
            for (size_t i = 0; i < n->parent->children.size(); ++i) {
                if (n->parent->children[i].get() == n) return createIndex(static_cast<int>(i), 0, n);
            }
        }
        return QModelIndex();
    };

    const QModelIndex parentIdx = getIndex(parentNode);

    // Count total child blocks before calling beginInsertRows.
    int childCount = 0;
    QRegularExpressionMatchIterator counterIt = childBlockRe.globalMatch(output);
    while (counterIt.hasNext()) {
        childCount++;
        counterIt.next();
    }

    if (childCount > 0) {
        beginInsertRows(parentIdx, 0, childCount - 1);

        // Parse each `child={name="...",exp="...",numchild="...",value="...",type="..."}` tuple.
        while (it.hasNext()) {
            const QString block = it.next().captured(1);
            const QString varobjName = nameRe.match(block).captured(1);

            static const QRegularExpression expRe("exp=\"([^\"]+)\"");
            const QString exp = expRe.match(block).captured(1);

            static const QRegularExpression numRe("numchild=\"(\\d+)\"");
            const int numChildren = numRe.match(block).hasMatch() ? numRe.match(block).captured(1).toInt() : 0;

            static const QRegularExpression valRe("value=\"([^\"]*)\"");
            const QString value = valRe.match(block).hasMatch() ? valRe.match(block).captured(1) : "";

            static const QRegularExpression typeRe("type=\"([^\"]+)\"");
            const QString type = typeRe.match(block).hasMatch() ? typeRe.match(block).captured(1) : "";

            // Allocate and register the child VariableNode in the varobj lookup table.
            auto* childNode = new VariableNode(exp, type, value, varobjName, numChildren, 0, parentNode);
            parentNode->children.push_back(std::unique_ptr<VariableNode>(childNode));
            m_varobjToNode[varobjName] = childNode;
            m_createdVarobjs.append(varobjName);
        }

        parentNode->childrenLoaded = true;
        endInsertRows();
    } else {
        parentNode->childrenLoaded = true;
    }
}

/**
 * @brief Resolves a QModelIndex to its internal VariableNode pointer.
 * @param index Model index to unwrap.
 * @return Pointer to the bound VariableNode, or m_rootNode for invalid indices.
 */
VariableNode* VariableTreeModel::getNode(const QModelIndex& index) const {
    // Extract the bound VariableNode* or fall back to the invisible root node for invalid indices.
    if (index.isValid()) {
        if (auto* node = static_cast<VariableNode*>(index.internalPointer())) {
            return node;
        }
    }
    return m_rootNode.get();
}

/**
 * @brief Recursively snapshots current variable values into m_previousValues for change highlighting.
 * @param node Root of the subtree to snapshot.
 */
void VariableTreeModel::storePreviousValues(VariableNode* node) {
    if (!node) return;
    // Cache concrete scalar values so subsequent steps can highlight modified variables.
    if (!node->name.isEmpty() && !node->value.isEmpty() && node->value != "{...}") {
        m_previousValues[node->name] = node->value;
    }
    for (const auto& child : node->children) {
        storePreviousValues(child.get());
    }
}

} // namespace gridlock
