# 🏗️ Environment Setup & Build Guide

GridLock relies on modern C++23 features and a robust native toolchain. We use **Meson + Ninja** for rapid compilation and **CQtDeployer** for creating distribution AppImages.

## 📦 OS Dependency Installation Matrix

> [!WARNING]  
> **Official Support:** **Arch Linux** is the only officially tested and supported platform for developing GridLock. Wayland QPA stability and raw memory zero-copy (`process_vm_readv`) have not been verified against the kernel configurations of the untested OS variants below.

### 🐧 Arch Linux (Tested & Officially Supported)
```bash
sudo pacman -S meson ninja qt6-base qt6-declarative qt6-svg qt6-wayland \
               vulkan-devel openmpi lldb llvm clang cqtdeployer
```

### 🎩 Fedora (Untested)
```bash
sudo dnf install meson ninja-build qt6-qtbase-devel qt6-qtdeclarative-devel \
                 qt6-qtsvg-devel qt6-qtwayland-devel vulkan-devel openmpi-devel lldb llvm clang
```

### 🟠 Debian / Ubuntu (Untested)
```bash
sudo apt-get update
sudo apt-get install meson ninja-build qt6-base-dev qt6-declarative-dev \
                     libqt6svg6-dev qt6-wayland libvulkan-dev libopenmpi-dev lldb llvm clang
```

### 🦎 openSUSE Tumbleweed (Untested)
```bash
sudo zypper install meson ninja qt6-base-devel qt6-declarative-devel \
                    qt6-svg-devel qt6-wayland-devel vulkan-devel openmpi-devel lldb llvm clang
```

### ❄️ NixOS (Untested)
```bash
nix-shell -p meson ninja qt6.qtbase qt6.qtdeclarative qt6.qtsvg qt6.qtwayland \
             vulkan-headers openmpi lldb llvm clang
```

## 🔨 Building the Project

GridLock utilizes a standard Meson out-of-source build structure.

```bash
# 1. Setup the build directory
meson setup builddir

# 2. Compile using Ninja
meson compile -C builddir

# 3. Run the development binary
./builddir/src/gridlock
```

## 🚀 AppImage Packaging (CQtDeployer)

To bundle the `GridLock` executable with its required Qt6 shared libraries and generate an AppImage for distribution, use our deployment script:

```bash
# This relies on the system having cqtdeployer available
./scripts/deploy.sh
```
