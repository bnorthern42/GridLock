# Programmer Calculator (`src/ui/dialogs/calculator/`)

Floating Programmer Calculator tool with multi-radix synchronization, 64-bit interactive bit toggles, endianness swapping, IEEE-754 decoding, and C-style expression evaluation.

## Contents

- `CalculatorEngine.hpp` (`105` lines):
  - Declares `EvalResult` and pure numeric conversion, bitwise, and parsing APIs free of `QtWidgets`.
- `CalculatorEngine.cpp`:
  - Implements recursive-descent C-style integer and floating-point expression parsing.
  - Handles 8/16/32/64-bit word-width masking, sign extension, endianness byte swapping (`BSWAP`), bit rotation, and IEEE-754 float/double/ASCII decoding.
- `ProgrammerCalculatorDialog.hpp` (`262` lines):
  - Declares the floating `Qt::Window` dialog shell, radix line-edit slots, bit-grid buttons, and format displays.
- `ProgrammerCalculatorDialog.cpp`:
  - Synchronizes Hexadecimal, Signed/Unsigned Decimal, Octal, and Binary text inputs with an interactive 64-bit clickable bit-toggle grid.
  - Masks inactive bit cells visually when switching between 8-bit, 16-bit, 32-bit, and 64-bit word widths.

## Expected Behavior & Invariants

- **Bidirectional Radix Sync Without Loops**:
  - Editing any radix box, clicking any bit button (`0..63`), changing word width (`8/16/32/64`), or evaluating an expression updates all sibling displays atomically under update guards (`QSignalBlocker` / internal guard flag).
- **Safe Integer & Division Semantics**:
  - `CalculatorEngine` handles shift counts `>= width`, negative shifts, and zero divisors cleanly via `EvalResult` errors without C++ undefined behavior.
- **UI-Free Math Backend**:
  - `CalculatorEngine` compiles without `QtWidgets`, keeping numeric parsing compact and modular.
- **Verification**:
  - Run `meson test -C builddir ui_tests --print-errorlogs` after modifying radix sync, bit grid behavior, or evaluator rules.

## Maintainer & Agent Notes

- Logic Separation:
  - Place all new operators, IEEE-754 helpers, and numeric formatters in `CalculatorEngine.{hpp,cpp}` rather than embedding parsing logic inside UI slots in `ProgrammerCalculatorDialog.cpp`.
- UI Selectors & Keyboard Ergonomics:
  - Preserve stable widget `objectName()` attributes across radix fields, word-width toggles, and bit buttons so `tests/ui/test_ui_capture.cpp` and UX checks continue matching cleanly.
  - Keep floating window focus, `Enter` evaluation, and `Esc` dismissal responsive without stealing main-window debugger shortcuts.
- Visual & File Hygiene:
  - Keep `ThemeManager` palette styling consistent across Light and Dark themes without hardcoding conflicting colors.
  - Avoid blocking modal prompts on expression evaluation errors; surface diagnostics inline inside the calculator HUD.
  - Maintain strict `< 950` lines per file and keep Google C++ style (`#pragma once`, `override`, `m_camelCase`).
  - Update this `README.md` whenever adding new math evaluators or floating tools.
