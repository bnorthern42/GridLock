# Test Harness & Coverage Validation

GridLock implements rigorous testing against its raw memory and backend logic using a headless architecture designed to simulate cluster environments.

## 🤖 Custom Backend Test Generation

| Component | Files | Description |
| :--- | :--- | :--- |
| **TestGenerator** | `TestGenerator.hpp`, `TestGenerator.cpp` | A utility that synthesizes dummy ELF executables designed to infinitely loop or predictably segfault for testing. |
| **GridLockAutomationRunner** | `GridLockAutomationRunner.hpp`, `GridLockAutomationRunner.cpp` | Parses the `--test-mode` CLI flag to bypass standard application mounting and execute internal headless workflows. |

## 🧪 Test Suite Coverage Map

The `tests/` directory strictly verifies the core components documented across the C++ codebase.

| Test File | Validates Core Implementation File(s) | Focus Area |
| :--- | :--- | :--- |
| **`test_coordinator.cpp`** | `GdbRankCoordinator.cpp`, `GdbRankCoordinator.hpp`, `DapCoordinator.cpp` | Mocked JSON/MI routing and multi-rank multiplexing verification. |
| **`test_ui.cpp`** | `ShortcutManager.cpp`, `ShortcutManager.hpp`, `DifferentialGrid.cpp` | Validating Vim chord event interception and ensuring yellow flash QColors trigger accurately on value diffs. |
| **`test_stride_security.cpp`** | `MiSanitizer.cpp`, `MiSanitizer.hpp`, `MemoryBoundsValidator.cpp` | Fuzz testing the sanitizer to ensure 64KB over-length strings and 50+ depth nested dictionaries are forcibly rejected. |
| **`test_advanced_features.cpp`** | `DeadlockAnalyzer.cpp`, `DeadlockAnalyzer.hpp` | Feeds simulated stack traces of blocked `MPI_Wait` states to trigger the expected analyzer warning outputs. |
| **`test_vulkan_backend.cpp`** | `DomainHeatmapRenderer.cpp`, `DomainHeatmapRenderer.hpp` | Employs Lavapipe to verify Vulkan compute shader compilation headless. |
