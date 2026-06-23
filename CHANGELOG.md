## 🚀 GridLock v0.4.7: Performance Tuning & CI/CD Stabilization

This release focuses on heavily optimizing the Qt6/Vulkan frontend, resolving CI/CD pipeline bottlenecks, and restoring the core HPC Domain Heatmap architecture.

### ⚡ Performance & Core Architecture

* **Asynchronous Memory Extraction:** Offloaded massive `process_vm_readv` MPI array reads to a background thread pool using `QtConcurrent`. The main Qt Event Loop is no longer blocked during high-speed matrix extraction.
* **Event-Driven Vulkan Rendering:** Fixed a severe bug causing 100% GPU utilization. The `QVulkanWindow` render loop is now strictly event-driven and only requests frame updates when new data is successfully fetched from the DAP backend.
* **VSync Profiling Hooks:** Added temporary overrides for Wayland/Vulkan swap intervals to allow for accurate rendering latency profiling.

### 🎨 UX & Interface Enhancements

* **Tactile UI Interactions:** Injected dynamic QSS pseudo-classes across the frontend. Buttons now provide visual, mechanical feedback (shifting on press, highlighting on hover) rather than relying on flat Fusion defaults.
* **Domain Heatmap Restoration:** Brought the Vulkan-powered Domain Heatmap out of hidden state and officially wired it into the bottom tab manager.
* **Lifecycle-Aware Controls:** Heatmap interactive controls (Render Frame, Row/Col inputs) are now safely locked to the DAP session lifecycle, preventing backend aborts from premature extraction requests.

### 🛠️ CI/CD & Build System

* **Automated AppImage Pipeline:** Fully configured GitHub Actions to build, package, and deploy GridLock AppImages on an `ubuntu-22.04` runner to guarantee broad `glibc` backward compatibility.
* **Modern Toolchain Injection:** Bypassed default `apt` packages in favor of `pip`-installed Meson/Ninja to fully support `c++23` language standards in the CI environment.
* **Headless Vulkan Testing:** Configured the testing sandbox to utilize `lavapipe` (Mesa software rasterizer) and `QOffscreenSurface`, allowing Vulkan shaders and buffers to be unit-tested in headless CI environments without crashing.
* **Manual Pipeline Triggers:** Added `workflow_dispatch` to GitHub Actions, allowing the AppImage pipeline to be run manually and output artifacts without requiring a new Git tag release.

### 🐛 Bug Fixes

* **LinuxDeploy Target Resolution:** Fixed a critical bug where `linuxdeploy-plugin-qt` failed to find Wayland/Vulkan dependencies by explicitly forcing the QMake path to `qmake6` and explicitly passing the GridLock executable target.
* **Compiler Strictness:** Silenced all CI build warnings, including zero-initializing `RankState`, safely casting `QCOMPARE` size types, and applying `Q_UNUSED` macros in test mocks.

