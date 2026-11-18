/**
 * @file ProcessScanner.hpp
 * @brief Linux `/proc` process scanner, MPI job rank grouper, and Yama ptrace_scope checker.
 *
 * Parses `/proc/[pid]/comm`, `/proc/[pid]/cmdline`, `/proc/[pid]/exe`, `/proc/[pid]/status`,
 * and `/proc/[pid]/environ` (`OMPI_COMM_WORLD_RANK`, `PMI_RANK`, `PMIX_RANK`, `SLURM_PROCID`)
 * to enumerate running user/system processes and group sibling ranks of active MPI jobs.
 */

#pragma once

#include <sys/types.h>
#include <unistd.h>
#include <QList>
#include <QString>

namespace gridlock {

/**
 * @struct ProcessEntry
 * @brief Metadata for a single running OS process discovered under `/proc/[pid]`.
 */
struct ProcessEntry {
  pid_t pid = 0;            ///< Operating system process ID.
  pid_t ppid = 0;           ///< Parent process ID from `/proc/[pid]/status`.
  uid_t uid = 0;            ///< Effective owner UID from `/proc/[pid]/status`.
  QString name;             ///< Process short command name (`comm` or `Name:`).
  QString cmdline;          ///< Space-joined command line arguments from `cmdline`.
  QString executablePath;   ///< Resolved target binary path from `/proc/[pid]/exe`.
  QString user;             ///< Resolved POSIX account username for `uid`.
  int mpiRank = -1;         ///< Detected MPI world rank (`>= 0` when running under MPI).
  int mpiWorldSize = -1;    ///< Detected MPI world size (`> 0` when running under MPI).
  QString mpiJobTag;        ///< Shared job identifier grouping sibling MPI ranks together.

  /** @brief Returns true if this process belongs to a detected MPI communicator/job. */
  bool isMpiProcess() const { return mpiRank >= 0 || !mpiJobTag.isEmpty(); }
};

/**
 * @struct RemoteTargetSpec
 * @brief Connection descriptor for a remote `gdbserver` or `qemu-user -g` stub.
 */
struct RemoteTargetSpec {
  QString endpoint;           ///< Normalized `host:port` endpoint (e.g. `"localhost:1234"`).
  QString localSymbolBinary;  ///< Optional host ELF binary with DWARF debug symbols.
  QString gdbBinary = QStringLiteral("gdb");  ///< Debugger binary (`"gdb"`, `"gdb-multiarch"`, etc.).
  bool extendedRemote = false;  ///< True for `-target-select extended-remote`, false for `remote`.
  int mpiRank = 0;            ///< Zero-based MPI rank index assigned to this endpoint.
};

/**
 * @class ProcessScanner
 * @brief Stateless `/proc` process discovery, remote endpoint expander, and `ptrace_scope` utility.
 */
class ProcessScanner {
 public:
  /**
   * @brief Scans @p procRoot for running processes and groups sibling MPI ranks by job tag.
   */
  static QList<ProcessEntry> scanRunningProcesses(
      const QString& procRoot = QStringLiteral("/proc"),
      bool currentUserOnly = false);

  /**
   * @brief Reads the Linux Yama LSM ptrace scope (`0` = permissive, `1` = restricted, `2` = admin, `3` = disabled).
   */
  static int checkPtraceScope(
      const QString& sysctlPath = QStringLiteral("/proc/sys/kernel/yama/ptrace_scope"));

  /**
   * @brief Parses and expands single, port-range (`host:1234..1237`), or comma-separated remote endpoints.
   * @param specText Raw endpoint specification string (e.g. `":1234"`, `"localhost:1234..1237"`, `"n0:1234, n1:1234"`).
   * @param localSymbolBinary Optional local ELF binary path with DWARF symbols.
   * @param gdbBinary Optional GDB executable name/path (`"gdb"`, `"gdb-multiarch"`).
   * @param extendedRemote True to use `extended-remote`, false for `remote`.
   * @return Ordered list of `RemoteTargetSpec` (Rank `0..N-1`), or empty list if any token is invalid.
   */
  static QList<RemoteTargetSpec> expandRemoteEndpoints(
      const QString& specText,
      const QString& localSymbolBinary = QString(),
      const QString& gdbBinary = QStringLiteral("gdb"),
      bool extendedRemote = false);
};

namespace core {
using ::gridlock::ProcessEntry;
using ::gridlock::ProcessScanner;
using ::gridlock::RemoteTargetSpec;
namespace hpc {
using ::gridlock::ProcessEntry;
using ::gridlock::ProcessScanner;
using ::gridlock::RemoteTargetSpec;
}  // namespace hpc
}  // namespace core

}  // namespace gridlock
