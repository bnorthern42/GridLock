/**
 * @file VariableNode.hpp
 * @brief Hierarchical AST node representing a local or composite variable in VariableTreeModel.
 *
 * Stores variable symbol metadata (name, type, evaluated value), GDB/MI variable
 * object handles (varobjName), LLDB-DAP structured child references (variablesReference),
 * and optional remote virtual memory bounds for array/pointer inspection.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <QString>

namespace gridlock {

/**
 * @brief Tree node backing a single row in the Variables dock QTreeView.
 */
struct VariableNode {
    /** @brief Display identifier of the variable or struct/array field. */
    QString name;

    /** @brief C/C++ type signature reported by GDB/MI or LLDB-DAP. */
    QString type;

    /** @brief Formatted scalar or aggregate summary value string. */
    QString value;

    /** @brief Internal GDB/MI variable object identifier (e.g. "var_1_data"). */
    QString varobjName;

    /** @brief Number of child fields reported by the backend debugger. */
    int numChildren = 0;

    /** @brief True once child nodes have been lazily fetched and populated. */
    bool childrenLoaded = false;

    /** @brief Positive LLDB-DAP variablesReference handle for structured expansion. */
    int variablesReference = 0;

    /** @brief Owned child VariableNode instances for structs, classes, and arrays. */
    std::vector<std::unique_ptr<VariableNode>> children;

    /** @brief Non-owning pointer to the enclosing parent node (nullptr for root). */
    VariableNode* parent = nullptr;

    /** @brief Resolved target virtual base address for pointer/array bounds validation. */
    uintptr_t memoryBaseAddress = 0;

    /** @brief Known allocation byte capacity associated with memoryBaseAddress. */
    size_t memoryAllocatedBytes = 0;

    /**
     * @brief Constructs a VariableNode with optional metadata and parent linkage.
     * @param n Variable display name.
     * @param t C/C++ data type string.
     * @param v Formatted value string.
     * @param vName GDB/MI varobj handle name.
     * @param nChildren Expected number of child sub-elements.
     * @param varRef LLDB-DAP variablesReference handle.
     * @param p Enclosing parent VariableNode pointer.
     */
    explicit VariableNode(const QString& n = "", const QString& t = "", const QString& v = "",
                          const QString& vName = "", int nChildren = 0, int varRef = 0,
                          VariableNode* p = nullptr)
        : name(n),
          type(t),
          value(v),
          varobjName(vName),
          numChildren(nChildren),
          variablesReference(varRef),
          parent(p) {}
};

} // namespace gridlock
