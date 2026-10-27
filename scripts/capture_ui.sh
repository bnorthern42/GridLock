#!/usr/bin/env bash
set -euo pipefail

ANNOTATE=0
OUTPUT_PATH="/tmp/gridlock_capture.png"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --annotate|-a)
            ANNOTATE=1
            shift
            ;;
        *)
            OUTPUT_PATH="$1"
            shift
            ;;
    esac
done

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

RAW_IMG="${OUTPUT_PATH}"
if [[ "${ANNOTATE}" == "1" ]] || [[ "${GRIDLOCK_USE_SATTY:-0}" == "1" ]]; then
    RAW_IMG="$(mktemp /tmp/gridlock_raw_XXXXXX.png)"
fi

if [[ -n "${WAYLAND_DISPLAY:-}" ]] && command -v grim >/dev/null 2>&1 && [[ "${GRIDLOCK_USE_GRIM:-0}" == "1" ]]; then
    "${GRIDLOCK_BIN}" &
    APP_PID=$!
    sleep 1.5
    grim "${RAW_IMG}"
    kill "${APP_PID}" 2>/dev/null || true
    wait "${APP_PID}" 2>/dev/null || true
else
    QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}" "${GRIDLOCK_BIN}" --snapshot "${RAW_IMG}"
fi

if [[ "${ANNOTATE}" == "1" ]] || [[ "${GRIDLOCK_USE_SATTY:-0}" == "1" ]]; then
    if [[ -n "${WAYLAND_DISPLAY:-}" ]] && command -v satty >/dev/null 2>&1; then
        satty --filename "${RAW_IMG}" --early-exit --output-filename "${OUTPUT_PATH}" || cp -f "${RAW_IMG}" "${OUTPUT_PATH}"
    else
        cp -f "${RAW_IMG}" "${OUTPUT_PATH}"
    fi
    rm -f "${RAW_IMG}"
fi

if [[ ! -s "${OUTPUT_PATH}" ]]; then
    echo "Error: Snapshot was not created or is empty at ${OUTPUT_PATH}" >&2
    exit 1
fi

echo "Captured UI snapshot to ${OUTPUT_PATH}"
