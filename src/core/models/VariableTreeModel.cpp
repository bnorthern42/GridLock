#include "VariableTreeModel.hpp"
#include "../hpc/IBackendCoordinator.hpp"
#include "../hpc/GdbRankCoordinator.hpp"
#include "../hpc/DapCoordinator.hpp"
#include <QRegularExpression>
#include <QJsonArray>
#include <QJsonObject>
#include <QFont>
#include <QColor>
#include <functional>

namespace gridlock {

VariableTreeModel::VariableTreeModel(IBackendCoordinator* coordinator, QObject* parent)
    : QAbstractItemModel(parent), m_coordinator(coordinator), m_currentRankId(-1) {
    m_rootNode = std::make_unique<VariableNode>();
    if (m_coordinator) {
        if (auto gdb = dynamic_cast<gridlock::GdbRankCoordinator*>(m_coordinator)) {
            connect(gdb, &gridlock::GdbRankCoordinator::gdbOutputReceived, this, &VariableTreeModel::handleGdbOutput);
        }
        if (auto dap = dynamic_cast<DapCoordinator*>(m_coordinator)) {
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

VariableTreeModel::~VariableTreeModel() {
    clear();
}

QModelIndex VariableTreeModel::index(int row, int column, const QModelIndex& parent) const {
    if (!hasIndex(row, column, parent)) return QModelIndex();
    VariableNode* parentNode = getNode(parent);
    if (row >= 0 && row < static_cast<int>(parentNode->children.size())) {
        return createIndex(row, column, parentNode->children[row].get());
    }
    return QModelIndex();
}

QModelIndex VariableTreeModel::parent(const QModelIndex& child) const {
    if (!child.isValid()) return QModelIndex();
    VariableNode* childNode = getNode(child);
    VariableNode* parentNode = childNode->parent;
    if (parentNode == m_rootNode.get() || !parentNode) return QModelIndex();

    VariableNode* grandParent = parentNode->parent;
    int row = 0;
    for (size_t i = 0; i < grandParent->children.size(); ++i) {
        if (grandParent->children[i].get() == parentNode) {
            row = i;
            break;
        }
    }
    return createIndex(row, 0, parentNode);
}

int VariableTreeModel::rowCount(const QModelIndex& parent) const {
    VariableNode* node = getNode(parent);
    return node->children.size();
}

bool VariableTreeModel::hasChildren(const QModelIndex& parent) const {
    VariableNode* node = getNode(parent);
    if (!node) return false;
    if (node->numChildren > 0) return true;
    return node->children.size() > 0;
}

int VariableTreeModel::columnCount(const QModelIndex& /*parent*/) const {
    return 3;
}

QVariant VariableTreeModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid()) return QVariant();
    VariableNode* node = getNode(index);

    if (role == Qt::DisplayRole) {
        if (index.column() == 0) return node->name;
        if (index.column() == 1) return node->value;
        if (index.column() == 2) return node->type;
    } else if (role == Qt::FontRole) {
        if (index.column() == 1 || index.column() == 2) {
            QFont font("JetBrains Mono");
            font.setStyleHint(QFont::Monospace);
            return font;
        }
    } else if (role == Qt::TextAlignmentRole) {
        if (index.column() == 2) {
            return QVariant(Qt::AlignRight | Qt::AlignVCenter);
        }
    } else if (role == Qt::BackgroundRole) {
        if (m_previousValues.contains(node->name)) {
            if (m_previousValues[node->name] != node->value) {
                return QColor("#f38ba8"); // Catppuccin Mocha Red for changed value
            }
        }
    } else if (role == Qt::ForegroundRole) {
        if (m_previousValues.contains(node->name)) {
            if (m_previousValues[node->name] != node->value) {
                return QColor("#1e1e2e"); // Dark text for contrast against red background
            }
        }
    }
    return QVariant();
}

QVariant VariableTreeModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        if (section == 0) return "Name";
        if (section == 1) return "Value";
        if (section == 2) return "Type";
    }
    return QVariant();
}

bool VariableTreeModel::canFetchMore(const QModelIndex& parent) const {
    VariableNode* node = getNode(parent);
    return node && (node->numChildren > 0 || node->variablesReference > 0) && !node->childrenLoaded;
}

