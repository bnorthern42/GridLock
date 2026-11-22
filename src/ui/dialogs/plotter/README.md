# Array Plotter & Tensor Table (`src/ui/dialogs/plotter/`)

Live auto-refreshing 1D/2D scientific array, complex field, multi-rank overlay, scalar history plotter, and zero-copy 2D/3D C/Fortran tensor slice model.

## Contents

- `ArrayPlotDialog.hpp` (`405` lines), `ArrayPlotDialog.cpp`:
  - Top-level visualization dialog coordinating live auto-refresh on stopped ranks and remote reads via `IBackendCoordinator` / `NativeMemoryReader`.
  - Manages rank subset overlays, statistics HUD metrics, slice controls, and 1D/2D/Tensor tab switching.
- `ArrayPlotCanvas.hpp` (`163` lines), `ArrayPlotCanvas.cpp` (`791` lines):
  - Custom anti-aliased `QPainter` rendering canvas supporting 1D Line, 1D Bar/Stem, 2D Heatmap, and Scalar Time-Series History modes.
  - Implements safe `Log10` Y/color scaling, interactive drag-box zoom with double-click reset, multi-rank legends, and NumPy-style `start:end:stride` slice parsing (`ArraySliceSpec`).
- `ArrayPlotExport.cpp`:
  - Implements typed contiguous/strided memory decoders (`Float64`, `Float32`, `Int32`, `Int64`, `UInt8`, `Complex64`, `Complex128`).
  - Computes scalar statistics (`min`, `max`, `mean`, `L2`, `NaN`/`Inf`, mutation diffing) and exports CSV, PNG, and Gnuplot outputs.
- `TensorSliceTableModel.hpp` (`113` lines), `TensorSliceTableModel.cpp`:
  - Zero-copy `QAbstractTableModel` indexing `[Nx, Ny, Nz]` row-major (C) and column-major (Fortran) slices on demand in `data()`.
  - Highlights IEEE-754 `NaN`/`Inf` cells in magenta and formats complex `(Re, Im)` tooltips.

## Expected Behavior & Invariants

- **Overflow-Safe Slicing**:
  - Stride, count, and element byte sizes validate against `MemoryBoundsValidator` before allocating sample buffers or calling `process_vm_readv`.
- **Log10 Paint Safety**:
  - `Log10` scaling safely clamps or masks non-positive samples (`<= 0`) so `QPainter` never receives `NaN` or `Inf` screen coordinates.
- **60 FPS Virtualized Tensor Grid**:
  - `TensorSliceTableModel` computes row/column offsets dynamically without allocating per-cell widgets or copying 100,000+ element strings upfront.
- **Verification**:
  - Run `meson test -C builddir stride_security_tests ui_tests --print-errorlogs`.

## Maintainer & Agent Notes

- File Partitioning & Size Ceiling:
  - `ArrayPlotCanvas.cpp` sits at `791` lines (`< 950` ceiling); keep typed binary decoders, stats reductions, and file exporters strictly inside `ArrayPlotExport.cpp`.
- Rendering & Memory Safety:
  - Follow strict `QPainter` hygiene inside `ArrayPlotCanvas`: reset pens and brushes explicitly at the top of each paint loop and guard zero-area viewports.
  - Keep `TensorSliceTableModel` strictly zero-copy; never pre-allocate per-cell formatting strings for large `[Nx, Ny, Nz]` buffers.
- Testing & Documentation Sync:
  - Verify any new slice expression syntax or IEEE-754 anomaly edge cases (`NaN`, `Inf`, negative stride, empty slice) in `tests/ui/test_ui_viz_and_ux.cpp`.
  - Update this `README.md` and `meson.build` whenever splitting rendering helpers or adding new tensor/plot formats.
