/**
 * @file test_dummy_plugin.c
 * @brief Pure C99 test shared-library plugin exercising the complete GridLock ABI v1.0 surface.
 *
 * Compiles into two Meson `shared_module` test artifacts (`test_dummy_plugin` and
 * `test_bad_abi_plugin`), includes an optional ELF constructor canary gated on
 * `GRIDLOCK_TEST_CTOR_MARKER_FILE` to verify untrusted workspace binaries never run
 * `.init_array` constructors prior to SHA-256 approval, and exercises dialogs, prompts,
 * toasts, nested slash menus, status-bar items, all 5 context-menu surfaces, editor
 * navigation/annotations, theme queries/broadcasts, declarative reports, and shutdown lockout.
 */

#include "gridlock/plugin_api.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DUMMY_MAX_EVAL_BYTES ((size_t)16u * 1024u * 1024u)

#if !defined(GRIDLOCK_MESON_PLUGIN_BUILD)
/* Emits a deterministic -Wunused-variable diagnostic when built out-of-tree by PluginInstaller. */
static int dummy_installer_warning_probe = 42;
#endif

#if defined(__GNUC__) || defined(__clang__)
__attribute__((constructor))
static void dummy_elf_constructor_canary(void) {
  const char* marker_path = getenv("GRIDLOCK_TEST_CTOR_MARKER_FILE");
  if (marker_path != NULL && marker_path[0] != '\0') {
    FILE* fp = fopen(marker_path, "wb");
    if (fp != NULL) {
      (void)fputs("elf_ctor_executed\n", fp);
      (void)fclose(fp);
    }
  }
}
#endif

typedef struct DummyPluginState {
  const GridLockHostApi* host;
  int32_t stop_count;
  int32_t session_ranks;
  int32_t init_is_dark;
  int32_t theme_changes;
  char init_theme_id[64];
} DummyPluginState;

static GridLockStatus GRIDLOCK_CALL dummy_on_variable_action(
    void* plugin_ctx,
    const GridLockHostApi* host,
    const GridLockVariableContext* var_ctx) {
  char tiny_buf[4];
  size_t required_len = 0u;
  size_t retry_len = 0u;
  char* exact_buf = NULL;
  GridLockStatus st = GRIDLOCK_OK;

  if (plugin_ctx == NULL || host == NULL || var_ctx == NULL || var_ctx->expression_utf8 == NULL) {
    return GRIDLOCK_ERR_INVALID_ARG;
  }

  st = host->eval_expr(
      host->host_ctx, var_ctx->rank, var_ctx->expression_utf8, tiny_buf, sizeof(tiny_buf), &required_len);
  if (st == GRIDLOCK_OK) {
    return host->emit_structured_event(host->host_ctx, "dummy.var_eval", tiny_buf);
  }
  if (st != GRIDLOCK_ERR_BUFFER_TOO_SMALL || required_len >= DUMMY_MAX_EVAL_BYTES) {
    return st;
  }

  exact_buf = (char*)host->alloc_bytes(host->host_ctx, required_len + 1u);
  if (exact_buf == NULL) {
    return GRIDLOCK_ERR_BUFFER_TOO_SMALL;
  }

  st = host->eval_expr(
      host->host_ctx, var_ctx->rank, var_ctx->expression_utf8, exact_buf, required_len + 1u, &retry_len);
  if (st == GRIDLOCK_OK) {
    (void)host->emit_structured_event(host->host_ctx, "dummy.var_eval", exact_buf);
  }
  host->free_bytes(host->host_ctx, exact_buf);
  return st;
}

