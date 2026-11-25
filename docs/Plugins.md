# Using Plugins in GridLock

Plugins extend GridLock with custom inspection tools, solver-specific visualizations, custom menus, status-bar indicators, source annotations, and structured report tabs without modifying the core application. You can install plugins directly from Git repositories inside **Preferences -> Plugins**, declare a reproducible plugin list in `plugins.toml`, scaffold a new plugin with [`scripts/init_plugin.lua`](../scripts/init_plugin.lua), or drop pre-built plugin files into your global or project plugin folder.

> [!TIP]
> Building your own solver or mesh extension? Run `lua scripts/init_plugin.lua` to generate a ready-to-build C++23 or C99 plugin project in seconds, or see [Plugin Authoring, UI Surfaces & Build Recipes](Plugin-Authoring-and-UI-Surfaces.md).

---

## 1. Discovering & Finding Plugins

GridLock uses a decentralized distribution model so teams and scientific computing communities can publish plugins on any Git server:

* **`gridlock-plugin` Repository Topic**: Browse community extensions tagged with `gridlock-plugin` on GitHub (`https://github.com/topics/gridlock-plugin`), GitLab, Codeberg, or institutional Gitea instances.
* **Curated `awesome-gridlock` Lists**: Community-maintained `awesome-gridlock` indexes catalog plugins by scientific domain (for example, PETSc/Trilinos vector and matrix viewers, Kokkos field inspectors, STL container viewers, mesh refinement exporters, and MPI rank health monitors).
* **Supported Git Hosts & Shorthand Formats**:
  * **GitHub (`github.com`)**: Shorthand `owner/repo` or `github:owner/repo`
  * **GitLab (`gitlab.com`)**: Shorthand `owner/repo`, nested subgroups `group/subgroup/repo` (`2..8` path segments), or `gitlab:group/subgroup/repo`
  * **Codeberg (`codeberg.org`)**: Shorthand `owner/repo` or `codeberg:owner/repo`
  * **Gitea & Self-Hosted Git Servers**: Any `https://<host>[:<port>]/<path>.git` or `git@<host>:<path>.git` repository URL

---

## 2. Installing Plugins via Preferences

To clone, build, verify, and install a plugin from source inside GridLock:

1. Open **Edit -> Preferences** (or press `Ctrl+,`) and select **Plugins** in the left category tree.
2. Switch to the **Install from Git** tab (`#pluginInstallTab`).
3. Choose the source forge in **Git Host** (`#pluginGitHostCombo`):
   * **GitHub (`github.com`)**
   * **GitLab (`gitlab.com`)**
   * **Codeberg (`codeberg.org`)**
   * **Custom Git URL (`https://` / `git@`)**
4. Enter the repository in **Git Host & Repository** (`#pluginGitRepoEdit`):
   * For **GitHub**, **GitLab**, or **Codeberg**, enter compact shorthand such as `bnorthern42/gridlock-stl-inspector` (or paste an explicit `github:`, `gitlab:`, `codeberg:`, `https://`, or `git@` URL, which GridLock honors regardless of the selected dropdown item).
   * For **Custom Git URL**, enter a complete HTTPS or SSH repository address such as `https://gitea.example.org/lab/mesh-inspector.git` or `git@gitea.example.org:lab/mesh-inspector.git`.
5. *(Optional)* Enter a branch name, release tag, or commit hash in **Optional branch/tag...** (`#pluginGithubRefEdit`, for example `main` or `v1.2.0`). Leave it blank to build the repository's default branch.
6. Select the target **Install Scope** (`#pluginInstallScopeCombo`):
   * **Global (`~/.config/gridlock/plugins`)**: Makes the plugin available across all projects for your user account.
   * **This Workspace (`<workspace>/.gridlock/plugins`)**: Installs the plugin only for the currently open project folder and automatically records `SHA-256` trust approval for the built binary.
7. Click **Install** (`#pluginCloneAndBuildBtn`, which toggles to **Cancel** while a build is active).
   * GridLock clones the repository into an isolated temporary staging folder, detects the build system (`meson.build`, `CMakeLists.txt`, `Makefile`, or standalone C99/C++23 sources), compiles the plugin, and runs an isolated post-build conformance check (`PluginVerifier`) before installing the binary atomically.
   * Live compiler output streams with semantic color highlighting: `#38bdf8` for `==>` pipeline stage headers, `#f59e0b` for compiler warnings, and `#ef4444` for compiler, linker, or conformance errors.
