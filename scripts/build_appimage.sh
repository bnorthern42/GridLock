#!/usr/bin/env bash
set -euo pipefail

export APPIMAGE_EXTRACT_AND_RUN=1
export NO_STRIP=1

APPDIR_ONLY=0
for arg in "$@"; do
    if [ "${arg}" = "--appdir-only" ]; then
        APPDIR_ONLY=1
    fi
done

# Dynamically set version from GitHub tag, defaulting to v0.7.0
if [ -n "${GITHUB_REF_NAME:-}" ] && [[ "$GITHUB_REF_NAME" == v* ]]; then
    export VERSION="${GITHUB_REF_NAME#v}"
else
    export VERSION="0.7.0"
fi

APP_NAME="GridLock"
BIN_NAME="gridlock"
ROOT_DIR="$(pwd)"
BUILD_DIR="builddir"
APPDIR="${ROOT_DIR}/AppDir"
ACSS_LIB_DIR="${ROOT_DIR}/external/Qt-Advanced-Stylesheets/build/lib"

QMAKE_BIN="$(command -v qmake6 || command -v qmake || true)"
if [ -z "${QMAKE_BIN}" ]; then
    echo "ERROR: qmake6 not found."
    exit 1
fi
export QMAKE="${QMAKE_BIN}"

echo "Building ${APP_NAME} Version: ${VERSION} (APPDIR_ONLY=${APPDIR_ONLY})"

if [ ! -f "${ACSS_LIB_DIR}/libqtadvancedcss.so" ] && [ -d "${ROOT_DIR}/external/Qt-Advanced-Stylesheets" ]; then
    echo "Building external/Qt-Advanced-Stylesheets..."
    cmake -S "${ROOT_DIR}/external/Qt-Advanced-Stylesheets" -B "${ROOT_DIR}/external/Qt-Advanced-Stylesheets/build" -GNinja -DQT_VERSION_MAJOR=6 -DCMAKE_BUILD_TYPE=Release
    ninja -C "${ROOT_DIR}/external/Qt-Advanced-Stylesheets/build"
fi

if [ "${APPDIR_ONLY}" -eq 0 ]; then
    echo "Initializing git submodules..."
    git submodule update --init --recursive || true
    rm -rf "${BUILD_DIR}" "${APPDIR}"
    rm -f "${APP_NAME}"-*.AppImage
    rm -f CQtDeployer_*.run
    rm -rf .cqtdeployer
    meson setup "${BUILD_DIR}" --prefix=/usr --buildtype=release -Db_coverage=false
    ninja -C "${BUILD_DIR}" gridlock
else
    rm -rf "${APPDIR}"
    if [ ! -f "./${BUILD_DIR}/${BIN_NAME}" ]; then
        if [ ! -d "${BUILD_DIR}" ]; then
            meson setup "${BUILD_DIR}" --prefix=/usr --buildtype=release -Db_coverage=false
        fi
        ninja -C "${BUILD_DIR}" gridlock
    fi
fi

echo "Installing into AppDir..."
if [ "${APPDIR_ONLY}" -eq 0 ] || [ ! -f "./${BUILD_DIR}/${BIN_NAME}" ]; then
    DESTDIR="${APPDIR}" ninja -C "${BUILD_DIR}" install
fi
mkdir -p "${APPDIR}/usr/bin" "${APPDIR}/usr/lib"
if [ -f "./${BUILD_DIR}/${BIN_NAME}" ]; then
    cp -af "./${BUILD_DIR}/${BIN_NAME}" "${APPDIR}/usr/bin/${BIN_NAME}"
fi
rm -rf "${APPDIR}/usr/bin/"*test*
chmod +x "${APPDIR}/usr/bin/${BIN_NAME}"

echo "Bundling external/Qt-Advanced-Stylesheets/build/lib/libqtadvancedcss.so* and builddir shared libraries into AppDir/usr/lib/..."
if [ -d "${ACSS_LIB_DIR}" ]; then
    cp -af "${ACSS_LIB_DIR}"/libqtadvancedcss.so* "${APPDIR}/usr/lib/" 2>/dev/null || true
