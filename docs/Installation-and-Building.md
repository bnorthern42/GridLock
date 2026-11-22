# Installation and Building

GridLock (`v0.8.8`) runs on x86_64 Linux systems under both native Wayland compositors (Niri, Sway, Hyprland, KDE Plasma, GNOME) and X11/XWayland servers. You can run a standalone AppImage from GitHub Releases or compile from source with Meson and Ninja.

---

## 1. Standalone AppImage (`x86_64` Linux)

Each release on [GitHub Releases](https://github.com/bnorthern42/GridLock/releases) ships two self-contained AppImage editions:

* **`GridLock-0.8.8-x86_64.AppImage` (Lite Edition)**: Bundles the `gridlock` binary, Qt6 Wayland (`libqwayland*.so`, `wayland-decoration-client`, `wayland-graphics-integration-client`, `wayland-shell-integration`) and XCB (`libqxcb.so`, `xcbglintegrations`, `libxcb-cursor.so.0`) platform plugins, `libqtadvancedcss`, `qtermwidget6` color schemes and keyboard layouts, and SQLite3 docset drivers without tutorial binaries.
* **`GridLock-0.8.8-Tutorials-x86_64.AppImage` (Tutorials Edition)**: Includes everything in the Lite Edition plus all 8 MPI tutorial source files and pre-compiled debug binaries under `usr/share/gridlock/tutorial/`.

### Running the AppImage

```bash
chmod +x GridLock-0.8.8-Tutorials-x86_64.AppImage
./GridLock-0.8.8-Tutorials-x86_64.AppImage
```

* **Wayland and X11 Selection**: `AppRun` configures `QT_PLUGIN_PATH="$APPDIR/usr/plugins"` and `usr/bin/qt.conf`. When both `WAYLAND_DISPLAY` and `DISPLAY` are active and `QT_QPA_PLATFORM` is unset, `AppRun` exports `QT_QPA_PLATFORM="wayland;xcb"` so Qt6 connects over Wayland first and falls back to X11/XCB if needed. On native (non-WSLg) Wayland compositors, `src/main.cpp` sets `QT_WAYLAND_DISABLE_WINDOWDECORATION=1`.
* **Running Without `libfuse2`**: On containers or minimal hosts without FUSE, extract and run on the fly:
  ```bash
  ./GridLock-0.8.8-x86_64.AppImage --appimage-extract-and-run
  # or export APPIMAGE_EXTRACT_AND_RUN=1
  ```
* **External Host Tools Used at Runtime**: The AppImage bundles the GUI and Qt6 runtime. Host debugging, compilation, and LSP features invoke tools from your system `PATH` when present: `gdb` (or `gdb-multiarch`, `lldb-dap`, `cuda-gdb`, `rocgdb`), `mpirun` / `mpiexec`, `mpicxx` / `mpicc` / `g++`, `objdump` (`binutils`), `clangd`, and `man`.

---

## 2. System Dependencies for Source Builds

GridLock requires a C++23 compiler (`GCC 13+` or `Clang 16+`), `Meson >= 1.1`, `Ninja`, `CMake` (when building `Qt-Advanced-Stylesheets` or `qtermwidget6`), `Qt6` (`Core`, `Gui`, `Widgets`, `Network`, `Sql`, `Concurrent`, `Svg`, `OpenGLWidgets`, `Qml`, `Test`), `qtermwidget6`, `tomlplusplus` (auto-fetched via `external/tomlplusplus.wrap` if absent), and an MPI implementation (`OpenMPI` or `MPICH`).

### Arch Linux (`pacman`)

```bash
sudo pacman -S --needed \
  base-devel gcc clang meson ninja cmake pkgconf git \
  qt6-base qt6-declarative qt6-svg qt6-tools qt6-wayland \
  qtermwidget tomlplusplus sqlite \
  openmpi gdb lldb binutils elfutils man-db zeal patchelf xcb-util-cursor
```

### Ubuntu 24.04+ / Debian Trixie (`apt`)

```bash
sudo apt update
sudo apt install -y \
  build-essential g++ clang clangd cmake meson ninja-build pkg-config git \
  qt6-base-dev qt6-base-dev-tools qt6-tools-dev qt6-tools-dev-tools \
  qt6-declarative-dev qt6-wayland qt6-wayland-dev \
  libqt6sql6-sqlite libqt6svg6-dev libqtermwidget6-dev libsqlite3-dev \
  openmpi-bin libopenmpi-dev gdb gdb-multiarch lldb llvm binutils elfutils \
  libxcb-cursor0 libxcb-cursor-dev patchelf man-db
```

> **Note for `qtermwidget6` on older Debian/Ubuntu releases**: If `libqtermwidget6-dev` is not in your APT repositories, build `lxqt-build-tools` (`2.0.0`) and `qtermwidget` (`2.0.1`) from source with `-DCMAKE_INSTALL_PREFIX=/usr` (see `.github/workflows/release.yml` for exact commands).

### Fedora 40+ / Ultramarine / RHEL (`dnf`)

```bash
sudo dnf install -y \
  gcc-c++ clang clang-tools-extra cmake meson ninja-build pkgconf-pkg-config git \
  qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtsvg-devel qt6-qttools-devel qt6-qtwayland-devel \
  qtermwidget-qt6-devel tomlplusplus-devel sqlite-devel \
  openmpi openmpi-devel gdb lldb binutils elfutils man-db zeal patchelf xcb-util-cursor
```

On Fedora, load the OpenMPI environment module into your shell before building or launching MPI targets if `mpicxx` and `mpirun` are installed under `/usr/lib64/openmpi/bin`:

```bash
export PATH="/usr/lib64/openmpi/bin:${PATH}"
```
(`GdbRankCoordinator` and `MainWindow::ensureTargetBinaryCompiled` also search `/usr/lib64/openmpi/bin`, `/usr/lib/openmpi/bin`, `/usr/lib64/mpich/bin`, and `/usr/lib/mpich/bin` directly.)

---

## 3. Building From Source

### 1. Clone With Submodules and Build `Qt-Advanced-Stylesheets`

GridLock links against `Qt-Advanced-Stylesheets` (`libqtadvancedcss`). If `QtAdvancedStylesheets` is not installed system-wide via CMake package config, compile the bundled submodule once:

```bash
git clone --recurse-submodules https://github.com/bnorthern42/GridLock.git
cd GridLock

cmake -S external/Qt-Advanced-Stylesheets \
      -B external/Qt-Advanced-Stylesheets/build \
      -GNinja -DQT_VERSION_MAJOR=6 -DCMAKE_BUILD_TYPE=Release
ninja -C external/Qt-Advanced-Stylesheets/build
```

### 2. Configure and Compile With Meson + Ninja

```bash
meson setup builddir
ninja -C builddir
```

This builds two default targets in `builddir/`:
* `builddir/gridlock`: Main workstation executable (configured with `build_rpath` pointing to `external/Qt-Advanced-Stylesheets/build/lib`).
* `builddir/mpi_mm`: Debug MPI matrix-multiplication binary compiled from `tutorial/mpi_mm.c` via `mpicc -g -O0`.

Launch GridLock directly from the build directory:

```bash
./builddir/gridlock
```

### 3. System-Wide Install, Uninstall, and Packaging Scripts

* **Local System Install (`scripts/build_local.sh`)**: Configures `build/`, compiles with Ninja, and runs `sudo meson install -C build` (installs `gridlock`, `data/gridlock.desktop`, and the `512x512` hicolor application icon).
* **Uninstall (`scripts/uninstall_local.sh`)**: Runs `sudo ninja -C build uninstall`.
* **Build Local AppImages (`scripts/build_appimage.sh`)**:
  ```bash
  # Build both Lite and Tutorials AppImages into /tmp:
  OUTPUT_DIR=/tmp bash scripts/build_appimage.sh

  # Build only the Lite AppImage:
  bash scripts/build_appimage.sh --lite --output-dir /tmp

  # Build only the Tutorials AppImage:
  bash scripts/build_appimage.sh --with-tutorials --output-dir /tmp

  # Populate AppDir/ and run the ldd / plugin verification gate without mksquashfs:
  bash scripts/build_appimage.sh --appdir-only
  ```
* **Build Ext4 Toolpak Image (`scripts/build_toolpak.sh`)**: Creates a 512 MB ext4 loop image `GridLock.toolpak` populated via `DESTDIR` install from `builddir/`.

---

## 4. Building the Included MPI Tutorial Targets

GridLock compiles any selected `tutorial/*.cpp` target on demand with `mpicxx -g -O0` (and sets `+x` executable permissions) when you click **`▶ Run`** or **`⚙ Build`** (`Ctrl+B`). To compile all 8 primary C++ showcase programs and the 5 auxiliary C/C++ demos manually from a terminal:

```bash
# 8 primary C++ MPI showcase programs
for prog in inspection_demo deadlock_ring collective_mismatch ghost_cell_drift \
            heap_imbalance tag_mismatch_stall simd_struct_watch catchpoint_exceptions; do
  mpicxx -g -O0 "tutorial/${prog}.cpp" -o "tutorial/${prog}"
done

# 5 auxiliary C/C++ tutorial programs (used by --tutorial-mode and manual labs)
mpicc  -g -O0 tutorial/deadlock_demo.c       -o tutorial/deadlock_demo
mpicc  -g -O0 tutorial/mpi_mm.c              -o tutorial/mpi_mm
mpicxx -g -O0 tutorial/register_demo.cpp     -o tutorial/register_demo
mpicxx -g -O0 tutorial/memview_demo.cpp      -o tutorial/memview_demo
mpicxx -g -O0 tutorial/memview_diff_demo.cpp -o tutorial/memview_diff_demo
```

See [Tutorials Walkthrough](Tutorials-Walkthrough.md) for line-by-line instructions on reproducing and inspecting each bug.

---

## 5. Command-Line Options and Environment Variables

### CLI Flags (`src/main.cpp`, `src/core/utils/CliParser.cpp`)

| Option | Description |
| :--- | :--- |
| `-v`, `--version` | Prints `gridlock 0.8.8` to `stdout` and exits `0` before initializing Qt/GUI plugins. |
| `-h`, `--help` | Prints command-line usage options and exits `0`. |
| `--help-all` | Prints GridLock options plus standard Qt6 command-line flags. |
| `[target] [-- <args...>]` | Loads `[target]` (ELF binary, `.cpp`/`.c` source, or workspace directory) and scopes `<args...>` to that target. |
| `--tutorial-mode` | Opens the modal `Interactive Tutorials` launcher (`TutorialDialog`) before mapping `MainWindow`. |
| `--test-mode [file]` | Runs `GridLockAutomationRunner`. If `[file]` is supplied, compiles it with `mpicc`/`mpic++ -g -O0` to `/tmp/<name>_test_demo` and starts a 2-rank session. |
| `--verbose` | Enables `[DEBUG]` and `[INFO]` diagnostic messages on `stderr`. |
| `--headless` | Runs under `QT_QPA_PLATFORM=offscreen` without showing the splash screen or raising a desktop window. |
| `--snapshot <file>` | Renders `MainWindow` offscreen, saves a PNG screenshot to `<file>`, and exits (`0` on success, `1` on error). |

### Environment Variables

| Variable | Effect |
| :--- | :--- |
| `WAYLAND_DISPLAY` / `DISPLAY` | Inspected at startup to select Wayland vs. X11/XCB, or fall back to `offscreen` in headless containers. |
| `QT_QPA_PLATFORM` | Overrides the Qt platform plugin (`wayland`, `xcb`, `wayland;xcb`, `offscreen`). |
| `GRIDLOCK_LIVE_WAYLAND` | When non-empty, keeps native Wayland active even when `--headless` or `--snapshot` is passed. |
| `GRIDLOCK_TEST_CONFIG_DIR` | Redirects `ConfigManager`'s global `config.toml` directory (used by isolated unit tests). |
| `GRIDLOCK_MEMTRACK_SO` | Path to the compiled heap-caller interposition shared library (`/tmp/gridlock_memtrack_v2_<uid>.so`) injected via `LD_PRELOAD` into GDB-launched ranks when `Track Heap Callers (LD_PRELOAD)` is enabled in `MemoryProfilerWidget`. |
| `SHELL` | Default login shell executed inside `TerminalDockWidget` (falls back to `/bin/bash`). |