void VariableTreeModel::fetchMore(const QModelIndex& parent) {
    if (!parent.isValid()) return;
    VariableNode* node = getNode(parent);
    if (node && (node->numChildren > 0 || node->variablesReference > 0) && !node->childrenLoaded) {
        node->childrenLoaded = true; // Prevent recursive or looped fetching
        if (auto gdb = dynamic_cast<gridlock::GdbRankCoordinator*>(m_coordinator)) {
            gdb->writeCmd(m_currentRankId, QString("-var-list-children --all-values %1\n").arg(node->varobjName));
        } else if (auto dap = dynamic_cast<DapCoordinator*>(m_coordinator)) {
            if (node->variablesReference > 0) {
                dap->requestVariables(m_currentRankId, node->variablesReference);
            }
        }
    }
}

void VariableTreeModel::loadLocals(int rankId) {
    storePreviousValues(m_rootNode.get());
    clear();
    m_currentRankId = rankId;
    if (auto gdb = dynamic_cast<gridlock::GdbRankCoordinator*>(m_coordinator)) {
        gdb->sendCommand(rankId, "-stack-list-variables --all-values");
    } else if (auto dap = dynamic_cast<DapCoordinator*>(m_coordinator)) {
        int activeFrame = dap->getActiveFrameId(rankId);
        if (activeFrame >= 0) {
            dap->requestScopes(activeFrame, rankId);
        } else {
            dap->requestStackTrace(rankId);
        }
    }
}

void VariableTreeModel::onLocalsUpdated(int rankId, int parentVarRef, const QJsonArray& variables) {
    if (m_currentRankId != -1 && rankId != m_currentRankId) return;
    if (m_currentRankId == -1) m_currentRankId = rankId;

    std::function<VariableNode*(VariableNode*)> findNodeByRef = [&](VariableNode* n) -> VariableNode* {
        if (!n) return nullptr;
        if (n->variablesReference == parentVarRef) return n;
        for (const auto& child : n->children) {
            if (auto found = findNodeByRef(child.get())) return found;
        }
        return nullptr;
    };

    VariableNode* targetParent = nullptr;
    if (parentVarRef > 0) {
        targetParent = findNodeByRef(m_rootNode.get());
    }
    
    std::function<QModelIndex(VariableNode*)> getIndex = [&](VariableNode* n) -> QModelIndex {
        if (n == m_rootNode.get() || !n) return QModelIndex();
        if (n->parent == m_rootNode.get()) {
            for (size_t i = 0; i < m_rootNode->children.size(); ++i) {
                if (m_rootNode->children[i].get() == n) return createIndex(i, 0, n);
            }
        } else {
            QModelIndex pIdx = getIndex(n->parent);
            if (!pIdx.isValid()) return QModelIndex();
            for (size_t i = 0; i < n->parent->children.size(); ++i) {
                if (n->parent->children[i].get() == n) return createIndex(i, 0, n);
            }
        }
        return QModelIndex();
    };

    QModelIndex parentIdx;
    if (!targetParent) {
        beginResetModel();
        m_rootNode->children.clear();
        m_rootNode->numChildren = 0;
        targetParent = m_rootNode.get();
    } else {
        if (targetParent->childrenLoaded) return;
        parentIdx = getIndex(targetParent);
        if (variables.size() > 0) {
            beginInsertRows(parentIdx, 0, variables.size() - 1);
        }
    }

    for (const QJsonValue& val : variables) {
        QJsonObject var = val.toObject();
        QString name = var["name"].toString();
        QString value = var["value"].toString();
        QString type = var["type"].toString();
        int varRef = var["variablesReference"].toInt(0);
        
        auto node = std::make_unique<VariableNode>(name, type, value, "", 0, varRef, targetParent);
        if (varRef > 0) {
            node->numChildren = 1; // Indicate it has children to allow expansion
        }
        targetParent->children.push_back(std::move(node));
    }
    
    if (targetParent == m_rootNode.get()) {
        m_rootNode->numChildren = m_rootNode->children.size();
        endResetModel();
    } else {
        targetParent->numChildren = targetParent->children.size();
        targetParent->childrenLoaded = true;
        if (variables.size() > 0) {
            endInsertRows();
        }
    }
}