fi
{ find "${ROOT_DIR}/external" "${ROOT_DIR}/${BUILD_DIR}" /usr/lib /usr/lib64 /usr/local/lib /usr/local/lib64 \
    -name "libqtadvancedcss.so*" \( -type f -o -type l \) 2>/dev/null || true; } | while IFS= read -r acss_path; do
    cp -af "${acss_path}" "${APPDIR}/usr/lib/" 2>/dev/null || true
done
{ find "${ROOT_DIR}/external" "${ROOT_DIR}/${BUILD_DIR}" \
    -name "*.so*" \( -type f -o -type l \) 2>/dev/null || true; } | while IFS= read -r ext_so; do
    cp -af "${ext_so}" "${APPDIR}/usr/lib/" 2>/dev/null || true
done

echo "Bundling shared libraries (libqtermwidget6, libsqlite3, libQt6Sql, libQt6Network, libQt6Widgets, libQt6Gui, libQt6Core, libtomlplusplus)..."
for lib_pattern in "libqtermwidget6.so*" "libsqlite3.so*" "libQt6Sql.so*" "libQt6Network.so*" "libQt6Widgets.so*" "libQt6Gui.so*" "libQt6Core.so*" "libQt6OpenGL*.so*" "libQt6Svg*.so*" "libQt6Qml*.so*" "libQt6Concurrent*.so*" "libtomlplusplus.so*"; do
    { find /usr/lib /usr/lib64 /usr/lib/x86_64-linux-gnu /usr/local/lib /usr/local/lib64 "${ROOT_DIR}/${BUILD_DIR}" \
        -maxdepth 2 -name "${lib_pattern}" \( -type f -o -type l \) 2>/dev/null || true; } | while IFS= read -r lib_path; do
        cp -af "${lib_path}" "${APPDIR}/usr/lib/" 2>/dev/null || true
    done
done

echo "Resolving ldd dependencies with LD_LIBRARY_PATH=${APPDIR}/usr/lib:${ACSS_LIB_DIR}..."
LD_LIBRARY_PATH="${APPDIR}/usr/lib:${ACSS_LIB_DIR}:${LD_LIBRARY_PATH:-}" ldd "${APPDIR}/usr/bin/${BIN_NAME}" | awk '/=> \// {print $3}' | while IFS= read -r dep_lib; do
    case "$(basename "${dep_lib}")" in
        libc.so*|libm.so*|libdl.so*|librt.so*|libpthread.so*|ld-linux*.so*|libresolv.so*)
            ;;
        *)
            if [ -f "${dep_lib}" ] && [ ! -e "${APPDIR}/usr/lib/$(basename "${dep_lib}")" ]; then
                cp -Lf "${dep_lib}" "${APPDIR}/usr/lib/" 2>/dev/null || true
            fi
            ;;
    esac
done

echo "Stamping \$ORIGIN/../lib RPATH onto AppDir/usr/bin/${BIN_NAME} and \$ORIGIN onto AppDir/usr/lib/libqtadvancedcss.so*..."
if command -v patchelf >/dev/null 2>&1; then
    patchelf --set-rpath '$ORIGIN/../lib' "${APPDIR}/usr/bin/${BIN_NAME}"
    for acss_so in "${APPDIR}/usr/lib"/libqtadvancedcss.so*; do
        if [ -f "${acss_so}" ] && [ ! -L "${acss_so}" ]; then
            patchelf --set-rpath '$ORIGIN' "${acss_so}" || true
        fi
    done
fi

echo "Bundling v0.7.0 tutorial C++ MPI targets into AppDir/usr/share/gridlock/tutorial/..."
mkdir -p "${APPDIR}/usr/share/gridlock/tutorial"
for src_target in "tutorial/inspection_demo.cpp" "tutorial/deadlock_ring.cpp"; do
    if [ -f "${ROOT_DIR}/${src_target}" ]; then
        cp -af "${ROOT_DIR}/${src_target}" "${APPDIR}/usr/share/gridlock/tutorial/"
    fi
done