#if !defined(GRIDLOCK_TEST_BAD_ABI_PLUGIN)
static GridLockStatus GRIDLOCK_CALL dummy_on_command_action(
    void* plugin_ctx,
    const GridLockHostApi* host,
    const GridLockCommandSpec* cmd_spec) {
  int32_t ranks = 0;
  char payload[96];

  if (plugin_ctx == NULL || host == NULL || cmd_spec == NULL) {
    return GRIDLOCK_ERR_INVALID_ARG;
  }
  if (host->get_rank_count(host->host_ctx, &ranks) != GRIDLOCK_OK) {
    return GRIDLOCK_ERR_UNSUPPORTED;
  }
  (void)snprintf(payload, sizeof(payload), "{\"ranks\":%d,\"active\":%d}",
                 (int)ranks, (int)cmd_spec->active_rank);
  return host->emit_structured_event(host->host_ctx, "dummy.cmd", payload);
}

static GridLockStatus GRIDLOCK_CALL dummy_on_menu_action(
    void* plugin_ctx,
    const GridLockHostApi* host,
    const GridLockCommandSpec* cmd_spec) {
  GridLockStatus st = GRIDLOCK_OK;
  if (plugin_ctx == NULL || host == NULL || cmd_spec == NULL) {
    return GRIDLOCK_ERR_INVALID_ARG;
  }
  st = host->publish_report_view(host->host_ctx,
                                 "dummy.report.md",
                                 "Mesh Summary",
                                 GRIDLOCK_REPORT_MARKDOWN,
                                 "# Mesh Summary\n\nAll ranks converged.");
  if (st != GRIDLOCK_OK) return st;

  st = host->publish_report_view(host->host_ctx,
                                 "dummy.report.kv",
                                 "Solver Metrics",
                                 GRIDLOCK_REPORT_KEY_VALUE_TABLE,
                                 "{\"iterations\":42,\"residual\":1.25e-6}");
  if (st != GRIDLOCK_OK) return st;

  st = host->publish_report_view(host->host_ctx,
                                 "dummy.report.json",
                                 "Rank Topology",
                                 GRIDLOCK_REPORT_JSON_TREE,
                                 "{\"comm\":\"MPI_COMM_WORLD\",\"ranks\":[0,1]}");
  if (st != GRIDLOCK_OK) return st;

  (void)host->show_toast(
      host->host_ctx, GRIDLOCK_DIALOG_INFO, "Mesh Exported", "Published 3 report views", 2500);
  return host->emit_structured_event(
      host->host_ctx, "dummy.menu", cmd_spec->menu_label_utf8 != NULL ? cmd_spec->menu_label_utf8 : "");
}

static GridLockStatus GRIDLOCK_CALL dummy_on_status_click(
    void* plugin_ctx,
    const GridLockHostApi* host,
    const char* item_id_utf8);

static GridLockStatus GRIDLOCK_CALL dummy_on_status_click(
    void* plugin_ctx,
    const GridLockHostApi* host,
    const char* item_id_utf8) {
  int32_t dlg_res = 0;
  char small_prompt[8];
  size_t prompt_len = 0u;
  char tag_value[96];
  char payload[256];
  GridLockStatus pst = GRIDLOCK_OK;

  if (plugin_ctx == NULL || host == NULL || item_id_utf8 == NULL) {
    return GRIDLOCK_ERR_INVALID_ARG;
  }

  tag_value[0] = '\0';
  (void)host->show_message_box(host->host_ctx,
                               GRIDLOCK_DIALOG_QUESTION,
                               GRIDLOCK_BUTTONS_YES_NO,
                               "Confirm Probe",
                               "Run mesh probe?",
                               &dlg_res);

  pst = host->prompt_input(host->host_ctx,
                           "Probe Label",
                           "Tag:",
                           "step_0",
                           small_prompt,
                           sizeof(small_prompt),
                           &prompt_len);
  if (pst == GRIDLOCK_OK) {
    (void)snprintf(tag_value, sizeof(tag_value), "%s", small_prompt);
  } else if (pst == GRIDLOCK_ERR_BUFFER_TOO_SMALL && prompt_len < 4096u) {
    char* heap_buf = (char*)host->alloc_bytes(host->host_ctx, prompt_len + 1u);
    if (heap_buf != NULL) {
      size_t retry_len = 0u;
      if (host->prompt_input(host->host_ctx,
                             "Probe Label",
                             "Tag:",
                             "step_0",
                             heap_buf,
                             prompt_len + 1u,
                             &retry_len) == GRIDLOCK_OK) {
        (void)snprintf(tag_value, sizeof(tag_value), "%s", heap_buf);
      }
      host->free_bytes(host->host_ctx, heap_buf);
    }
  }

  (void)host->set_status_item(host->host_ctx,
                              "dummy.mesh_status",
                              "Mesh: Active",
                              "Probe active",
                              "warning",
                              &dummy_on_status_click,
                              plugin_ctx);
  (void)snprintf(payload, sizeof(payload),
                 "{\"item\":\"%s\",\"dialog\":%d,\"tag\":\"%s\"}",
                 item_id_utf8, (int)dlg_res, tag_value);
  return host->emit_structured_event(host->host_ctx, "dummy.status_click", payload);
}

