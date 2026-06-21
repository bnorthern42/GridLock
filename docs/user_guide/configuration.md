# Configuration & Management

GridLock incorporates advanced configuration options to persist your debugging environment and optimize your keyboard-driven workflow.

## Shortcut Manager

To cater to various developer habits, GridLock features a robust **Shortcut Manager** capable of handling both standard IDE bindings and Vim-style chorded sequences.

* **Standard Chords**: Common actions are bound to intuitive chords, such as `Alt+R` to initiate a Run/Continue command, or `Alt+B` to toggle a breakpoint at the current cursor position.
* **Vim-style Integration**: For power users, the Shortcut Manager intercepts key presses at the application level to support modal operations and multi-key sequences without interfering with standard text input fields. 
* **Customization**: All shortcuts can be remapped in the Preferences dialog under the "Keybindings" section.

## Session Persistence

Setting up a complex MPI debugging session—specifying binary paths, command-line arguments, environment variables, and watchlists—can be tedious. GridLock resolves this via **Session Persistence**.

* **TOML Configuration**: Workspaces and session configurations are serialized to disk using the standard, human-readable TOML format (via `toml++`).
* **State Resumption**: Upon launching GridLock in a previously configured directory, the IDE automatically restores your binary paths, MPI run arguments, and the precise state of your Multi-Rank Differential Grid and active breakpoints.

## Reference Manual Integration

HPC environments frequently operate on air-gapped nodes or behind strict firewalls, making web-based documentation inaccessible. GridLock provides built-in offline documentation via the **Reference Manual** widget.

* **Docset Support**: GridLock loads and renders standard `.docset` databases (compatible with Dash/Zeal). 
* **Supported Contexts**: You can load docsets for C++ standards, MPI specifications, CMake, and Docker directly into the IDE.
* **Workflow**: Press `F1` while hovering over a standard library function or MPI call to immediately open the corresponding offline manual page in a dedicated dock, styled consistently with the IDE's dark theme.
