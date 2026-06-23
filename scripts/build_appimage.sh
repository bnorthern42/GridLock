#!/bin/bash
set -e

export APPIMAGE_EXTRACT_AND_RUN=1
export NO_STRIP=1

echo "Building GridLock..."
meson setup builddir --prefix=/usr --buildtype=release

echo "Installing into AppDir..."
DESTDIR="$(pwd)/AppDir" ninja -C builddir install

echo "Setting up desktop file and icon..."
mkdir -p AppDir/usr/share/applications
mkdir -p AppDir/usr/share/icons/hicolor/256x256/apps/
cp packaging/gridlock.desktop AppDir/usr/share/applications/

if [ -f "packaging/gridlock.png" ]; then
    cp packaging/gridlock.png AppDir/usr/share/icons/hicolor/256x256/apps/gridlock.png
fi

echo "Installing CQtDeployer..."
wget -c -nv https://github.com/QuasarApp/CQtDeployer/releases/download/v1.6.0/CQtDeployer_1.6.0_linux.run
chmod +x CQtDeployer_1.6.0_linux.run
./CQtDeployer_1.6.0_linux.run --script in
export PATH="$PATH:$HOME/CQtDeployer/bin"

echo "Running CQtDeployer..."
cqtdeployer -bin AppDir/usr/bin/gridlock \
    -qmake $(which qmake6) \
    -ext "wayland,xcb,vulkan" \
    -name "GridLock" \
    -targetDir AppDir

echo "Packaging AppImage..."
wget -c -nv "https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage"
chmod +x appimagetool-x86_64.AppImage
./appimagetool-x86_64.AppImage AppDir builddir/GridLock-x86_64.AppImage

echo "Done!"
