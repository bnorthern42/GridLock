# Accessibility Statement

## Commitment & Goals

GridLock aims to be usable across Linux desktop environments and tiling window manager setups. Our engineering targets include:

- **WCAG AA Text Contrast**: Built-in dark and light workstation themes target readable contrast for source code, disassembly, tables, and control labels.
- **Full Keyboard Reachability**: Core debugging workflows (target launch, stepping, breakpoints, view switching, and dialog dismissal) are reachable via keyboard shortcuts and standard tab traversal.
- **Non-Color-Only Status Indicators**: Rank states, breakpoint types, and diagnostic alerts pair color with text badges, icons, and structured table columns rather than relying on color alone.

## Supported Environments & Built-In Accessibility Features

### Desktop & Assistive Technology

- **Native Qt6 Accessibility**: GridLock is built with Qt6, integrating with the Linux AT-SPI2 accessibility bridge via `QAccessible` on both Wayland and X11.
- **Command-Line Interface**: Standard CLI flags (`--help`, `--version`, `--headless`) work in terminal and screen-reader environments with standard POSIX exit codes.

### Colorblind-Safe & High-Contrast Palettes

- **Built-In Dark & Light Themes**: GridLock provides curated dark and light workstation themes (`GridLock Slate`, `Obsidian Teal`, `Tokyo Night`, `Catppuccin Mocha`, `Gruvbox Dark`, `Daylight Light`, `Solarized Light`, `Catppuccin Latte`) selectable in `Preferences -> Appearance`.
- **Memory Profiler Palettes**: The Library Memory Map Profiler (`Preferences -> Appearance -> Memory Map Color Scheme`) supports perceptually uniform and colorblind-safe palettes (`HighContrast` Okabe-Ito, `Viridis`, `Plasma`, `Ocean`, and `Distinct`) with automatic luminance-based foreground text contrast on treemap tiles.

### Typography & HiDPI Scaling

- **Configurable Fonts**: Source editor, disassembly, console, and terminal font families and point sizes are independently configurable under `Preferences -> Appearance` and `Preferences -> Editing`.
- **HiDPI & Fractional Scaling**: GridLock uses Qt6 fractional and HiDPI scaling (`QT_SCALE_FACTOR` and native Wayland compositor scaling).
- **Frameless & Tiling Compositor Support**: Dialogs expose explicit `✕ CLOSE` toolbar buttons and `Esc` bindings so users on frameless or tiling Wayland compositors (`niri`, `sway`, `Hyprland`) never rely on window-manager titlebars.

### Keyboard Navigation

- **Customizable Shortcuts**: Keybindings are managed through `ShortcutManager` and customizable under `Preferences -> Shortcuts`.
- **Execution Shortcuts**: Standard debugging actions use dedicated function keys (`F5` Continue/Run, `F9` Toggle Breakpoint, `F10` Step Over / Header-Source Switch, `F11` Step Into), alongside `F1` context-sensitive reference lookup.
- **Mouse Gesture Alternatives**: Gutter breakpoint actions and variable inspection actions provide explicit context-menu and keyboard alternatives.

## Known Limitations & Workarounds

- **Custom 2D Graphics Canvases**: Custom `QGraphicsView` and `QPainter` canvases (`DisassemblyView` 2D Control Flow Graph, `PointerGraphDialog` struct/pointer canvas, and `MemoryProfilerWidget` 2D treemap) do not currently expose per-node screen-reader sub-trees via AT-SPI2.
  - **Workaround**: Switch `DisassemblyView` to the synchronized linear disassembly mode, inspect structs in the `VariablesDockWidget` tree view, and use the sortable breakdown table inside `MemoryProfilerWidget`.
- **Embedded PTY Terminal**: The integrated terminal dock (`QTermWidget`) delegates accessibility events directly to the underlying terminal widget implementation.

## Contributor Guidelines

When modifying GridLock's UI, contributors should:

- Preserve keyboard navigation (`Tab` / `Shift+Tab` focus order) and visible focus indicators across all widgets and dialogs.
- Provide clear labels and accessible names/descriptions for form controls and icon-only buttons.
- Never convey status, errors, or rank state solely through color; pair colors with text labels, symbols, or icons.
- Include descriptive alt text when adding or updating screenshots in documentation.

## Reporting Accessibility Barriers

If you encounter a keyboard trap, missing label, low-contrast element, scaling defect, or assistive-technology barrier, please open an issue using the **[Accessibility Barrier Report](.github/ISSUE_TEMPLATE/accessibility.yml)** template.

Blocking barriers (`Critical` or `High` severity) are prioritized for remediation and followed up with documented workarounds whenever possible.
