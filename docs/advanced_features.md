# Architecture of Advanced Features

GridLock v0.4.6 introduces several advanced debugging and memory analysis features. This document outlines the internal mechanics and GDB/MI orchestration required to implement these capabilities without blocking the primary UI thread.

## Deadlock Detector (Barrier/Wait Analyzer)

The Deadlock Analyzer is designed to automatically detect when MPI ranks are indefinitely blocked in collective or point-to-point synchronization calls. Rather than requiring manual inspection of all ranks, the engine hooks into the `GdbRankCoordinator` to asynchronously parse `-stack-list-frames` when execution halts.

### Mechanics
1. Upon a `*stopped` asynchronous record from GDB, the analyzer triggers a frame fetch for depths 0-3 across all active ranks.
2. The output is parsed to identify blocking MPI primitives such as `MPI_Barrier`, `MPI_Wait`, `MPI_Waitall`, or `MPI_Recv`.
3. If a match is found, the specific rank is flagged in the UI (e.g., in the MpiDiagnosticsWidget) to alert the user of the stall location.

## Floating-Point Exception (FPE) Trapper

To aid in numerical stability debugging, GridLock can automatically halt execution across the cluster the moment a divergent calculation results in a NaN or Infinity.

### Mechanics
1. If the "Trap FPE" setting is enabled in the Configuration Manager, the `LaunchCommand` broadcasts a setup sequence to all active sessions.
2. The `GdbRankCoordinator` injects the specific command into the GDB/MI stream:
   ```bash
   catch signal SIGFPE
   ```
3. When the mathematical hardware traps a divergent value, GDB emits a `*stopped,reason="signal-received",signal-name="SIGFPE"` record.
4. The IDE intercepts this record, halts the cluster, and automatically focuses the SourceCodeView cursor precisely on the divergent line.

## Conditional Breakpoint Expressions

Users can define plain C++ expressions (e.g. `i == 100 && rank == 0`) directly from the editor gutter to halt numerical loops dynamically without authoring custom Python MI scripts.

### Mechanics
1. A lightweight `QDialog` captures the target expression string.
2. The `EditorTabManager` propagates the state to the `GdbRankCoordinator`.
3. The coordinator explicitly targets the location and applies the condition string strictly formatting the GDB/MI command as:
   ```bash
   -break-insert -c "<condition_string>" <location>
   ```
4. Visual cues are synced via `LineNumberArea` rendering custom orange dots for conditional variants.

## Value-Change Visual Highlighting

To reduce cognitive load during tight loop stepping, the `VariableTreeModel` and `RegisterView` cache previous execution values.

### Mechanics
1. Just before issuing a `-stack-list-variables` or register refresh, the active state is cached in a `QMap<QString, QString>`.
2. When the new DAP or MI payload is parsed, the updated values are compared against the cache.
3. If a drift is detected, the models return a specific Catppuccin-Mocha `Qt::BackgroundRole` highlight, triggering an instantaneous UI repaint that visually signals state mutation.
