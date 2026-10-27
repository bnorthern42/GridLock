#!/usr/bin/env bash
set -euo pipefail

OUTPUT_PATH="${1:-/tmp/gridlock_capture.png}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

if [[ -x "${PROJECT_ROOT}/builddir/gridlock" ]]; then
    GRIDLOCK_BIN="${PROJECT_ROOT}/builddir/gridlock"
elif [[ -x "${PROJECT_ROOT}/build/gridlock" ]]; then
    GRIDLOCK_BIN="${PROJECT_ROOT}/build/gridlock"
else
    echo "Error: gridlock binary not found in builddir/ or build/." >&2
    exit 1
fi

# Prefer deterministic snapshot via GridLock's --snapshot flag, with optional grim/satty pipeline under active Wayland
if [[ -n "${WAYLAND_DISPLAY:-}" ]] && command -v grim >/dev/null 2>&1 && [[ "${GRIDLOCK_USE_GRIM:-0}" == "1" ]]; then
    "${GRIDLOCK_BIN}" &
    APP_PID=$!
    sleep 1.5
    if command -v satty >/dev/null 2>&1 && [[ "${GRIDLOCK_USE_SATTY:-0}" == "1" ]]; then
        grim - | satty --filename - --output-filename "${OUTPUT_PATH}" --early-exit
    else
        grim "${OUTPUT_PATH}"
    fi
    kill "${APP_PID}" 2>/dev/null || true
    wait "${APP_PID}" 2>/dev/null || true
else
    QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}" "${GRIDLOCK_BIN}" --snapshot "${OUTPUT_PATH}"
fi

if [[ ! -s "${OUTPUT_PATH}" ]]; then
    echo "Error: Snapshot was not created or is empty at ${OUTPUT_PATH}" >&2
    exit 1
fi

echo "Captured UI snapshot to ${OUTPUT_PATH}"
