## Summary & Root Cause

<!-- Describe the problem, verified root cause, and technical motivation. Do not submit unverified or blind changes. -->

## Related Issues

<!-- Link relevant issues using GitHub keywords. -->
- Closes #
- Relates to #

## Reproducer / Sanitized GDB/MI Trace (Required for GDB, MPI, Slurm, or Cluster Fixes)

<!-- For debugger, parser, MPI launcher, or HPC cluster fixes, attach a Sanitized GDB/MI Trace (Tools -> Export Sanitized GDB/MI Trace... or `--export-mi-trace <file>`) plus `gdb --version` and MPI launcher command line, or note "N/A". -->

## Changes Made

<!-- Keep diffs small, auditable, and focused on a single concern. -->
- `src/core/hpc`:
- `src/core/plugins`:
- `src/ui/`:
- `tests/`:

## UI Screenshots (Required for UI / Widget / Theme Changes)

> Any PR modifying Qt widgets, dialogs, docks, layouts, or themes **must** attach actual `.png` screenshots captured from the running GridLock binary (no SVGs, mockups, or placeholder stubs). You can generate updated workstation captures via `bash scripts/capture_ui.sh` or `QT_QPA_PLATFORM=offscreen ./builddir/gridlock --headless --snapshot <path>.png`.

| Before | After |
| :--- | :--- |
| <!-- Paste before.png here or "N/A (non-UI change)" --> | <!-- Paste after.png here or "N/A (non-UI change)" --> |

## Mandatory PR Checklist

- [ ] Summary of problem and verified root cause included (no unverified/blind changes).
- [ ] Sanitized GDB/MI trace or minimal reproducer attached (required for GDB/MPI/Slurm/cluster fixes).
- [ ] Added or updated unit / headless UI regression test covering the change (fails before fix, passes after).
- [ ] Built and verified locally with zero warnings (`ninja -C builddir` and `QT_QPA_PLATFORM=offscreen meson test -C builddir --print-errorlogs`).
- [ ] All modified and new C++ source and header files are strictly under `800` lines (`agent.md`).
- [ ] Plugin C ABI (`include/gridlock/plugin_api.h`) backward compatibility preserved (no reordered, removed, or altered callback signatures).
- [ ] No `.gridlock/` or temporary build artifacts are staged, and no unit tests scrape `.md` documentation files.

## Accessibility Checklist (For UI Changes)

- [ ] Keyboard navigation (`Tab` order, shortcuts, `Esc` / close buttons) works end-to-end for modified dialogs and widgets.
- [ ] Visible focus indicators are preserved on interactive controls.
- [ ] Form controls and icon buttons have clear labels or accessible names.
- [ ] Color is not the sole indicator of status or meaning (paired with text, icons, or badges).
- [ ] Documentation images include descriptive alt text.
