# GridLock Themes & Font Settings

GridLock includes **8 built-in color themes** (5 dark and 3 light) for the editor, disassembly view, control-flow graph, and dock panels.

You can switch themes at any time in **`Edit -> Preferences (Ctrl+,) -> Appearance`**. The dialog shows a 5-color swatch preview (`Canvas`, `Header`, `Border`, `Accent`, `Text`) for the selected theme and updates the main window, source editor, disassembly/CFG/decompiler views, and dialogs immediately when applied.

---

## Color Palette Reference Table

| Theme Name | Internal ID | Canvas (`bg`) | Header (`hdr`) | Structural Border | Primary Accent | Primary Text | Polarity |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **GridLock Slate** | `gridlock_slate` | `#16181d` | `#21252b` | `#2c313a` | `#00a2ff` | `#d4d4d4` | Dark (Default) |
| **Obsidian Teal** | `dark_teal` | `#111a1d` | `#172429` | `#22353c` | `#00bfa5` | `#d8e6e7` | Dark |
| **Tokyo Night** | `tokyo_night` | `#1a1b26` | `#24283b` | `#3b4261` | `#7aa2f7` | `#c0caf5` | Dark |
| **Catppuccin Mocha** | `catppuccin_mocha` | `#1e1e2e` | `#181825` | `#313244` | `#89b4fa` | `#cdd6f4` | Dark |
| **Gruvbox Industrial** | `gruvbox_dark` | `#1d2021` | `#282828` | `#3c3836` | `#fe8019` | `#ebdbb2` | Dark |
| **Daylight Paper (Light)** | `daylight_light` | `#f5f6f8` | `#e6e9ef` | `#ccd0da` | `#1e66f5` | `#1f2328` | Light |
| **Solarized Warm (Light)** | `solarized_light` | `#fdf6e3` | `#eee8d5` | `#d3cbb7` | `#268bd2` | `#586e75` | Light |
| **Catppuccin Latte (Light)** | `catppuccin_latte` | `#eff1f5` | `#e6e9ef` | `#ccd0da` | `#1e66f5` | `#4c4f69` | Light |

---

## Font Settings

In **`Edit -> Preferences -> Appearance`**, you can adjust UI and code fonts independently without restarting GridLock:

1. **UI Font Family (`appearance/ui_font_family`):** Font used for menus, dock headers, trees, tables, buttons, and status bars (`Inter`, `Cantarell`, `Noto Sans`, `DejaVu Sans`, `Liberation Sans`, or `System Default`).
2. **UI Font Size (`appearance/ui_font_size`, `8 pt – 24 pt`):** Scales widget labels and control heights across menus, toolbars, tables, and tabs.
3. **Code / Disassembly Font Family (`appearance/code_font_family`):** Monospace font used in `SourceCodeView`, `DisassemblyView` (linear assembly, CFG, and decompiler), and `MemView` (`Cascadia Code`, `Fira Code`, `DejaVu Sans Mono`, `Liberation Mono`, `Source Code Pro`, or `Monospace`).
4. **Code / Disassembly Font Size (`appearance/code_font_size`, `8 pt – 24 pt`):** Scales editor line numbers, assembly listings, CFG nodes, decompiler output, and the hex memory viewer. The embedded terminal (`TerminalDockWidget`) maintains its own monospace terminal font so character grid alignment is preserved.

---

## Theme Screenshots

### 1. GridLock Slate (`gridlock_slate`)
Default dark slate theme with blue accents.

![GridLock Slate Theme](screenshots/theme_gridlock_slate.png)

---

### 2. Obsidian Teal (`dark_teal`)
Dark blue-green background with teal accents.

![Obsidian Teal Theme](screenshots/theme_obsidian_teal.png)

---

### 3. Tokyo Night (`tokyo_night`)
Dark indigo background with soft blue highlights.

![Tokyo Night Theme](screenshots/theme_tokyo_night.png)

---

### 4. Catppuccin Mocha (`catppuccin_mocha`)
Charcoal background with pastel blue and lavender tones.

![Catppuccin Mocha Theme](screenshots/theme_catppuccin_mocha.png)

---

### 5. Gruvbox Industrial (`gruvbox_dark`)
Warm dark background with amber and orange highlights.

![Gruvbox Industrial Theme](screenshots/theme_gruvbox_dark.png)

---

### 6. Daylight Paper (`daylight_light`)
Neutral light background with dark text and blue accents for bright rooms or screenshots.

![Daylight Paper Light Theme](screenshots/theme_daylight_light.png)

---

### 7. Solarized Warm (`solarized_light`)
Warm cream background with muted slate text and blue accents based on Solarized Light.

---

### 8. Catppuccin Latte (`catppuccin_latte`)
Soft light background with dark slate text and blue accents from the Catppuccin Latte palette.
