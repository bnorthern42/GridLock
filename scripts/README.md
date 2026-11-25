# GridLock Developer, Packaging & Scaffolding Scripts (`scripts/`)

This directory contains developer automation, plugin scaffolding, packaging, UI screenshot capture, code coverage, and GitHub Wiki synchronization scripts for GridLock.

---

## Script Inventory

| Script | Runtime | Purpose |
| :--- | :---: | :--- |
| [`init_plugin.lua`](init_plugin.lua) | Lua 5.1 / 5.3 / 5.4 / LuaJIT | Interactive and CLI-flag generator for out-of-tree C++23 or C99 native GridLock plugin repositories. |
| [`build_local.sh`](build_local.sh) | `bash` | Configures, compiles, and installs GridLock locally on a developer workstation. |
| [`uninstall_local.sh`](uninstall_local.sh) | `bash` | Removes files installed by `build_local.sh`. |
| [`build_appimage.sh`](build_appimage.sh) | `bash` | Packages `GridLock-<ver>-x86_64.AppImage` and `GridLock-<ver>-Tutorials-x86_64.AppImage` using `CQtDeployer` (`-ext "wayland,xcb,vulkan"`). |
| [`build_toolpak.sh`](build_toolpak.sh) | `bash` | Builds auxiliary HPC toolchain/tutorial bundles. |
| [`capture_ui.sh`](capture_ui.sh) | `bash` | Headless UI screenshot generator (`--docs`) driving `gridlock --snapshot`. |
| [`run_coverage.sh`](run_coverage.sh) | `bash` | Reconfigures with coverage instrumentation, runs Meson tests, and emits HTML line/branch reports. |
| [`sync_wiki.sh`](sync_wiki.sh) | `bash` | Mirrors `docs/*.md` into a GitHub Wiki checkout with rewritten wiki links and sidebar navigation. |
| [`e2e_cli_suite.mjs`](e2e_cli_suite.mjs) | Node.js | Manual local developer CLI and live `mpicxx`/`mpicc` tutorial verification runner (`lib/` holds helper modules). |

---

## Scaffolding Out-of-Tree Native Plugins (`init_plugin.lua`)

`scripts/init_plugin.lua` generates a self-contained GridLock ABI v1.0 plugin repository with `src/plugin.cpp` (`c++23`) or `src/plugin.c` (`c99`), a `plugin.map` GNU/LLVM linker version script (exporting only `gridlock_plugin_init_v1` so in-place **Hot-Reload Now** unmaps cleanly without `STB_GNU_UNIQUE` symbol pinning), build files (`meson.build` + `meson_options.txt`, `CMakeLists.txt`, or `Makefile`), `README.md`, `LICENSE`, and `.gitignore`.

```bash
# Interactive mode (prompts for 9 fields with sensible defaults in [brackets]):
lua scripts/init_plugin.lua

# Non-interactive scripted / CI mode:
lua scripts/init_plugin.lua --yes \
  --name "Domain Mesh Inspector" \
  --author "gridlock" \
  --lang c++23 \
  --build meson \
  --license MIT \
  --dir /tmp/domain-mesh-inspector

# Build and verify conformance headlessly:
meson setup /tmp/domain-mesh-inspector/build /tmp/domain-mesh-inspector -Dgridlock_include_dir="$PWD/include"
ninja -C /tmp/domain-mesh-inspector/build
./builddir/gridlock --verify-plugin /tmp/domain-mesh-inspector/build/libgridlock_domain_mesh_inspector.so
```

### Supported `init_plugin.lua` Flags

* `-y`, `--yes`: Accept defaults for any unspecified prompt without reading stdin.
* `--name <title>`: Human-readable plugin title (`plugin_name_utf8`).
* `--slug <kebab-slug>`: Repository/identifier slug (derived from `--name` when omitted).
* `--id <reverse.dns>`: Globally unique plugin ID (defaults to `io.github.<author>.<slug>`).
* `--version <semver>`: Initial semantic version string (defaults to `0.1.0`).
* `--description <text>` / `--desc <text>`: One-line summary (`description_utf8`).
* `--author <name>`: Author or organization (defaults to `git config user.name` or `$USER`).
* `--license <spdx>`: `MIT` (default), `Apache-2.0`, `BSD-3-Clause`, `GPL-3.0-or-later`, or `Unlicense`.
* `--lang <std>`: `c++23` (default) or `c99`.
* `--build <sys>`: `meson` (default), `cmake`, or `make`.
* `--dir <path>`: Output directory (defaults to `./<slug>`).
* `-h`, `--help`: Print CLI help and exit.

> [!TIP]
> See [Plugin Authoring, UI Surfaces & Build Recipes](../docs/Plugin-Authoring-and-UI-Surfaces.md) and [C99 Plugin ABI & C++23 Wrapper Specification](../docs/Plugin-C99-ABI-Reference.md) for complete callback, struct layout, and UI integration details.

---

## Packaging & Manual E2E Policy

> [!IMPORTANT]
> * **AppImage Packaging (`build_appimage.sh`)**: Always uses `CQtDeployer` with `-ext "wayland,xcb,vulkan"` (never `linuxdeploy`).
> * **Manual E2E Runner (`e2e_cli_suite.mjs`)**: Strictly a manual local developer verification tool. Never register `scripts/e2e_cli_suite.mjs` in `meson.build` (`meson test`) and never add it to `.github/workflows/release.yml` or any automated CI workflow.
