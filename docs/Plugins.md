# Using Plugins in GridLock

Plugins extend GridLock with custom inspection tools, solver-specific visualizations, custom menus, status-bar indicators, source annotations, and structured report tabs without modifying the core application. You can install plugins directly from Git repositories inside **Preferences -> Plugins**, declare a reproducible plugin list in `plugins.toml`, or drop pre-built plugin files into your global or project plugin folder.

---

## 1. Discovering & Finding Plugins

GridLock uses a decentralized distribution model so teams and scientific computing communities can publish plugins on any Git server:

* **`gridlock-plugin` Repository Topic**: Browse community extensions tagged with `gridlock-plugin` on GitHub (`https://github.com/topics/gridlock-plugin`), GitLab, Codeberg, or institutional Gitea instances.
* **Curated `awesome-gridlock` Lists**: Community-maintained `awesome-gridlock` indexes catalog plugins by scientific domain (for example, PETSc/Trilinos vector and matrix viewers, Kokkos field inspectors, STL container viewers, mesh refinement exporters, and MPI rank health monitors).
* **Supported Git Hosts**:
  * **GitHub (`github.com`)**: Shorthand `owner/repo` or `github:owner/repo`
  * **GitLab (`gitlab.com`)**: Shorthand `owner/repo`, nested subgroups `group/subgroup/repo`, or `gitlab:group/subgroup/repo`
  * **Codeberg (`codeberg.org`)**: Shorthand `owner/repo` or `codeberg:owner/repo`
  * **Gitea & Self-Hosted Git Servers**: Any `https://<host>/<path>.git` or `git@<host>:<path>.git` repository URL

---

## 2. Installing Plugins via Preferences

To clone, build, verify, and install a plugin from source inside GridLock:

1. Open **Edit -> Preferences** (or press `Ctrl+,`) and select **Plugins** in the left category tree.
2. Switch to the **Install from Git** tab.
3. Choose the source forge in **Git Host**:
   * **GitHub (`github.com`)**
   * **GitLab (`gitlab.com`)**
   * **Codeberg (`codeberg.org`)**
   * **Custom Git URL (`https://` / `git@`)**
4. Enter the repository in **Git Host & Repository**:
   * For **GitHub**, **GitLab**, or **Codeberg**, enter compact shorthand such as `bnorthern42/gridlock-stl-inspector` (or paste a full `https://` or `git@` URL, which GridLock honors regardless of the selected dropdown item).
   * For **Custom Git URL**, enter a complete HTTPS or SSH repository address such as `https://gitea.example.org/lab/mesh-inspector.git` or `git@gitea.example.org:lab/mesh-inspector.git`.
5. *(Optional)* Enter a branch name, release tag, or commit hash in the ref field (for example, `main` or `v1.2.0`). Leave it blank to use the repository's default branch.
6. Select the target **Install Scope**:
   * **Global (`~/.config/gridlock/plugins`)**: Makes the plugin available across all projects for your user account.
   * **This Workspace (`<workspace>/.gridlock/plugins`)**: Installs the plugin only for the currently open project folder and automatically records trust approval for the built binary.
7. Click **Install** (which switches to **Cancel** while a build is running).
   * GridLock clones the repository into an isolated temporary folder, detects the project build file (`meson.build`, `CMakeLists.txt`, `Makefile`, or standalone source), compiles the plugin, and automatically runs a sandboxed post-build verification check before completing installation.
   * Warning lines are highlighted in amber, and errors are highlighted in red, with a summary count displayed above the log.
8. When installation succeeds, click **Hot-Reload Now** on the banner to activate the plugin immediately without closing your open workspace or active paused MPI debug session, or click **Restart GridLock Now** for a full application restart.

---

## 3. Declarative `plugins.toml` & `plugins-lock.toml`

Instead of installing plugins one by one on every workstation, you can declare your plugin list in `plugins.toml` and synchronize everything with a single click.

### Manifest Locations

