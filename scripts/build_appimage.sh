#!/usr/bin/env bash
set -euo pipefail

export APPIMAGE_EXTRACT_AND_RUN=1
export NO_STRIP=1

APPDIR_ONLY=0
BUILD_LITE=1
BUILD_TUTORIALS=1
OUTPUT_DIR="${OUTPUT_DIR:-}"

while [ $# -gt 0 ]; do
    case "$1" in
        --appdir-only)
            APPDIR_ONLY=1
            ;;
        --lite)
            BUILD_LITE=1
            BUILD_TUTORIALS=0
            ;;
        --with-tutorials)
            BUILD_LITE=0
            BUILD_TUTORIALS=1
            ;;
        --output-dir)
            shift
            OUTPUT_DIR="${1:-}"
            ;;
        --output-dir=*)
            OUTPUT_DIR="${1#*=}"
            ;;
    esac
    shift
done

ROOT_DIR="$(pwd)"
OUTPUT_DIR="${OUTPUT_DIR:-${ROOT_DIR}}"
mkdir -p "${OUTPUT_DIR}"
BUILD_DIR="builddir"
APP_NAME="GridLock"
BIN_NAME="gridlock"

# Dynamically determine version: environment variable > git tag > meson.build project version
if [ -n "${GRIDLOCK_VERSION:-}" ]; then
    export VERSION="${GRIDLOCK_VERSION}"
elif [ -n "${GITHUB_REF_NAME:-}" ] && [[ "$GITHUB_REF_NAME" == v* ]]; then
    export VERSION="${GITHUB_REF_NAME#v}"
