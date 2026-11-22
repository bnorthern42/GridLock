/**
 * @file e2e_helpers.mjs
 * @brief Zero-dependency Node.js process, compiler, filesystem, and hygiene helpers for GridLock CLI E2E tests.
 */

import { spawn, spawnSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { performance } from 'node:perf_hooks';

/**
 * Parses CLI flags passed to `scripts/e2e_cli_suite.mjs`.
 * @param {string[]} argv Raw `process.argv.slice(2)` tokens.
 */
export function parseSuiteArgs(argv) {
  const opts = { filter: '', verbose: false, keepTmp: false, help: false };
  for (let i = 0; i < argv.length; ++i) {
    const arg = argv[i];
    if (arg === '--verbose' || arg === '-v') {
      opts.verbose = true;
    } else if (arg === '--keep-tmp') {
      opts.keepTmp = true;
    } else if (arg === '--help' || arg === '-h') {
      opts.help = true;
    } else if (arg === '--filter' && i + 1 < argv.length) {
      opts.filter = argv[++i];
    } else if (arg.startsWith('--filter=')) {
      opts.filter = arg.slice('--filter='.length);
    } else {
      throw new Error(`Unknown option for e2e_cli_suite.mjs: ${arg}`);
    }
  }
  return opts;
}

/**
 * Resolves an executable in `PATH` or standard Linux OpenMPI/MPICH directories.
 * @param {string} name Executable basename (e.g. `mpic++`, `mpirun`, `g++`).
 * @returns {string|null} Absolute executable path if found, or `null`.
 */
export function findExecutable(name) {
  const extraDirs = [
    '/usr/lib64/openmpi/bin',
    '/usr/lib/openmpi/bin',
    '/usr/lib64/mpich/bin',
    '/usr/lib/mpich/bin',
    '/usr/local/bin',
    '/usr/bin',
    '/bin',
  ];
  const pathDirs = (process.env.PATH || '').split(path.delimiter).filter(Boolean);
  for (const dir of [...pathDirs, ...extraDirs]) {
    const candidate = path.join(dir, name);
    try {
      fs.accessSync(candidate, fs.constants.X_OK);
      if (fs.statSync(candidate).isFile()) return candidate;
    } catch {
      // Continue scanning directories.
    }
  }
  return null;
}

/**
 * Compiles a C or C++ source file into `outBin` with `-g -O0`.
 * Uses `mpicc`/`mpic++` automatically if the source includes `mpi.h`.
 */
export function compileTargetSource(srcFile, outBin, compilers, verbose = false) {
  const srcText = fs.readFileSync(srcFile, 'utf8');
  const usesMpi = /#\s*include\s*[<"]mpi\.h[>"]/m.test(srcText);
  const isC = path.extname(srcFile).toLowerCase() === '.c';
  const compiler = usesMpi
    ? (isC ? compilers.mpicc : compilers.mpicxx)
    : (isC ? compilers.gcc : compilers.gxx);

  if (!compiler) {
    return { ok: false, skipped: true, reason: `Missing compiler for ${path.basename(srcFile)} (MPI=${usesMpi})` };
  }

  const env = {
    ...process.env,
    PATH: `/usr/lib64/openmpi/bin:/usr/lib/openmpi/bin:${process.env.PATH || ''}`,
  };
  const args = ['-g', '-O0', srcFile, '-o', outBin];
  if (verbose) {
    process.stdout.write(`  [compile] ${compiler} ${args.join(' ')}\n`);
  }
  const res = spawnSync(compiler, args, { encoding: 'utf8', env, timeout: 25000 });
  if (res.error || res.status !== 0) {
    return {
      ok: false,
      skipped: false,
      reason: `Compile failed (exit ${res.status}): ${(res.stderr || res.stdout || String(res.error)).trim()}`,
    };
  }
  return { ok: true, skipped: false, usesMpi, binaryPath: outBin };
}

/**
 * Builds an isolated child environment for `./builddir/gridlock` invocations.
 */
export function buildIsolatedEnv(xdgConfigHome, tmpSubdir, extraEnv = {}) {
  fs.mkdirSync(xdgConfigHome, { recursive: true });
  fs.mkdirSync(tmpSubdir, { recursive: true });
  const env = { ...process.env };
  delete env.DISPLAY;
  delete env.WAYLAND_DISPLAY;
  delete env.GRIDLOCK_LIVE_WAYLAND;
  delete env.GRIDLOCK_TEST_CONFIG_DIR;

  return {
    ...env,
    XDG_CONFIG_HOME: xdgConfigHome,
    TMPDIR: tmpSubdir,
    QT_QPA_PLATFORM: 'offscreen',
    FI_PROVIDER: '^psm3',
    OMPI_MCA_btl: '^openib',
    PATH: `/usr/lib64/openmpi/bin:/usr/lib/openmpi/bin:${env.PATH || ''}`,
    LD_LIBRARY_PATH: `/usr/lib64/openmpi/lib:/usr/lib/openmpi/lib:${env.LD_LIBRARY_PATH || ''}`,
    ...extraEnv,
  };
}

/**
 * Seeds `default_ranks` inside `<xdgConfigHome>/gridlock/config.toml` prior to launching GridLock.
 */
export function seedDefaultRanksConfig(xdgConfigHome, ranks) {
  const cfgDir = path.join(xdgConfigHome, 'gridlock');
  fs.mkdirSync(cfgDir, { recursive: true });
  const cfgFile = path.join(cfgDir, 'config.toml');
  const content = [
    '[global]',
    'clangd_path = "clangd"',
    'dap_adapter_path = "lldb-dap"',
    'gdb_path = "gdb"',
    'mpi_launch_args = "--oversubscribe"',
    '',
    '[debugger]',
    `default_ranks = ${ranks}`,
    'gdb_path = "gdb"',
    '',
  ].join('\n');
  fs.writeFileSync(cfgFile, content, 'utf8');
}

/**
 * Spawns a child command with a hard timeout, capturing `stdout`, `stderr`, exit code, signal, and duration.
 */
export function runProcess(cmd, args, { cwd, env, timeoutMs = 20000, verbose = false, sendSignalAfterMs = 0 } = {}) {
  return new Promise((resolve) => {
    const startMs = performance.now();
    if (verbose) {
      process.stdout.write(`  [exec] ${cmd} ${args.map((a) => (/\s/.test(a) ? JSON.stringify(a) : a)).join(' ')}\n`);
    }

    const child = spawn(cmd, args, {
      cwd,
      env,
      stdio: ['ignore', 'pipe', 'pipe'],
    });

    let stdout = '';
    let stderr = '';
    let timedOut = false;
    let sigTimer = null;
    let killTimer = null;

    child.stdout.setEncoding('utf8');
    child.stderr.setEncoding('utf8');
    child.stdout.on('data', (chunk) => { stdout += chunk; });
    child.stderr.on('data', (chunk) => { stderr += chunk; });

    if (sendSignalAfterMs > 0) {
      sigTimer = setTimeout(() => {
        if (child.exitCode === null && child.signalCode === null) {
          child.kill('SIGTERM');
        }
      }, sendSignalAfterMs);
    }

    const timeoutHandle = setTimeout(() => {
      timedOut = true;
      child.kill('SIGTERM');
      killTimer = setTimeout(() => {
        if (child.exitCode === null && child.signalCode === null) {
          child.kill('SIGKILL');
        }
      }, 1500);
    }, timeoutMs);

    child.on('close', (code, signal) => {
      clearTimeout(timeoutHandle);
      if (sigTimer) clearTimeout(sigTimer);
      if (killTimer) clearTimeout(killTimer);
      const durationMs = Math.round(performance.now() - startMs);
      resolve({ code, signal, timedOut, stdout, stderr, durationMs });
    });

    child.on('error', (err) => {
      clearTimeout(timeoutHandle);
      if (sigTimer) clearTimeout(sigTimer);
      if (killTimer) clearTimeout(killTimer);
      const durationMs = Math.round(performance.now() - startMs);
      resolve({ code: -1, signal: null, timedOut, stdout, stderr: `${stderr}\n${err.message}`, durationMs });
    });
  });
}

/**
 * Asserts that `stderr` contains zero fatal/critical Qt diagnostic lines or sanitizer/segfault crashes.
 */
export function assertNoFatalDiagnostics(res, contextLabel) {
  if (res.timedOut) {
    throw new Error(`${contextLabel} timed out after ${res.durationMs}ms`);
  }
  if (res.signal) {
    throw new Error(`${contextLabel} terminated by signal ${res.signal}`);
  }
  const badPatterns = [
    /\[FATAL\]/,
    /\[CRITICAL\]/,
    /AddressSanitizer/,
    /UndefinedBehaviorSanitizer/,
    /Segmentation fault/i,
  ];
  for (const rx of badPatterns) {
    if (rx.test(res.stderr)) {
      throw new Error(`${contextLabel} emitted fatal stderr matching ${rx}: ${tailText(res.stderr, 12)}`);
    }
  }
}

/**
 * Snapshots `/dev/shm/psm3_shm*`, `/tmp/gridlock_mi_*`, and debugger processes owned by the current UID.
 */
export function snapshotSystemHygiene(tmpRootPrefix = '') {
  const uid = typeof process.getuid === 'function' ? process.getuid() : -1;
  const shmFiles = new Set();
  try {
    for (const name of fs.readdirSync('/dev/shm')) {
      if (name.startsWith('psm3_shm')) shmFiles.add(name);
    }
  } catch {
    // /dev/shm unavailable on non-Linux hosts.
  }

  const fifoDirs = new Set();
  try {
    for (const name of fs.readdirSync(os.tmpdir())) {
      if (name.startsWith('gridlock_mi_')) fifoDirs.add(name);
    }
  } catch {
    // Ignore unreadable tmp entries.
  }

  const debuggerProcs = new Map();
  try {
    for (const entry of fs.readdirSync('/proc')) {
      if (!/^\d+$/.test(entry)) continue;
      const pid = Number(entry);
      if (pid === process.pid) continue;
      try {
        const statusRaw = fs.readFileSync(`/proc/${entry}/status`, 'utf8');
        if (uid >= 0) {
          const uidMatch = statusRaw.match(/^Uid:\s+(\d+)/m);
          if (!uidMatch || Number(uidMatch[1]) !== uid) continue;
        }
        const stateMatch = statusRaw.match(/^State:\s+(\S+)/m);
        if (stateMatch && stateMatch[1] === 'Z') continue;
        const comm = fs.readFileSync(`/proc/${entry}/comm`, 'utf8').trim();
        const cmdBytes = fs.readFileSync(`/proc/${entry}/cmdline`);
        const cmdline = cmdBytes.toString('utf8').replace(/\0+/g, ' ').trim();
        const isDbg = /^(gdb|mpirun|mpiexec|orterun|prterun|orted|prted)$/.test(comm);
        const touchesTmp = tmpRootPrefix && cmdline.includes(tmpRootPrefix);
        if (isDbg || touchesTmp) {
          debuggerProcs.set(pid, { pid, comm, cmdline });
        }
      } catch {
        // Individual PID exited during scan; continue scanning remaining /proc entries.
      }
    }
  } catch {
    // /proc unavailable.
  }

  return { shmFiles, fifoDirs, debuggerProcs };
}

/**
 * Returns the last `maxLines` lines of `text` for concise failure diagnostics.
 */
export function tailText(text, maxLines = 20) {
  const lines = String(text || '').trim().split(/\r?\n/).filter(Boolean);
  if (lines.length <= maxLines) return lines.join('\n');
  return `... (${lines.length - maxLines} earlier lines omitted)\n` + lines.slice(-maxLines).join('\n');
}

/**
 * Renders structured terminal summary tables (per-suite breakdown + per-case pass/fail matrix + failed recap).
 * @param {Array<{name: string, status: 'PASS'|'FAIL'|'SKIP', durationMs: number, detail: string}>} results
 * @param {number} totalMs
 */
export function renderE2eSummaryTables(results, totalMs) {
  const useColor = Boolean(process.stdout.isTTY && !process.env.NO_COLOR);
  const paint = (txt, ansi) => (useColor ? `\x1b[${ansi}m${txt}\x1b[0m` : txt);
  const badge = (status) => {
    const padded = status.padEnd(6);
    if (status === 'PASS') return paint(padded, '32;1');
    if (status === 'FAIL') return paint(padded, '31;1');
    return paint(padded, '33');
  };

  // 1. Suite-level aggregation table.
  const suiteOrder = [];
  const suites = new Map();
  for (const r of results) {
    const slash = r.name.indexOf('/');
    const suiteName = slash > 0 ? r.name.slice(0, slash) : 'general';
    if (!suites.has(suiteName)) {
      suiteOrder.push(suiteName);
      suites.set(suiteName, { pass: 0, fail: 0, skip: 0, total: 0, ms: 0 });
    }
    const bucket = suites.get(suiteName);
    bucket.total += 1;
    bucket.ms += r.durationMs;
    if (r.status === 'PASS') bucket.pass += 1;
    else if (r.status === 'FAIL') bucket.fail += 1;
    else bucket.skip += 1;
  }

  const suiteW = Math.max(10, ...suiteOrder.map((s) => s.length));
  const sBorder = `+-${'-'.repeat(suiteW)}-+--------+--------+---------+-------+-----------+`;
  process.stdout.write('\n=== GridLock E2E Suite Summary ===\n');
  process.stdout.write(`${sBorder}\n`);
  process.stdout.write(
    `| ${'SUITE'.padEnd(suiteW)} | PASSED | FAILED | SKIPPED | TOTAL |  DURATION |\n`,
  );
  process.stdout.write(`${sBorder}\n`);
  for (const s of suiteOrder) {
    const b = suites.get(s);
    process.stdout.write(
      `| ${s.padEnd(suiteW)} | ${String(b.pass).padStart(6)} | ${String(b.fail).padStart(6)} | ${String(b.skip).padStart(7)} | ${String(b.total).padStart(5)} | ${(String(b.ms) + 'ms').padStart(9)} |\n`,
    );
  }
  process.stdout.write(`${sBorder}\n`);

  // 2. Full per-test Pass/Fail matrix table.
  const idxW = Math.max(2, String(results.length).length);
  const nameW = Math.max(28, ...results.map((r) => r.name.length));
  const cBorder = `+-${'-'.repeat(idxW)}-+--------+-----------+--${'-'.repeat(nameW)}-+`;
  process.stdout.write('\n=== GridLock E2E Test Case Matrix ===\n');
  process.stdout.write(`${cBorder}\n`);
  process.stdout.write(
    `| ${'#'.padStart(idxW)} | RESULT |  DURATION | ${'TEST CASE'.padEnd(nameW)} |\n`,
  );
  process.stdout.write(`${cBorder}\n`);
  results.forEach((r, idx) => {
    const dur = `${r.durationMs}ms`.padStart(9);
    const label = r.status === 'SKIP' && r.detail ? `${r.name} (${r.detail})` : r.name;
    process.stdout.write(
      `| ${String(idx + 1).padStart(idxW)} | ${badge(r.status)} | ${dur} | ${label.slice(0, nameW).padEnd(nameW)} |\n`,
    );
  });
  process.stdout.write(`${cBorder}\n`);

  const passed = results.filter((r) => r.status === 'PASS').length;
  const failedItems = results.filter((r) => r.status === 'FAIL');
  const skipped = results.filter((r) => r.status === 'SKIP').length;

  process.stdout.write(
    `Totals: ${passed} passed, ${failedItems.length} failed, ${skipped} skipped (${results.length} total) in ${totalMs}ms\n`,
  );
  if (failedItems.length === 0) {
    process.stdout.write('Failed Cases: None (all executed E2E checks passed)\n');
  } else {
    process.stdout.write(`Failed Cases (${failedItems.length}):\n`);
    for (const f of failedItems) {
      const firstLine = String(f.detail || '').split(/\r?\n/)[0] || 'Assertion failed';
      process.stdout.write(`  - [FAIL] ${f.name}: ${firstLine}\n`);
    }
  }
  return { passed, failed: failedItems.length, skipped };
}