static GridLockStatus GRIDLOCK_CALL dummy_on_context_action(
    void* plugin_ctx,
    const GridLockHostApi* host,
    const GridLockSurfaceContext* ctx) {
  char payload[256];
  const char* sym = NULL;

  if (plugin_ctx == NULL || host == NULL || ctx == NULL) {
    return GRIDLOCK_ERR_INVALID_ARG;
  }
  if (ctx->surface == GRIDLOCK_SURFACE_CODE_EDITOR && ctx->file_utf8 != NULL && ctx->line > 0) {
    (void)host->set_editor_annotation(host->host_ctx,
                                      "dummy.ann.hotspot",
                                      ctx->file_utf8,
                                      ctx->line,
                                      "HOTSPOT",
                                      "MPI halo exchange bottleneck",
                                      "warning");
  } else if (ctx->surface == GRIDLOCK_SURFACE_CALL_STACK && ctx->file_utf8 != NULL && ctx->line > 0) {
    (void)host->navigate_to_location(host->host_ctx, ctx->file_utf8, ctx->line, 1);
  }

  sym = (ctx->symbol_utf8 != NULL) ? ctx->symbol_utf8
                                   : ((ctx->selected_text_utf8 != NULL) ? ctx->selected_text_utf8 : "");
  (void)snprintf(payload, sizeof(payload),
                 "{\"surface\":%d,\"rank\":%d,\"line\":%d,\"bp\":%d,\"sym\":\"%s\"}",
                 (int)ctx->surface, (int)ctx->rank, (int)ctx->line, (int)ctx->bp_id, sym);
  return host->emit_structured_event(host->host_ctx, "dummy.ctx", payload);
}

static void GRIDLOCK_CALL dummy_on_theme_changed(
    void* plugin_ctx,
    const GridLockThemeInfo* theme) {
  DummyPluginState* st = (DummyPluginState*)plugin_ctx;
  char payload[160];
  if (st == NULL || st->host == NULL || theme == NULL) {
    return;
  }
  st->theme_changes += 1;
  (void)snprintf(payload, sizeof(payload),
                 "{\"theme\":\"%s\",\"is_dark\":%d,\"count\":%d}",
                 theme->theme_id_utf8, (int)theme->is_dark, (int)st->theme_changes);
  (void)st->host->emit_structured_event(st->host->host_ctx, "dummy.theme_changed", payload);
}

static GridLockStatus GRIDLOCK_CALL dummy_on_session_start(
    void* plugin_ctx,
    const GridLockHostApi* host,
    int32_t rank_count) {
  DummyPluginState* st = (DummyPluginState*)plugin_ctx;
  if (st == NULL || host == NULL) {
    return GRIDLOCK_ERR_INVALID_ARG;
  }
  st->session_ranks = rank_count;
  return host->log_message(host->host_ctx, GRIDLOCK_LOG_INFO, "dummy session started");
}

