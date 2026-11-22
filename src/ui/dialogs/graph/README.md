# Pointer Graph Visualizer (`src/ui/dialogs/graph/`)

Interactive 2D pointer, struct, and linked-data topology visualizer backed by Sugiyama-style layered DAG layout and live backend expression evaluation.

## Contents

- `PointerGraphDialog.hpp` (`260` lines), `PointerGraphDialog.cpp` (`747` lines):
  - Modeless dialog providing root expression controls, DAG relayout, 1-hop pointer expansion, field display toggles, zoom/fit actions, SVG/PNG export, `PointerGraphView` minimap/zoom, and `PointerGraphEdgeItem` Bezier arrows.
  - Exposes per-node width expansion (`Shift+Click`), row/header tooltip queries, and placeholder status inspection.
- `PointerGraphLayout.hpp` (`256` lines), `PointerGraphLayout.cpp` (`921` lines):
  - Defines `PointerLayoutNode`, `PointerLayoutEdge`, `PointerLayoutConfig`, `PointerLayoutResult`, `AliasCycleAnalysis`, and `PointerGraphNodeItem`.
  - Implements deterministic Sugiyama layered DAG rank assignment, crossing minimization, coordinate placement, per-row hover tooltips, and `Shift+Click` expanded-width card sizing (capped at `900 px`).
- `PointerGraphEvaluator.cpp` (`575` lines):
  - Connects to `GdbRankCoordinator` and `DapCoordinator` to evaluate root expressions and expand nested struct/pointer fields asynchronously (`*(expr)` for non-null `T *` pointer nodes).
  - Transitions unexpanded cards through `(loading fields...)` to populated member rows, `(null pointer)`, or `(no fields / opaque type)`, and reconciles alias/cycle topologies.

## Expected Behavior & Invariants

- **Un-Elided Inspection (`Hover` + `Shift+Click`)**:
  - Hovering over any card header or field row displays a tooltip with the full un-elided `name : value (type)` text; `Shift+Click` toggles the card between compact (`290 px`) and expanded width (`<= 900 px`) and re-layouts neighbors without overlap.
- **Double-Click Pointer Expansion & Bounded Dereferencing**:
  - Double-clicking an unexpanded card enters `(loading fields...)`, evaluates `*(expr)` for non-null pointer types, populates member rows or reports `(null pointer)` / `(no fields / opaque type)`, and enforces visited-address cycle bounds.
- **Deterministic DAG Coordinates**:
  - Identical `PointerLayoutNode` and `PointerLayoutEdge` inputs yield repeatable layer assignments, self-loop arcs, and non-overlapping struct boxes.
- **Verification**:
  - Run `meson test -C builddir ui_tests --print-errorlogs` after modifying layout geometry, card interactions, or evaluator parsing.

## Maintainer & Agent Notes

- Separation of Layout & IPC:
  - Keep pure Sugiyama DAG algorithms in `PointerGraphLayout` free of GDB/MI or DAP coupling so layout geometry tests run directly on synthetic graphs (`tests/ui/test_ui_viz_and_ux.cpp`).
  - Place asynchronous dereference queues and `*(expr)` query mapping inside `PointerGraphEvaluator.cpp` while keeping window chrome, `PointerGraphView`, and `PointerGraphEdgeItem` in `PointerGraphDialog.cpp`.
- GraphicsScene & Event Forwarding Safety:
  - `PointerGraphView::mouseDoubleClickEvent` forwards card double-clicks directly to `PointerGraphNodeItem::handleDoubleClickAt` so `ScrollHandDrag` and `QGraphicsScene` mouse-grabber tracking never swallow double-clicks.
  - Reset painter states inside custom item `paint()` methods to avoid pen/brush leakage across zoomed minimap frames.
- Line Ceiling Watch:
  - `PointerGraphLayout.cpp` sits at `921` lines (`< 950` limit); extract custom node painting into a separate unit before adding major new scene items, and update this `README.md`.
