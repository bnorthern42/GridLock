#!/bin/bash
set -e

# Change to project root
cd "$(dirname "$0")/.."

mkdir -p builddir

echo "Configuring and running native coverage pipeline..."
if [ ! -f builddir/build.ninja ]; then
    meson setup builddir -Db_coverage=true
else
    meson configure builddir -Db_coverage=true
fi

ninja -C builddir
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json
(meson test -C builddir || true)
mkdir -p builddir/meson-logs/coveragereport
gcovr -r . --html --html-details -o builddir/meson-logs/coveragereport/index.html

echo "[CI] Coverage report generated at: builddir/meson-logs/coveragereport/index.html"