8. When installation finishes, click **Hot-Reload Now** (`#pluginHotReloadBtn`) on the `#d97706` notification banner to activate the plugin immediately without closing your open workspace or active paused MPI debug session, or click **Restart GridLock Now** for a full application restart.

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
| `enabled` | No | `true` | Set to `false` to skip this entry during **Sync `plugins.toml`** without deleting it from the file. |

### Editing, Syncing & Lockfile Pinning

* **Open `plugins.toml`** (`#pluginOpenManifestBtn`): In **Preferences -> Plugins -> Install from Git**, select **Global** or **This Workspace** and click **Open `plugins.toml`**. GridLock creates a starter template if the file does not exist yet and opens it directly in the source editor.
* **Automatic Recording**: Installing a plugin via **Install** automatically adds or updates its `[[plugins]]` entry in the selected scope's `plugins.toml`.
* **Sync `plugins.toml`** (`#pluginSyncManifestBtn`): Clicking **Sync `plugins.toml`** checks every enabled entry against `~/.config/gridlock/plugins-lock.toml` and remote Git commits (`git ls-remote`), builds and verifies only missing or outdated plugins sequentially, and shows the **Hot-Reload Now** / **Restart GridLock Now** banner when binaries were built or updated.

> [!IMPORTANT]
> GridLock writes resolved 40-character Git commit hashes (`commit_sha`) and binary `SHA-256` checksums (`binary_sha256`) exclusively to your user-global `~/.config/gridlock/plugins-lock.toml`. Lockfile entries are never read from an untrusted workspace directory.

---

## 4. Managing, Updating & Hot-Reloading Installed Plugins

The **Preferences -> Plugins -> Installed** tab (`#pluginInstalledTab`) displays every discovered plugin in an 8-column table (`#pluginsTableWidget`): **Global**, **Workspace**, **Name**, **Version**, **Scope / ABI**, **Status**, **Trust**, and **Update**.

* **`Version (<commit_short>)` & Provenance Tooltip**: The **Version** column pairs each plugin's declared semantic version (`plugin_version_utf8`) with its 7-character Git commit hash from `~/.config/gridlock/plugins-lock.toml` (for example, `0.1.0 (a1b2c3d)`, or `-` / `0.1.0` for local manually placed binaries). Hovering any cell displays the canonical path, full `SHA-256` digest, Git repository URL, ref, and 40-character commit SHA.
* **Toolbar Controls**: Use **Plugin Directories** (`#pluginSearchDirsCombo`) + **Open Folder** (`#openPluginFolderBtn`) to inspect `~/.config/gridlock/plugins` or `<workspace>/.gridlock/plugins`, **Reload Plugins** (`#reloadPluginsBtn`) to rescan directories, and **Update All** (`#pluginUpdateAllBtn`) to update every Git-backed plugin.

### In-Place Updates & Live Hot-Reload

* **Per-Row `Update` (`#pluginUpdateBtn_<id>`) & Toolbar `Update All` (`#pluginUpdateAllBtn`)**: Click **Update** on any Git-installed plugin row or **Update All** in the toolbar to re-clone, rebuild, verify, and atomically replace the binary in its existing Global or Workspace scope (preserving previously granted Workspace trust). Local binaries without a Git source show a disabled **Update** button with a `Local binary (no Git source)` tooltip.
* **Hot-Reload Now** (`#pluginHotReloadBtn`): After installing, updating, syncing `plugins.toml`, or applying checkbox/trust changes, click **Hot-Reload Now** on the banner (shown in both **Preferences -> Plugins** and the main window `#mainWindowPluginRestartBanner`). GridLock cleanly unloads previous plugin handles, loads updated `.so`/`.dylib`/`.dll` binaries from disk, refreshes menus, status badges, annotations, and report panels, and replays `on_session_start(rankCount)` onto newly loaded plugins so paused multi-rank MPI debug sessions continue uninterrupted.

