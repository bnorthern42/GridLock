# Project Setup Dialogs (`src/ui/dialogs/project/`)

Modal dialogs and multi-step project wizard managing active MPI target binaries, CLI arguments, workspace paths, and new project creation.

## Contents

- `ProjectSettingsDialog.hpp` (`141` lines):
  - Declares four compact dark-slate modal dialogs inside `gridlock::ui::dialogs`.
- `ProjectSettingsDialog.cpp`:
  - `ProjectSettingsDialog`: Configures executable binary path, runtime arguments, and working directory persisted to `.gridlock/settings.toml`.
  - `AttachProcessDialog`: Compact PID/command attach table selector with live process filtering.
  - `NewProjectDialog`: Single-card quick MPI project creation dialog scaffolding source and build defaults.
  - `AboutDialog`: Workstation build metadata, GDB/MI + ELF64 architecture overview, and environment telemetry card.
- `ProjectWizard.hpp` (`101` lines), `ProjectWizard.cpp`:
  - Multi-step `QWizard` walking users through workspace folder creation and build target verification.
  - Captures target executable path, runtime CLI arguments, MPI rank counts, CPU core binding rules, and OpenMPI launcher flags.
  - Validates directory write permissions and executable existence before completing setup.

## Expected Behavior & Invariants

- **Non-Destructive Persistence Sync**:
  - Accepting `ProjectSettingsDialog` writes updated target path, CLI arguments, and working directory through `ConfigManager` without overwriting stored workspace breakpoints or watch expressions.
- **Namespace Separation**:
  - `gridlock::ui::dialogs::AttachProcessDialog` stays cleanly isolated from the two-tab `/proc` + `gdbserver` `AttachProcessDialog` in `src/ui/dialogs/debugger/`.
- **Wizard Completeness Validation**:
  - `QWizardPage::isComplete()` enforces non-empty workspace folders, target binaries, and valid rank counts before enabling Next/Finish.
- **Verification**:
  - Run `meson test -C builddir ui_tests config_tests --print-errorlogs` after editing project forms or wizard pages.

## Maintainer & Agent Notes

- Config Schema Alignment:
  - Keep project settings schema additions synchronized with `ProjectSettings` in `src/core/managers/ConfigManager.hpp` and covered by `tests/core/test_config.cpp`.
- Wizard vs. Quick Modal Scope:
  - Place multi-page project scaffolding workflows (CMake/Meson/Spack presets) in `ProjectWizard` rather than overcrowding `NewProjectDialog`.
- UI Selectors & File Hygiene:
  - Preserve stable widget `objectName()` attributes across form controls so UI automation checks in `tests/ui/test_ui_debugger.cpp` target controls reliably.
  - Verify offscreen dialog construction (`QT_QPA_PLATFORM=offscreen`) whenever modifying wizard pages or modal layouts.
  - Maintain strict `< 950` lines per file and preserve `ThemeManager` palette styling across Light and Dark themes.
  - Update `meson.build` (`src_files`, `moc_headers`, `ui_test_src`, `ui_test_moc_headers`) and this `README.md` when adding project dialogs.
