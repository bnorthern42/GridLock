# Domain Mesh Probe Sample Plugin

`domain_mesh_probe` is a standalone out-of-tree C++23 GridLock plugin built solely against `include/gridlock/plugin_api.h` and `include/gridlock/plugin_cpp_helpers.hpp` with zero Qt6 or internal GridLock dependencies.

## Capabilities

* Exports only the versioned `gridlock_plugin_init_v1` entry point while hiding internal symbols via `-fvisibility=hidden`.
* Registers the **Dump Mesh Refinement Step** variable context-menu action for any variable whose debugger type matches `.*Mesh.*`.
* Evaluates `(expr).num_vertices`, `(expr).num_elements`, and `(expr).vertices` on the stopped MPI rank via `gridlock::plugin::evalExpr`, then reads raw `double` `(x, y, z)` coordinates via `gridlock::plugin::readBytes`.
* Writes an ASCII Legacy VTK snapshot (`mesh_step_<rank>_<step>.vtk`) into `GRIDLOCK_MESH_PROBE_DIR` (defaults to `/tmp`) and publishes a JSON notification on channel `domain.mesh_probe.step` via `host->emit_structured_event` so external viewers (ParaView, GLVis, or custom reload scripts) can follow each refinement step.
* Automatically captures subsequent stops on `on_rank_stopped` once a mesh variable has been selected (or when `GRIDLOCK_MESH_PROBE_EXPR` is set in the environment).

## Expected Target Mesh Struct Layout

```cpp
struct FemMesh {
  std::size_t num_vertices;
  std::size_t num_elements;
  const double* vertices;  // Interleaved (x, y, z) coordinates: 3 * num_vertices doubles.
};
```

## Build Out-of-Tree

### Option A: Meson

```bash
cd examples/plugins/domain_mesh_probe
meson setup build
meson compile -C build
```

### Option B: Make

```bash
cd examples/plugins/domain_mesh_probe
make
```

## Install into GridLock

Copy the compiled shared library into the user plugin directory (or `<workspace>/.gridlock/plugins/`) and click **Reload Plugins** under **Preferences -> Plugins**:

```bash
mkdir -p ~/.config/gridlock/plugins
cp build/libdomain_mesh_probe.so ~/.config/gridlock/plugins/
```
