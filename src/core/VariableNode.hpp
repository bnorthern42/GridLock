#pragma once
#include <QString>
#include <vector>
#include <memory>

namespace gridlock {

struct VariableNode {
    QString name;
    QString type;
    QString value;
    QString varobjName;
    int numChildren = 0;
    bool childrenLoaded = false;
    std::vector<std::unique_ptr<VariableNode>> children;
    VariableNode* parent = nullptr;

    VariableNode(const QString& n = "", const QString& t = "", const QString& v = "",
                 const QString& vName = "", int nChildren = 0, VariableNode* p = nullptr)
        : name(n), type(t), value(v), varobjName(vName), numChildren(nChildren), parent(p) {}
};

} // namespace gridlock
