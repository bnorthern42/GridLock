/**
 * @file domain_mesh_probe.cpp
 * @brief Out-of-tree C++23 sample plugin exporting FEM mesh refinement steps.
 *
 * Demonstrates inspecting stopped MPI rank expressions and raw vertex memory
 * solely through `gridlock/plugin_api.h` and `gridlock/plugin_cpp_helpers.hpp`
 * without linking Qt6 or internal GridLock headers. Writes ASCII VTK snapshots
 * (`mesh_step_<rank>_<step>.vtk`) and publishes JSON events via
 * `emit_structured_event` so external viewers (ParaView, GLVis, or custom
 * scripts) can reload each mesh refinement pass.
 */

#include "gridlock/plugin_api.h"
#include "gridlock/plugin_cpp_helpers.hpp"

#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace gp = gridlock::plugin;

constexpr std::size_t kMaxProbeVertices = 65536u;

struct MeshProbeContext {
  uint64_t step_counter = 0u;
  std::string output_dir = "/tmp";
  std::string auto_probe_expr;
};

[[nodiscard]] std::string trimScalarToken(std::string_view raw) {
  while (!raw.empty() && (raw.front() == ' ' || raw.front() == '\t' || raw.front() == '"')) {
    raw.remove_prefix(1);
  }
  while (!raw.empty() && (raw.back() == ' ' || raw.back() == '\t' || raw.back() == '"' ||
                          raw.back() == '\n' || raw.back() == '\r')) {
    raw.remove_suffix(1);
  }
  return std::string(raw);
}

[[nodiscard]] bool parseUnsigned64(std::string_view raw, uint64_t* outValue) noexcept {
  if (outValue == nullptr) {
    return false;
  }
  const std::string token = trimScalarToken(raw);
  if (token.empty()) {
    return false;
  }
  std::string_view view(token);
  int base = 10;
  if (view.size() > 2u && view[0] == '0' && (view[1] == 'x' || view[1] == 'X')) {
    view.remove_prefix(2);
    base = 16;
  } else {
    const std::size_t hexPos = view.find("0x");
    if (hexPos != std::string_view::npos) {
      view.remove_prefix(hexPos + 2u);
      base = 16;
    }
  }
  uint64_t parsed = 0u;
  const auto [ptr, ec] = std::from_chars(view.data(), view.data() + view.size(), parsed, base);
  (void)ptr;
  if (ec != std::errc{}) {
    return false;
  }
  *outValue = parsed;
  return true;
}

[[nodiscard]] std::string escapeJson(std::string_view input) {
  std::string out;
  out.reserve(input.size() + 8u);
  for (const char ch : input) {
    if (ch == '\\' || ch == '"') {
      out.push_back('\\');
      out.push_back(ch);
    } else if (ch == '\n') {
      out.append("\\n");
    } else {
      out.push_back(ch);
    }
  }
  return out;
}

[[nodiscard]] bool writeLegacyVtkPoints(
    const std::string& filePath,
    int32_t rank,
    uint64_t step,
    uint64_t elementCount,
    std::span<const double> xyzCoords) noexcept {
  std::FILE* fp = std::fopen(filePath.c_str(), "w");
  if (fp == nullptr) {
    return false;
  }

  const std::size_t vertexCount = xyzCoords.size() / 3u;
  std::fprintf(fp, "# vtk DataFile Version 3.0\n");
  std::fprintf(fp, "GridLock domain_mesh_probe rank=%d step=%llu elements=%llu\n",
               rank,
               static_cast<unsigned long long>(step),
               static_cast<unsigned long long>(elementCount));
  std::fprintf(fp, "ASCII\nDATASET UNSTRUCTURED_GRID\n");
  std::fprintf(fp, "POINTS %zu double\n", vertexCount);

  for (std::size_t i = 0u; i < vertexCount; ++i) {
    std::fprintf(fp, "%.9g %.9g %.9g\n",
                 xyzCoords[i * 3u],
                 xyzCoords[i * 3u + 1u],
                 xyzCoords[i * 3u + 2u]);
  }

  std::fprintf(fp, "CELLS %zu %zu\n", vertexCount, vertexCount * 2u);
  for (std::size_t i = 0u; i < vertexCount; ++i) {
    std::fprintf(fp, "1 %zu\n", i);
  }
  std::fprintf(fp, "CELL_TYPES %zu\n", vertexCount);
  for (std::size_t i = 0u; i < vertexCount; ++i) {
    std::fprintf(fp, "1\n");
  }

  const int rc = std::fclose(fp);
  return rc == 0;
}