void VariableTreeModel::onScopesUpdated(int rankId, const QJsonArray& scopes) {
    if (m_currentRankId != -1 && rankId != m_currentRankId) return;

    for (const QJsonValue& val : scopes) {
        QJsonObject scope = val.toObject();
        if (scope["name"].toString() == "Locals") {
            int varRef = scope["variablesReference"].toInt();
            if (auto dap = dynamic_cast<DapCoordinator*>(m_coordinator)) {
                dap->requestVariables(rankId, varRef);
            }
            break; // ONLY request the top-level Locals scope
        }
    }
}

void VariableTreeModel::clear() {
    beginResetModel();
    if (m_coordinator && m_currentRankId != -1) {
        for (const QString& varobj : m_createdVarobjs) {
            if (auto gdb = dynamic_cast<gridlock::GdbRankCoordinator*>(m_coordinator)) {
                gdb->writeCmd(m_currentRankId, QString("-var-delete %1\n").arg(varobj));
            }
        }
    }
    m_createdVarobjs.clear();
    m_varobjToNode.clear();
    m_rootNode = std::make_unique<VariableNode>();
    endResetModel();
}

void VariableTreeModel::handleGdbOutput(int rankId, const QString& output) {
    if (rankId != m_currentRankId) return;

    if (output.startsWith("^done,variables=[")) {
        parseListVariables(output);
    } else if (output.startsWith("^done,name=\"var_")) {
        parseVarCreate(output);
    } else if (output.startsWith("^done,numchild=") && output.contains("children=[")) {
        parseVarListChildren(output);
    } else {
        QRegularExpression evalRe("^(\\d+)\\^done,value=\"([^\"]+)\"");
        QRegularExpressionMatch match = evalRe.match(output);
        if (match.hasMatch()) {
            int token = match.captured(1).toInt();
            if (m_evalTokenToVarobj.contains(token)) {
                QString varobjName = m_evalTokenToVarobj[token];
                if (m_varobjToNode.contains(varobjName)) {
                    VariableNode* node = m_varobjToNode[varobjName];
                    QString result = match.captured(2);
                    if (result.contains(" ")) {
                        result = result.split(" ").first();
                    }
                    if (!node->value.contains("@") && node->value.startsWith("{")) {
                        node->value = QString("%1 @ %2").arg(node->value).arg(result);
                        emit layoutChanged();
                    }
                }
                m_evalTokenToVarobj.remove(token);
            }
        }
    }
}

void VariableTreeModel::parseListVariables(const QString& output) {
    m_updateCounter++;
    QRegularExpression re("\\{name=\"([^\"]+)\"");
    QRegularExpressionMatchIterator i = re.globalMatch(output);
    while (i.hasNext()) {
        QRegularExpressionMatch match = i.next();
        QString name = match.captured(1);
        QString varName = QString("var_%1_%2").arg(m_updateCounter).arg(name);
        if (auto gdb = dynamic_cast<gridlock::GdbRankCoordinator*>(m_coordinator)) {
            gdb->writeCmd(m_currentRankId, QString("-var-create %1 * %2\n").arg(varName).arg(name));
        }
    }
}

void VariableTreeModel::parseVarCreate(const QString& output) {
    QRegularExpression nameRe("name=\"([^\"]+)\"");
    QRegularExpression numRe("numchild=\"(\\d+)\"");
    QRegularExpression typeRe("type=\"([^\"]+)\"");
    QRegularExpression valRe("value=\"([^\"]*)\"");

    if (!nameRe.match(output).hasMatch()) return;

    QString varobjName = nameRe.match(output).captured(1);
    if (!varobjName.startsWith("var_")) return;

    int numChildren = numRe.match(output).hasMatch() ? numRe.match(output).captured(1).toInt() : 0;
    QString type = typeRe.match(output).hasMatch() ? typeRe.match(output).captured(1) : "";
    QString value = valRe.match(output).hasMatch() ? valRe.match(output).captured(1) : "";

    QString name = varobjName;
    if (name.startsWith("var_")) {
        int firstUnderscore = name.indexOf('_');
        int secondUnderscore = name.indexOf('_', firstUnderscore + 1);
        if (secondUnderscore != -1) {
            name = name.mid(secondUnderscore + 1);
        }
    }

    int newRow = m_rootNode->children.size();
    beginInsertRows(QModelIndex(), newRow, newRow);
    
    VariableNode* node = new VariableNode(name, type, value, varobjName, numChildren, 0, m_rootNode.get());
    m_rootNode->children.push_back(std::unique_ptr<VariableNode>(node));
    m_varobjToNode[varobjName] = node;
    m_createdVarobjs.append(varobjName);
    
    if (value == "{...}" || type.contains("struct") || type.contains("class") || type.contains("*") || type == "MPI_Status" || type.contains("MPI_")) {
        m_evalCounter++;
        m_evalTokenToVarobj[m_evalCounter] = varobjName;
        if (auto gdb = dynamic_cast<gridlock::GdbRankCoordinator*>(m_coordinator)) {
            gdb->writeCmd(m_currentRankId, QString("%1-data-evaluate-expression &%2\n").arg(m_evalCounter).arg(name));
        }
    }
    
    endInsertRows();
}