else
    export VERSION="$(grep -E "version\s*:\s*'" "${ROOT_DIR}/meson.build" 2>/dev/null | head -n1 | sed -E "s/.*version\s*:\s*'([^']+)'.*/\1/")"
    if [ -z "${VERSION}" ]; then
        VERSION="$(grep -E "version\s*:\s*'" meson.build 2>/dev/null | head -n1 | sed -E "s/.*version\s*:\s*'([^']+)'.*/\1/")"
    fi
fi

APPDIR="${APPDIR:-${ROOT_DIR}/AppDir}"
ACSS_LIB_DIR="${ROOT_DIR}/external/Qt-Advanced-Stylesheets/build/lib"

QTPATHS_BIN="$(command -v qtpaths6 || command -v qtpaths || true)"
QMAKE_BIN="$(command -v qmake6 || command -v qmake || true)"
if [ -z "${QTPATHS_BIN}" ] && [ -z "${QMAKE_BIN}" ]; then
    echo "ERROR: Neither qtpaths6 nor qmake6 was found."
    exit 1
fi
if [ -n "${QMAKE_BIN}" ]; then
    export QMAKE="${QMAKE_BIN}"
fi

QT_PLUGINS_DIR=""
if [ -n "${QTPATHS_BIN}" ]; then
    QT_PLUGINS_DIR="$("${QTPATHS_BIN}" --query QT_INSTALL_PLUGINS 2>/dev/null || true)"
fi
if [ -z "${QT_PLUGINS_DIR}" ] && [ -n "${QMAKE_BIN}" ]; then
    QT_PLUGINS_DIR="$("${QMAKE_BIN}" -query QT_INSTALL_PLUGINS 2>/dev/null || true)"
fi
QT_PLUGINS_DIR="${QT_PLUGINS_DIR:-/usr/lib64/qt6/plugins}"

echo "Building ${APP_NAME} Version: ${VERSION} (APPDIR_ONLY=${APPDIR_ONLY}, QT_PLUGINS_DIR=${QT_PLUGINS_DIR}, OUTPUT_DIR=${OUTPUT_DIR})"

if [ ! -f "${ACSS_LIB_DIR}/libqtadvancedcss.so" ] && [ -d "${ROOT_DIR}/external/Qt-Advanced-Stylesheets" ]; then
    echo "Building external/Qt-Advanced-Stylesheets..."
    cmake -S "${ROOT_DIR}/external/Qt-Advanced-Stylesheets" -B "${ROOT_DIR}/external/Qt-Advanced-Stylesheets/build" -GNinja -DQT_VERSION_MAJOR=6 -DCMAKE_BUILD_TYPE=Release
    ninja -C "${ROOT_DIR}/external/Qt-Advanced-Stylesheets/build"
fi

if [ "${APPDIR_ONLY}" -eq 0 ]; then
    echo "Initializing git submodules..."
    git submodule update --init --recursive || true
    rm -rf "${APPDIR}"
    rm -f "${OUTPUT_DIR}/${APP_NAME}"-*.AppImage
    if [ ! -d "${BUILD_DIR}" ]; then
        meson setup "${BUILD_DIR}" --prefix=/usr --buildtype=release -Db_coverage=false
    fi
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
if [ ! -f "./${BUILD_DIR}/${BIN_NAME}" ]; then
    DESTDIR="${APPDIR}" ninja -C "${BUILD_DIR}" install
fi
mkdir -p "${APPDIR}/usr/bin" "${APPDIR}/usr/lib"
if [ -f "./${BUILD_DIR}/${BIN_NAME}" ]; then
    cp -af "./${BUILD_DIR}/${BIN_NAME}" "${APPDIR}/usr/bin/${BIN_NAME}"
fi
rm -rf "${APPDIR}/usr/bin/"*test*
chmod +x "${APPDIR}/usr/bin/${BIN_NAME}"

echo "Writing AppDir/usr/bin/qt.conf..."
cat << 'EOF' > "${APPDIR}/usr/bin/qt.conf"
[Paths]
Prefix = ..
Plugins = plugins
EOF

echo "Bundling Qt6 plugins (platforms, wayland-*, xcbglintegrations, sqldrivers, tls, iconengines, imageformats)..."
mkdir -p "${APPDIR}/usr/plugins/platforms" \
         "${APPDIR}/usr/plugins/wayland-decoration-client" \
         "${APPDIR}/usr/plugins/wayland-graphics-integration-client" \
         "${APPDIR}/usr/plugins/wayland-shell-integration" \
         "${APPDIR}/usr/plugins/xcbglintegrations" \
         "${APPDIR}/usr/plugins/sqldrivers" \
         "${APPDIR}/usr/plugins/tls" \
         "${APPDIR}/usr/plugins/iconengines" \
         "${APPDIR}/usr/plugins/imageformats"

for plat_so in libqxcb.so libqoffscreen.so libqminimal.so; do
    if [ -f "${QT_PLUGINS_DIR}/platforms/${plat_so}" ]; then
        cp -Lf "${QT_PLUGINS_DIR}/platforms/${plat_so}" "${APPDIR}/usr/plugins/platforms/${plat_so}"
    fi
done
for wayland_plat_so in "${QT_PLUGINS_DIR}/platforms/"libqwayland*.so; do
    if [ -f "${wayland_plat_so}" ]; then
        cp -Lf "${wayland_plat_so}" "${APPDIR}/usr/plugins/platforms/$(basename "${wayland_plat_so}")"
    fi
done

for sub in wayland-decoration-client wayland-graphics-integration-client wayland-shell-integration xcbglintegrations tls iconengines imageformats; do
    if [ -d "${QT_PLUGINS_DIR}/${sub}" ]; then
        for sub_so in "${QT_PLUGINS_DIR}/${sub}/"*.so; do
            if [ -f "${sub_so}" ]; then
                cp -Lf "${sub_so}" "${APPDIR}/usr/plugins/${sub}/$(basename "${sub_so}")"
            fi
        done
    fi
done

if [ -d "${QT_PLUGINS_DIR}/platforminputcontexts" ]; then
    mkdir -p "${APPDIR}/usr/plugins/platforminputcontexts"
    for pic_so in "${QT_PLUGINS_DIR}/platforminputcontexts/"*.so; do
        if [ -f "${pic_so}" ]; then
            cp -Lf "${pic_so}" "${APPDIR}/usr/plugins/platforminputcontexts/$(basename "${pic_so}")"
        fi
    done
fi

if [ -f "${QT_PLUGINS_DIR}/sqldrivers/libqsqlite.so" ]; then
    cp -Lf "${QT_PLUGINS_DIR}/sqldrivers/libqsqlite.so" "${APPDIR}/usr/plugins/sqldrivers/libqsqlite.so"
fi

echo "Bundling external/Qt-Advanced-Stylesheets/build/lib/libqtadvancedcss.so* and builddir shared libraries into AppDir/usr/lib/..."
if [ -d "${ACSS_LIB_DIR}" ]; then
    cp -af "${ACSS_LIB_DIR}"/libqtadvancedcss.so* "${APPDIR}/usr/lib/" 2>/dev/null || true
fi
find "${ROOT_DIR}/external" "${ROOT_DIR}/${BUILD_DIR}" \
    \( -type f -o -type l \) -name "*.so*" -exec cp -af {} "${APPDIR}/usr/lib/" \; 2>/dev/null || true
{ find /usr/lib /usr/lib64 /usr/local/lib /usr/local/lib64 \
    -name "libqtadvancedcss.so*" \( -type f -o -type l \) 2>/dev/null || true; } | while IFS= read -r acss_path; do
    cp -af "${acss_path}" "${APPDIR}/usr/lib/" 2>/dev/null || true
done

echo "Bundling Qt6, Wayland, XCB, and core application shared libraries..."
for lib_pattern in \
    "libqtermwidget6.so*" \
    "libsqlite3.so*" \
    "libQt6Sql.so*" \
    "libQt6Network.so*" \
    "libQt6Widgets.so*" \
    "libQt6Gui.so*" \
    "libQt6Core.so*" \
    "libQt6DBus.so*" \
    "libQt6OpenGL*.so*" \
    "libQt6Svg*.so*" \
    "libQt6Qml*.so*" \
    "libQt6Concurrent*.so*" \
    "libQt6WaylandClient*.so*" \
    "libQt6WaylandEglClientHwIntegration*.so*" \
    "libQt6WlShellIntegration*.so*" \
    "libQt6XcbQpa*.so*" \
    "libwayland-client.so*" \
    "libwayland-cursor.so*" \
    "libwayland-egl.so*" \
    "libxcb-cursor.so*" \
    "libxcb-icccm.so*" \
    "libxcb-image.so*" \
    "libxcb-keysyms.so*" \
    "libxcb-render-util.so*" \
    "libxcb-util.so*" \
    "libxcb-xinerama.so*" \
    "libxkbcommon-x11.so*" \
    "libxkbcommon.so*" \
    "libtomlplusplus.so*"; do
    { find /usr/lib /usr/lib64 /usr/lib/x86_64-linux-gnu /usr/local/lib /usr/local/lib64 "${ROOT_DIR}/${BUILD_DIR}" \
        -maxdepth 3 -name "${lib_pattern}" \( -type f -o -type l \) 2>/dev/null || true; } | while IFS= read -r lib_path; do
        cp -af "${lib_path}" "${APPDIR}/usr/lib/" 2>/dev/null || true
    done
done

TOML_BUILD_DIR="${ROOT_DIR}/${BUILD_DIR}/external/tomlplusplus-3.4.0/src"
RESOLVE_LD_PATH="${APPDIR}/usr/lib:${TOML_BUILD_DIR}:${ACSS_LIB_DIR}:${LD_LIBRARY_PATH:-}"

is_excluded_syslib() {
    case "$1" in
        libc.so*|libm.so*|libdl.so*|librt.so*|libpthread.so*|ld-linux*.so*|libresolv.so*|libnss_*.so*|libnsl.so*|libutil.so*|libanl.so*|libBrokenLocale.so*)
            return 0
            ;;
        libGL.so*|libGLX.so*|libGLdispatch.so*|libEGL.so*|libOpenGL.so*|libGLESv2.so*|libdrm.so*|libgbm.so*|libvulkan.so*)
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

echo "Running recursive ldd dependency collector across gridlock binary, all Qt6 plugins, and bundled libs..."
for pass in 1 2 3 4 5 6; do
    copied_in_pass=0
    targets=()
    targets+=("${APPDIR}/usr/bin/${BIN_NAME}")
    while IFS= read -r plugin_so; do
        targets+=("${plugin_so}")
    done < <(find "${APPDIR}/usr/plugins" -type f -name "*.so" 2>/dev/null)
    while IFS= read -r lib_so; do
        targets+=("${lib_so}")
    done < <(find "${APPDIR}/usr/lib" -maxdepth 1 -type f -name "*.so*" 2>/dev/null)

    while IFS=' ' read -r soname dep_lib; do
        [ -n "${soname}" ] && [ -n "${dep_lib}" ] || continue
        if is_excluded_syslib "${soname}"; then
            continue
        fi
        if [ -f "${dep_lib}" ] && [ ! -f "${APPDIR}/usr/lib/${soname}" ]; then
            rm -f "${APPDIR}/usr/lib/${soname}"
            cp -Lf "${dep_lib}" "${APPDIR}/usr/lib/${soname}"
            chmod u+w "${APPDIR}/usr/lib/${soname}" 2>/dev/null || true
            copied_in_pass=$((copied_in_pass + 1))
        fi
    done < <(LD_LIBRARY_PATH="${RESOLVE_LD_PATH}" ldd "${targets[@]}" 2>/dev/null | awk '/=> \// {print $1, $3}' | sort -u)

    echo "  ldd pass ${pass}: bundled ${copied_in_pass} new shared library(ies)"
    if [ "${copied_in_pass}" -eq 0 ]; then
        break
    fi
done

# Ensure excluded core glibc/GPU driver libraries were not pulled in via earlier bulk copies.
for excluded_pat in libc.so\* libm.so\* libdl.so\* librt.so\* libpthread.so\* ld-linux\*.so\* libresolv.so\* libnss_\*.so\* \
                    libGL.so\* libGLX.so\* libGLdispatch.so\* libEGL.so\* libOpenGL.so\* libGLESv2.so\* libdrm.so\* libgbm.so\* libvulkan.so\*; do
    rm -f "${APPDIR}/usr/lib/"${excluded_pat} 2>/dev/null || true
done

echo "Stamping RPATH onto AppDir/usr/bin/${BIN_NAME}, AppDir/usr/lib/*.so*, and AppDir/usr/plugins/*/*.so..."
if command -v patchelf >/dev/null 2>&1; then
    chmod u+w "${APPDIR}/usr/bin/${BIN_NAME}" 2>/dev/null || true
    patchelf --set-rpath '$ORIGIN/../lib' "${APPDIR}/usr/bin/${BIN_NAME}"
    for bundled_so in "${APPDIR}/usr/lib"/*.so*; do
        if [ -f "${bundled_so}" ] && [ ! -L "${bundled_so}" ]; then
            chmod u+w "${bundled_so}" 2>/dev/null || true
            patchelf --set-rpath '$ORIGIN' "${bundled_so}" 2>/dev/null || true
        fi
    done
    while IFS= read -r plugin_so; do
        chmod u+w "${plugin_so}" 2>/dev/null || true
        patchelf --set-rpath '$ORIGIN/../../lib' "${plugin_so}" 2>/dev/null || true
    done < <(find "${APPDIR}/usr/plugins" -type f -name "*.so" 2>/dev/null)
fi

bundle_tutorials() {
    echo "Bundling and compiling tutorial C++ MPI targets into AppDir/usr/share/gridlock/tutorial/ and AppDir/tutorial/..."
    mkdir -p "${APPDIR}/usr/share/gridlock/tutorial"
    mkdir -p "${APPDIR}/usr/bin/tutorial"
    mkdir -p "${APPDIR}/tutorial"
    MPICXX_BIN="$(command -v mpicxx || command -v mpic++ || command -v g++ || true)"
    for src_target in "tutorial/inspection_demo.cpp" "tutorial/deadlock_ring.cpp" "tutorial/collective_mismatch.cpp" "tutorial/ghost_cell_drift.cpp" "tutorial/heap_imbalance.cpp" "tutorial/tag_mismatch_stall.cpp" "tutorial/simd_struct_watch.cpp"; do
        if [ -f "${ROOT_DIR}/${src_target}" ]; then
            base="$(basename "${src_target}" .cpp)"
            cp -af "${ROOT_DIR}/${src_target}" "${APPDIR}/usr/share/gridlock/tutorial/"
            cp -af "${ROOT_DIR}/${src_target}" "${APPDIR}/tutorial/"
            if [ -n "${MPICXX_BIN}" ]; then
                echo "Compiling ${src_target} -> ${base}..."
                "${MPICXX_BIN}" -g -O0 "${ROOT_DIR}/${src_target}" -o "${APPDIR}/usr/share/gridlock/tutorial/${base}" || true
                if [ -f "${APPDIR}/usr/share/gridlock/tutorial/${base}" ]; then
                    cp -af "${APPDIR}/usr/share/gridlock/tutorial/${base}" "${APPDIR}/usr/bin/tutorial/${base}"
                    cp -af "${APPDIR}/usr/share/gridlock/tutorial/${base}" "${APPDIR}/tutorial/${base}"
                fi
            fi
        fi
    done
}

clean_tutorials() {
    echo "Cleaning tutorial directories from AppDir for Lite edition..."
    rm -rf "${APPDIR}/usr/share/gridlock/tutorial"
    rm -rf "${APPDIR}/usr/bin/tutorial"
    rm -rf "${APPDIR}/tutorial"
}

echo "Bundling QTermWidget color-schemes and kb-layouts into AppDir/usr/share/qtermwidget6/..."
mkdir -p "${APPDIR}/usr/share/qtermwidget6"
if [ -d "/usr/share/qtermwidget6" ]; then
    cp -af /usr/share/qtermwidget6/* "${APPDIR}/usr/share/qtermwidget6/" || true
fi

echo "Generating AppDir/gridlock.desktop, AppDir/gridlock.svg, and hicolor icons..."
mkdir -p "${APPDIR}/usr/share/applications"
cat << 'EOF' > "${APPDIR}/gridlock.desktop"
[Desktop Entry]
Type=Application
Name=GridLock
Comment=HPC MPI & Reverse Engineering Workstation
Exec=gridlock
Icon=gridlock
Categories=Development;Debugger;IDE;
Terminal=false
EOF
cp -af "${APPDIR}/gridlock.desktop" "${APPDIR}/usr/share/applications/gridlock.desktop"

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

echo "Writing AppDir/AppRun launcher with QT_PLUGIN_PATH, LD_LIBRARY_PATH, and graceful Wayland/XCB fallback..."
cat << 'EOF' > "${APPDIR}/AppRun"
#!/usr/bin/env bash
APPDIR="$(dirname "$(readlink -f "${0}")")"
export APPDIR
export LD_LIBRARY_PATH="${APPDIR}/usr/lib:${LD_LIBRARY_PATH:-}"
export QT_PLUGIN_PATH="${APPDIR}/usr/plugins"
export QTERMWIDGET_COLOR_SCHEME_DIR="${APPDIR}/usr/share/qtermwidget6/color-schemes"
export QTERMWIDGET_KB_LAYOUT_DIR="${APPDIR}/usr/share/qtermwidget6/kb-layouts"

HEADLESS_REQ=0
for _arg in "$@"; do
    case "${_arg}" in
        --headless|--snapshot|--snapshot=*) HEADLESS_REQ=1 ;;
    esac
done
if [ "${HEADLESS_REQ}" -eq 0 ] && [ -z "${QT_QPA_PLATFORM:-}" ]; then
    if [ -n "${WAYLAND_DISPLAY:-}" ] && [ -n "${DISPLAY:-}" ]; then
        export QT_QPA_PLATFORM="wayland;xcb"
    elif [ -n "${WAYLAND_DISPLAY:-}" ]; then
        export QT_QPA_PLATFORM="wayland"
    elif [ -n "${DISPLAY:-}" ]; then
        export QT_QPA_PLATFORM="xcb"
    fi
fi

exec "${APPDIR}/usr/bin/gridlock" "$@"
EOF
chmod +x "${APPDIR}/AppRun"

echo "Running AppDir Qt6 platform plugin & ldd verification gate..."
if [ ! -f "${APPDIR}/usr/bin/qt.conf" ]; then
    echo "ERROR: Verification failed: AppDir/usr/bin/qt.conf is missing." >&2
    exit 1
fi
if [ ! -f "${APPDIR}/usr/plugins/platforms/libqxcb.so" ]; then
    echo "ERROR: Verification failed: AppDir/usr/plugins/platforms/libqxcb.so is missing." >&2
    exit 1
fi
WAYLAND_PLUGINS=("${APPDIR}/usr/plugins/platforms/"libqwayland*.so)
if [ ! -f "${WAYLAND_PLUGINS[0]}" ]; then
    echo "ERROR: Verification failed: AppDir/usr/plugins/platforms/libqwayland*.so is missing." >&2
    exit 1
fi
if [ ! -e "${APPDIR}/usr/lib/libxcb-cursor.so.0" ]; then
    echo "ERROR: Verification failed: AppDir/usr/lib/libxcb-cursor.so.0 is missing." >&2
    exit 1
fi

MISSING_DEPS="$(LD_LIBRARY_PATH="${APPDIR}/usr/lib" ldd "${APPDIR}/usr/bin/${BIN_NAME}" "${APPDIR}/usr/plugins/platforms/"*.so 2>&1 | grep -E 'not found' || true)"
if [ -n "${MISSING_DEPS}" ]; then
    echo "ERROR: Verification failed: unresolved shared library dependencies in AppDir:" >&2
    echo "${MISSING_DEPS}" >&2
    exit 1
fi
ALL_PLUGIN_SOS=()
while IFS= read -r p_so; do
    ALL_PLUGIN_SOS+=("${p_so}")
done < <(find "${APPDIR}/usr/plugins" -type f -name "*.so" 2>/dev/null)
if [ "${#ALL_PLUGIN_SOS[@]}" -gt 0 ]; then
    MISSING_SUB_DEPS="$(LD_LIBRARY_PATH="${APPDIR}/usr/lib" ldd "${ALL_PLUGIN_SOS[@]}" 2>&1 | grep -E 'not found' || true)"
    if [ -n "${MISSING_SUB_DEPS}" ]; then
        echo "ERROR: Verification failed: unresolved shared library dependencies in Qt6 sub-plugins:" >&2
        echo "${MISSING_SUB_DEPS}" >&2
        exit 1
    fi
fi
echo "Verification gate passed: libqxcb.so, ${WAYLAND_PLUGINS[*]##*/}, libxcb-cursor.so.0 present; 0 missing ldd deps."

if [ "${APPDIR_ONLY}" -eq 1 ]; then
    if [ "${BUILD_TUTORIALS}" -eq 1 ]; then
        bundle_tutorials
    fi
    echo "AppDir populated successfully (--appdir-only)."
    exit 0
fi

APPIMAGETOOL="/tmp/appimagetool-x86_64.AppImage"
if [ ! -x "${APPIMAGETOOL}" ]; then
    echo "Downloading appimagetool-x86_64.AppImage..."
    wget -q -O "${APPIMAGETOOL}" "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage" || \
        curl -fsSL -o "${APPIMAGETOOL}" "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage"
    chmod +x "${APPIMAGETOOL}"
fi

package_appimage() {
    local target_name="$1"
    echo "Packaging AppDir into ${OUTPUT_DIR}/${target_name} via appimagetool..."
    rm -f "${OUTPUT_DIR}/${target_name}"
    ARCH=x86_64 APPIMAGE_EXTRACT_AND_RUN=1 "${APPIMAGETOOL}" --appimage-extract-and-run "${APPDIR}" "${OUTPUT_DIR}/${target_name}"
    chmod +x "${OUTPUT_DIR}/${target_name}"
}

if [ "${BUILD_LITE}" -eq 1 ]; then
    clean_tutorials
    package_appimage "${APP_NAME}-${VERSION}-x86_64.AppImage"
fi

if [ "${BUILD_TUTORIALS}" -eq 1 ]; then
    bundle_tutorials
    package_appimage "${APP_NAME}-${VERSION}-Tutorials-x86_64.AppImage"
fi

echo "Created AppImage artifacts:"
ls -lah "${OUTPUT_DIR}/${APP_NAME}"-*.AppImage
