#!/usr/bin/env lua
-- scripts/init_plugin.lua
-- Interactive and flag-driven generator for out-of-tree GridLock native plugins.
-- Compatible with Lua 5.1, 5.3, 5.4, and LuaJIT.

local VALID_LICENSES = {
  ["MIT"] = true, ["Apache-2.0"] = true, ["BSD-3-Clause"] = true,
  ["GPL-3.0-or-later"] = true, ["Unlicense"] = true,
}
local VALID_LANGS = { ["c++23"] = true, ["cpp"] = "c++23", ["cxx"] = "c++23", ["c99"] = true, ["c"] = "c99" }
local VALID_BUILDS = { ["meson"] = true, ["cmake"] = true, ["make"] = true }
local FLAG_MAP = {
  name = "name", slug = "slug", id = "id", version = "version",
  description = "description", desc = "description", author = "author",
  license = "license", lang = "lang", build = "build", dir = "dir",
}

local function trim(s)
  if not s then return "" end
  return (s:gsub("^%s+", ""):gsub("%s+$", ""))
end

local function slugify(raw, sep)
  sep = sep or "-"
  local s = trim(raw):lower():gsub("[^a-z0-9]+", sep)
  s = s:gsub("^" .. sep .. "+", ""):gsub(sep .. "+$", "")
  if s == "" then s = "custom" .. sep .. "plugin" end
  if s:match("^[0-9]") then s = "p" .. sep .. s end
  return s
end

local function c_escape(s)
  return (tostring(s or ""):gsub("\\", "\\\\"):gsub('"', '\\"'):gsub("[\r\n\t]", " "))
end

local function shell_quote(path)
  return "'" .. tostring(path):gsub("'", "'\\''") .. "'"
end

local function read_cmd_line(cmd)
  local fh = io.popen(cmd .. " 2>/dev/null", "r")
  if not fh then return "" end
  local line = fh:read("*l") or ""
  fh:close()
  return trim(line)
end

local function ensure_dir(dir_path)
  local code = os.execute("mkdir -p " .. shell_quote(dir_path))
  return code == true or code == 0
end

local function write_text_file(path, contents)
  local fh, err = io.open(path, "wb")
  if not fh then
    io.stderr:write("error: failed to open " .. path .. " for writing: " .. tostring(err) .. "\n")
    os.exit(1)
  end
  fh:write(contents)
  fh:close()
end

local function print_help()
  io.stdout:write([=[
Usage: lua scripts/init_plugin.lua [options]

Scaffolds a ready-to-build out-of-tree GridLock native plugin repository.

Options:
  -y, --yes                 Accept defaults without interactive prompts
      --name <title>        Human-readable plugin title (e.g. "My Domain Inspector")
      --slug <kebab-slug>   Repository / identifier slug (default: derived from --name)
      --id <reverse.dns>    Unique plugin ID (default: io.github.<author>.<slug>)
      --version <semver>    Initial version string (default: 0.1.0)
      --description <text>  One-line capability summary
      --author <name>       Author or organization (default: git config user.name)
      --license <spdx>      MIT | Apache-2.0 | BSD-3-Clause | GPL-3.0-or-later | Unlicense
      --lang <std>          c++23 (default) | c99
      --build <sys>         meson (default) | cmake | make
      --dir <path>          Output directory (default: ./<slug>)
  -h, --help                Show this help text and exit
]=])
end

local function parse_args(argv)
  local opts = { yes = false }
  local i = 1
  while i <= #argv do
    local a = argv[i]
    local key, eq_val = a:match("^%-%-([a-z0-9%-]+)=(.*)$")
    local bare = key or a:match("^%-%-([a-z0-9%-]+)$")
    if a == "-h" or a == "--help" then
      print_help()
      os.exit(0)
    elseif a == "-y" or a == "--yes" then
      opts.yes = true
    elseif bare and FLAG_MAP[bare] then
      local val = eq_val
      if val == nil then
        i = i + 1
        if i > #argv then
          io.stderr:write("error: missing value for --" .. bare .. "\n")
          os.exit(1)
        end
        val = argv[i]
      end
      opts[FLAG_MAP[bare]] = val
    else
      io.stderr:write("error: unknown argument: " .. tostring(a) .. "\n")
      print_help()
      os.exit(1)
    end
    i = i + 1
  end
  return opts
end

local function prompt_value(yes_mode, preset, label, default_val, validator)
  if preset ~= nil and trim(preset) ~= "" then
    local norm = validator and validator(trim(preset)) or trim(preset)
    if not norm then
      io.stderr:write("error: invalid value '" .. tostring(preset) .. "' for " .. label .. "\n")
      os.exit(1)
    end
    return norm
  end
  if yes_mode then
    return validator and validator(default_val) or default_val
  end
  while true do
    io.stdout:write(string.format("%s [%s]: ", label, default_val))
    io.stdout:flush()
    local line = io.read("*l")
    if line == nil then return validator and validator(default_val) or default_val end
    line = trim(line)
    if line == "" then line = default_val end
    local norm = validator and validator(line) or line
    if norm then return norm end
    io.stdout:write("  Please enter a valid option.\n")
  end
end

local function normalize_license(v)
  for k, _ in pairs(VALID_LICENSES) do
    if k:lower() == v:lower() then return k end
  end
  return nil
end

local function normalize_lang(v)
  local hit = VALID_LANGS[v:lower()]
  if hit == true then return v:lower() end
  return type(hit) == "string" and hit or nil
end

local function normalize_build(v)
  local k = v:lower()
  return VALID_BUILDS[k] and k or nil
