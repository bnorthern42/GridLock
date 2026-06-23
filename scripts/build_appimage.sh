#!/bin/bash
set -e

export APPIMAGE_EXTRACT_AND_RUN=1
export NO_STRIP=1

echo "Building GridLock..."
meson setup builddir --prefix=/usr --buildtype=release

echo "Installing into AppDir..."
DESTDIR=AppDir ninja -C builddir install

echo "Setting up desktop file and icon..."
mkdir -p AppDir/usr/share/applications
mkdir -p AppDir/usr/share/icons/hicolor/256x256/apps/
cp packaging/gridlock.desktop AppDir/usr/share/applications/

if [ -f "packaging/gridlock.png" ]; then
    cp packaging/gridlock.png AppDir/usr/share/icons/hicolor/256x256/apps/gridlock.png
else
    echo "Warning: packaging/gridlock.png not found. Please add an icon later."
fi

echo "Downloading linuxdeploy and Qt plugin..."
if [ ! -f linuxdeploy-x86_64.AppImage ]; then
    wget -q https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
    chmod +x linuxdeploy-x86_64.AppImage
fi

if [ ! -f linuxdeploy-plugin-qt-x86_64.AppImage ]; then
    wget -q https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
    chmod +x linuxdeploy-plugin-qt-x86_64.AppImage
fi

echo "Running linuxdeploy..."
./linuxdeploy-x86_64.AppImage --appdir AppDir --plugin qt --output appimage

echo "Done!"