GridLockStatus exportMeshSnapshot(
    MeshProbeContext* ctx,
    const GridLockHostApi* host,
    int32_t rank,
    std::string_view expr,
    std::string_view triggerSource) {
  if (ctx == nullptr || host == nullptr || expr.empty()) {
    return GRIDLOCK_ERR_INVALID_ARG;
  }

  const std::string baseExpr(expr);
  const auto numVertsStr = gp::evalExpr(host, rank, "(" + baseExpr + ").num_vertices");
  if (!numVertsStr.has_value()) {
    return numVertsStr.error();
  }
  const auto numElemsStr = gp::evalExpr(host, rank, "(" + baseExpr + ").num_elements");
  if (!numElemsStr.has_value()) {
    return numElemsStr.error();
  }
  const auto vertsPtrStr =
      gp::evalExpr(host, rank, "(unsigned long long)((" + baseExpr + ").vertices)");
  if (!vertsPtrStr.has_value()) {
    return vertsPtrStr.error();
  }

  uint64_t vertexCount = 0u;
  uint64_t elementCount = 0u;
  uint64_t verticesAddr = 0u;
  if (!parseUnsigned64(*numVertsStr, &vertexCount) ||
      !parseUnsigned64(*numElemsStr, &elementCount) ||
      !parseUnsigned64(*vertsPtrStr, &verticesAddr)) {
    return GRIDLOCK_ERR_EVAL_FAILED;
  }

  const std::size_t clampedVertices =
      (vertexCount > kMaxProbeVertices) ? kMaxProbeVertices : static_cast<std::size_t>(vertexCount);
  std::vector<double> xyzCoords(clampedVertices * 3u, 0.0);
  if (clampedVertices > 0u && verticesAddr != 0u) {
    const auto readRes = gp::readBytes(
        host, rank, verticesAddr, std::as_writable_bytes(std::span<double>(xyzCoords)));
    if (!readRes.has_value()) {
      return readRes.error();
    }
  }

  const uint64_t stepIndex = ctx->step_counter++;
  const std::string fileName =
      "mesh_step_" + std::to_string(rank) + "_" + std::to_string(stepIndex) + ".vtk";
  const std::string fullPath = ctx->output_dir + "/" + fileName;
  if (!writeLegacyVtkPoints(fullPath, rank, stepIndex, elementCount, xyzCoords)) {
    if (host->log_message != nullptr) {
      host->log_message(
          host->host_ctx, GRIDLOCK_LOG_ERROR, "domain_mesh_probe: failed to write VTK step file");
    }
    return GRIDLOCK_ERR_UNSUPPORTED;
  }

  if (host->emit_structured_event != nullptr) {
    const std::string jsonPayload =
        std::string("{\"rank\":") + std::to_string(rank) +
        ",\"step\":" + std::to_string(stepIndex) +
        ",\"trigger\":\"" + escapeJson(triggerSource) + "\"" +
        ",\"expression\":\"" + escapeJson(baseExpr) + "\"" +
        ",\"num_vertices\":" + std::to_string(clampedVertices) +
        ",\"num_elements\":" + std::to_string(elementCount) +
        ",\"file\":\"" + escapeJson(fileName) + "\"" +
        ",\"vtk_path\":\"" + escapeJson(fullPath) + "\"}";
    host->emit_structured_event(host->host_ctx, "domain.mesh_probe.step", jsonPayload.c_str());
  }

  if (host->log_message != nullptr) {
    const std::string msg = "domain_mesh_probe: wrote " + fullPath;
    host->log_message(host->host_ctx, GRIDLOCK_LOG_INFO, msg.c_str());
  }
  return GRIDLOCK_OK;
}

