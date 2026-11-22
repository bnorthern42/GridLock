#!/usr/bin/env node
/**
 * @file e2e_cli_suite.mjs
 * @brief Standalone zero-dependency Node.js end-to-end CLI and tutorial test harness for GridLock.
 *
 * Usage:
 *   ./scripts/e2e_cli_suite.mjs [--filter <substring>] [--verbose] [--keep-tmp]
 */

import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { performance } from 'node:perf_hooks';

import {
  assertNoFatalDiagnostics,
  buildIsolatedEnv,
  compileTargetSource,
  findExecutable,
  parseSuiteArgs,
  renderE2eSummaryTables,
  runProcess,
  seedDefaultRanksConfig,
  snapshotSystemHygiene,
  tailText,
} from './lib/e2e_helpers.mjs';

const REPO_ROOT = path.resolve(import.meta.dirname, '..');
const GRIDLOCK_BIN = path.join(REPO_ROOT, 'builddir', 'gridlock');
const TUTORIAL_DIR = path.join(REPO_ROOT, 'tutorial');

class SkipTestError extends Error {
  constructor(reason) {
    super(reason);
    this.name = 'SkipTestError';
  }
}

function printHelp() {
  process.stdout.write(
    [
      'Usage: ./scripts/e2e_cli_suite.mjs [options]',
      '',
      'Options:',
      '  --filter <substring>  Run only E2E cases whose name matches <substring>',
      '  --verbose, -v         Print compiler/process commands and live output summaries',
      '  --keep-tmp            Preserve the temporary sandbox directory after completion',
      '  --help, -h            Display this help message',
      '',
    ].join('\n'),
  );
}

async function sleep(ms) {
  return new Promise((r) => setTimeout(r, ms));
}

/**
 * Generates synthetic C++ sources and a >5 MB CSV fixture inside `tmpRoot`.
 */
function buildSyntheticTargets(tmpRoot, compilers, verbose) {
  const srcDir = path.join(tmpRoot, 'synthetic_src');
  const binDir = path.join(tmpRoot, 'synthetic_bin');
  const dataDir = path.join(tmpRoot, 'data');
  fs.mkdirSync(srcDir, { recursive: true });
  fs.mkdirSync(binDir, { recursive: true });
  fs.mkdirSync(dataDir, { recursive: true });

  const argsSrc = path.join(srcDir, 'args_echo.cpp');
  fs.writeFileSync(
    argsSrc,
    [
      '#include <cstdio>',
      'int main(int argc, char** argv) {',
      '  std::printf("E2E_ARGC=%d\\n", argc);',
      '  for (int i = 0; i < argc; ++i) {',
      '    std::printf("E2E_ARGV_%d=%s\\n", i, argv[i]);',
      '  }',
      '  std::fflush(stdout);',
      '  return 0;',
      '}',
      '',
    ].join('\n'),
    'utf8',
  );
  const argsOut = path.join(binDir, 'args_echo');
  const argsRes = compileTargetSource(argsSrc, argsOut, compilers, verbose);
  if (!argsRes.ok) throw new Error(`Failed compiling synthetic args_echo.cpp: ${argsRes.reason}`);

  const crashSrc = path.join(srcDir, 'crash_target.cpp');
  fs.writeFileSync(
    crashSrc,
    [
      '#include <csignal>',
      '#include <cstdio>',
      '#include <cstdlib>',
      '#include <cstring>',
      'int main(int argc, char** argv) {',
      '  const char* mode = (argc > 1) ? argv[1] : "segv";',
      '  std::printf("E2E_CRASH_MODE=%s\\n", mode);',
      '  std::fflush(stdout);',
      '  if (std::strcmp(mode, "abrt") == 0) {',
      '    std::abort();',
      '  }',
      '  std::raise(SIGSEGV);',
      '  return 1;',
      '}',
      '',
    ].join('\n'),
    'utf8',
  );
  const crashOut = path.join(binDir, 'crash_target');
  const crashRes = compileTargetSource(crashSrc, crashOut, compilers, verbose);
  if (!crashRes.ok) throw new Error(`Failed compiling synthetic crash_target.cpp: ${crashRes.reason}`);

  // Generate a >5 MB CSV file (~5.25 MB) using buffered chunks for sub-50ms creation.
  const csvPath = path.join(dataDir, 'telemetry_5mb.csv');
  const fd = fs.openSync(csvPath, 'w');
  fs.writeSync(fd, 'step,rank,node,elapsed_ms,residual_l2,ghost_flux,bandwidth_gbps,status\n');
  let chunk = '';
  let bytesWritten = 0;
  const targetBytes = 5 * 1024 * 1024 + 262144;
  let row = 0;
  while (bytesWritten < targetBytes) {
    chunk += `${row},${row % 8},compute-${row % 4},${(row * 0.125).toFixed(3)},1.234567e-05,0.987654,42.50,OK\n`;
    ++row;
    if (chunk.length >= 65536) {
      bytesWritten += fs.writeSync(fd, chunk);
      chunk = '';
    }
  }
  if (chunk.length > 0) bytesWritten += fs.writeSync(fd, chunk);
  fs.closeSync(fd);

  return { argsBin: argsOut, crashBin: crashOut, csvPath, csvBytes: bytesWritten };
}

