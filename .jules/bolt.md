## 2025-01-20 - QRegularExpression performance
**Learning:** Instantiating `QRegularExpression` inside loops or frequently called functions (like message parsing handlers) is a common performance bottleneck in Qt. Compiling a regular expression is relatively expensive.
**Action:** Move frequently used `QRegularExpression` instances to static variables, member variables, or class-level constants.
## 2025-01-20 - Flatpak CI fixes
**Learning:** Flatpak runner in Github Actions failed due to missing `org.kde.Sdk//6.7` dependency because the specific Qt runtime version was removed/unsupported in standard Flathub repos.
**Action:** Always maintain updated `runtime-version` parameters (e.g. `6.8`) in Flatpak manifests that are actively supported by Flathub.
## 2025-01-20 - Testing MemoryBoundsValidator without Qt Gui
**Learning:** We can successfully run tests for classes that don't depend on complex UI bindings by directly invoking `moc` and `g++` against `qtestlib`, bypassing heavier Meson setups in constrained CI/sandbox environments.
**Action:** Created standalone shell scripts to run specific test subset manually.

### Learning/Action
* Fixed a `O(N^2)` memory reallocation issue in `escapeGdbMiString` within `GdbRankCoordinator.cpp` by explicitly calling `res.reserve(str.length() * 2 + 2)`.
* This prevents redundant memory allocations during a `QString` concatenation `+=` loop, providing a minor performance improvement when serializing strings to the DAP backend.