> [!WARNING]
> **Linux `STB_GNU_UNIQUE` Fallback Notice**: On Linux (`glibc`), C++ plugins compiled without `-Wl,--version-script` (or `-fno-gnu-unique`) may export `STB_GNU_UNIQUE` (`'u'`) dynamic symbols that cause `dlclose()` to keep old code pages pinned in process memory until exit. If GridLock detects `STB_GNU_UNIQUE` symbols (or an active plugin callback frame on the call stack) when you click **Hot-Reload Now**, it displays an explicit warning banner prompting you to click **Restart GridLock Now** so the updated binary loads into a fresh process address space. Plugins scaffolded with `lua scripts/init_plugin.lua` include `plugin.map` by default so hot-reloading always unmaps cleanly.

### Staged Edits (`Apply` vs `Cancel`)

* Toggling **Global** (`#pluginGlobalCheck_<id>`), **Workspace** (`#pluginWorkspaceCheck_<id>`), or **Trust & Approve** (`#pluginTrustBtn_<id>`) stages your changes in memory and updates the **Status** preview (`Staged: ... (Apply to save)`) without modifying configuration files immediately.
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

Switching projects via **File -> Open Workspace / Folder** automatically re-evaluates workspace enablement and workspace plugins for the newly opened folder.

---

## 5. Workspace Security & SHA-256 Trust Gate

> [!CAUTION]
> Opening an untrusted repository downloaded from the internet never executes code from `<workspace>/.gridlock/plugins/` or `<workspace>/.gridlock/plugins.toml`. Review workspace plugin sources before clicking **Trust & Approve** or running **Sync `plugins.toml`** under **This Workspace**.

GridLock enforces three safeguards on workspace-scoped plugins:

1. **Zero Automatic Execution Prior to Trust**: Untrusted shared libraries inside `<workspace>/.gridlock/plugins/` are hashed before `dlopen()`, so even C++ static constructors (`.init_array`) cannot run until you explicitly approve the binary. Likewise, `<workspace>/.gridlock/plugins.toml` runs only when you explicitly select **This Workspace** and click **Sync `plugins.toml`**.
2. **User-Owned Trust Registry (`config.toml`)**: When you click **Trust & Approve (<sha8>)** and **Apply** (or build a workspace plugin yourself via **Install** / **Sync `plugins.toml`**), GridLock stores the canonical file path and 64-character lowercase `SHA-256` fingerprint in your personal `~/.config/gridlock/config.toml` under `[plugins.trusted_workspace_binaries]`.
3. **Automatic Tamper Detection**: Before loading any workspace plugin, GridLock recomputes its `SHA-256` checksum. Modifying or replacing a binary by even a single byte invalidates trust immediately until you review and approve the new fingerprint.

---

## 6. Where Active Plugins Appear in the Workstation

Depending on what a plugin provides, its features integrate into six areas of the GridLock interface:

* **Tools -> Plugins Menu**: Top-level commands and nested submenus up to 4 levels deep (for example, **Tools -> Plugins -> Mesh Tools -> Refinement -> Export VTK**). Built-in GridLock keyboard shortcuts always take precedence if a plugin suggests a conflicting keybinding.
* **Right-Click `Plugin Actions` Menus**: Context-sensitive actions appear under **Plugin Actions** when right-clicking rows or lines across all 5 surfaces:
  * **Variables** dock (matched against variable C/C++ types such as `PetscVec`, `std::vector<double>`, or `Kokkos::View<double**>`)
  * **Source Editor** (matched against the variable expression under the cursor, its resolved DWARF type when paused, or selected text, ignoring C++ member function calls like `obj.method(...)`)
  * **Call Stack** dock (stack frames)
  * **Breakpoint Gutter** (right-clicking the editor line-number margin)
  * **Server Rack / Rank List** view (individual MPI rank cards)
* **Status-Bar Badges**: Clickable status pills on the bottom status bar styled with theme tokens (`accent` `#38bdf8`, `warning` `#f59e0b`, `error` `#ef4444`, or custom `#RRGGBB` hex codes).
* **Inline Source Annotations**: Colored line highlights, inline pill badges, and hover tooltips inside the source editor.
* **Plugin Reports Dock**: Closable structured tabs rendering Markdown summaries, two-column key-value tables, or expandable JSON trees. Plugins also inspect C/C++ contiguous containers (`T[N]`, `std::array`, `std::vector`, `std::span`, `std::valarray`) via zero-call DWARF introspection without polluting your **Evaluator** tab history.

