# Security Policy

## Supported Versions

The following table indicates which versions of GridLock currently receive security updates.

| Version | Supported          |
| ------- | ------------------ |
| >= 0.4.0 | :white_check_mark: |
| < 0.4.0  | :x:                |

## Threat Model & Scope

GridLock is an HPC-focused graphical debugger. As such, it interfaces with low-level kernel APIs, external debug servers, and remote batch-scheduling systems. We consider the following attack surfaces within our threat model and actively defend against them:

* **GDB/MI Telemetry Parsing:** We defend against stack exhaustion, buffer overflows, and format string attacks that could be initiated via poisoned `gdbserver` outputs. These are mitigated by our strict 32-level nesting and 64KB payload limits within the telemetry parsing engine.
* **Network Side-Channel Leaks:** We actively prevent the inference of HPC payload structures (e.g., sparse matrix non-zero density) via network packet timing and size analysis. This is mitigated through the implementation of our `TelemetryObfuscator`.
* **Memory Extraction Bounds:** We enforce strict bounds checking to prevent out-of-bounds reads or malicious memory strides during `process_vm_readv` native kernel memory extraction.
* **SLURM/Spack Command Injection:** We explicitly defend against arbitrary code execution by ensuring proper escaping and validation of shell formatting during remote batch job submission through SLURM or Spack.

## Reporting a Vulnerability

**DO NOT OPEN A PUBLIC GITHUB ISSUE TO REPORT A VULNERABILITY.**

If you discover a security vulnerability within GridLock, please use the GitHub Security Advisory feature. Navigate to the **Security** tab of the GridLock repository and click **Report a vulnerability** to submit a private report.

When submitting your report, you **must** include the following:
1. A fully reproducible Proof of Concept (PoC) demonstrating the exploit.
2. Exact environment details, including:
   - Your Wayland compositor and version.
   - The MPI implementation (e.g., OpenMPI, MPICH) and version.
   - The Debugger backend (e.g., GDB, LLDB) and version.

Our team will evaluate the report and work with you privately to triage, patch, and properly disclose the issue.
