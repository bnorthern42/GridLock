# Security Policy

## Supported Versions

The following table indicates which versions of GridLock currently receive security updates.

| Version | Supported | Notes |
| :--- | :---: | :--- |
| `0.9.x` (`>= 0.9.0`) | ✅ | Current `0.x` release line (`0.9.1`, best-effort pre-`1.0.0` security fixes) |
| `< 0.9.0` | ❌ | Unsupported |

> [!NOTE]
> GridLock is pre-`1.0.0` software. Security fixes target the latest `0.9.x` release line on `master` rather than backporting to older `0.8.x` or earlier snapshots.

## Threat Model & Scope

GridLock is an HPC-focused graphical debugger frontend for GDB (`GDB/MI`) and LLDB (`DAP`). As such, it interfaces with low-level kernel APIs, external debug servers, dynamically loaded native plugins, and remote batch-scheduling systems. We consider the following attack surfaces within our threat model and actively defend against them:

* **Native C99 Plugin Trust & Isolation (`v0.9.0`):** Workspace-scoped native plugin binaries (`<workspace>/.gridlock/plugins/*.so`, `.dylib`, `.dll`) are never loaded via `dlopen()` until explicitly approved by `SHA-256` fingerprint in the user-owned `[plugins.trusted_workspace_binaries]` allowlist (`~/.config/gridlock/config.toml`). `PluginManager` rejects world-writable directories/binaries and symlinks escaping the plugin root prior to load, `PluginInstaller` executes `git` and build tools strictly via shell-free `QProcess` argument vectors, and `PluginHostBridge` enforces PCRE2 backtracking limits and strict buffer/quota caps across all 28 C99 callbacks.
* **GDB/MI Telemetry Parsing:** We defend against stack exhaustion, buffer overflows, and format string attacks that could be initiated via poisoned `gdbserver` outputs. These are mitigated by strict 32-level nesting and 64 KB payload limits (`MiSanitizer`) within the telemetry parsing engine.
* **Network Side-Channel Leaks:** We actively prevent the inference of HPC payload structures (for example, sparse matrix non-zero density) via network packet timing and size analysis through `TelemetryObfuscator`.
* **Local `ptrace` & `process_vm_readv` Memory Boundaries:** We enforce strict bounds checking (`MemoryBoundsValidator`: null-page guard, natural alignment, 64-bit overflow prevention, and 64 MiB read / 16 MiB write caps) during `process_vm_readv` native kernel memory extraction and honor OS `kernel.yama.ptrace_scope` restrictions.
* **SLURM / Spack Command Injection:** We explicitly defend against arbitrary code execution by validating and escaping arguments during package queries and batch template generation.

## Reporting a Vulnerability

**DO NOT OPEN A PUBLIC GITHUB ISSUE TO REPORT A VULNERABILITY.**

If you discover a security vulnerability within GridLock, please use the GitHub Security Advisory feature. Navigate to the **Security** tab of the GridLock repository and click **Report a vulnerability** to submit a private report.

When submitting your report, you **must** include the following:
1. A fully reproducible Proof of Concept (PoC) demonstrating the exploit.
2. Exact environment details, including:
   - Your Linux distribution and Wayland compositor / X11 version.
   - The MPI implementation (for example, OpenMPI, MPICH) and version.
   - The debugger backend (for example, GDB, LLDB) and version.

Our team will evaluate the report and work with you privately to triage, patch, and properly disclose the issue.