| Scope | Manifest Path | Installed Binary Folder | Purpose |
| :--- | :--- | :--- | :--- |
| **User Global** | `~/.config/gridlock/plugins.toml` | `~/.config/gridlock/plugins/` | Personal plugins shared across every project on your machine. |
| **Workspace** | `<workspace>/.gridlock/plugins.toml` | `<workspace>/.gridlock/plugins/` | Project-specific plugins version-controlled alongside solver source code. |

### Example `plugins.toml`

```toml
# Global (~/.config/gridlock/plugins.toml) or Workspace (<workspace>/.gridlock/plugins.toml)

[[plugins]]
repo = "bnorthern42/gridlock-stl-inspector"
ref = "main"
build = "auto"
enabled = true

[[plugins]]
repo = "gitlab:hpc-tools/mpi/mesh-inspector"
ref = "v1.2.0"
build = "meson"

[[plugins]]
repo = "codeberg:scicomp/kokkos-field-view"
build = "cmake"

[[plugins]]
repo = "https://gitea.example.org/team/custom-solver-probe.git"
ref = "9f13365"
build = "make"
enabled = false
```

### Supported `[[plugins]]` Fields

| Field | Required | Default | Description |
| :--- | :---: | :---: | :--- |
| `repo` | Yes | None | `owner/repo` (GitHub), `github:owner/repo`, `gitlab:group/sub/repo`, `codeberg:owner/repo`, or any `https://` / `git@` Git URL. |
| `ref` | No | Remote default branch | Branch name, release tag, or 7 to 40 character commit hash. |
| `build` | No | `"auto"` | `"auto"`, `"meson"`, `"cmake"`, `"make"`, or `"single"`. |
| `enabled` | No | `true` | Set to `false` to skip this entry during `Sync plugins.toml` without deleting it from the file. |

### Editing, Syncing & Lockfile Pinning

* **Open `plugins.toml`**: In **Preferences -> Plugins -> Install from Git**, select **Global** or **This Workspace** and click **Open `plugins.toml`**. GridLock creates a starter template if the file does not exist yet and opens it directly in the editor.
* **Automatic Recording**: Installing a plugin via **Install** automatically adds or updates its `[[plugins]]` entry in the selected scope's `plugins.toml`.
* **Sync `plugins.toml`**: Clicking **Sync `plugins.toml`** checks every enabled entry against `~/.config/gridlock/plugins-lock.toml` and remote Git commits, builds and verifies only missing or outdated plugins sequentially, and shows the **Hot-Reload Now** / **Restart GridLock Now** banner when new or updated binaries were built.
* **User-Global `plugins-lock.toml`**: GridLock writes exact resolved commit hashes (`commit_sha`) and binary `SHA-256` checksums (`binary_sha256`) exclusively to `~/.config/gridlock/plugins-lock.toml`. Lock data is never read from a project directory.

---

## 4. Managing, Updating & Hot-Reloading Installed Plugins

The **Preferences -> Plugins -> Installed** tab displays every discovered plugin in an 8-column table: **Global**, **Workspace**, **Name**, **Version** (showing `<version> (<short_commit>)` with a full provenance tooltip), **Scope / ABI**, **Status**, **Trust**, and **Update**.

### In-Place Updates & Live Hot-Reload

* **Per-Plugin `Update` & Toolbar `Update All`**: Click **Update** on an individual Git-installed plugin row or **Update All** in the toolbar to re-clone, rebuild, and verify the latest upstream revision in its existing scope.
* **Hot-Reload Now**: After installing, updating, syncing `plugins.toml`, or applying checkbox/trust changes, click **Hot-Reload Now** on the notification banner (available in both **Preferences -> Plugins** and the main window status bar). GridLock unloads previous plugin handles, loads the updated binaries from disk, refreshes menus, status badges, and report panels, and keeps any active paused MPI debug session intact.
* **When a Full Restart Is Required**: On Linux, certain C++ plugins compiled without a linker version script export pinned symbols that the system loader keeps in memory until the application exits. If GridLock detects those symbols during **Hot-Reload Now**, it displays an inline notice advising you to click **Restart GridLock Now** so the new binary is loaded cleanly.

