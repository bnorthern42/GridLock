#!/usr/bin/env bash
set -euo pipefail

export APPIMAGE_EXTRACT_AND_RUN=1
export NO_STRIP=1

# Dynamically set version from GitHub tag, defaulting to v0.7.0
if [ -n "${GITHUB_REF_NAME:-}" ] && [[ "$GITHUB_REF_NAME" == v* ]]; then
    export VERSION="${GITHUB_REF_NAME#v}"
else
    export VERSION="0.7.0"
fi

APP_NAME="GridLock"
BIN_NAME="gridlock"
ROOT_DIR="$(pwd)"
APPDIR="${ROOT_DIR}/AppDir"

QMAKE_BIN="$(command -v qmake6 || command -v qmake || true)"
if [ -z "${QMAKE_BIN}" ]; then
    echo "ERROR: qmake6 not found."
    exit 1
fi
export QMAKE="${QMAKE_BIN}"

echo "Building ${APP_NAME} Version: ${VERSION}"

echo "Initializing git submodules..."
git submodule update --init --recursive || true

echo "Cleaning old build artifacts..."
rm -rf builddir "${APPDIR}"
rm -f "${APP_NAME}"-*.AppImage
rm -f CQtDeployer_*.run
rm -rf .cqtdeployer

echo "Configuring and compiling ${APP_NAME} v${VERSION}..."
meson setup builddir --prefix=/usr --buildtype=release
ninja -C builddir

echo "Installing into AppDir..."
DESTDIR="${APPDIR}" ninja -C builddir install
mkdir -p "${APPDIR}/usr/bin"
if [ -f "./builddir/${BIN_NAME}" ] && [ ! -f "${APPDIR}/usr/bin/${BIN_NAME}" ]; then
    cp -a "./builddir/${BIN_NAME}" "${APPDIR}/usr/bin/${BIN_NAME}"
fi
chmod +x "${APPDIR}/usr/bin/${BIN_NAME}"

echo "Bundling v0.7.0 tutorial C++ MPI targets into AppDir/usr/share/gridlock/tutorial/..."
mkdir -p "${APPDIR}/usr/share/gridlock/tutorial"
for src_target in "tutorial/inspection_demo.cpp" "tutorial/deadlock_ring.cpp"; do
    if [ -f "${ROOT_DIR}/${src_target}" ]; then
        cp -a "${ROOT_DIR}/${src_target}" "${APPDIR}/usr/share/gridlock/tutorial/"
    fi
done

