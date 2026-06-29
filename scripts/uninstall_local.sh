#!/usr/bin/env bash
set -euo pipefail

echo "=========================================="
echo " Uninstalling GridLock (Local Installation)"
echo "=========================================="

if [ ! -d "build" ]; then
    echo "ERROR: 'build' directory not found!"
    echo "Meson requires the original build directory to process uninstallation."
    echo "If you deleted it, please run './scripts/build_local.sh' to regenerate it, then run this script."
    exit 1
fi

echo "This step requires root privileges."
sudo ninja -C build uninstall

echo "GridLock has been uninstalled successfully."
