# Core Models (`src/core/models/`)

Protocol-neutral variable AST structures and Qt `QAbstractItemModel` backing docked variable inspectors and cross-rank evaluation views.

## Contents

- `VariableNode.hpp` (`80` lines):
  - Defines `VariableNode` AST structure representing local variables, struct members, and array elements.
  - Stores symbol `name`, C/C++ `type`, evaluated `value`, GDB/MI `varobjName`, DAP `variablesReference`, child loading state, and optional memory bounds.
  - Owns nested child nodes cleanly via `std::unique_ptr<VariableNode>` vectors without raw owning pointers.
- `VariableTreeModel.hpp` (`165` lines), `VariableTreeModel.cpp` (`813` lines):
  - Implements `VariableTreeModel` deriving from `QAbstractItemModel` (`index`, `parent`, `rowCount`, `columnCount`, `data`, `setData`, `flags`).
  - Bridges root frame locals and asynchronous child expansions from `IBackendCoordinator` into `VariablesDockWidget`.
  - Tracks previous step scalar values to highlight modified expressions cleanly after stepping.
  - Supports inline scalar value editing forwarded back through the active backend coordinator.

## Expected Behavior & Invariants

- **Lazy Tree Population**:
  - Composite structs, arrays, and pointers populate child rows lazily on expand (`canFetchMore` / `fetchMore` -> `requestVariableChildren`) instead of eagerly dereferencing unbounded pointer graphs.
- **Qt Model Notification Discipline**:
  - Every structural insert, update, or clear operation brackets changes with `beginInsertRows`/`endInsertRows` or `beginResetModel`/`endResetModel` so `QSortFilterProxyModel` indices stay consistent.
- **Backend Neutrality**:
  - `VariableTreeModel` operates uniformly across GDB/MI `varobjName` handles and DAP `variablesReference` IDs via `IBackendCoordinator`.
- **Rank-Switch Handle Isolation**:
  - Switching active MPI ranks invalidates prior `varobjName` / `variablesReference` caches before querying the newly focused rank (`tests/core/test_advanced_features.cpp`, `tests/dap/test_dap_variables.cpp`).
- **Mutation Delta Highlighting**:
  - Scalar value changes across debugger steps mark nodes dirty cleanly without losing expanded parent tree paths or scroll positions.
- **Verification**:
  - Run `meson test -C builddir dap_variables_tests advanced_features_tests ui_tests --print-errorlogs` after editing tree structures or model roles.

## Maintainer & Agent Notes

- Thread & Model Safety:
  - Never mutate `VariableNode` child vectors from background callbacks without bracketing model changes (`beginInsertRows`/`endInsertRows` or `beginResetModel`/`endResetModel`) on the owner thread.
  - Guard all `QModelIndex` pointer casts with `internalPointer()` null checks and valid parent boundaries.
- Linkage & Modularity:
  - Keep `VariableNode.hpp` completely free of `QtWidgets` headers so headless targets (`dap_variables_tests`, `advanced_features_tests`) build cleanly.
  - `VariableTreeModel.cpp` sits at `813` lines (`< 950` ceiling); extract formatting or cross-rank comparison helpers into a separate `.cpp` file before expanding tree columns.
- Documentation Sync:
  - Update this `README.md` and `meson.build` whenever introducing new core model classes or splitting `VariableTreeModel.cpp`.