### Staged Edits (`Apply` vs `Cancel`)

* Toggling **Global**, **Workspace**, or **Trust & Approve** stages your changes in memory and updates the **Status** preview (`Staged: ... (Apply to save)`) without modifying configuration files immediately.
* Clicking **Cancel** discards all staged changes.
* Clicking **Apply** or **OK** saves your changes to `~/.config/gridlock/config.toml` and displays the **Hot-Reload Now** / **Restart GridLock Now** banner.

### Effective Enablement Matrix

A plugin is active only when **both** its **Global** and **Workspace** checkboxes are checked and (for workspace plugins) its binary is **Trusted**:

| Plugin Origin | Global Checked | Workspace Checked | Workspace Binary Trusted | Effective State |
| :--- | :---: | :---: | :---: | :--- |
| **Global** (`~/.config/gridlock/plugins/`) | Yes | Yes | Not applicable (User Global) | **Loaded** |
| **Global** (`~/.config/gridlock/plugins/`) | Yes | No | Not applicable (User Global) | **Disabled (Workspace)** (active in other workspaces) |
| **Global** (`~/.config/gridlock/plugins/`) | No | Yes or No | Not applicable (User Global) | **Disabled (Global)** (inactive everywhere) |
| **Workspace** (`<workspace>/.gridlock/plugins/`) | Yes | Yes | Yes | **Loaded** |
| **Workspace** (`<workspace>/.gridlock/plugins/`) | Yes | Yes | No | **Untrusted Workspace Plugin (Approval Required)** |
| **Workspace** (`<workspace>/.gridlock/plugins/`) | Yes | No | Yes | **Disabled (Workspace)** |
| **Workspace** (`<workspace>/.gridlock/plugins/`) | No | Yes or No | Yes or No | **Disabled (Global)** |

Switching projects via **File -> Open Workspace / Folder** automatically re-evaluates workspace enablement and workspace plugins for the new project.

---

## 5. Workspace Security & SHA-256 Trust Gate

Opening an untrusted repository downloaded from the internet must never execute arbitrary code hidden inside `<workspace>/.gridlock/plugins/` or `<workspace>/.gridlock/plugins.toml`. GridLock enforces three safeguards:

1. **Zero Automatic Execution**: Untrusted shared libraries inside `<workspace>/.gridlock/plugins/` are never loaded into memory until you explicitly approve them. Similarly, `<workspace>/.gridlock/plugins.toml` is never built automatically when opening a folder; it runs only when you explicitly select **This Workspace** and click **Sync `plugins.toml`**.
2. **User-Owned Trust Registry (`config.toml`)**: When you click **Trust & Approve (<sha8>)** and **Apply** (or build a workspace plugin yourself via **Install** / **Sync `plugins.toml`**), GridLock stores the canonical file path and `SHA-256` fingerprint in your personal `~/.config/gridlock/config.toml` under `[plugins.trusted_workspace_binaries]`.
3. **Automatic Tamper Detection**: Before loading any workspace plugin at startup, GridLock recomputes its `SHA-256` checksum. If a binary is replaced, rebuilt outside GridLock, or modified by even a single byte, the checksum no longer matches `[plugins.trusted_workspace_binaries]` and GridLock blocks the binary until you review and approve the new fingerprint.

---

## 6. Where Active Plugins Appear in the Workstation

Depending on what a plugin provides, its features integrate into six areas of the GridLock interface:

* **Tools -> Plugins Menu**: Top-level commands and nested submenus (for example, **Tools -> Plugins -> Mesh Tools -> Refinement -> Export VTK**). Built-in GridLock keyboard shortcuts always take precedence if a plugin suggests a conflicting keybinding.
* **Right-Click `Plugin Actions` Menus**: Context-sensitive actions appear under **Plugin Actions** when right-clicking rows or lines in:
  * **Variables** dock (matched against variable C/C++ types such as `PetscVec`, `std::vector<double>`, or `Kokkos::View<double**>`)
  * **Source Editor** (matched against the variable token under the cursor, its resolved type when paused, or selected text, without triggering on function calls)
  * **Breakpoint Gutter** (right-clicking the editor line-number margin)
  * **Call Stack** dock (stack frames)
  * **Server Rack / Rank List** view (individual MPI ranks)