echo "Bundling QTermWidget color-schemes and kb-layouts into AppDir/usr/share/qtermwidget6/..."
mkdir -p "${APPDIR}/usr/share/qtermwidget6"
if [ -d "/usr/share/qtermwidget6" ]; then
    cp -af /usr/share/qtermwidget6/* "${APPDIR}/usr/share/qtermwidget6/" || true
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
        cp -af "${QT_PLUGINS_DIR}/platforms/${plat_so}" "${APPDIR}/usr/plugins/platforms/"
    fi
done
if [ -f "${QT_PLUGINS_DIR}/sqldrivers/libqsqlite.so" ]; then
    cp -af "${QT_PLUGINS_DIR}/sqldrivers/libqsqlite.so" "${APPDIR}/usr/plugins/sqldrivers/"
fi
for sub in tls iconengines imageformats; do
    if [ -d "${QT_PLUGINS_DIR}/${sub}" ]; then
        cp -af "${QT_PLUGINS_DIR}/${sub}/"*.so "${APPDIR}/usr/plugins/${sub}/" 2>/dev/null || true
    fi
done

echo "Generating AppDir/gridlock.desktop, AppDir/gridlock.svg, and hicolor icons..."
mkdir -p "${APPDIR}/usr/share/applications"
if [ -f "${ROOT_DIR}/packaging/gridlock.desktop" ]; then
    cp -af "${ROOT_DIR}/packaging/gridlock.desktop" "${APPDIR}/usr/share/applications/gridlock.desktop"
    cp -af "${ROOT_DIR}/packaging/gridlock.desktop" "${APPDIR}/gridlock.desktop"
else
    cat << 'EOF' > "${APPDIR}/gridlock.desktop"
[Desktop Entry]
Type=Application
Name=GridLock
Exec=gridlock
Icon=gridlock
Categories=Development;Debugger;
Terminal=false
EOF
    cp -af "${APPDIR}/gridlock.desktop" "${APPDIR}/usr/share/applications/gridlock.desktop"
fi

cat << 'EOF' > "${APPDIR}/gridlock.svg"
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64" width="64" height="64">
  <rect width="64" height="64" rx="12" fill="#16181d" stroke="#528bff" stroke-width="3"/>
  <circle cx="22" cy="22" r="6" fill="#4ec9b0"/>
  <circle cx="42" cy="22" r="6" fill="#61afef"/>
  <circle cx="22" cy="42" r="6" fill="#e5c07b"/>
  <circle cx="42" cy="42" r="6" fill="#e06c75"/>
  <path d="M28 22H36M22 28V36M42 28V36M28 42H36" stroke="#abb2bf" stroke-width="2.5"/>
</svg>
EOF

ICON_SRC="${ROOT_DIR}/resources/icon.png"
if [ ! -f "${ICON_SRC}" ] && [ -f "${ROOT_DIR}/packaging/gridlock.png" ]; then
    ICON_SRC="${ROOT_DIR}/packaging/gridlock.png"
fi

for size in 512 256 128 64 48 32 16; do
    out_dir="${APPDIR}/usr/share/icons/hicolor/${size}x${size}/apps"
    mkdir -p "${out_dir}"
    out="${out_dir}/gridlock.png"
    if [ "${APPDIR_ONLY}" -eq 0 ] && command -v magick >/dev/null 2>&1; then
        magick "${ICON_SRC}" -resize "${size}x${size}!" "${out}"
    elif [ "${APPDIR_ONLY}" -eq 0 ] && command -v convert >/dev/null 2>&1; then
        convert "${ICON_SRC}" -resize "${size}x${size}!" "${out}"
    else
        cp -af "${ICON_SRC}" "${out}"
    fi
done
cp -af "${ICON_SRC}" "${APPDIR}/gridlock.png"
cp -af "${ICON_SRC}" "${APPDIR}/.DirIcon"

echo "Writing AppDir/AppRun launcher with QT_PLUGIN_PATH, LD_LIBRARY_PATH, and QTERMWIDGET_COLOR_SCHEME_DIR..."
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

if [ "${APPDIR_ONLY}" -eq 1 ]; then
    echo "AppDir populated successfully (--appdir-only)."
    exit 0
fi

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
cp -af "${FOUND_APPIMAGE}" "./${APP_NAME}-${VERSION}-x86_64.AppImage"
if [ "${FOUND_APPIMAGE}" != "./${APP_NAME}-x86_64.AppImage" ]; then
    mv "${FOUND_APPIMAGE}" "./${APP_NAME}-x86_64.AppImage"
fi

echo "Created AppImage artifacts:"
ls -lah "./${APP_NAME}-${VERSION}-x86_64.AppImage" "./${APP_NAME}-x86_64.AppImage"
