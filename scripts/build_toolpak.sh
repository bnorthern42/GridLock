#!/usr/bin/env bash
set -euo pipefail

echo "Building Toolpak for GridLock..."

# Toolpak is essentially a Discoverable Disk Image (DDI) containing a rootfs with the app
IMAGE_NAME="GridLock.toolpak"
MOUNT_DIR="toolpak_mount"
IMAGE_SIZE_MB=512

echo "Creating ${IMAGE_SIZE_MB}MB disk image..."
dd if=/dev/zero of="${IMAGE_NAME}" bs=1M count="${IMAGE_SIZE_MB}"

echo "Formatting image with ext4..."
mkfs.ext4 -F "${IMAGE_NAME}"

echo "Mounting image..."
mkdir -p "${MOUNT_DIR}"
# Use sudo to mount loopback if possible, but in CI environments this might fail without privileges.
# We'll use guestmount or e2tools if possible, but standard is sudo mount -o loop.
# The user specified: use `dd` to create an empty `.img` file, format it with `mkfs.ext4`, mount it via `sudo mount` (or loopback), install GridLock and its dependencies into it, then unmount it and rename to `GridLock.toolpak`
sudo mount -o loop "${IMAGE_NAME}" "${MOUNT_DIR}"

echo "Installing GridLock into Toolpak..."
# Assuming builddir is already built by meson
DESTDIR="$(pwd)/${MOUNT_DIR}" ninja -C builddir install

echo "Unmounting image..."
sudo umount "${MOUNT_DIR}"
rmdir "${MOUNT_DIR}"

echo "Toolpak generation complete: ${IMAGE_NAME}"
ls -lh "${IMAGE_NAME}"
