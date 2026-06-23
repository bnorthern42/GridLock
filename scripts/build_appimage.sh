#!/usr/bin/env bash
set -euo pipefail

export APPIMAGE_EXTRACT_AND_RUN=1
export NO_STRIP=1
export QMAKE="$(command -v qmake6)"

APP_NAME="GridLock"
BIN_NAME="gridlock"
APPDIR="$(pwd)/AppDir"

echo "Cleaning old build artifacts..."
rm -rf builddir AppDir
rm -f "${APP_NAME}"-*.AppImage
rm -f linuxdeploy-x86_64.AppImage
rm -f linuxdeploy-plugin-qt-x86_64.AppImage

echo "Building ${APP_NAME}..."
meson setup builddir --prefix=/usr --buildtype=release
ninja -C builddir

echo "Installing into AppDir..."
DESTDIR="${APPDIR}" ninja -C builddir install

echo "Verifying AppDir contents..."
ls -lah "${APPDIR}/usr/bin"
ls -lah "${APPDIR}/usr/share/applications" || true
ls -lah "${APPDIR}/usr/share/icons/hicolor/512x512/apps" || true

echo "Checking binary..."
file "${APPDIR}/usr/bin/${BIN_NAME}"
chmod +x "${APPDIR}/usr/bin/${BIN_NAME}"

echo "Checking Qt linkage..."
ldd "${APPDIR}/usr/bin/${BIN_NAME}" | grep -E "Qt6|not found" || true

if ! ldd "${APPDIR}/usr/bin/${BIN_NAME}" | grep -q "Qt6"; then
    echo "ERROR: ${BIN_NAME} does not appear to link against Qt6."
    echo "Full ldd output:"
    ldd "${APPDIR}/usr/bin/${BIN_NAME}" || true
    exit 1
fi

if [ ! -f "${APPDIR}/usr/share/applications/gridlock.desktop" ]; then
    echo "ERROR: Missing desktop file at ${APPDIR}/usr/share/applications/gridlock.desktop"
    find "${APPDIR}" -name "*.desktop" -print
    exit 1
fi

if [ ! -f "${APPDIR}/usr/share/icons/hicolor/512x512/apps/gridlock.png" ]; then
    echo "ERROR: Missing icon at ${APPDIR}/usr/share/icons/hicolor/512x512/apps/gridlock.png"
    find "${APPDIR}" -name "*.png" -print
    exit 1
fi

echo "Checking desktop file..."
grep -E "^(Name|Exec|Icon|Type|Categories)=" "${APPDIR}/usr/share/applications/gridlock.desktop" || true

echo "Downloading linuxdeploy..."
wget -c -nv \
  "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"

echo "Downloading linuxdeploy Qt plugin..."
wget -c -nv \
  "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage"

chmod +x linuxdeploy-x86_64.AppImage
chmod +x linuxdeploy-plugin-qt-x86_64.AppImage

echo "Running linuxdeploy..."
./linuxdeploy-x86_64.AppImage \
  --appdir "${APPDIR}" \
  --executable "${APPDIR}/usr/bin/${BIN_NAME}" \
  --desktop-file "${APPDIR}/usr/share/applications/gridlock.desktop" \
  --icon-file "${APPDIR}/usr/share/icons/hicolor/512x512/apps/gridlock.png" \
  --plugin qt \
  --output appimage

echo "Renaming AppImage..."
FOUND_APPIMAGE="$(find . -maxdepth 1 -type f -name '*.AppImage' | head -n 1)"

if [ -z "${FOUND_APPIMAGE}" ]; then
    echo "ERROR: AppImage was not created."
    find . -maxdepth 2 -type f -name "*.AppImage" -print
    exit 1
fi

chmod +x "${FOUND_APPIMAGE}"
mv "${FOUND_APPIMAGE}" "${APP_NAME}-x86_64.AppImage"

echo "Created:"
ls -lah "${APP_NAME}-x86_64.AppImage"