# System & Telemetry Utilities (`src/core/hpc/system/`)

Linux `/proc` process and MPI-rank discovery scanner paired with a constant-rate MTU-padded telemetry shaper for SSH transport hardening.

## Contents

- `ProcessScanner.hpp` (`96` lines), `ProcessScanner.cpp`:
  - Defines `ProcessEntry` (`pid`, `ppid`, `uid`, `name`, `cmdline`, `executablePath`, `user`, `mpiRank`, `mpiWorldSize`, `mpiJobTag`) and `RemoteTargetSpec`.
  - Scans `/proc/[pid]/comm`, `cmdline`, `exe`, `status`, and `environ` (`OMPI_COMM_WORLD_RANK`, `PMI_RANK`, `PMIX_RANK`, `SLURM_PROCID`).
  - Groups sibling MPI ranks under shared job tags and inspects `/proc/sys/kernel/yama/ptrace_scope` prior to local attach.
- `TelemetryObfuscator.hpp` (`66` lines), `TelemetryObfuscator.cpp`:
  - Implements `TelemetryObfuscator` (`QObject`), buffering outbound telemetry payloads under mutex protection.
  - Emits constant `1400`-byte (`MTU_THRESHOLD`) frames every `50 ms` (`FLUSH_INTERVAL_MS`, 20 Hz) padded with PRNG noise.
  - Covered offline by `tests/hpc/test_obfuscator.cpp`.

## Expected Behavior & Invariants

- **Transient `/proc` Tolerance**:
  - Processes exiting mid-scan or unreadable `/proc/[pid]/environ` and `/proc/[pid]/exe` entries must be skipped safely without throwing or stalling `AttachProcessDialog`.
- **Rank vs. PID Separation**:
  - Non-MPI processes retain `mpiRank == -1` and an empty `mpiJobTag` so callers never mistake OS PIDs for MPI world ranks.
- **Fixed Packet Frame Size**:
  - Every packet emitted by `TelemetryObfuscator` matches exact `MTU_THRESHOLD` (`1400` bytes) with deterministic length headers at 20 Hz.
- **No Unbounded Queues**:
  - Outbound buffers slice cleanly into fixed MTU chunks across consecutive flush ticks without leaking packet boundaries or dropping partial payloads.
- **Ptrace Scope Inspection**:
  - `ProcessScanner` checks `kernel.yama.ptrace_scope` explicitly so attach dialogs surface actionable guidance instead of cryptic permission failures.
- **Verification**:
  - Run `meson test -C builddir obfuscator_tests ui_tests --print-errorlogs` after modifying telemetry framing or process scanning logic.

## Maintainer & Agent Notes

- Linux `/proc` & Yama Isolation:
  - Keep Linux `/proc` and Yama `ptrace_scope` inspection isolated inside `ProcessScanner` rather than scattering raw `/proc` file reads across UI dialogs or backend coordinators.
  - Ensure every supported MPI launcher environment family (`OMPI_*`, `PMI_*`, `PMIX_*`, `SLURM_*`) preserves fallback detection cleanly.
- Packet Shaper Isolation:
  - Keep `TelemetryObfuscator` independent of `QtWidgets` so `obfuscator_tests` in `meson.build` builds cleanly against `QtCore` only.
  - Guard mutex lock scopes tightly around queue push/pop operations so timer ticks never contend on slow callbacks.
- Operational Hygiene:
  - Avoid blocking sleeps or synchronous recursive directory walks on the GUI thread when invoking `ProcessScanner`.
  - Document packet framing and padding invariants clearly, keep every translation unit well below `< 950` lines, and update this `README.md` when adding new OS probes.