static GridLockStatus GRIDLOCK_CALL dummy_on_rank_stopped(
    void* plugin_ctx,
    const GridLockHostApi* host,
    const GridLockStopInfo* stop_info) {
  DummyPluginState* st = (DummyPluginState*)plugin_ctx;
  char payload[128];

  if (st == NULL || host == NULL || stop_info == NULL) {
    return GRIDLOCK_ERR_INVALID_ARG;
  }
  st->stop_count += 1;
  (void)snprintf(payload, sizeof(payload),
                 "{\"rank\":%d,\"line\":%d,\"stops\":%d}",
                 (int)stop_info->rank, (int)stop_info->line, (int)st->stop_count);
  return host->emit_structured_event(host->host_ctx, "dummy.stop", payload);
}

static GridLockStatus GRIDLOCK_CALL dummy_on_session_end(
    void* plugin_ctx, const GridLockHostApi* host) {
  if (plugin_ctx == NULL || host == NULL) {
    return GRIDLOCK_ERR_INVALID_ARG;
  }
  return host->emit_structured_event(host->host_ctx, "dummy.lifecycle", "{\"event\":\"ended\"}");
}

static void GRIDLOCK_CALL dummy_shutdown(void* plugin_ctx, const GridLockHostApi* host) {
  if (host != NULL) {
    char reg_payload[64];
    GridLockStatus reg_st = host->register_variable_action(
        host->host_ctx, "dummy.shutdown_leak", "Leaked", ".*", &dummy_on_variable_action, NULL);
    (void)snprintf(reg_payload, sizeof(reg_payload), "{\"reg_status\":%d}", (int)reg_st);
    (void)host->emit_structured_event(host->host_ctx, "dummy.shutdown_reg", reg_payload);
    (void)host->emit_structured_event(
        host->host_ctx, "dummy.lifecycle", "{\"event\":\"shutdown\"}");
    if (plugin_ctx != NULL && host->free_bytes != NULL) {
      host->free_bytes(host->host_ctx, plugin_ctx);
    }
  }
}
#endif

