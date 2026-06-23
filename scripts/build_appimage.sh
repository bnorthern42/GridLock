#!/usr/bin/env bash
set -euo pipefail

export APPIMAGE_EXTRACT_AND_RUN=1
export NO_STRIP=1

APP_NAME="GridLock"
BIN_NAME="gridlock"
ROOT_DIR="$(pwd)"
APPDIR="${ROOT_DIR}/AppDir"

QMAKE_BIN="$(command -v qmake6 || true)"

if [ -z "${QMAKE_BIN}" ]; then
    echo "ERROR: qmake6 not found."
    exit 1
fi

export QMAKE="${QMAKE_BIN}"

echo "Cleaning old build artifacts..."
rm -rf builddir AppDir
rm -f "${APP_NAME}"-*.AppImage
rm -f linuxdeploy-x86_64.AppImage
rm -f linuxdeploy-plugin-qt-x86_64.AppImage

echo "Configuring ${APP_NAME}..."
meson setup builddir --prefix=/usr --buildtype=release

echo "Installing into AppDir..."
DESTDIR="${APPDIR}" ninja -C builddir install

echo "Fixing icon resolutions..."
mkdir -p "${APPDIR}/usr/share/icons/hicolor/512x512/apps"
mkdir -p "${APPDIR}/usr/share/icons/hicolor/256x256/apps"
mkdir -p "${APPDIR}/usr/share/icons/hicolor/128x128/apps"
mkdir -p "${APPDIR}/usr/share/icons/hicolor/64x64/apps"
mkdir -p "${APPDIR}/usr/share/icons/hicolor/48x48/apps"
mkdir -p "${APPDIR}/usr/share/icons/hicolor/32x32/apps"
mkdir -p "${APPDIR}/usr/share/icons/hicolor/16x16/apps"

ICON_SRC="${ROOT_DIR}/resources/icon.png"

if [ ! -f "${ICON_SRC}" ]; then
    echo "ERROR: Missing source icon at ${ICON_SRC}"
    exit 1
fi

resize_icon() {
    local size="$1"
    local out="${APPDIR}/usr/share/icons/hicolor/${size}x${size}/apps/gridlock.png"

    if command -v magick >/dev/null 2>&1; then
        magick "${ICON_SRC}" -resize "${size}x${size}!" "${out}"
    elif command -v convert >/dev/null 2>&1; then
        convert "${ICON_SRC}" -resize "${size}x${size}!" "${out}"
    else
        echo "ERROR: ImageMagick not found. Install imagemagick in CI."
        exit 1
    fi

    echo "Created icon: ${out}"
    file "${out}"

    if command -v identify >/dev/null 2>&1; then
        identify -format "Icon geometry: %wx%h\n" "${out}"
    fi
}

resize_icon 512
resize_icon 256
resize_icon 128
resize_icon 64
resize_icon 48
resize_icon 32
resize_icon 16

echo "Verifying AppDir contents..."
ls -lah "${APPDIR}/usr/bin"
ls -lah "${APPDIR}/usr/share/applications" || true
ls -lah "${APPDIR}/usr/share/icons/hicolor/512x512/apps" || true

echo "Checking binary..."
if [ ! -f "${APPDIR}/usr/bin/${BIN_NAME}" ]; then
    echo "ERROR: Missing binary at ${APPDIR}/usr/bin/${BIN_NAME}"
    find "${APPDIR}" -maxdepth 5 -type f -print
    exit 1
fi

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

echo "Bundling tomlplusplus if Meson built it locally..."
mkdir -p "${APPDIR}/usr/lib"

TOML_LIBS="$(
    find "${ROOT_DIR}" \
        -path "${APPDIR}" -prune -o \
        -name 'libtomlplusplus.so*' \
        \( -type f -o -type l \) \
        -print 2>/dev/null || true
)"

if [ -n "${TOML_LIBS}" ]; then
    echo "${TOML_LIBS}" | while IFS= read -r lib; do
        echo "Copying ${lib}"
        cp -a "${lib}" "${APPDIR}/usr/lib/"
    done
else
    echo "WARNING: Could not find libtomlplusplus.so* in project/build tree."
fi

export LD_LIBRARY_PATH="${APPDIR}/usr/lib:${LD_LIBRARY_PATH:-}"

echo "Rechecking dependencies after bundling local libs..."
ldd "${APPDIR}/usr/bin/${BIN_NAME}" | grep -E "tomlplusplus|Qt6|not found" || true

if ldd "${APPDIR}/usr/bin/${BIN_NAME}" | grep -q "not found"; then
    echo "WARNING: Some dependencies are still unresolved before linuxdeploy."
    echo "Full ldd output:"
    ldd "${APPDIR}/usr/bin/${BIN_NAME}" || true
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
FOUND_APPIMAGE="$(
    find "${ROOT_DIR}" \
        -maxdepth 1 \
        -type f \
        -name '*.AppImage' \
        ! -name 'linuxdeploy*.AppImage' \
        | head -n 1 || true
)"

if [ -z "${FOUND_APPIMAGE}" ]; then
    echo "ERROR: AppImage was not created."
    find "${ROOT_DIR}" -maxdepth 2 -type f -name "*.AppImage" -print
    exit 1
fi

chmod +x "${FOUND_APPIMAGE}"

if [ "${FOUND_APPIMAGE}" != "${ROOT_DIR}/${APP_NAME}-x86_64.AppImage" ]; then
    mv "${FOUND_APPIMAGE}" "${ROOT_DIR}/${APP_NAME}-x86_64.AppImage"
fi

echo "Created:"
ls -lah "${ROOT_DIR}/${APP_NAME}-x86_64.AppImage"

echo "Done."