#!/usr/bin/env bash
set -euo pipefail

WIKI_DIR="${1:-/tmp/gridlock-wiki}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
DOCS_DIR="${ROOT_DIR}/docs"

mkdir -p "${WIKI_DIR}/screenshots"

# Copy screenshots into Wiki screenshots/ directory.
if [[ -d "${DOCS_DIR}/screenshots" ]]; then
    cp -f "${DOCS_DIR}/screenshots/"*.png "${WIKI_DIR}/screenshots/" 2>/dev/null || true
fi

# Map source Markdown paths (relative to docs/) to flat GitHub Wiki page names.
declare -A PAGE_MAP=(
    ["ARCHITECTURE.md"]="Architecture"
    ["ROADMAP.md"]="Roadmap"
    ["THEMES.md"]="Themes"
    ["TUTORIAL.md"]="Tutorial"
    ["USER_GUIDE.md"]="User-Guide"
    ["user/README.md"]="User-Manual"
    ["user/interface_guide.md"]="User-Interface-Guide"
    ["user/hpc_workflows.md"]="User-HPC-Workflows"
    ["user/advanced_debugging.md"]="User-Advanced-Debugging"
    ["developer/README.md"]="Developer-Guide"
    ["developer/environment_setup.md"]="Developer-Environment-Setup"
    ["developer/architecture_core.md"]="Developer-Architecture-Core"
    ["developer/ui_internals.md"]="Developer-UI-Internals"
    ["developer/testing_and_ci.md"]="Developer-Testing-and-CI"
    ["developer/core/hpc_and_memory.md"]="Developer-Core-HPC-and-Memory"
    ["developer/core/managers_and_models.md"]="Developer-Core-Managers-and-Models"
    ["developer/ui/views_and_docks.md"]="Developer-UI-Views-and-Docks"
    ["developer/ui/widgets_and_dialogs.md"]="Developer-UI-Widgets-and-Dialogs"
    ["developer/testing/test_harness.md"]="Developer-Testing-Harness"
)

rewrite_markdown_links() {
    local src_rel="$1"
    local src_file="${DOCS_DIR}/${src_rel}"
    local dst_file="$2"
    local src_dir
    src_dir="$(dirname "${src_rel}")"

    cp -f "${src_file}" "${dst_file}"

    # Normalize screenshot paths to flat `screenshots/<file>.png` inside Wiki.
    sed -i -E 's|\((\.\./)+screenshots/|(|g; s|\(\./screenshots/|(screenshots/|g; s|\(docs/screenshots/|(screenshots/|g' "${dst_file}"
    # Ensure screenshot paths that lost `screenshots/` prefix via (../)+ regain it.
    sed -i -E 's|\(([A-Za-z0-9_.-]+\.png)\)|(screenshots/\1)|g' "${dst_file}"

    # Rewrite known cross-links to flat GitHub Wiki page names (without .md).
    for key in "${!PAGE_MAP[@]}"; do
        local wiki_page="${PAGE_MAP[${key}]}"
        local base_name
        base_name="$(basename "${key}")"
        local rel_dir
        rel_dir="$(dirname "${key}")"

        # Match exact path relative to docs/ (e.g. docs/TUTORIAL.md or user/hpc_workflows.md).
        sed -i -E "s|\((docs/)?${key}(#[^)]*)?\)|(${wiki_page}\2)|g" "${dst_file}"
        # Match ./<file> or ../<file> or bare <file> for unique filenames.
        if [[ "${base_name}" != "README.md" ]]; then
            sed -i -E "s|\((\.\./)*(\./)?([A-Za-z0-9_/-]+/)?${base_name}(#[^)]*)?\)|(${wiki_page}\4)|g" "${dst_file}"
        elif [[ "${src_dir}" == "${rel_dir}" ]]; then
            sed -i -E "s|\(\./README\.md(#[^)]*)?\)|(${wiki_page}\1)|g" "${dst_file}"
        fi
    done

    # Rewrite links to top-level repo files (ROADMAP.md, ACCESSIBILITY.md) to Wiki or GitHub.
    sed -i -E 's|\((\.\./)*ROADMAP\.md(#[^)]*)?\)|(Roadmap\2)|g' "${dst_file}"
}

for src_rel in "${!PAGE_MAP[@]}"; do
    if [[ -f "${DOCS_DIR}/${src_rel}" ]]; then
        wiki_name="${PAGE_MAP[${src_rel}]}"
        rewrite_markdown_links "${src_rel}" "${WIKI_DIR}/${wiki_name}.md"
    fi
done

cat > "${WIKI_DIR}/Home.md" << 'EOF'
# GridLock Documentation Wiki

Welcome to the **GridLock** documentation wiki, automatically synchronized from the `docs/` tree on `master`.

![GridLock Workspace Overview](screenshots/workspace_overview.png)

## User Documentation

- [User Guide](User-Guide) - End-to-end walkthrough for MPI debugging, source editing, conditional breakpoints, and SLURM import
- [Hands-On Tutorial](Tutorial) - Step-by-step exercises using the bundled MPI showcase targets
- [Themes and Fonts](Themes) - Previews and color palettes for all built-in dark and light themes
- [User Manual](User-Manual) - Interface, HPC workflows, and advanced debugging reference
  - [Interface Guide](User-Interface-Guide)
  - [HPC Workflows](User-HPC-Workflows)
  - [Advanced Debugging](User-Advanced-Debugging)

## Architecture & Roadmap

- [Architecture Overview](Architecture) - Window layout, dock structure, and coordinator signal wiring
- [Engineering Roadmap](Roadmap) - Planned milestones from v0.8.7 through v1.0.0

## Developer Documentation

- [Developer Guide](Developer-Guide) - Internal subsystem index
- [Environment Setup and Build](Developer-Environment-Setup)
- [Backend Architecture Core](Developer-Architecture-Core)
  - [HPC and Native Memory Engine](Developer-Core-HPC-and-Memory)
  - [Managers and Models](Developer-Core-Managers-and-Models)
- [UI Internals and Views](Developer-UI-Internals)
  - [Views and Docks](Developer-UI-Views-and-Docks)
  - [Widgets and Dialogs](Developer-UI-Widgets-and-Dialogs)
- [Testing and CI Pipeline](Developer-Testing-and-CI)
  - [Test Harness](Developer-Testing-Harness)
EOF

cat > "${WIKI_DIR}/_Sidebar.md" << 'EOF'
### GridLock Wiki

- [Home](Home)

**User Guides**
- [User Guide](User-Guide)
- [Tutorial](Tutorial)
- [Themes & Fonts](Themes)
- [User Manual](User-Manual)
  - [Interface Guide](User-Interface-Guide)
  - [HPC Workflows](User-HPC-Workflows)
  - [Advanced Debugging](User-Advanced-Debugging)

**Architecture & Plan**
- [Architecture](Architecture)
- [Roadmap](Roadmap)

**Developer Docs**
- [Developer Guide](Developer-Guide)
- [Environment Setup](Developer-Environment-Setup)
- [Architecture Core](Developer-Architecture-Core)
  - [HPC & Memory](Developer-Core-HPC-and-Memory)
  - [Managers & Models](Developer-Core-Managers-and-Models)
- [UI Internals](Developer-UI-Internals)
  - [Views & Docks](Developer-UI-Views-and-Docks)
  - [Widgets & Dialogs](Developer-UI-Widgets-and-Dialogs)
- [Testing & CI](Developer-Testing-and-CI)
  - [Test Harness](Developer-Testing-Harness)
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
