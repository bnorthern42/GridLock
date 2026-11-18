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
 * @class ProcessScanner
 * @brief Stateless `/proc` process discovery and `kernel.yama.ptrace_scope` inspection utility.
 */
class ProcessScanner {
 public:
  /**
   * @brief Scans @p procRoot for running processes and groups sibling MPI ranks by job tag.
   * @param procRoot Root procfs directory path (defaults to `"/proc"`, overridable for tests).
   * @param currentUserOnly When true, returns only processes matching `geteuid()`.
   * @return Sorted list of discovered process entries (MPI jobs first, ordered by rank).
   */
  static QList<ProcessEntry> scanRunningProcesses(
      const QString& procRoot = QStringLiteral("/proc"),
      bool currentUserOnly = false);

  /**
   * @brief Reads the Linux Yama LSM ptrace scope (`0` = permissive, `1` = restricted, `2` = admin, `3` = disabled).
   * @param sysctlPath Path to `ptrace_scope` sysctl file.
   * @return Parsed integer scope (`0..3`), or `0` if Yama is not active on the host kernel.
   */
  static int checkPtraceScope(
      const QString& sysctlPath = QStringLiteral("/proc/sys/kernel/yama/ptrace_scope"));
};

namespace core {
using ::gridlock::ProcessEntry;
using ::gridlock::ProcessScanner;
namespace hpc {
using ::gridlock::ProcessEntry;
using ::gridlock::ProcessScanner;
}  // namespace hpc
}  // namespace core

}  // namespace gridlock