end

local function render_version_script()
  return "{\n  global:\n    gridlock_plugin_init_v1;\n  local:\n    *;\n};\n"
end

local function render_gitignore()
  return "/build/\n/builddir/\n/*.so\n/*.dylib\n/*.dll\n/*.o\n/compile_commands.json\n/.cache/\n"
end

local function render_license(spdx, year, author)
  local hdr = string.format("SPDX-License-Identifier: %s\nCopyright (c) %s %s\n\n", spdx, year, author)
  if spdx == "Unlicense" then
    return hdr .. "Dedicated to the public domain under The Unlicense.\nFull text: https://spdx.org/licenses/Unlicense.html\n"
  end
  return hdr .. string.format("Licensed under the %s License.\nFull license terms: https://spdx.org/licenses/%s.html\n", spdx, spdx)
end

local function render_cpp_source(cfg)
  return string.format([=[/**
 * @file plugin.cpp
 * @brief %s (%s) - GridLock C++23 plugin scaffold.
 *
 * Quick-Start Map:
 *   1. Add custom per-session fields to `PluginState` below.
 *   2. Implement inspection and UI logic in the `on*` callbacks (`noexcept` is
 *      required across the C ABI boundary so C++ exceptions never unwind into
 *      the GridLock host).
 *   3. Register menu actions, context-menu actions, and status badges in
 *      `gridlock_plugin_init_v1`.
 *   4. Update `plugin_version_utf8` and `description_utf8` in `out_desc`
 *      before publishing or tagging a release.
 */

#include "gridlock/plugin_api.h"
#include "gridlock/plugin_cpp_helpers.hpp"

#include <cstdio>
#include <cstring>
#include <new>

namespace gp = gridlock::plugin;

namespace {

// ---------------------------------------------------------------------------
// 1. Plugin Instance State
// ---------------------------------------------------------------------------
// Allocate `PluginState` with `host->alloc_bytes` and release it in
// `onShutdown` via `host->free_bytes` rather than using mutable file-scope
// globals. Host-managed allocation keeps multiple reloads clean during
// in-place Hot-Reload Now.
struct PluginState {
  const GridLockHostApi* host = nullptr;
  int32_t session_ranks = 0;
  int32_t stop_events = 0;
  int32_t is_dark_theme = 1;
  char accent_hex[16] = "#38bdf8";

  // TODO: Add your own per-session fields here (counters, cached addresses,
  // thresholds, or pointers to host-allocated analysis buffers).
};

// ---------------------------------------------------------------------------
// 2. Menu & Context-Menu Action Callbacks
// ---------------------------------------------------------------------------
// Triggered from Tools -> Plugins -> <Menu Path>.
GridLockStatus GRIDLOCK_CALL onMenuSummary(
    void* plugin_ctx, const GridLockHostApi* host, const GridLockCommandSpec* spec) noexcept {
  auto* st = static_cast<PluginState*>(plugin_ctx);
  if (st == nullptr || host == nullptr || spec == nullptr) return GRIDLOCK_ERR_INVALID_ARG;

  // TODO: Replace this starter summary with your own report content.
  // `host->publish_report_view(...)` supports three declarative formats:
  //   - GRIDLOCK_REPORT_MARKDOWN        : Rich Markdown text document
  //   - GRIDLOCK_REPORT_KEY_VALUE_TABLE : Two-column table (JSON object or `key=value` lines)
  //   - GRIDLOCK_REPORT_JSON_TREE       : Expandable hierarchical JSON tree
  char md[512];
  std::snprintf(md, sizeof(md),
                "### %s\n\n"
                "* **Plugin ID**: `%s`\n"
                "* **Active Rank**: `%%d` / `%%d`\n"
                "* **Recorded Stops**: `%%d`\n"
                "* **Theme**: `%%s` (`%%s`)\n",
                static_cast<int>(spec->active_rank), static_cast<int>(spec->rank_count),
                static_cast<int>(st->stop_events),
                st->is_dark_theme != 0 ? "dark" : "light", st->accent_hex);

  (void)host->publish_report_view(
      host->host_ctx, "%s.report", "%s", GRIDLOCK_REPORT_MARKDOWN, md);
  return host->show_toast(
      host->host_ctx, GRIDLOCK_DIALOG_INFO, "%s", "Updated report panel.", 2500);
}

// Triggered when the user right-clicks an item on a registered surface.
GridLockStatus GRIDLOCK_CALL onInspectContext(
    void* plugin_ctx, const GridLockHostApi* host, const GridLockSurfaceContext* ctx) noexcept {
  if (plugin_ctx == nullptr || host == nullptr || ctx == nullptr) return GRIDLOCK_ERR_INVALID_ARG;

  // TODO: Inspect `ctx->surface` (Variables, Code Editor, Call Stack,
  // Breakpoints, or Rank List), `ctx->rank`, and `ctx->symbol_utf8`.
  // Use `gp::resolveContiguousRange(...)` for zero-inferior-call container
  // inspection, then read target bytes via `host->read_memory(...)` (or
  // `gp::readBytes(...)` / `gp::readPod<T>(...)` from `plugin_cpp_helpers.hpp`).
  if (ctx->symbol_utf8 != nullptr && ctx->rank >= 0) {
    if (const auto rng = gp::resolveContiguousRange(host, ctx->rank, ctx->symbol_utf8); rng.has_value()) {
      char msg[384];
      std::snprintf(msg, sizeof(msg), "%%s [%%llu elements @ 0x%%llx]",
                    rng->container_type_utf8,
                    static_cast<unsigned long long>(rng->element_count),
                    static_cast<unsigned long long>(rng->data_address));
      return host->show_toast(host->host_ctx, GRIDLOCK_DIALOG_INFO, "%s", msg, 3000);
    }
  }
  return host->log_message(host->host_ctx, GRIDLOCK_LOG_INFO, "Context action triggered.");
}

// Triggered when the user clicks this plugin's status-bar badge.
GridLockStatus GRIDLOCK_CALL onStatusClicked(
    void* plugin_ctx, const GridLockHostApi* host, const char* /*item_id_utf8*/) noexcept {
  auto* st = static_cast<PluginState*>(plugin_ctx);
  if (st == nullptr || host == nullptr) return GRIDLOCK_ERR_INVALID_ARG;
  char note[128];
  std::snprintf(note, sizeof(note), "Ranks: %%d, stops observed: %%d",
                static_cast<int>(st->session_ranks), static_cast<int>(st->stop_events));
  return host->show_toast(host->host_ctx, GRIDLOCK_DIALOG_INFO, "%s", note, 2500);
}

// ---------------------------------------------------------------------------
// 3. MPI Debug Session Lifecycle & Theme Hooks
// ---------------------------------------------------------------------------
// Called once when a parallel debug session begins with `rank_count` MPI ranks.
GridLockStatus GRIDLOCK_CALL onSessionStart(
    void* plugin_ctx, const GridLockHostApi* host, int32_t rank_count) noexcept {
  auto* st = static_cast<PluginState*>(plugin_ctx);
  if (st == nullptr || host == nullptr) return GRIDLOCK_ERR_INVALID_ARG;
  st->session_ranks = rank_count;
  st->stop_events = 0;
  // TODO: Reset per-session analysis state or allocate per-rank buffers here.
  return GRIDLOCK_OK;
}

// Called whenever an MPI rank halts (breakpoint, signal, watchpoint, or step).
GridLockStatus GRIDLOCK_CALL onRankStopped(
    void* plugin_ctx, const GridLockHostApi* host, const GridLockStopInfo* info) noexcept {
  auto* st = static_cast<PluginState*>(plugin_ctx);
  if (st == nullptr || host == nullptr || info == nullptr) return GRIDLOCK_ERR_INVALID_ARG;
  st->stop_events += 1;

  // TODO: Inspect `info->rank`, `info->reason_utf8`, `info->file_utf8`, and
  // `info->line` to run automated checks or update inline editor annotations.
  char badge[64];
  std::snprintf(badge, sizeof(badge), "%s: %%d", static_cast<int>(st->stop_events));
  return host->set_status_item(
      host->host_ctx, "%s.status", badge, "%s status badge", "accent", &onStatusClicked, st);
}

// Called when the active MPI debug session terminates or detaches.
GridLockStatus GRIDLOCK_CALL onSessionEnd(void* plugin_ctx, const GridLockHostApi* host) noexcept {
  auto* st = static_cast<PluginState*>(plugin_ctx);
  if (st == nullptr || host == nullptr) return GRIDLOCK_ERR_INVALID_ARG;
  st->session_ranks = 0;
  // TODO: Clear session-scoped annotations or close temporary report views here.
  return GRIDLOCK_OK;
}

void GRIDLOCK_CALL onThemeChanged(void* plugin_ctx, const GridLockThemeInfo* theme) noexcept {
  auto* st = static_cast<PluginState*>(plugin_ctx);
  if (st == nullptr || theme == nullptr) return;
  st->is_dark_theme = theme->is_dark;
  std::memcpy(st->accent_hex, theme->accent_hex, sizeof(st->accent_hex));
  st->accent_hex[sizeof(st->accent_hex) - 1] = '\0';
}

void GRIDLOCK_CALL onShutdown(void* plugin_ctx, const GridLockHostApi* host) noexcept {
  if (plugin_ctx != nullptr && host != nullptr && host->free_bytes != nullptr) {
    static_cast<PluginState*>(plugin_ctx)->~PluginState();
    host->free_bytes(host->host_ctx, plugin_ctx);
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// 4. Single Exported ABI Entry Point (visible in plugin.map)
// ---------------------------------------------------------------------------
extern "C" GRIDLOCK_PLUGIN_EXPORT GridLockStatus GRIDLOCK_CALL gridlock_plugin_init_v1(
    const GridLockHostApi* host, GridLockPluginDescriptor* out_desc) {
  if (const GridLockStatus abi = gp::validateHostAbi(host); abi != GRIDLOCK_OK) return abi;
  if (const GridLockStatus hdr = gp::initPluginDescriptorHeader(out_desc); hdr != GRIDLOCK_OK) return hdr;

  void* raw_mem = host->alloc_bytes(host->host_ctx, sizeof(PluginState));
  if (raw_mem == nullptr) return GRIDLOCK_ERR_BUFFER_TOO_SMALL;
  auto* st = new (raw_mem) PluginState{};
  st->host = host;

  if (const auto theme = gp::getThemeInfo(host); theme.has_value()) {
    onThemeChanged(st, &(*theme));
  }

  // TODO: Customize menu actions, context surfaces, and status-bar badges:
  // - `register_menu_action`: `"MenuName/Action Label"` creates nested items
  //   under Tools -> Plugins (up to 4 path segments).
  // - `register_context_action`: target `GRIDLOCK_SURFACE_VARIABLES`,
  //   `GRIDLOCK_SURFACE_CODE_EDITOR`, `GRIDLOCK_SURFACE_CALL_STACK`,
  //   `GRIDLOCK_SURFACE_BREAKPOINTS`, or `GRIDLOCK_SURFACE_RANK_LIST`, and
  //   narrow the regex filter from `".*"` to a type/symbol pattern (e.g.
  //   `".*(std::vector|MeshField).*"`).
  GridLockStatus rc = host->register_menu_action(
      host->host_ctx, "%s.summary", "%s/Open Summary Report", nullptr, &onMenuSummary, st);
  if (rc == GRIDLOCK_OK) {
    rc = host->register_context_action(
        host->host_ctx, GRIDLOCK_SURFACE_VARIABLES, "%s.inspect_var",
        "Inspect with %s", ".*", &onInspectContext, st);
  }
  if (rc == GRIDLOCK_OK) {
    rc = host->set_status_item(
        host->host_ctx, "%s.status", "%s: Ready", "%s status badge", "accent", &onStatusClicked, st);
  }
  if (rc != GRIDLOCK_OK) {
    st->~PluginState();
    host->free_bytes(host->host_ctx, raw_mem);
    return rc;
  }

  // TODO: Update `plugin_version_utf8` and `description_utf8` before publishing.
  out_desc->plugin_id_utf8 = "%s";
  out_desc->plugin_name_utf8 = "%s";
  out_desc->plugin_version_utf8 = "%s";
  out_desc->description_utf8 = "%s";
  out_desc->plugin_ctx = st;
  out_desc->on_session_start = &onSessionStart;
  out_desc->on_rank_stopped = &onRankStopped;
  out_desc->on_session_end = &onSessionEnd;
  out_desc->shutdown = &onShutdown;
  out_desc->on_theme_changed = &onThemeChanged;
  return GRIDLOCK_OK;
}
]=],
    cfg.c_name, cfg.c_id,
    cfg.c_name, cfg.c_id,
    cfg.c_stem, cfg.c_name, cfg.c_name,
    cfg.c_name,
    cfg.c_name,
    cfg.c_name, cfg.c_stem, cfg.c_name,
    cfg.c_stem, cfg.c_name,
    cfg.c_stem, cfg.c_name,
    cfg.c_stem, cfg.c_name, cfg.c_name,
    cfg.c_id, cfg.c_name, cfg.c_version, cfg.c_desc)
end

local function render_c99_source(cfg)
  return string.format([=[/**
 * @file plugin.c
 * @brief %s (%s) - GridLock pure C99 plugin scaffold.
 *
 * Quick-Start Map:
 *   1. Add custom per-session fields to `PluginState` below.
 *   2. Implement inspection and UI logic in the `on_*` callbacks.
 *   3. Register menu actions, context-menu actions, and status badges in
 *      `gridlock_plugin_init_v1`.
 *   4. Update `plugin_version_utf8` and `description_utf8` in `out_desc`
 *      before publishing or tagging a release.
 */

#include "gridlock/plugin_api.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * 1. Plugin Instance State
 * -------------------------------------------------------------------------
 * Allocate `PluginState` with `host->alloc_bytes` and release it in
 * `on_shutdown` via `host->free_bytes` instead of mutable file-scope globals
 * so in-place Hot-Reload Now unloads and reloads cleanly.
 */
typedef struct PluginState {
  const GridLockHostApi* host;
  int32_t session_ranks;
  int32_t stop_events;
  int32_t is_dark_theme;
  char accent_hex[16];

  /* TODO: Add your own per-session fields here (counters, cached addresses,
   * thresholds, or pointers to host-allocated analysis buffers). */
} PluginState;

/* -------------------------------------------------------------------------
 * 2. Menu & Context-Menu Action Callbacks
 * ------------------------------------------------------------------------- */
static GridLockStatus GRIDLOCK_CALL on_status_clicked(
    void* plugin_ctx, const GridLockHostApi* host, const char* item_id_utf8) {
  PluginState* st = (PluginState*)plugin_ctx;
  char note[128];
  (void)item_id_utf8;
  if (st == NULL || host == NULL) return GRIDLOCK_ERR_INVALID_ARG;
  (void)snprintf(note, sizeof(note), "Ranks: %%d, stops observed: %%d",
                 (int)st->session_ranks, (int)st->stop_events);
  return host->show_toast(host->host_ctx, GRIDLOCK_DIALOG_INFO, "%s", note, 2500);
}

/* Triggered from Tools -> Plugins -> <Menu Path>. */
static GridLockStatus GRIDLOCK_CALL on_menu_summary(
    void* plugin_ctx, const GridLockHostApi* host, const GridLockCommandSpec* spec) {
  PluginState* st = (PluginState*)plugin_ctx;
  char md[512];
  if (st == NULL || host == NULL || spec == NULL) return GRIDLOCK_ERR_INVALID_ARG;

  /* TODO: Replace this starter summary with your own report content.
   * `host->publish_report_view(...)` supports:
   *   - GRIDLOCK_REPORT_MARKDOWN        : Rich Markdown text document
   *   - GRIDLOCK_REPORT_KEY_VALUE_TABLE : Two-column table (JSON or `key=value` lines)
   *   - GRIDLOCK_REPORT_JSON_TREE       : Expandable hierarchical JSON tree */
  (void)snprintf(md, sizeof(md),
                 "### %s\n\n"
                 "* **Plugin ID**: `%s`\n"
                 "* **Active Rank**: `%%d` / `%%d`\n"
                 "* **Recorded Stops**: `%%d`\n"
                 "* **Theme**: `%%s` (`%%s`)\n",
                 (int)spec->active_rank, (int)spec->rank_count,
                 (int)st->stop_events,
                 st->is_dark_theme != 0 ? "dark" : "light", st->accent_hex);

  (void)host->publish_report_view(
      host->host_ctx, "%s.report", "%s", GRIDLOCK_REPORT_MARKDOWN, md);
  return host->show_toast(
      host->host_ctx, GRIDLOCK_DIALOG_INFO, "%s", "Updated report panel.", 2500);
}

/* Triggered when the user right-clicks an item on a registered surface. */
static GridLockStatus GRIDLOCK_CALL on_inspect_context(
    void* plugin_ctx, const GridLockHostApi* host, const GridLockSurfaceContext* ctx) {
  if (plugin_ctx == NULL || host == NULL || ctx == NULL) return GRIDLOCK_ERR_INVALID_ARG;

  /* TODO: Inspect `ctx->surface`, `ctx->rank`, and `ctx->symbol_utf8`.
   * Use `host->resolve_contiguous_range(...)` to locate contiguous buffers
   * without calling inferior functions, then read target bytes with
   * `host->read_memory(...)`. */
  if (ctx->symbol_utf8 != NULL && ctx->rank >= 0) {
    GridLockContiguousRange rng;
    memset(&rng, 0, sizeof(rng));
    rng.struct_size = (uint32_t)sizeof(rng);
    rng.abi_major = GRIDLOCK_PLUGIN_ABI_MAJOR;
    rng.abi_minor = GRIDLOCK_PLUGIN_ABI_MINOR;
    if (host->resolve_contiguous_range(host->host_ctx, ctx->rank, ctx->symbol_utf8, &rng) == GRIDLOCK_OK) {
      char msg[384];
      (void)snprintf(msg, sizeof(msg), "%%s [%%llu elements]",
                     rng.container_type_utf8, (unsigned long long)rng.element_count);
      return host->show_toast(host->host_ctx, GRIDLOCK_DIALOG_INFO, "%s", msg, 3000);
    }
  }
  return host->log_message(host->host_ctx, GRIDLOCK_LOG_INFO, "Context action triggered.");
}

/* -------------------------------------------------------------------------
 * 3. MPI Debug Session Lifecycle & Theme Hooks
 * ------------------------------------------------------------------------- */
static GridLockStatus GRIDLOCK_CALL on_session_start(
    void* plugin_ctx, const GridLockHostApi* host, int32_t rank_count) {
  PluginState* st = (PluginState*)plugin_ctx;
  if (st == NULL || host == NULL) return GRIDLOCK_ERR_INVALID_ARG;
  st->session_ranks = rank_count;
  st->stop_events = 0;
  /* TODO: Reset per-session analysis state here. */
  return GRIDLOCK_OK;
}

static GridLockStatus GRIDLOCK_CALL on_rank_stopped(
    void* plugin_ctx, const GridLockHostApi* host, const GridLockStopInfo* info) {
  PluginState* st = (PluginState*)plugin_ctx;
  char badge[64];
  if (st == NULL || host == NULL || info == NULL) return GRIDLOCK_ERR_INVALID_ARG;
  st->stop_events += 1;

  /* TODO: Inspect `info->rank`, `info->reason_utf8`, `info->file_utf8`, and
   * `info->line` to run automated checks or update inline editor annotations. */
  (void)snprintf(badge, sizeof(badge), "%s: %%d", (int)st->stop_events);
  return host->set_status_item(
      host->host_ctx, "%s.status", badge, "%s status badge", "accent", &on_status_clicked, st);
}

static GridLockStatus GRIDLOCK_CALL on_session_end(void* plugin_ctx, const GridLockHostApi* host) {
  PluginState* st = (PluginState*)plugin_ctx;
  if (st == NULL || host == NULL) return GRIDLOCK_ERR_INVALID_ARG;
  st->session_ranks = 0;
  /* TODO: Clear session-scoped annotations or close temporary report views here. */
  return GRIDLOCK_OK;
}

static void GRIDLOCK_CALL on_theme_changed(void* plugin_ctx, const GridLockThemeInfo* theme) {
  PluginState* st = (PluginState*)plugin_ctx;
  if (st == NULL || theme == NULL) return;
  st->is_dark_theme = theme->is_dark;
  memcpy(st->accent_hex, theme->accent_hex, sizeof(st->accent_hex));
  st->accent_hex[sizeof(st->accent_hex) - 1] = '\0';
}

static void GRIDLOCK_CALL on_shutdown(void* plugin_ctx, const GridLockHostApi* host) {
  if (plugin_ctx != NULL && host != NULL && host->free_bytes != NULL) {
    host->free_bytes(host->host_ctx, plugin_ctx);
  }
}

/* -------------------------------------------------------------------------
 * 4. Single Exported ABI Entry Point (visible in plugin.map)
 * ------------------------------------------------------------------------- */
GRIDLOCK_PLUGIN_EXPORT GridLockStatus GRIDLOCK_CALL gridlock_plugin_init_v1(
    const GridLockHostApi* host, GridLockPluginDescriptor* out_desc) {
  PluginState* st = NULL;
  GridLockThemeInfo theme;
  GridLockStatus rc = GRIDLOCK_OK;

  if (host == NULL || out_desc == NULL) return GRIDLOCK_ERR_INVALID_ARG;
  if (host->abi_major != GRIDLOCK_PLUGIN_ABI_MAJOR ||
      host->struct_size < offsetof(GridLockHostApi, reserved)) {
    return GRIDLOCK_ERR_ABI_MISMATCH;
  }
  if (out_desc->struct_size != 0u && out_desc->struct_size < sizeof(GridLockPluginDescriptor)) {
    return GRIDLOCK_ERR_ABI_MISMATCH;
  }

  st = (PluginState*)host->alloc_bytes(host->host_ctx, sizeof(PluginState));
  if (st == NULL) return GRIDLOCK_ERR_BUFFER_TOO_SMALL;
  memset(st, 0, sizeof(*st));
  st->host = host;
  st->is_dark_theme = 1;
  (void)snprintf(st->accent_hex, sizeof(st->accent_hex), "%%s", "#38bdf8");

  memset(&theme, 0, sizeof(theme));
  theme.struct_size = (uint32_t)sizeof(theme);
  theme.abi_major = GRIDLOCK_PLUGIN_ABI_MAJOR;
  theme.abi_minor = GRIDLOCK_PLUGIN_ABI_MINOR;
  if (host->get_theme_info(host->host_ctx, &theme) == GRIDLOCK_OK) {
    on_theme_changed(st, &theme);
  }

  /* TODO: Customize menu actions, context surfaces, and status-bar badges:
   * - `register_menu_action`: `"MenuName/Action Label"` creates nested items
   *   under Tools -> Plugins (up to 4 path segments).
   * - `register_context_action`: target `GRIDLOCK_SURFACE_VARIABLES`,
   *   `GRIDLOCK_SURFACE_CODE_EDITOR`, `GRIDLOCK_SURFACE_CALL_STACK`,
   *   `GRIDLOCK_SURFACE_BREAKPOINTS`, or `GRIDLOCK_SURFACE_RANK_LIST`, and
   *   narrow the regex filter from `".*"` to a type/symbol pattern. */
  rc = host->register_menu_action(
      host->host_ctx, "%s.summary", "%s/Open Summary Report", NULL, &on_menu_summary, st);
  if (rc == GRIDLOCK_OK) {
    rc = host->register_context_action(
        host->host_ctx, GRIDLOCK_SURFACE_VARIABLES, "%s.inspect_var",
        "Inspect with %s", ".*", &on_inspect_context, st);
  }
  if (rc == GRIDLOCK_OK) {
    rc = host->set_status_item(
        host->host_ctx, "%s.status", "%s: Ready", "%s status badge", "accent", &on_status_clicked, st);
  }
  if (rc != GRIDLOCK_OK) {
    host->free_bytes(host->host_ctx, st);
    return rc;
  }

  /* TODO: Update `plugin_version_utf8` and `description_utf8` before publishing. */
  memset(out_desc, 0, sizeof(*out_desc));
  out_desc->struct_size = (uint32_t)sizeof(GridLockPluginDescriptor);
  out_desc->abi_major = GRIDLOCK_PLUGIN_ABI_MAJOR;
  out_desc->abi_minor = GRIDLOCK_PLUGIN_ABI_MINOR;
  out_desc->plugin_id_utf8 = "%s";
  out_desc->plugin_name_utf8 = "%s";
  out_desc->plugin_version_utf8 = "%s";
  out_desc->description_utf8 = "%s";
  out_desc->plugin_ctx = st;
  out_desc->on_session_start = &on_session_start;
  out_desc->on_rank_stopped = &on_rank_stopped;
  out_desc->on_session_end = &on_session_end;
  out_desc->shutdown = &on_shutdown;
  out_desc->on_theme_changed = &on_theme_changed;
  return GRIDLOCK_OK;
}
]=],
    cfg.c_name, cfg.c_id,
    cfg.c_name,
    cfg.c_name, cfg.c_id,
    cfg.c_stem, cfg.c_name, cfg.c_name,
    cfg.c_name,
    cfg.c_name, cfg.c_stem, cfg.c_name,
    cfg.c_stem, cfg.c_name,
    cfg.c_stem, cfg.c_name,
    cfg.c_stem, cfg.c_name, cfg.c_name,
    cfg.c_id, cfg.c_name, cfg.c_version, cfg.c_desc)
end

local function render_meson_files(cfg)
  local meson_lang = (cfg.lang == "c++23") and "cpp" or "c"
  local std_opt = (cfg.lang == "c++23") and "cpp_std=c++23" or "c_std=c99"
  local src_rel = (cfg.lang == "c++23") and "src/plugin.cpp" or "src/plugin.c"
  local extra_arg = (cfg.lang == "c++23") and "cpp_args: ['-fvisibility-inlines-hidden'],\n  " or ""
  local options_txt = "option('gridlock_include_dir',\n  type: 'string',\n  value: '/usr/local/include',\n  description: 'Directory containing gridlock/plugin_api.h')\n"
  local build_txt = string.format([=[project('%s', '%s',
  version: '%s',
  license: '%s',
  default_options: ['%s', 'warning_level=3'])

inc = include_directories(get_option('gridlock_include_dir'))
map_file = meson.current_source_dir() / 'plugin.map'
ld_args = host_machine.system() == 'linux' ? ['-Wl,--version-script=' + map_file] : []

shared_module('%s',
  '%s',
  include_directories: inc,
  %sgnu_symbol_visibility: 'hidden',
  link_args: ld_args,
  name_prefix: 'lib',
  install: true)
]=], cfg.target_name, meson_lang, cfg.version, cfg.license, std_opt, cfg.target_name, src_rel, extra_arg)
  return build_txt, options_txt
end

local function render_cmake_file(cfg)
  local is_cpp = (cfg.lang == "c++23")
  local cmake_lang = is_cpp and "CXX" or "C"
  local src_rel = is_cpp and "src/plugin.cpp" or "src/plugin.c"
  local feat = is_cpp and "cxx_std_23" or "c_std_99"
  local vis = is_cpp and "  CXX_VISIBILITY_PRESET hidden\n  VISIBILITY_INLINES_HIDDEN ON" or "  C_VISIBILITY_PRESET hidden"
  return string.format([=[cmake_minimum_required(VERSION 3.22)
project(%s VERSION %s LANGUAGES %s)

set(GRIDLOCK_INCLUDE_DIR "/usr/local/include" CACHE PATH "Directory containing gridlock/plugin_api.h")

add_library(%s MODULE %s)
target_compile_features(%s PRIVATE %s)
target_include_directories(%s PRIVATE "${GRIDLOCK_INCLUDE_DIR}")
target_compile_options(%s PRIVATE -Wall -Wextra -Wpedantic)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_options(%s PRIVATE "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/plugin.map")
endif()

set_target_properties(%s PROPERTIES
%s
  PREFIX "lib"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
]=], cfg.target_name, cfg.version, cmake_lang, cfg.target_name, src_rel,
    cfg.target_name, feat, cfg.target_name, cfg.target_name, cfg.target_name, cfg.target_name, vis)
end

local function render_makefile(cfg)
  local is_cpp = (cfg.lang == "c++23")
  local compiler_var = is_cpp and "CXX ?= c++" or "CC ?= cc"
  local flags_var = is_cpp
    and "CXXFLAGS ?= -std=c++23 -O2 -Wall -Wextra -Wpedantic -fvisibility=hidden -fvisibility-inlines-hidden -fPIC"
    or "CFLAGS ?= -std=c99 -O2 -Wall -Wextra -Wpedantic -fvisibility=hidden -fPIC"
  local extra_var = is_cpp and "EXTRA_CXXFLAGS" or "EXTRA_CFLAGS"
  local extra_example = is_cpp
    and "# EXTRA_CXXFLAGS += -I/path/to/third_party/include -DMY_FEATURE=1"
    or "# EXTRA_CFLAGS += -I/path/to/third_party/include -DMY_FEATURE=1"
  local invoke = is_cpp and "$(CXX) $(CXXFLAGS) $(EXTRA_CXXFLAGS)" or "$(CC) $(CFLAGS) $(EXTRA_CFLAGS)"
  local src_rel = is_cpp and "src/plugin.cpp" or "src/plugin.c"
  local extra_src_hint = is_cpp and "src/analyzer.cpp" or "src/analyzer.c"
  return string.format([=[# GridLock native plugin Makefile
%s

# Path to the directory containing `gridlock/plugin_api.h`:
# - Local GridLock checkout: make GRIDLOCK_INCLUDE_DIR=/path/to/GridLock/include
# - System-wide install    : /usr/local/include (default)
GRIDLOCK_INCLUDE_DIR ?= /usr/local/include

%s

# Append additional source files here as your plugin grows (e.g. %s):
SRCS := %s

# Optional third-party compiler flags and libraries:
%s
# LDLIBS += -lm

# `plugin.map` hides all symbols except `gridlock_plugin_init_v1` on Linux to
# prevent symbol collisions and `STB_GNU_UNIQUE` pinning during dlopen hot-reload.
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
  SO_EXT := .dylib
  MAP_FLAGS :=
else
  SO_EXT := .so
  MAP_FLAGS := -Wl,--version-script=plugin.map
endif

TARGET := build/lib%s$(SO_EXT)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRCS) plugin.map
	@mkdir -p build
	%s -shared -I"$(GRIDLOCK_INCLUDE_DIR)" $(SRCS) $(MAP_FLAGS) $(LDLIBS) -o $@

clean:
	rm -rf build
]=], compiler_var, flags_var, extra_src_hint, src_rel, extra_example, cfg.target_name, invoke)
end

local function build_commands_for(cfg)
  local so_path = "build/lib" .. cfg.target_name .. ".so"
  if cfg.build == "meson" then
    return {
      "meson setup build -Dgridlock_include_dir=/path/to/GridLock/include",
      "ninja -C build",
      "gridlock --verify-plugin " .. so_path,
    }
  elseif cfg.build == "cmake" then
    return {
      "cmake -S . -B build -DGRIDLOCK_INCLUDE_DIR=/path/to/GridLock/include",
      "cmake --build build",
      "gridlock --verify-plugin " .. so_path,
    }
  end
  return {
    "make GRIDLOCK_INCLUDE_DIR=/path/to/GridLock/include",
    "gridlock --verify-plugin " .. so_path,
  }
end

local function render_readme(cfg)
  local cmds = build_commands_for(cfg)
  local src_rel = (cfg.lang == "c++23") and "src/plugin.cpp" or "src/plugin.c"
  local build_files = "Makefile"
  local rebuild_cmd = "make GRIDLOCK_INCLUDE_DIR=/path/to/GridLock/include"
  if cfg.build == "meson" then
    build_files = "meson.build`, `meson_options.txt"
    rebuild_cmd = "ninja -C build"
  elseif cfg.build == "cmake" then
    build_files = "CMakeLists.txt"
    rebuild_cmd = "cmake --build build"
  end
  local so_path = "build/lib" .. cfg.target_name .. ".so"
  return string.format([=[# %s

%s

* **Plugin ID**: `%s`
* **Version**: `%s`
* **Language / Build**: `%s` (`%s`)
* **License**: `%s`

## Project Structure & Where to Add Your Code

| File | Purpose |
| :--- | :--- |
| `%s` | Main plugin source: `PluginState`, `on*` lifecycle/UI callbacks, and `gridlock_plugin_init_v1` registration entry point. Look for `TODO:` markers to plug in your own inspection logic. |
| `%s` | Build configuration: set `GRIDLOCK_INCLUDE_DIR`, add extra source files, or link third-party libraries. |
| `plugin.map` | GNU/LLVM linker version script that exports only `gridlock_plugin_init_v1` so symbol collisions and `STB_GNU_UNIQUE` pinning do not block hot-reload. |

## Building & Verifying Locally

```bash
%s
```

## Local Development & Hot-Reload Loop

1. Edit `%s` (add state to `PluginState`, implement inspection logic in `onInspectContext` / `onRankStopped`, or customize menu/report output).
2. Rebuild the shared library:
   ```bash
   %s
   ```
3. Verify ABI headers, exported symbol hygiene, and zero allocator leaks:
   ```bash
   gridlock --verify-plugin %s
   ```
4. Copy or symlink `%s` into `~/.config/gridlock/plugins/` (or `<workspace>/.gridlock/plugins/`) and click **Hot-Reload Now** in **Preferences -> Plugins** to reload without ending your active MPI debug session.

## Declarative Installation (`plugins.toml`)

Add the following entry to `~/.config/gridlock/plugins.toml` (Global) or `<workspace>/.gridlock/plugins.toml` (Workspace):

```toml
[[plugins]]
repo = "%s/%s"
ref = "main"
build = "%s"
enabled = true
```
]=], cfg.name, cfg.description, cfg.id, cfg.version, cfg.lang, cfg.build, cfg.license,
    src_rel, build_files, table.concat(cmds, "\n"), src_rel, rebuild_cmd, so_path, so_path,
    cfg.author_slug, cfg.slug, cfg.build)
end

local function main(argv)
  local opts = parse_args(argv)
  local git_author = read_cmd_line("git config user.name")
  if git_author == "" then git_author = os.getenv("USER") or "developer" end

  if not opts.yes then
    io.stdout:write("GridLock Plugin Project Generator (ABI v1.0)\n")
    io.stdout:write("--------------------------------------------\n")
  end

  local name = prompt_value(opts.yes, opts.name, "1. Plugin Name", "My Domain Inspector")
  local slug = opts.slug and slugify(opts.slug, "-") or slugify(name, "-")
  local seed_author = slugify((opts.author and trim(opts.author) ~= "") and opts.author or git_author, "")
  local default_id = string.format("io.github.%s.%s", seed_author, slug)
  local plugin_id = prompt_value(opts.yes, opts.id, "2. Plugin Reverse-DNS ID", default_id)
  local version = prompt_value(opts.yes, opts.version, "3. Initial Version", "0.1.0")
  local description = prompt_value(
    opts.yes, opts.description, "4. Description", "Custom MPI domain inspection extension for GridLock.")
  local author = prompt_value(opts.yes, opts.author, "5. Author", git_author)
  local license = prompt_value(
    opts.yes, opts.license, "6. License (MIT / Apache-2.0 / BSD-3-Clause / GPL-3.0-or-later / Unlicense)",
    "MIT", normalize_license)
  local lang = prompt_value(opts.yes, opts.lang, "7. Language (c++23 / c99)", "c++23", normalize_lang)
  local build = prompt_value(opts.yes, opts.build, "8. Build System (meson / cmake / make)", "meson", normalize_build)
  local out_dir = prompt_value(opts.yes, opts.dir, "9. Output Directory", "./" .. slug)

  local c_stem = slugify(slug, "_")
  local cfg = {
    name = name, slug = slug, c_stem = c_stem, target_name = "gridlock_" .. c_stem,
    id = plugin_id, version = version, description = description,
    author = author, author_slug = slugify(author, ""), license = license,
    lang = lang, build = build, out_dir = out_dir,
    c_name = c_escape(name), c_id = c_escape(plugin_id),
    c_version = c_escape(version), c_desc = c_escape(description),
  }

  if not ensure_dir(out_dir .. "/src") then
    io.stderr:write("error: failed to create output directory: " .. out_dir .. "\n")
    os.exit(1)
  end

  local created = {}
  local function emit_file(rel_path, content)
    local full = out_dir .. "/" .. rel_path
    write_text_file(full, content)
    created[#created + 1] = full
  end

  emit_file(lang == "c++23" and "src/plugin.cpp" or "src/plugin.c",
            lang == "c++23" and render_cpp_source(cfg) or render_c99_source(cfg))
  emit_file("plugin.map", render_version_script())

  if build == "meson" then
    local meson_build, meson_opts = render_meson_files(cfg)
    emit_file("meson.build", meson_build)
    emit_file("meson_options.txt", meson_opts)
  elseif build == "cmake" then
    emit_file("CMakeLists.txt", render_cmake_file(cfg))
  else
    emit_file("Makefile", render_makefile(cfg))
  end

  emit_file("README.md", render_readme(cfg))
  emit_file("LICENSE", render_license(license, os.date("%Y") or "2026", author))
  emit_file(".gitignore", render_gitignore())

  io.stdout:write("\nScaffolded GridLock plugin '" .. cfg.name .. "' (" .. cfg.id .. ") in " .. out_dir .. ":\n")
  for _, f in ipairs(created) do io.stdout:write("  + " .. f .. "\n") end
  io.stdout:write("\nNext steps:\n  cd " .. out_dir .. "\n")
  for _, cmd in ipairs(build_commands_for(cfg)) do io.stdout:write("  " .. cmd .. "\n") end
end

main(arg or {})
