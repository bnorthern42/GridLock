## Summary

<!-- Describe the bug fix, feature, or refactor, including the root cause or technical motivation. -->

## Related Issues

<!-- Link relevant issues using GitHub keywords. -->
- Closes #
- Relates to #

## Changes Made

<!-- List the modified subsystems and key behavioral changes. -->
- `src/core/hpc`:
- `src/core/dap`:
- `src/ui/views`:
- `src/ui/docks`:
- `src/ui/dialogs`:
- `tests/`:

## UI Screenshots (Required for UI / Widget / Theme Changes)

> Any PR modifying Qt widgets, dialogs, docks, layouts, or themes **must** attach actual `.png` screenshots captured from the running GridLock binary (no SVGs, mockups, or placeholder stubs). You can generate updated workstation captures via `bash scripts/capture_ui.sh` or `QT_QPA_PLATFORM=offscreen ./builddir/gridlock --headless --snapshot <path>.png`.

| Before | After |
| :--- | :--- |
| <!-- Paste before.png here or "N/A (non-UI change)" --> | <!-- Paste after.png here or "N/A (non-UI change)" --> |

## Verification & Testing

- [ ] Compiles cleanly with zero warnings (`ninja -C builddir`).
- [ ] All Meson test suites pass (`meson test -C builddir --print-errorlogs`).
- [ ] Headless UI test suite passes (`QT_QPA_PLATFORM=offscreen ./builddir/gridlock_ui_tests`).
- [ ] Manually tested against an MPI/C++ target (if touching debugger backends or UI docks).

## Code Standards Checklist

- [ ] Adheres to the Google C++ Style Guide and repository `agent.md` invariants (`< 950` lines per file, no bulk script mutations).
- [ ] No unit tests scrape `.md` documentation or source files on disk.
- [ ] `.gridlock/` or temporary build artifacts are not staged.