GridLockStatus onDumpMeshVariableAction(
    void* pluginCtx,
    const GridLockHostApi* host,
    const GridLockVariableContext* varCtx) {
  auto* ctx = static_cast<MeshProbeContext*>(pluginCtx);
  if (ctx == nullptr || host == nullptr || varCtx == nullptr || varCtx->expression_utf8 == nullptr) {
    return GRIDLOCK_ERR_INVALID_ARG;
  }
  ctx->auto_probe_expr = varCtx->expression_utf8;
  return exportMeshSnapshot(ctx, host, varCtx->rank, varCtx->expression_utf8, "variable_action");
}

GridLockStatus onSessionStart(
    void* pluginCtx,
    const GridLockHostApi* host,
    int32_t rankCount) {
  auto* ctx = static_cast<MeshProbeContext*>(pluginCtx);
  if (ctx == nullptr) {
    return GRIDLOCK_ERR_INVALID_ARG;
  }
  ctx->step_counter = 0u;
  if (host != nullptr && host->log_message != nullptr) {
    const std::string msg =
        "domain_mesh_probe: session started across " + std::to_string(rankCount) + " MPI ranks";
    host->log_message(host->host_ctx, GRIDLOCK_LOG_DEBUG, msg.c_str());
  }
  return GRIDLOCK_OK;
}

GridLockStatus onRankStopped(
    void* pluginCtx,
    const GridLockHostApi* host,
    const GridLockStopInfo* stopInfo) {
  auto* ctx = static_cast<MeshProbeContext*>(pluginCtx);
  if (ctx == nullptr || host == nullptr || stopInfo == nullptr) {
    return GRIDLOCK_ERR_INVALID_ARG;
  }
  if (ctx->auto_probe_expr.empty()) {
    return GRIDLOCK_OK;
  }
  (void)exportMeshSnapshot(ctx, host, stopInfo->rank, ctx->auto_probe_expr, "on_rank_stopped");
  return GRIDLOCK_OK;
}

GridLockStatus onSessionEnd(void* pluginCtx, const GridLockHostApi* /*host*/) {
  auto* ctx = static_cast<MeshProbeContext*>(pluginCtx);
  if (ctx == nullptr) {
    return GRIDLOCK_ERR_INVALID_ARG;
  }
  ctx->step_counter = 0u;
  return GRIDLOCK_OK;
}

void onShutdown(void* pluginCtx, const GridLockHostApi* /*host*/) {
  delete static_cast<MeshProbeContext*>(pluginCtx);
}

}  // namespace

GRIDLOCK_PLUGIN_EXPORT GridLockStatus gridlock_plugin_init_v1(
    const GridLockHostApi* host,
    GridLockPluginDescriptor* outDesc) {
  GridLockStatus status = gp::validateHostAbi(host);
  if (status != GRIDLOCK_OK) {
    return status;
  }
  status = gp::initPluginDescriptorHeader(outDesc);
  if (status != GRIDLOCK_OK) {
    return status;
  }

  auto* ctx = new MeshProbeContext{};
  if (const char* envDir = std::getenv("GRIDLOCK_MESH_PROBE_DIR");
      envDir != nullptr && envDir[0] != '\0') {
    ctx->output_dir = envDir;
  }
  if (const char* envExpr = std::getenv("GRIDLOCK_MESH_PROBE_EXPR");
      envExpr != nullptr && envExpr[0] != '\0') {
    ctx->auto_probe_expr = envExpr;
  }

  if (host->register_variable_action != nullptr) {
    status = host->register_variable_action(
        host->host_ctx,
        "mesh_probe.dump_refinement_step",
        "Dump Mesh Refinement Step",
        ".*Mesh.*",
        &onDumpMeshVariableAction,
        ctx);
    if (status != GRIDLOCK_OK) {
      delete ctx;
      return status;
    }
  }

  outDesc->plugin_id_utf8 = "org.gridlock.examples.domain_mesh_probe";
  outDesc->plugin_name_utf8 = "Domain Mesh Probe";
  outDesc->plugin_version_utf8 = "1.0.0";
  outDesc->description_utf8 =
      "Exports stopped MPI rank FEM mesh refinement coordinates to VTK files and structured events.";
  outDesc->plugin_ctx = ctx;
  outDesc->on_session_start = &onSessionStart;
  outDesc->on_rank_stopped = &onRankStopped;
  outDesc->on_session_end = &onSessionEnd;
  outDesc->shutdown = &onShutdown;
  return GRIDLOCK_OK;
}
