/**
 * @file test_dummy_plugin.c
 * @brief Pure C99 test shared-library plugin exercising GridLock ABI v1.0 and security gates.
 *
 * Compiles into two Meson `shared_module` test artifacts (`test_dummy_plugin` and
 * `test_bad_abi_plugin`), includes an optional ELF constructor canary gated on
 * `GRIDLOCK_TEST_CTOR_MARKER_FILE` to prove untrusted workspace binaries never execute
 * `.init_array` constructors prior to SHA-256 approval, and tests registration lockout
 * inside `shutdown()`.
 */

#include "gridlock/plugin_api.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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
  int32_t stop_count;
  int32_t session_ranks;
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

  /* First pass: probe with an intentionally tiny 4-byte caller buffer. */
  st = host->eval_expr(host->host_ctx,
                       var_ctx->rank,
                       var_ctx->expression_utf8,
                       tiny_buf,
                       sizeof(tiny_buf),
                       &required_len);
  if (st == GRIDLOCK_OK) {
    return host->emit_structured_event(host->host_ctx, "dummy.var_eval", tiny_buf);
  }
  if (st != GRIDLOCK_ERR_BUFFER_TOO_SMALL || required_len >= DUMMY_MAX_EVAL_BYTES) {
    return st;
  }

  /* Second pass: allocate exact capacity (required_len + 1 for NUL) via host allocator. */
  exact_buf = (char*)host->alloc_bytes(host->host_ctx, required_len + 1u);
  if (exact_buf == NULL) {
    return GRIDLOCK_ERR_BUFFER_TOO_SMALL;
  }

  st = host->eval_expr(host->host_ctx,
                       var_ctx->rank,
                       var_ctx->expression_utf8,
                       exact_buf,
                       required_len + 1u,
                       &retry_len);
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
    /* Attempt registration during shutdown to verify CRIT-03 registration lockout. */
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
  /* Stage an action prior to returning an incompatible major version to test rollback. */
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
  state->stop_count = 0;
  state->session_ranks = 0;

  st = host->register_variable_action(host->host_ctx,
                                      "dummy.inspect_field",
                                      "Inspect Domain Field",
                                      "^(?:PetscVec|Kokkos::View.*)$",
                                      &dummy_on_variable_action,
                                      state);
  if (st != GRIDLOCK_OK) {
    host->free_bytes(host->host_ctx, state);
    return st;
  }

  st = host->register_command_action(host->host_ctx,
                                     "dummy.ping_ranks",
                                     "Ping Active Ranks",
                                     "Ctrl+Alt+P",
                                     &dummy_on_command_action,
                                     state);
  if (st != GRIDLOCK_OK) {
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
  return GRIDLOCK_OK;
#endif
}
