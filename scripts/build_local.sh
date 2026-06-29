#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(pwd)"

echo "========================================"
echo " Building GridLock (Local Installation) "
echo "========================================"

echo "Initializing git submodules..."
git submodule update --init --recursive || true

echo "Configuring Meson build directory..."
if [ -d "build" ]; then
    echo "Existing build directory found, reconfiguring..."
    meson setup build --reconfigure
else
    echo "Setting up new build directory..."
    meson setup build
fi

echo "Compiling GridLock..."
ninja -C build

echo "========================================"
echo "Build complete. Installing to system..."
echo "This step requires root privileges."
echo "========================================"

sudo meson install -C build

echo "GridLock installed successfully!"
