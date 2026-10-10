#!/usr/bin/env bash
set -euo pipefail

export APPIMAGE_EXTRACT_AND_RUN=1
export NO_STRIP=1

# Dynamically set version from GitHub tag, default to 0.5.3 if not running in CI
if [ -n "$GITHUB_REF_NAME" ] && [[ "$GITHUB_REF_NAME" == v* ]]; then
    export VERSION="${GITHUB_REF_NAME#v}"
else
    export VERSION="0.6.2"
fi

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

echo "Building GridLock Version: ${VERSION}"

echo "Initializing git submodules..."
git submodule update --init --recursive || true

echo "Cleaning old build artifacts..."
rm -rf builddir AppDir
rm -f "${APP_NAME}"-*.AppImage
rm -f CQtDeployer_*.run
rm -rf .cqtdeployer

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

echo "Configuring styles directory for AppImage..."
if [ -d "${APPDIR}/usr/share/gridlock/styles" ]; then
    ln -s "../share/gridlock/styles" "${APPDIR}/usr/bin/styles"
fi
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

chmod +x "${APPDIR}/usr/bin/${BIN_NAME}"

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

echo "Downloading CQtDeployer..."
wget -c -nv "https://github.com/QuasarApp/CQtDeployer/releases/download/v1.6.2441.67ee1ef/CQtDeployer_1.6.2441.67ee1ef_Linux_x86_64.run"
chmod +x CQtDeployer_1.6.2441.67ee1ef_Linux_x86_64.run

echo "Installing CQtDeployer..."
./CQtDeployer_1.6.2441.67ee1ef_Linux_x86_64.run in .cqtdeployer

echo "Running CQtDeployer..."
./.cqtdeployer/cqtdeployer -bin "${APPDIR}/usr/bin/${BIN_NAME}" \
    -qmake "${QMAKE}" \
    -ext "wayland,xcb,vulkan" \
    -appImage \
    -targetDir "${APPDIR}" \
    -icon "${APPDIR}/usr/share/icons/hicolor/512x512/apps/gridlock.png" \
    -desktop "${APPDIR}/usr/share/applications/gridlock.desktop" \
    -libDir "${APPDIR}/usr/lib"

echo "Renaming AppImage..."
FOUND_APPIMAGE="$(
    find . \
        -maxdepth 1 \
        -type f \
        -name '*.AppImage' \
        | head -n 1 || true
)"

if [ -z "${FOUND_APPIMAGE}" ]; then
    echo "ERROR: AppImage was not created."
    find . -maxdepth 2 -type f -name "*.AppImage" -print
    exit 1
fi

chmod +x "${FOUND_APPIMAGE}"

if [ "${FOUND_APPIMAGE}" != "./${APP_NAME}-x86_64.AppImage" ]; then
    mv "${FOUND_APPIMAGE}" "./${APP_NAME}-x86_64.AppImage"
fi

echo "Created:"
ls -lah "./${APP_NAME}-x86_64.AppImage"

echo "Done."