async function main() {
  let opts;
  try {
    opts = parseSuiteArgs(process.argv.slice(2));
  } catch (err) {
    process.stderr.write(`${err.message}\n`);
    printHelp();
    process.exit(2);
  }
  if (opts.help) {
    printHelp();
    process.exit(0);
  }

  if (!fs.existsSync(GRIDLOCK_BIN)) {
    process.stderr.write(`Missing GridLock binary at ${GRIDLOCK_BIN}. Run 'ninja -C builddir' first.\n`);
    process.exit(1);
  }

  const compilers = {
    gcc: findExecutable('gcc'),
    gxx: findExecutable('g++'),
    mpicc: findExecutable('mpicc'),
    mpicxx: findExecutable('mpic++') || findExecutable('mpicxx'),
    mpirun: findExecutable('mpirun') || findExecutable('mpiexec'),
    gdb: findExecutable('gdb'),
  };

  const tmpRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'gridlock-e2e-'));
  let cleanedUp = false;
  const cleanup = () => {
    if (cleanedUp) return;
    cleanedUp = true;
    if (opts.keepTmp) {
      process.stdout.write(`Preserved E2E temp directory: ${tmpRoot}\n`);
    } else {
      fs.rmSync(tmpRoot, { recursive: true, force: true });
    }
  };
  process.on('exit', cleanup);
  process.on('SIGINT', () => { cleanup(); process.exit(130); });
  process.on('SIGTERM', () => { cleanup(); process.exit(143); });

  const baselineHygiene = snapshotSystemHygiene(tmpRoot);
  const suiteStartMs = performance.now();

  if (opts.verbose) {
    process.stdout.write(`Temp sandbox: ${tmpRoot}\n`);
    process.stdout.write(`Compilers: ${JSON.stringify(compilers, null, 2)}\n`);
  }

  const synthetic = buildSyntheticTargets(tmpRoot, compilers, opts.verbose);

  // Discover all C/C++ tutorial sources in tutorial/ sorted deterministically.
  const tutorialSources = fs
    .readdirSync(TUTORIAL_DIR)
    .filter((f) => /\.(c|cpp|cc|cxx)$/i.test(f))
    .sort()
    .map((f) => path.join(TUTORIAL_DIR, f));

  const tutorialBinDir = path.join(tmpRoot, 'tutorial_bin');
  fs.mkdirSync(tutorialBinDir, { recursive: true });
  const compiledTutorials = new Map();

  let caseCounter = 0;
  const freshEnv = (label, extraEnv = {}) => {
    const id = `${++caseCounter}_${label.replace(/[^a-zA-Z0-9_-]/g, '_')}`;
    return buildIsolatedEnv(path.join(tmpRoot, 'xdg', id), path.join(tmpRoot, 'tmp', id), extraEnv);
  };

  /** @type {Array<{name: string, timeoutMs: number, fn: () => Promise<void>}>} */
  const tests = [];
  const register = (name, timeoutMs, fn) => tests.push({ name, timeoutMs, fn });

