#!/usr/bin/env bash
set -euo pipefail

WIKI_DIR="${1:-/tmp/gridlock-wiki}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
DOCS_DIR="${ROOT_DIR}/docs"

mkdir -p "${WIKI_DIR}/screenshots"

# Remove stale top-level Wiki Markdown pages before regenerating so deleted/renamed docs do not linger.
rm -f "${WIKI_DIR}"/*.md

# Copy screenshots into Wiki screenshots/ directory.
if [[ -d "${DOCS_DIR}/screenshots" ]]; then
    cp -f "${DOCS_DIR}/screenshots/"*.png "${WIKI_DIR}/screenshots/" 2>/dev/null || true
fi

# Map source Markdown paths (relative to docs/) to flat GitHub Wiki page names.
declare -A PAGE_MAP=(
    ["Installation-and-Building.md"]="Installation-and-Building"
    ["Quickstart-and-Workflows.md"]="Quickstart-and-Workflows"
    ["User-Manual-UI-and-Debugging.md"]="User-Manual-UI-and-Debugging"
    ["Keyboard-Shortcuts-and-Reference.md"]="Keyboard-Shortcuts-and-Reference"
    ["MPI-Diagnostics-and-HPC-Analysis.md"]="MPI-Diagnostics-and-HPC-Analysis"
    ["Memory-Reverse-Engineering-and-Profiling.md"]="Memory-Reverse-Engineering-and-Profiling"
    ["Tutorials-Walkthrough.md"]="Tutorials-Walkthrough"
    ["Configuration-and-Customization.md"]="Configuration-and-Customization"
    ["Developer-Architecture-and-Testing.md"]="Developer-Architecture-and-Testing"
    ["ROADMAP.md"]="Roadmap"
)

rewrite_markdown_links() {
    local src_rel="$1"
    local src_file="${DOCS_DIR}/${src_rel}"
    local dst_file="$2"

    cp -f "${src_file}" "${dst_file}"

    # Normalize screenshot paths to flat `screenshots/<file>.png` inside Wiki.
    sed -i -E 's|\((\.\./)+screenshots/|(|g; s|\(\./screenshots/|(screenshots/|g; s|\(docs/screenshots/|(screenshots/|g' "${dst_file}"
    sed -i -E 's|\(([A-Za-z0-9_.-]+\.png)\)|(screenshots/\1)|g' "${dst_file}"

    # Rewrite cross-links to flat GitHub Wiki page names (without .md).
    for key in "${!PAGE_MAP[@]}"; do
        local wiki_page="${PAGE_MAP[${key}]}"
        local base_name
        base_name="$(basename "${key}")"

        sed -i -E "s|\((docs/)?${key}(#[^)]*)?\)|(${wiki_page}\2)|g" "${dst_file}"
        sed -i -E "s|\((\.\./)*(\./)?([A-Za-z0-9_/-]+/)?${base_name}(#[^)]*)?\)|(${wiki_page}\4)|g" "${dst_file}"
    done

    # Rewrite links to root ROADMAP.md to the Wiki Roadmap page.
    sed -i -E 's|\((\.\./)*ROADMAP\.md(#[^)]*)?\)|(Roadmap\2)|g' "${dst_file}"
}

for src_rel in "${!PAGE_MAP[@]}"; do
    if [[ -f "${DOCS_DIR}/${src_rel}" ]]; then
        wiki_name="${PAGE_MAP[${src_rel}]}"
        rewrite_markdown_links "${src_rel}" "${WIKI_DIR}/${wiki_name}.md"
    else
        echo "ERROR: Expected documentation file missing: ${DOCS_DIR}/${src_rel}" >&2
        exit 1
    fi
done

cat > "${WIKI_DIR}/Home.md" << 'EOF'
# GridLock Documentation Wiki (`v0.8.7`)

Welcome to the **GridLock** documentation wiki, synchronized automatically from `docs/` on `master`.

![GridLock Workspace Overview](screenshots/workspace_overview.png)

## Getting Started & Workflows

- [Installation and Building](Installation-and-Building): Linux dependencies (Arch, Ubuntu/Debian, Fedora), AppImage usage on Wayland and X11, Meson/Ninja build, tutorial compilation, and CLI options.
- [Quickstart and Workflows](Quickstart-and-Workflows): Launching local MPI jobs, attaching to live `/proc` OpenMPI/MPICH/SLURM jobs (`Ctrl+Alt+P`), debugging `qemu-user -g` and `gdbserver` targets (`target remote`), and importing `#SBATCH` scripts.
- [Tutorials Walkthrough](Tutorials-Walkthrough): Step-by-step exercises for all 8 primary C++ MPI showcase programs (including `catchpoint_exceptions.cpp`) and 5 auxiliary demos in `tutorial/`.

## User Reference

- [Keyboard Shortcuts and Quick Reference](Keyboard-Shortcuts-and-Reference): Scannable lookup tables for execution controls (`nexti`, `Ctrl+F10`, PC jump, early return), Command Palette prefixes (`>`, `@`, `#`), floating tools (`Ctrl+Alt+C/G/S/K/P`), breakpoint lifecycle, GDB readline bindings, and CFG shortcuts.
- [User Manual: UI and Debugging](User-Manual-UI-and-Debugging): Perspectives (`Alt+1..3`), Command Palette, non-linear execution, non-destructive/temporary/regex breakpoints, `SignalCatchpointDialog` (`Ctrl+Alt+S`), `CheckpointDialog` (`Ctrl+Alt+K`), and `GdbConsoleWidget` readline ergonomics.
- [MPI Diagnostics and HPC Analysis](MPI-Diagnostics-and-HPC-Analysis): MPMD FIFO coordinator mechanics, `ServerRackView`, `DeadlockAnalyzer`, `MpiNetworkLogWidget`, `DifferentialGrid` outlier highlighting, and `Package Manager` (`spack`, `dnf`, `pacman`, `apt`).
- [Memory, Reverse Engineering, and Profiling](Memory-Reverse-Engineering-and-Profiling): `MemView` (`process_vm_readv`, `Diff Ranks`, `Export`), `MemoryProfilerWidget` (`smaps` + `LD_PRELOAD` heap callers), `ProgrammerCalculatorDialog` (`Ctrl+Alt+C`), `ArrayPlotDialog` (`Ctrl+Alt+G`), persistent `PointerGraphDialog`, `DisassemblyView` (2D CFG + Decompiler + Xrefs + `Alt+P` patching), and `RegisterView`.
- [Configuration and Customization](Configuration-and-Customization): `PreferencesDialog`, 12 built-in dark and light theme palettes, `config.toml` and `.gridlock/` session files, and build auto-detection.

## Architecture, Testing & Roadmap

- [Developer Architecture and Testing](Developer-Architecture-and-Testing): Repository structure, coordinator and lazy-loading startup architecture, QSS disk cache, all 22 Meson test suites, coverage, and screenshot generation.
- [Engineering Roadmap](Roadmap): Completed `v0.8.7` deliverables and planned milestones from `v0.8.8` through `v1.0.0`.
EOF

cat > "${WIKI_DIR}/_Sidebar.md" << 'EOF'
### GridLock Wiki

- [Home](Home)

**Getting Started**
- [Installation & Building](Installation-and-Building)
- [Quickstart & Workflows](Quickstart-and-Workflows)
- [Tutorials Walkthrough](Tutorials-Walkthrough)

**User Reference**
- [Keyboard Shortcuts](Keyboard-Shortcuts-and-Reference)
- [UI & Debugging Manual](User-Manual-UI-and-Debugging)
- [MPI Diagnostics & HPC](MPI-Diagnostics-and-HPC-Analysis)
- [Memory, RE & Profiling](Memory-Reverse-Engineering-and-Profiling)
- [Configuration & Themes](Configuration-and-Customization)

**Internals & Plan**
- [Developer Architecture](Developer-Architecture-and-Testing)
- [Engineering Roadmap](Roadmap)
EOF

REPO_SLUG="${GITHUB_REPOSITORY:-}"
if [[ -z "${REPO_SLUG}" ]]; then
    ORIGIN_URL="$(git -C "${ROOT_DIR}" remote get-url origin 2>/dev/null || true)"
    REPO_SLUG="$(echo "${ORIGIN_URL}" | sed -E 's#^(git@github\.com:|https?://github\.com/)##; s#\.git$##')"
fi
REPO_SLUG="${REPO_SLUG:-bnorthern42/GridLock}"

cat > "${WIKI_DIR}/_Footer.md" << EOF
---

[GridLock Repository](https://github.com/${REPO_SLUG}) | [Releases](https://github.com/${REPO_SLUG}/releases) | [Report an Issue](https://github.com/${REPO_SLUG}/issues/new/choose) | [Roadmap](https://github.com/${REPO_SLUG}/blob/master/ROADMAP.md) | [Accessibility](https://github.com/${REPO_SLUG}/blob/master/ACCESSIBILITY.md) | [License (MIT)](https://github.com/${REPO_SLUG}/blob/master/LICENSE)

*Documentation mirrored from \`docs/\` on \`master\`.*
EOF

echo "Synced ${#PAGE_MAP[@]} documentation pages + Home.md + _Sidebar.md + _Footer.md into ${WIKI_DIR}"