GRIDLOCK_PLUGIN_EXPORT GridLockStatus GRIDLOCK_CALL gridlock_plugin_init_v1(
    const GridLockHostApi* host,
    GridLockPluginDescriptor* out_desc) {
#if defined(GRIDLOCK_TEST_BAD_ABI_PLUGIN)
  if (host == NULL || out_desc == NULL) {
    return GRIDLOCK_ERR_INVALID_ARG;
  }
  (void)host->register_variable_action(host->host_ctx,
                                       "bad_abi.leaked_action",
                                       "Should Never Appear",
                                       ".*",
                                       &dummy_on_variable_action,
                                       NULL);
  out_desc->struct_size = (uint32_t)sizeof(GridLockPluginDescriptor);
  out_desc->abi_major = 999u;
  out_desc->abi_minor = 0u;
  out_desc->reserved_flags = 0u;
  out_desc->plugin_id_utf8 = "org.gridlock.bad_abi";
  out_desc->plugin_name_utf8 = "Bad ABI Test Plugin";
  return GRIDLOCK_OK;
#else
  DummyPluginState* state = NULL;
  GridLockThemeInfo theme;
  GridLockStatus st = GRIDLOCK_OK;

  if (host == NULL || out_desc == NULL) {
    return GRIDLOCK_ERR_INVALID_ARG;
  }
  if (host->abi_major != GRIDLOCK_PLUGIN_ABI_MAJOR ||
      host->struct_size < offsetof(GridLockHostApi, reserved)) {
    return GRIDLOCK_ERR_ABI_MISMATCH;
  }

  state = (DummyPluginState*)host->alloc_bytes(host->host_ctx, sizeof(DummyPluginState));
  if (state == NULL) {
    return GRIDLOCK_ERR_BUFFER_TOO_SMALL;
  }
  memset(state, 0, sizeof(*state));
  state->host = host;

  memset(&theme, 0, sizeof(theme));
  theme.struct_size = (uint32_t)sizeof(theme);
  theme.abi_major = GRIDLOCK_PLUGIN_ABI_MAJOR;
  theme.abi_minor = GRIDLOCK_PLUGIN_ABI_MINOR;
  st = host->get_theme_info(host->host_ctx, &theme);
  if (st != GRIDLOCK_OK) {
    host->free_bytes(host->host_ctx, state);
    return st;
  }
  state->init_is_dark = theme.is_dark;
  (void)snprintf(state->init_theme_id, sizeof(state->init_theme_id), "%s", theme.theme_id_utf8);

  if ((st = host->register_variable_action(
           host->host_ctx, "dummy.inspect_field", "Inspect Domain Field",
           "^(?:PetscVec|Kokkos::View.*)$", &dummy_on_variable_action, state)) != GRIDLOCK_OK ||
      (st = host->register_command_action(
           host->host_ctx, "dummy.ping_ranks", "Ping Active Ranks",
           "Ctrl+Alt+P", &dummy_on_command_action, state)) != GRIDLOCK_OK ||
      (st = host->register_menu_action(
           host->host_ctx, "dummy.export_vtk", "Mesh Tools/Refinement/Export VTK",
           "Ctrl+Alt+M", &dummy_on_menu_action, state)) != GRIDLOCK_OK ||
      (st = host->set_status_item(
           host->host_ctx, "dummy.mesh_status", "Mesh: Ready",
           "Click to run mesh probe", "accent", &dummy_on_status_click, state)) != GRIDLOCK_OK ||
      (st = host->register_context_action(
           host->host_ctx, GRIDLOCK_SURFACE_VARIABLES, "dummy.ctx.vars",
           "Tag Variable Context", "^(?:PetscVec|Kokkos::View.*)$", &dummy_on_context_action, state)) != GRIDLOCK_OK ||
      (st = host->register_context_action(
           host->host_ctx, GRIDLOCK_SURFACE_CODE_EDITOR, "dummy.ctx.editor",
           "Annotate Editor Line", NULL, &dummy_on_context_action, state)) != GRIDLOCK_OK ||
      (st = host->register_context_action(
           host->host_ctx, GRIDLOCK_SURFACE_CALL_STACK, "dummy.ctx.stack",
           "Inspect Frame Context", NULL, &dummy_on_context_action, state)) != GRIDLOCK_OK ||
      (st = host->register_context_action(
           host->host_ctx, GRIDLOCK_SURFACE_BREAKPOINTS, "dummy.ctx.bp",
           "Audit Breakpoint Context", NULL, &dummy_on_context_action, state)) != GRIDLOCK_OK ||
      (st = host->register_context_action(
           host->host_ctx, GRIDLOCK_SURFACE_RANK_LIST, "dummy.ctx.rank",
           "Inspect Rank State", NULL, &dummy_on_context_action, state)) != GRIDLOCK_OK) {
    host->free_bytes(host->host_ctx, state);
    return st;
  }

  out_desc->struct_size = (uint32_t)sizeof(GridLockPluginDescriptor);
  out_desc->abi_major = GRIDLOCK_PLUGIN_ABI_MAJOR;
  out_desc->abi_minor = GRIDLOCK_PLUGIN_ABI_MINOR;
  out_desc->reserved_flags = 0u;
  out_desc->plugin_id_utf8 = "org.gridlock.test_dummy";
  out_desc->plugin_name_utf8 = "GridLock Test Dummy Plugin";
  out_desc->plugin_version_utf8 = "1.0.0";
  out_desc->description_utf8 = "Compiled C99 test plugin for PluginManager verification.";
  out_desc->plugin_ctx = state;
  out_desc->on_session_start = &dummy_on_session_start;
  out_desc->on_rank_stopped = &dummy_on_rank_stopped;
  out_desc->on_session_end = &dummy_on_session_end;
  out_desc->shutdown = &dummy_shutdown;
  out_desc->on_theme_changed = &dummy_on_theme_changed;
  return GRIDLOCK_OK;
#endif
}