echo "Bundling QTermWidget color-schemes and kb-layouts into AppDir/usr/share/qtermwidget6/..."
mkdir -p "${APPDIR}/usr/share/qtermwidget6"
if [ -d "/usr/share/qtermwidget6" ]; then
    cp -a /usr/share/qtermwidget6/* "${APPDIR}/usr/share/qtermwidget6/" || true
fi

echo "Bundling Qt6 plugins (platforms, sqldrivers/libqsqlite.so, tls, iconengines, imageformats)..."
QT_PLUGINS_DIR="$("${QMAKE_BIN}" -query QT_INSTALL_PLUGINS 2>/dev/null || echo "/usr/lib64/qt6/plugins")"
mkdir -p "${APPDIR}/usr/plugins/platforms" \
         "${APPDIR}/usr/plugins/sqldrivers" \
         "${APPDIR}/usr/plugins/tls" \
         "${APPDIR}/usr/plugins/iconengines" \
         "${APPDIR}/usr/plugins/imageformats"

for plat_so in libqxcb.so libqwayland-generic.so libqoffscreen.so; do
    if [ -f "${QT_PLUGINS_DIR}/platforms/${plat_so}" ]; then
        cp -a "${QT_PLUGINS_DIR}/platforms/${plat_so}" "${APPDIR}/usr/plugins/platforms/"
    fi
done
if [ -f "${QT_PLUGINS_DIR}/sqldrivers/libqsqlite.so" ]; then
    cp -a "${QT_PLUGINS_DIR}/sqldrivers/libqsqlite.so" "${APPDIR}/usr/plugins/sqldrivers/"
fi
for sub in tls iconengines imageformats; do
    if [ -d "${QT_PLUGINS_DIR}/${sub}" ]; then
        cp -a "${QT_PLUGINS_DIR}/${sub}/"*.so "${APPDIR}/usr/plugins/${sub}/" 2>/dev/null || true
    fi
done

echo "Bundling shared libraries (libqtermwidget6, libsqlite3, libQt6Sql, libQt6Network, libQt6Widgets, libQt6Gui, libQt6Core, libtomlplusplus)..."
mkdir -p "${APPDIR}/usr/lib"
for lib_pattern in "libqtermwidget6.so*" "libsqlite3.so*" "libQt6Sql.so*" "libQt6Network.so*" "libQt6Widgets.so*" "libQt6Gui.so*" "libQt6Core.so*" "libtomlplusplus.so*"; do
    find /usr/lib /usr/lib64 /usr/lib/x86_64-linux-gnu "${ROOT_DIR}/builddir" \
        -maxdepth 3 -name "${lib_pattern}" \( -type f -o -type l \) 2>/dev/null | while IFS= read -r lib_path; do
        cp -a "${lib_path}" "${APPDIR}/usr/lib/" 2>/dev/null || true
    done
done

echo "Generating desktop entry and hicolor icons..."
mkdir -p "${APPDIR}/usr/share/applications"
if [ -f "${ROOT_DIR}/packaging/gridlock.desktop" ]; then
    cp -a "${ROOT_DIR}/packaging/gridlock.desktop" "${APPDIR}/usr/share/applications/gridlock.desktop"
    cp -a "${ROOT_DIR}/packaging/gridlock.desktop" "${APPDIR}/gridlock.desktop"
fi

ICON_SRC="${ROOT_DIR}/resources/icon.png"
if [ ! -f "${ICON_SRC}" ] && [ -f "${ROOT_DIR}/packaging/gridlock.png" ]; then
    ICON_SRC="${ROOT_DIR}/packaging/gridlock.png"
fi

for size in 512 256 128 64 48 32 16; do
    out_dir="${APPDIR}/usr/share/icons/hicolor/${size}x${size}/apps"
    mkdir -p "${out_dir}"
    out="${out_dir}/gridlock.png"
    if command -v magick >/dev/null 2>&1; then
        magick "${ICON_SRC}" -resize "${size}x${size}!" "${out}"
    elif command -v convert >/dev/null 2>&1; then
        convert "${ICON_SRC}" -resize "${size}x${size}!" "${out}"
    else
        cp -a "${ICON_SRC}" "${out}"
    fi
done
cp -a "${ICON_SRC}" "${APPDIR}/gridlock.png"
cp -a "${ICON_SRC}" "${APPDIR}/.DirIcon"

echo "Writing AppRun launcher with QT_PLUGIN_PATH, LD_LIBRARY_PATH, and QTERMWIDGET_COLOR_SCHEME_DIR..."
cat << 'EOF' > "${APPDIR}/AppRun"
#!/usr/bin/env bash
HERE="$(dirname "$(readlink -f "${0}")")"
export LD_LIBRARY_PATH="${HERE}/usr/lib:${LD_LIBRARY_PATH:-}"
export QT_PLUGIN_PATH="${HERE}/usr/plugins:${QT_PLUGIN_PATH:-}"
export QTERMWIDGET_COLOR_SCHEME_DIR="${HERE}/usr/share/qtermwidget6/color-schemes"
export QTERMWIDGET_KB_LAYOUT_DIR="${HERE}/usr/share/qtermwidget6/kb-layouts"
exec "${HERE}/usr/bin/gridlock" "$@"
EOF
chmod +x "${APPDIR}/AppRun"

export LD_LIBRARY_PATH="${APPDIR}/usr/lib:${LD_LIBRARY_PATH:-}"

echo "Downloading and running CQtDeployer..."
wget -c -nv "https://github.com/QuasarApp/CQtDeployer/releases/download/v1.6.2441.67ee1ef/CQtDeployer_1.6.2441.67ee1ef_Linux_x86_64.run"
chmod +x CQtDeployer_1.6.2441.67ee1ef_Linux_x86_64.run
./CQtDeployer_1.6.2441.67ee1ef_Linux_x86_64.run install "CQtDeployer" -t "$(pwd)/.cqtdeployer" -c

./.cqtdeployer/1.6/cqtdeployer.sh -bin "${APPDIR}/usr/bin/${BIN_NAME}" \
    -qmake "${QMAKE}" \
    -ext "wayland,xcb,vulkan,sql" \
    -appImage \
    -targetDir "${APPDIR}" \
    -icon "${APPDIR}/usr/share/icons/hicolor/512x512/apps/gridlock.png" \
    -desktop "${APPDIR}/usr/share/applications/gridlock.desktop" \
    -libDir "${APPDIR}/usr/lib"

FOUND_APPIMAGE="$(find . -maxdepth 1 -type f -name '*.AppImage' | head -n 1 || true)"
if [ -z "${FOUND_APPIMAGE}" ]; then
    echo "ERROR: AppImage was not created."
    exit 1
fi
chmod +x "${FOUND_APPIMAGE}"
cp -a "${FOUND_APPIMAGE}" "./${APP_NAME}-${VERSION}-x86_64.AppImage"
if [ "${FOUND_APPIMAGE}" != "./${APP_NAME}-x86_64.AppImage" ]; then
    mv "${FOUND_APPIMAGE}" "./${APP_NAME}-x86_64.AppImage"
fi

echo "Created AppImage artifacts:"
ls -lah "./${APP_NAME}-${VERSION}-x86_64.AppImage" "./${APP_NAME}-x86_64.AppImage"