function formatResTail(res) {
  return `exit=${res.code} sig=${res.signal} time=${res.durationMs}ms\n--- stdout tail ---\n${tailText(res.stdout, 15)}\n--- stderr tail ---\n${tailText(res.stderr, 15)}`;
}

function expectCleanZeroExit(res, label) {
  assertNoFatalDiagnostics(res, label);
  if (res.code !== 0) {
    throw new Error(`${label} failed:\n${formatResTail(res)}`);
  }
}

  // ─── 1. CLI Surface & Error Handling ─────────────────────────────────────────
  register('cli/help-banner', 15000, async () => {
    const env = freshEnv('help');
    const res = await runProcess(GRIDLOCK_BIN, ['--help'], { cwd: tmpRoot, env, timeoutMs: 15000, verbose: opts.verbose });
    expectCleanZeroExit(res, '--help');
    assert.match(res.stdout, /Usage:/);
    assert.match(res.stdout, /--test-mode/);
    assert.match(res.stdout, /--headless/);
    assert.match(res.stdout, /--snapshot/);
  });

  register('cli/version-banner', 15000, async () => {
    const env = freshEnv('version');
    for (const flag of ['--version', '-v']) {
      const res = await runProcess(GRIDLOCK_BIN, [flag], { cwd: tmpRoot, env, timeoutMs: 15000, verbose: opts.verbose });
      expectCleanZeroExit(res, flag);
      assert.match(res.stdout.trim(), /^gridlock \d+\.\d+\.\d+/);
    }
  });

  register('cli/unknown-flag', 15000, async () => {
    const env = freshEnv('unknown_flag');
    const res = await runProcess(GRIDLOCK_BIN, ['--headless', '--definitely-invalid-flag'], {
      cwd: tmpRoot,
      env,
      timeoutMs: 15000,
      verbose: opts.verbose,
    });
    assert.equal(res.timedOut, false, 'Unknown flag hung until timeout');
    assert.equal(res.signal, null, `Unknown flag crashed with signal ${res.signal}`);
    assert.notEqual(res.code, 0, 'Unknown flag must exit non-zero');
    assert.match(res.stderr, /definitely-invalid-flag|Unknown option/i);
  });

  register('cli/nonexistent-target', 15000, async () => {
    const env = freshEnv('missing_target');
    const missingPath = path.join(tmpRoot, 'does_not_exist_target_bin');
    const res = await runProcess(GRIDLOCK_BIN, ['--headless', missingPath], {
      cwd: tmpRoot,
      env,
      timeoutMs: 15000,
      verbose: opts.verbose,
    });
    assert.equal(res.timedOut, false, 'Non-existent target hung');
    assert.equal(res.signal, null, `Non-existent target crashed with ${res.signal}`);
    assert.notEqual(res.code, 0, 'Non-existent target must exit non-zero');
    assert.match(res.stderr, /does not exist|not found/i);
  });

  register('cli/non-executable-file', 15000, async () => {
    const env = freshEnv('non_exec');
    const noExecElf = path.join(tmpRoot, 'no_exec_binary');
    fs.copyFileSync(synthetic.argsBin, noExecElf);
    fs.chmodSync(noExecElf, 0o644);

    const resElf = await runProcess(GRIDLOCK_BIN, ['--headless', noExecElf], {
      cwd: tmpRoot,
      env,
      timeoutMs: 15000,
      verbose: opts.verbose,
    });
    assert.equal(resElf.timedOut, false, 'Non-executable ELF hung');
    assert.equal(resElf.signal, null, 'Non-executable ELF crashed');
    assert.notEqual(resElf.code, 0, 'Non-executable ELF must exit non-zero');

    const plainBlob = path.join(tmpRoot, 'not_executable.dat');
    fs.writeFileSync(plainBlob, 'plain non-executable payload\n', 'utf8');
    const resBlob = await runProcess(GRIDLOCK_BIN, ['--headless', '--test-mode', plainBlob], {
      cwd: tmpRoot,
      env,
      timeoutMs: 15000,
      verbose: opts.verbose,
    });
    assert.equal(resBlob.timedOut, false, 'Non-executable --test-mode file hung');
    assert.equal(resBlob.signal, null, 'Non-executable --test-mode file crashed');
    assert.notEqual(resBlob.code, 0, 'Non-executable --test-mode file must exit non-zero');
  });

  register('cli/positional-target-args-and-workspace', 20000, async () => {
    const env = freshEnv('pos_workspace');
    const wsDir = path.join(tmpRoot, 'sample_workspace');
    fs.mkdirSync(wsDir, { recursive: true });
    const wsBin = path.join(wsDir, 'args_echo');
    fs.copyFileSync(synthetic.argsBin, wsBin);
    fs.chmodSync(wsBin, 0o755);

    const snapWs = path.join(tmpRoot, 'ws_snap.png');
    const resWs = await runProcess(GRIDLOCK_BIN, ['--snapshot', snapWs, wsDir], {
      cwd: tmpRoot,
      env,
      timeoutMs: 20000,
      verbose: opts.verbose,
    });
    expectCleanZeroExit(resWs, 'workspace --snapshot');
    const pngHdr = fs.readFileSync(snapWs).subarray(0, 4);
    assert.equal(pngHdr[0], 0x89);
    assert.equal(pngHdr.subarray(1, 4).toString('ascii'), 'PNG');

    const snapArgs = path.join(tmpRoot, 'args_snap.png');
    const resArgs = await runProcess(
      GRIDLOCK_BIN,
      ['--snapshot', snapArgs, wsBin, '--', 'pos1', 'two words', '--threads=4'],
      { cwd: tmpRoot, env, timeoutMs: 20000, verbose: opts.verbose },
    );
    expectCleanZeroExit(resArgs, 'positional binary + -- args');

    const settingsToml = path.join(wsDir, '.gridlock', 'settings.toml');
    assert.equal(fs.existsSync(settingsToml), true, 'Expected .gridlock/settings.toml to be written');
    const tomlText = fs.readFileSync(settingsToml, 'utf8');
    assert.match(tomlText, /pos1/);
    assert.match(tomlText, /two words/);
    assert.match(tomlText, /--threads=4/);
  });

  // ─── 2. All tutorial/ Programs (Compile + Single-Rank & Multi-Rank Runs) ─────
  for (const srcPath of tutorialSources) {
    const baseName = path.basename(srcPath, path.extname(srcPath));
    const outBin = path.join(tutorialBinDir, baseName);

    register(`tutorial/${baseName}/single-rank`, 25000, async () => {
      if (!compilers.gdb) throw new SkipTestError('gdb not available in PATH');
      if (!compiledTutorials.has(baseName)) {
        compiledTutorials.set(baseName, compileTargetSource(srcPath, outBin, compilers, opts.verbose));
      }
      const comp = compiledTutorials.get(baseName);
      if (comp.skipped) throw new SkipTestError(comp.reason);
      assert.equal(comp.ok, true, comp.reason);

      const env = freshEnv(`tut_${baseName}_np1`, { GRIDLOCK_TEST_DWELL_MS: '1800' });
      seedDefaultRanksConfig(env.XDG_CONFIG_HOME, 1);
      const res = await runProcess(GRIDLOCK_BIN, ['--headless', '--test-mode', comp.binaryPath], {
        cwd: tmpRoot,
        env,
        timeoutMs: 25000,
        verbose: opts.verbose,
      });
      expectCleanZeroExit(res, `${baseName} (ranks=1)`);
    });

    register(`tutorial/${baseName}/multi-rank-np2`, 30000, async () => {
      if (!compilers.gdb || !compilers.mpirun) {
        throw new SkipTestError('gdb or mpirun not available in PATH');
      }
      if (!compiledTutorials.has(baseName)) {
        compiledTutorials.set(baseName, compileTargetSource(srcPath, outBin, compilers, opts.verbose));
      }
      const comp = compiledTutorials.get(baseName);
      if (comp.skipped) throw new SkipTestError(comp.reason);
      assert.equal(comp.ok, true, comp.reason);

      const env = freshEnv(`tut_${baseName}_np2`, { GRIDLOCK_TEST_DWELL_MS: '2000' });
      seedDefaultRanksConfig(env.XDG_CONFIG_HOME, 2);
      const res = await runProcess(GRIDLOCK_BIN, ['--headless', '--test-mode', comp.binaryPath], {
        cwd: tmpRoot,
        env,
        timeoutMs: 30000,
        verbose: opts.verbose,
      });
      expectCleanZeroExit(res, `${baseName} (ranks=2)`);
    });
  }

  // ─── 3. Synthetic Real-World Stress & Edge-Case Targets ──────────────────────
  register('stress/cli-target-args-forwarding', 20000, async () => {
    if (!compilers.gdb) throw new SkipTestError('gdb not available in PATH');
    const env = freshEnv('stress_args', { GRIDLOCK_TEST_DWELL_MS: '3000' });
    seedDefaultRanksConfig(env.XDG_CONFIG_HOME, 1);
    const res = await runProcess(
      GRIDLOCK_BIN,
      ['--headless', '--test-mode', synthetic.argsBin, '--', 'arg1', 'arg 2', '--mode=fast'],
      { cwd: tmpRoot, env, timeoutMs: 20000, verbose: opts.verbose },
    );
    expectCleanZeroExit(res, 'args_echo forwarding');
    assert.match(res.stdout, /E2E_ARGC=4/, formatResTail(res));
    assert.match(res.stdout, /E2E_ARGV_1=arg1/, formatResTail(res));
    assert.match(res.stdout, /E2E_ARGV_2=arg 2/, formatResTail(res));
    assert.match(res.stdout, /E2E_ARGV_3=--mode=fast/, formatResTail(res));
  });

  register('stress/crash-signal-sigsegv', 20000, async () => {
    if (!compilers.gdb) throw new SkipTestError('gdb not available in PATH');
    const env = freshEnv('stress_segv', { GRIDLOCK_TEST_DWELL_MS: '2500' });
    seedDefaultRanksConfig(env.XDG_CONFIG_HOME, 1);
    const res = await runProcess(
      GRIDLOCK_BIN,
      ['--headless', '--test-mode', synthetic.crashBin, '--', 'segv'],
      { cwd: tmpRoot, env, timeoutMs: 20000, verbose: opts.verbose },
    );
    expectCleanZeroExit(res, 'crash_target SIGSEGV');
  });

  register('stress/crash-signal-sigabrt', 20000, async () => {
    if (!compilers.gdb) throw new SkipTestError('gdb not available in PATH');
    const env = freshEnv('stress_abrt', { GRIDLOCK_TEST_DWELL_MS: '2500' });
    seedDefaultRanksConfig(env.XDG_CONFIG_HOME, 1);
    const res = await runProcess(
      GRIDLOCK_BIN,
      ['--headless', '--test-mode', synthetic.crashBin, '--', 'abrt'],
      { cwd: tmpRoot, env, timeoutMs: 20000, verbose: opts.verbose },
    );
    expectCleanZeroExit(res, 'crash_target SIGABRT');
  });

  register('stress/large-5mb-csv-open', 15000, async () => {
    assert.ok(synthetic.csvBytes > 5 * 1024 * 1024, 'CSV fixture must exceed 5 MB');
    const env = freshEnv('stress_csv');
    const csvSnap = path.join(tmpRoot, 'csv_5mb.png');
    const resSnap = await runProcess(GRIDLOCK_BIN, ['--snapshot', csvSnap, synthetic.csvPath], {
      cwd: tmpRoot,
      env,
      timeoutMs: 15000,
      verbose: opts.verbose,
    });
    expectCleanZeroExit(resSnap, '5 MB CSV --snapshot');
    assert.ok(resSnap.durationMs < 5000, `Expected 5 MB CSV open+exit < 5000ms, got ${resSnap.durationMs}ms`);
    assert.ok(!/CRITICAL: Failed to load/i.test(resSnap.stderr), `CSV preview logged failure:\n${formatResTail(resSnap)}`);

    // Also verify interactive headless loop + SIGTERM exit speed on the 5 MB CSV.
    const resHeadless = await runProcess(GRIDLOCK_BIN, ['--headless', synthetic.csvPath], {
      cwd: tmpRoot,
      env,
      timeoutMs: 15000,
      sendSignalAfterMs: 600,
      verbose: opts.verbose,
    });
    expectCleanZeroExit(resHeadless, '5 MB CSV --headless SIGTERM');
    assert.ok(resHeadless.durationMs < 5000, `Expected headless CSV open+SIGTERM < 5000ms, got ${resHeadless.durationMs}ms`);
  });

  // ─── 4. Config Unification, Recent Sessions & Teardown Hygiene ───────────────
  register('hygiene/unified-xdg-config-path', 15000, async () => {
    const env = freshEnv('xdg_unification');
    const snap = path.join(tmpRoot, 'xdg_check.png');
    const res = await runProcess(GRIDLOCK_BIN, ['--snapshot', snap], {
      cwd: tmpRoot,
      env,
      timeoutMs: 15000,
      verbose: opts.verbose,
    });
    expectCleanZeroExit(res, 'unified XDG config check');

    const unifiedToml = path.join(env.XDG_CONFIG_HOME, 'gridlock', 'config.toml');
    const nestedDir = path.join(env.XDG_CONFIG_HOME, 'gridlock', 'gridlock');
    const camelDir = path.join(env.XDG_CONFIG_HOME, 'GridLock');

    assert.equal(fs.existsSync(unifiedToml), true, `Missing ${unifiedToml}`);
    assert.ok(fs.statSync(unifiedToml).size > 0, 'config.toml must be non-empty');
    assert.equal(fs.existsSync(nestedDir), false, `Forbidden nested dir created: ${nestedDir}`);
    assert.equal(fs.existsSync(camelDir), false, `Forbidden legacy dir created: ${camelDir}`);
  });

  register('hygiene/recent-sessions-persistence', 20000, async () => {
    const env = freshEnv('recent_sessions');
    const wsDir = path.join(tmpRoot, 'recent_ws');
    fs.mkdirSync(wsDir, { recursive: true });
    const targetBin = path.join(wsDir, 'args_echo_recent');
    fs.copyFileSync(synthetic.argsBin, targetBin);
    fs.chmodSync(targetBin, 0o755);

    const snap1 = path.join(tmpRoot, 'rec1.png');
    const r1 = await runProcess(GRIDLOCK_BIN, ['--snapshot', snap1, wsDir], {
      cwd: tmpRoot,
      env,
      timeoutMs: 15000,
      verbose: opts.verbose,
    });
    expectCleanZeroExit(r1, 'recent-sessions workspace snapshot');

    // Second launch in headless mode terminated cleanly via SIGTERM to exercise closeEvent() flush.
    const r2 = await runProcess(GRIDLOCK_BIN, ['--headless', targetBin], {
      cwd: tmpRoot,
      env,
      timeoutMs: 15000,
      sendSignalAfterMs: 650,
      verbose: opts.verbose,
    });
    expectCleanZeroExit(r2, 'recent-sessions SIGTERM flush');

    const cfgToml = fs.readFileSync(path.join(env.XDG_CONFIG_HOME, 'gridlock', 'config.toml'), 'utf8');
    const dbgConf = fs.readFileSync(path.join(env.XDG_CONFIG_HOME, 'gridlock', 'debugger.conf'), 'utf8');
    const canonWs = fs.realpathSync(wsDir);
    const canonBin = fs.realpathSync(targetBin);

    assert.ok(
      cfgToml.includes(canonWs) || dbgConf.includes(canonWs),
      `Workspace ${canonWs} missing from Recent Sessions persistence`,
    );
    assert.ok(
      cfgToml.includes(canonBin) || dbgConf.includes(canonBin),
      `Binary ${canonBin} missing from Recent Sessions persistence`,
    );
  });

  register('hygiene/no-leaked-shm-or-orphan-processes', 15000, async () => {
    await sleep(350);
    const after = snapshotSystemHygiene(tmpRoot);

    const leakedShm = [...after.shmFiles].filter((f) => !baselineHygiene.shmFiles.has(f));
    const leakedFifos = [...after.fifoDirs].filter((d) => !baselineHygiene.fifoDirs.has(d));
    const leakedProcs = [...after.debuggerProcs.values()].filter(
      (p) => !baselineHygiene.debuggerProcs.has(p.pid),
    );

    assert.deepEqual(leakedShm, [], `Leaked /dev/shm/psm3_shm* files: ${leakedShm.join(', ')}`);
    assert.deepEqual(leakedFifos, [], `Leaked /tmp/gridlock_mi_* FIFO dirs: ${leakedFifos.join(', ')}`);
    assert.deepEqual(
      leakedProcs,
      [],
      `Orphaned debugger/MPI processes: ${leakedProcs.map((p) => `${p.pid}:${p.comm} (${p.cmdline})`).join('; ')}`,
    );
  });

  // ─── Execute Registered E2E Cases Sequentially ───────────────────────────────
  const results = [];
  const filterLower = opts.filter.trim().toLowerCase();

  for (const t of tests) {
    if (filterLower && !t.name.toLowerCase().includes(filterLower)) {
      results.push({ name: t.name, status: 'SKIP', durationMs: 0, detail: 'filtered' });
      continue;
    }
    const t0 = performance.now();
    try {
      await Promise.race([
        t.fn(),
        new Promise((_, reject) =>
          setTimeout(() => reject(new Error(`Suite case exceeded ${t.timeoutMs}ms hard limit`)), t.timeoutMs + 2000),
        ),
      ]);
      const ms = Math.round(performance.now() - t0);
      results.push({ name: t.name, status: 'PASS', durationMs: ms, detail: '' });
      if (opts.verbose) process.stdout.write(`[PASS] ${t.name} (${ms}ms)\n`);
    } catch (err) {
      const ms = Math.round(performance.now() - t0);
      if (err instanceof SkipTestError) {
        results.push({ name: t.name, status: 'SKIP', durationMs: ms, detail: err.message });
        if (opts.verbose) process.stdout.write(`[SKIP] ${t.name}: ${err.message}\n`);
      } else {
        const msg = err && err.stack ? err.stack : String(err);
        results.push({ name: t.name, status: 'FAIL', durationMs: ms, detail: msg });
        process.stderr.write(`\n[FAIL] ${t.name} (${ms}ms)\n${tailText(msg, 25)}\n`);
      }
    }
  }

  // ─── Summary Tables ──────────────────────────────────────────────────────────
  const totalMs = Math.round(performance.now() - suiteStartMs);
  const { failed } = renderE2eSummaryTables(results, totalMs);

  process.exit(failed === 0 ? 0 : 1);
}

main().catch((err) => {
  process.stderr.write(`Unhandled E2E runner error: ${err && err.stack ? err.stack : err}\n`);
  process.exit(1);
});
