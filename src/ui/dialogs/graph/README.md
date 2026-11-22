# Pointer Graph Visualizer (`src/ui/dialogs/graph/`)

Interactive 2D pointer, struct, and linked-data topology visualizer backed by Sugiyama-style layered DAG layout and live backend expression evaluation.

## Contents

- `PointerGraphDialog.hpp` (`245` lines), `PointerGraphDialog.cpp`:
  - Modeless dialog providing root expression controls, max traversal depth spinboxes, field display toggles, zoom/fit actions, and SVG/PNG export.
  - Manages toolbar state and connects evaluation signals to the graph scene.
- `PointerGraphLayout.hpp` (`223` lines), `PointerGraphLayout.cpp` (`910` lines):
  - Defines `PointerLayoutNode`, `PointerLayoutEdge`, `PointerLayoutConfig`, `PointerLayoutResult`, and `AliasCycleAnalysis`.
  - Implements deterministic Sugiyama layered DAG rank assignment, crossing minimization, and coordinate placement.
  - Provides custom `QGraphicsItem` struct boxes/edges (`PointerGraphNodeItem`, `PointerGraphEdgeItem`) and zoom/minimap navigation (`PointerGraphView`).
- `PointerGraphEvaluator.cpp`:
  - Connects to `GdbRankCoordinator` and `DapCoordinator` to evaluate root expressions and traverse nested struct/pointer fields asynchronously.
  - Parses member layouts, computes pointer aliasing, and reconciles cycle topologies.

## Expected Behavior & Invariants

- **Bounded Recursive Dereferencing**:
  - Pointer traversal tracks visited virtual addresses (`AliasCycleAnalysis`) and enforces node/depth limits so circular lists or corrupted pointers terminate deterministically.
- **Deterministic DAG Coordinates**:
  - Identical `PointerLayoutNode` and `PointerLayoutEdge` inputs yield repeatable layer assignments, self-loop arcs, and non-overlapping struct boxes.
- **Critical Line Ceiling Watch**:
  - `PointerGraphLayout.cpp` sits at `910` lines (`< 950` limit); split custom `QGraphicsItem` painting or SVG export helpers before expanding layout stages.
- **Verification**:
  - Run `meson test -C builddir ui_tests --print-errorlogs` after modifying layout geometry or evaluator parsing.

## Maintainer & Agent Notes

- Separation of Layout & IPC:
  - Keep pure Sugiyama DAG algorithms in `PointerGraphLayout` free of GDB/MI or DAP coupling so layout geometry tests run directly on synthetic graphs (`tests/ui/test_ui_viz_and_ux.cpp`).
  - Place asynchronous dereference queues and field parsers inside `PointerGraphEvaluator.cpp` while keeping window chrome in `PointerGraphDialog.cpp`.
- GraphicsScene Safety:
  - Guard `QGraphicsScene` item pointers carefully whenever clearing or replacing scenes between rank execution steps.
  - Reset painter states inside custom item `paint()` methods to avoid pen/brush leakage across zoomed minimap frames.
- Line Ceiling & Build Sync:
  - `PointerGraphLayout.cpp` sits at `910` lines (`< 950` limit); split SVG export or custom item classes if expanding visualization features.
  - Guard all node/edge allocations using Qt parent-child ownership or value containers (`std::vector` / `QVector`).
  - Update this `README.md` and `meson.build` whenever extracting scene helpers out of `PointerGraphLayout.cpp`.
