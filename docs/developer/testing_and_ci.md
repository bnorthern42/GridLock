# 🧪 Testing & CI Pipeline

GridLock maintains strict quality assurance through automated headless testing. Because our debugger interacts heavily with the OS kernel (via `ptrace` and `process_vm_readv`), standard containerized testing environments require specialized configurations.

## 🐳 Dockerized TDD Sandbox

The core of our testing strategy is a stateless Docker environment defined in `docker-compose.yml`.

> [!CAUTION]
> **Kernel Capabilities**
> To successfully test the `GdbRankCoordinator` and `NativeMemoryReader`, the CI container **must** run with `cap_add: [SYS_PTRACE]` and `security_opt: [seccomp:unconfined]`. Without these privileges, the debugger will receive `EPERM` when attaching to dummy test executables.

Our CI pipeline executes tests in headless mode (using `QT_QPA_PLATFORM=minimal` or Lavapipe for Vulkan coverage) to ensure pure logic verification without a physical display.

## 🚦 Unit Test Suite (`tests/`)

We utilize the Google Test (gtest) framework paired with Qt's `QTest` module.

*   **`test_coordinator.cpp`**: Mocks the GDB interface to test asynchronous multiplexing, verifying that the `GdbRankCoordinator` routes rank 1's variables separately from rank 2's.
*   **`test_ui.cpp`**: Synthesizes mouse clicks and global shortcuts using `QTest::keyClick`. Verifies the `ShortcutManager` correctly focuses splits and that the `DifferentialGrid` triggers the correct QColor updates.
*   **`test_stride_security.cpp`**: Actively attacks the `MiSanitizer.cpp` by feeding it 70KB payloads and depth-50 JSON tree structs to ensure it successfully terminates the connection rather than overflowing.
*   **`test_vulkan_backend.cpp`**: Validates the headless initialization of the `DomainHeatmapWidget` Vulkan compute pipeline using Mesa's software rasterizer.
