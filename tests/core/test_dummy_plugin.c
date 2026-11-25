/**
 * @file test_dummy_plugin.c
 * @brief Pure C99 test shared-library plugin exercising GridLock ABI v1.0.
 *
 * Compiles into two Meson `shared_module` test artifacts:
 * 1. `test_dummy_plugin`: Valid ABI v1.0 plugin exercising `alloc_bytes`/`free_bytes`,
 *    `register_variable_action`, `register_command_action`, `eval_expr` two-pass
 *    buffer sizing (`GRIDLOCK_ERR_BUFFER_TOO_SMALL` -> exact retry), `on_rank_stopped`,
 *    and `shutdown` without mutable global state.
 * 2. `test_bad_abi_plugin` (`-DGRIDLOCK_TEST_BAD_ABI_PLUGIN=1`): Reports `abi_major = 999`
 *    after attempting to stage a variable action, verifying clean rejection and staging
 *    rollback in `PluginManager`.
 */

#include "gridlock/plugin_api.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define DUMMY_MAX_EVAL_BYTES ((size_t)16u * 1024u * 1024u)

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
  if (st != GRIDLOCK_ERR_BUFFER_TOO_SMALL) {
    return st;
  }
  if (required_len >= DUMMY_MAX_EVAL_BYTES) {
    return GRIDLOCK_ERR_BUFFER_TOO_SMALL;
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