---

## 7. Command-Line Verification, Manual Placement & Troubleshooting

### Verifying a Plugin File from the Terminal (`--verify-plugin`)

Before copying a pre-compiled plugin into a plugin folder or publishing a release in CI, run GridLock's isolated conformance verifier from the terminal:

```bash
gridlock --verify-plugin /path/to/libmy_plugin.so
```

> [!NOTE]
> `gridlock --verify-plugin <path.so>` runs headlessly inside a temporary sandbox without opening a window or touching `~/.config/gridlock/`. It validates dynamic symbol hygiene (`gridlock_plugin_init_v1` visibility and `STB_GNU_UNIQUE` checks), ABI v1.0 descriptor headers, menu/regex registrations, dry-run session lifecycle hooks, and zero leaked `alloc_bytes` allocations (`exit 0` on `[PASS]`, `exit 1` on `[FAIL]`).

### Scaffolding or Manually Placing Plugins

To scaffold a new out-of-tree plugin repository with build files, `plugin.map` symbol hiding, and starter UI hooks, run `lua scripts/init_plugin.lua` (see [Plugin Authoring, UI Surfaces & Build Recipes](Plugin-Authoring-and-UI-Surfaces.md#0-quick-start-scaffolding-a-new-plugin-scriptsinit_pluginlua)).

To install a pre-compiled plugin manually (`.so` on Linux, `.dylib` on macOS, `.dll` on Windows):

1. Open **Preferences -> Plugins -> Installed**, choose the target folder in **Plugin Directories**, and click **Open Folder**.
2. Copy the regular shared-library file into `~/.config/gridlock/plugins/` (Global) or `<workspace>/.gridlock/plugins/` (Workspace). Do not use symbolic links pointing outside the plugin directory, and ensure neither the directory nor the file is world-writable (`chmod 755` or `chmod 644`).
3. Click **Reload Plugins** (and **Trust & Approve** -> **Apply** -> **Hot-Reload Now** if placed inside a workspace folder).

### Status & Troubleshooting Reference

| Status Shown in `Preferences -> Plugins` | Cause | Resolution |
| :--- | :--- | :--- |
| `Loaded` | Plugin passed verification and is active in the current session. | None required. |
| `Disabled (Global)` | Unchecked in the **Global** column (`[plugins] disabled_ids`). | Check **Global**, click **Apply**, and click **Hot-Reload Now**. |
| `Disabled (Workspace)` | Unchecked in the **Workspace** column for the open project. | Check **Workspace**, click **Apply**, and click **Hot-Reload Now**. |
| `Restart Required` | Enablement, trust, or binary installation changed during this session. | Click **Hot-Reload Now** to reload in-place, or **Restart GridLock Now** if prompted by an `STB_GNU_UNIQUE` pinned-symbol warning. |
| `Untrusted Workspace Plugin (Approval Required)` | New or modified binary in `<workspace>/.gridlock/plugins/` has no matching `SHA-256` entry in `~/.config/gridlock/config.toml`. | Verify the binary comes from a trusted source, click **Trust & Approve (<sha8>)**, click **Apply**, and click **Hot-Reload Now**. |
| `ABI Rejected: vX.Y` / `ABI Rejected: invalid` | Plugin failed version, descriptor, or entry-point validation. | Run `gridlock --verify-plugin <path>` for detailed diagnostics, or click **Update** / **Install** to rebuild against the current GridLock release. |
| `ABI Rejected: Duplicate plugin ID...` | Two plugin files export the same unique plugin identifier (for example, installed in both Global and Workspace folders). | Remove the duplicate `.so`/`.dylib`/`.dll` from one of the plugin directories and click **Reload Plugins**. |
| `ABI Rejected: Rejected symlink escaping plugin directory` | The plugin file is a symbolic link resolving outside the plugin folder. | Replace the symlink with a regular file copy inside the plugin folder. |
| `ABI Rejected: Rejected world-writable plugin binary` | File or directory permissions allow arbitrary local users to modify the binary (`o+w`). | Restrict permissions with `chmod go-w <path>` and click **Reload Plugins**. |
