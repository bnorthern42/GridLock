#!/bin/bash
set -e

# Change to project root
cd "$(dirname "$0")/.."

mkdir -p builddir

echo "Building and starting Docker test environment..."
docker compose up --build -d

docker compose exec -T test bash -c "rm -rf builddir/* && meson setup builddir -Db_coverage=true && (meson test -C builddir || true) && mkdir -p builddir/meson-logs/coveragereport && gcovr -r . --html --html-details -o builddir/meson-logs/coveragereport/index.html"

echo "[CI] Coverage report generated at: builddir/meson-logs/coveragereport/index.html"