void VariableTreeModel::parseVarListChildren(const QString& output) {
    QRegularExpression childBlockRe("child=\\{([^}]+)\\}");
    QRegularExpressionMatchIterator it = childBlockRe.globalMatch(output);

    if (!it.hasNext()) return;

    QString firstChildBlock = it.peekNext().captured(1);
    QRegularExpression nameRe("name=\"([^\"]+)\"");
    QRegularExpressionMatch nameMatch = nameRe.match(firstChildBlock);
    if (!nameMatch.hasMatch()) return;

    QString firstChildName = nameMatch.captured(1);
    int lastDotIndex = firstChildName.lastIndexOf('.');
    if (lastDotIndex == -1) return;

    QString parentVarobj = firstChildName.left(lastDotIndex);
    if (!m_varobjToNode.contains(parentVarobj)) return;

    VariableNode* parentNode = m_varobjToNode[parentVarobj];
    if (parentNode->childrenLoaded) return;

    std::function<QModelIndex(VariableNode*)> getIndex = [&](VariableNode* n) -> QModelIndex {
        if (n == m_rootNode.get() || !n) return QModelIndex();
        if (n->parent == m_rootNode.get()) {
            for (size_t i = 0; i < m_rootNode->children.size(); ++i) {
                if (m_rootNode->children[i].get() == n) return createIndex(i, 0, n);
            }
        } else {
            QModelIndex pIdx = getIndex(n->parent);
            if (!pIdx.isValid()) return QModelIndex();
            for (size_t i = 0; i < n->parent->children.size(); ++i) {
                if (n->parent->children[i].get() == n) return createIndex(i, 0, n);
            }
        }
        return QModelIndex();
    };

    QModelIndex parentIdx = getIndex(parentNode);

    int childCount = 0;
    QRegularExpressionMatchIterator counterIt = childBlockRe.globalMatch(output);
    while (counterIt.hasNext()) { childCount++; counterIt.next(); }

    if (childCount > 0) {
        beginInsertRows(parentIdx, 0, childCount - 1);

        while (it.hasNext()) {
            QString block = it.next().captured(1);

            QString varobjName = nameRe.match(block).captured(1);
            
            QRegularExpression expRe("exp=\"([^\"]+)\"");
            QString exp = expRe.match(block).captured(1);

            QRegularExpression numRe("numchild=\"(\\d+)\"");
            int numChildren = numRe.match(block).hasMatch() ? numRe.match(block).captured(1).toInt() : 0;

            QRegularExpression valRe("value=\"([^\"]*)\"");
            QString value = valRe.match(block).hasMatch() ? valRe.match(block).captured(1) : "";

            QRegularExpression typeRe("type=\"([^\"]+)\"");
            QString type = typeRe.match(block).hasMatch() ? typeRe.match(block).captured(1) : "";

            VariableNode* childNode = new VariableNode(exp, type, value, varobjName, numChildren, 0, parentNode);
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

VariableNode* VariableTreeModel::getNode(const QModelIndex& index) const {
    if (index.isValid()) {
        VariableNode* node = static_cast<VariableNode*>(index.internalPointer());
        if (node) return node;
    }
    return m_rootNode.get();
}

void VariableTreeModel::storePreviousValues(VariableNode* node) {
    if (!node) return;
    if (!node->name.isEmpty() && !node->value.isEmpty() && node->value != "{...}") {
        m_previousValues[node->name] = node->value;
    }
    for (const auto& child : node->children) {
        storePreviousValues(child.get());
    }
}

} // namespace gridlock