* **Status-Bar Badges**: Clickable status pills on the bottom status bar displaying live metrics or quick-trigger actions.
* **Inline Source Annotations**: Colored line highlights, inline pill badges, and hover tooltips inside the source editor.
* **Plugin Reports Dock**: Structured tabs displaying Markdown summaries, two-column key-value tables, or expandable JSON trees. Plugins can also inspect standard C/C++ contiguous arrays and containers (`std::vector`, `std::array`, `std::span`, `std::valarray`) silently without cluttering the **Evaluator** tab.

---

## 7. Command-Line Verification, Manual Placement & Troubleshooting

### Verifying a Plugin File from the Terminal (`--verify-plugin`)

Before copying a pre-compiled plugin into a plugin folder or publishing a release in CI, you can run GridLock's isolated plugin checker from the command line:

```bash
gridlock --verify-plugin /path/to/libmy_plugin.so
```

This command checks the plugin inside an isolated sandbox without opening a window or touching your personal configuration, prints a `PASS` or `FAIL` summary with any warnings or errors, and exits with code `0` on pass or `1` on failure.

### Manual Plugin Placement

To install a pre-compiled plugin manually (`.so` on Linux, `.dylib` on macOS, `.dll` on Windows):

1. Open **Preferences -> Plugins -> Installed**, choose the target folder in **Plugin Directories**, and click **Open Folder**.
2. Copy the regular shared-library file into `~/.config/gridlock/plugins/` (Global) or `<workspace>/.gridlock/plugins/` (Workspace). Do not use symbolic links pointing outside the plugin directory, and ensure the file is not world-writable (`chmod 755` or `chmod 644`).
3. Click **Reload Plugins** (and **Trust & Approve** -> **Apply** -> **Hot-Reload Now** if placed inside a workspace folder).

### Status & Troubleshooting Reference

| Status Shown in `Preferences -> Plugins` | Cause | Resolution |
| :--- | :--- | :--- |
| `Loaded` | Plugin passed verification and is active in the current session. | None required. |
| `Disabled (Global)` | Unchecked in the **Global** column (`[plugins] disabled_ids`). | Check **Global**, click **Apply**, and click **Hot-Reload Now**. |
| `Disabled (Workspace)` | Unchecked in the **Workspace** column for the open project. | Check **Workspace**, click **Apply**, and click **Hot-Reload Now**. |
| `Restart Required` | Enablement, trust, or binary installation changed during this session. | Click **Hot-Reload Now** to reload in-place, or **Restart GridLock Now** if prompted by a pinned-symbol warning. |
| `Untrusted Workspace Plugin (Approval Required)` | New or modified binary in `<workspace>/.gridlock/plugins/` has no matching `SHA-256` entry in `~/.config/gridlock/config.toml`. | Verify the binary comes from a trusted source, click **Trust & Approve (<sha8>)**, click **Apply**, and click **Hot-Reload Now**. |
| `ABI Rejected: vX.Y` / `ABI Rejected: invalid` | Plugin failed version, descriptor, or entry-point validation. | Run `gridlock --verify-plugin <path>` for detailed diagnostics, or click **Update** / **Install** to rebuild against the current GridLock release. |
| `ABI Rejected: Duplicate plugin ID...` | Two plugin files export the same unique plugin identifier (for example, installed in both Global and Workspace folders). | Remove the duplicate `.so`/`.dylib`/`.dll` from one of the plugin directories and click **Reload Plugins**. |
| `ABI Rejected: Rejected symlink escaping plugin directory` | The plugin file is a symbolic link resolving outside the plugin folder. | Replace the symlink with a regular file copy inside the plugin folder. |
| `ABI Rejected: Rejected world-writable plugin binary` | File or directory permissions allow arbitrary local users to modify the binary (`o+w`). | Restrict permissions with `chmod go-w <path>` and click **Reload Plugins**. |
