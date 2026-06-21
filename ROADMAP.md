# GridLock IDE Roadmap

## PHASE 1: Debugger Stability

* [x] Breakpoint sync: Sync gutter-selected breakpoints to GDB.
* [x] Rank Control: Implement individual rank-pausing (using specific GDB thread-apply/interrupt commands).
* [x] Layout Persistence: Ensure `QPlainTextEdit`/ViewportMargins are robust against layout regressions.
* [x] GDB Process Manager: Robust teardown sequence to prevent dangling zombie processes.

## PHASE 2: Variable Watcher (The "CLion Clone")

* [ ] Expression Evaluator: Handle arbitrary C++ expressions (e.g., `calc + 5`).
* [x] Tooltips: Hover-variables in the code editor to see values.
* [x] Differential View: Dynamic variable tracking via GDB `-var-update`.

## PHASE 3: Advanced Debugger

* [x] MemView: Hex dump viewer for raw pointers (similar to `mcu-debug/memview`).
* [x] Register Viewer: Real-time tracking of CPU registers (EAX/RAX, RSP, etc.).
* [x] Conditional Breakpoints: Add logic for hit counts and conditions.

## PHASE 4: IDE Experience & Productization

* [ ] **Toolbar Robustness:** Fully decouple execution logic (Run, Continue, Step, Pause, Terminate) from UI lambdas; implement a Command Pattern for toolbar actions.
* [ ] **Editor Tabs:** Support opening multiple source files simultaneously via a tabbed editor interface.
* [ ] **Project Support:** Add a file explorer/project manager to handle entire repositories and directories.
* [ ] **Asset/Branding Pipeline:** Integrate official app icon (`.ico`/.icns/.png), splash screen, and high-DPI resource handling using Qt Resource System (`.qrc`).
* [ ] **Shortcut Manager:** Implement global IDE keybindings (Vim-style or CLion-style).
* [ ] **Session Persistence:** Save/load debug configurations (binary arguments, custom GDB paths, watchlists).
* [ ] **Deployment:** Create standardized install targets (`meson install`), desktop entries (`.desktop` file), and directory structure for cross-platform distribution.

#### Phase 5: The Polyglot Core (DAP Refactor)
    
* [ ] **Refactor `DebuggerBackend`:** Transform your abstract `DebuggerBackend` into a `DapBackend`.
* [ ] **Standardized Launch:** Implement the startup sequence for all language sessions:
* [ ] GridLock spawns the adapter process.
* [ ] GridLock performs a TCP handshake.
* [ ] GridLock sends a standard JSON-RPC `launch` command.
        
        
        
#### Phase 6: Cross-Language Variable Inspector
        
* [ ] **Unified Grid:** Utilize the standard `VariablesRequest` enforced by DAP so `DifferentialGrid` works for all languages.
* [ ] **The "Dream" Feature:** Enable simultaneous debugging of different language runtimes (e.g., C++, Python, Node.js) paused at the same logical simulation timestamp, with the `DifferentialGrid` tracking memory addresses and objects in parallel.
        
#### Phase 7: The Plugin Marketplace (Productionization)
        
* [ ] **Dynamic Loading:** Replace hardcoded language support with a `plugins/` directory system.
