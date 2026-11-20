/**
 * @file catchpoint_exceptions.cpp
 * @brief GridLock Tutorial: Signal Disposition Matrix & C++/Syscall Catchpoints Showcase
 *
 * Demonstrates multi-rank C++ exception catchpoints (`throw`, `rethrow`, `catch`),
 * exception-type regular expression filtering (`MeshBoundaryException` vs.
 * `SolverConvergenceException`), Linux syscall catchpoints (`openat`, `mmap`),
 * and POSIX signal disposition configuration (`SIGUSR1`).
 *
 * Recommended Debugging Workflow:
 * 1. Set `RANKS (-np): 4` in the toolbar and open `Debug -> Signals & Catchpoints...` (`Ctrl+Alt+S`).
 * 2. In the **Catchpoints** tab:
 *    - Select **C++ `throw`**, enter regex `SolverConvergenceException`, and click **Add C++ Catchpoint**
 *      so Rank 2 stops at the exact `throw` site while Rank 1's handled `MeshBoundaryException` is ignored.
 *    - Select **C++ `rethrow`** and click **Add C++ Catchpoint** to catch the rethrown exception
 *      propagating out of `solveSubdomain()`.
 *    - Toggle the **Linux Syscall** preset chip `openat` (or `mmap`) and click **Add Syscall Catchpoint**
 *      to intercept Rank 0's `/dev/null` descriptor probe and scratch page allocation.
 * 3. In the **Signal Disposition Matrix** tab:
 *    - Configure **`SIGUSR1`** to `Stop = Off, Print = On, Pass to App = On` so Rank 0's self-sent
 *      `raise(SIGUSR1)` telemetry heartbeat logs without halting the parallel run.
 */

#include <fcntl.h>
#include <mpi.h>
#include <sys/mman.h>
#include <unistd.h>

#include <csignal>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace {

volatile std::sig_atomic_t g_usr1HeartbeatCount = 0;

void handleSigUsr1Heartbeat(int /*signum*/) {
  g_usr1HeartbeatCount = g_usr1HeartbeatCount + 1;
}

}  // namespace

/**
 * @class MeshBoundaryException
 * @brief Benign subdomain halo clamping exception thrown and recovered locally on Rank 1.
 */
class MeshBoundaryException : public std::runtime_error {
 public:
  explicit MeshBoundaryException(const std::string& message)
      : std::runtime_error(message) {}
};

/**
 * @class SolverConvergenceException
 * @brief Critical Krylov residual divergence exception thrown and rethrown on Rank 2.
 */
class SolverConvergenceException : public std::runtime_error {
 public:
  SolverConvergenceException(const std::string& message, double residualNorm)
      : std::runtime_error(message), m_residualNorm(residualNorm) {}

  double residualNorm() const noexcept { return m_residualNorm; }

 private:
  double m_residualNorm = 0.0;
};

namespace {

void validateHaloExchange(int rank) {
  if (rank == 1) {
    throw MeshBoundaryException("Rank 1 east ghost-layer index out of bounds (clamped to boundary)");
  }
}

void solveSubdomain(int rank) {
  try {
    if (rank == 2) {
      throw SolverConvergenceException("Rank 2 BiCGSTAB residual exceeded tolerance (1.42e+03)", 1420.0);
    }
  } catch (const SolverConvergenceException& ex) {
    std::printf("[Rank %d] Inner solver catch logged residual=%.2f; rethrowing to driver...\n",
                rank, ex.residualNorm());
    throw;  // Rethrow site targeted by GDB `-catch-rethrow`
  }
}

void performCoordinatorSyscallAndSignalProbe(int rank) {
  if (rank != 0) return;

  // Trigger `openat` syscall catchpoint on Rank 0.
  const int fd = ::open("/dev/null", O_RDONLY);
  if (fd >= 0) {
    ::close(fd);
  }

  // Trigger `mmap` syscall catchpoint on Rank 0.
  void* scratchPage = ::mmap(nullptr, 4096, PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (scratchPage != MAP_FAILED) {
    *static_cast<int*>(scratchPage) = 42;
    ::munmap(scratchPage, 4096);
  }

  // Trigger `SIGUSR1` disposition matrix handling on Rank 0.
  std::signal(SIGUSR1, handleSigUsr1Heartbeat);
  std::raise(SIGUSR1);
}

}  // namespace

int main(int argc, char** argv) {
  MPI_Init(&argc, &argv);

  int rank = 0;
  int worldSize = 1;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &worldSize);

  performCoordinatorSyscallAndSignalProbe(rank);

  // 1. Rank 1 throws and locally recovers from MeshBoundaryException.
  try {
    validateHaloExchange(rank);
  } catch (const MeshBoundaryException& ex) {
    std::printf("[Rank %d/%d] Recovered from MeshBoundaryException: %s\n",
                rank, worldSize, ex.what());
  }

  // 2. Rank 2 throws SolverConvergenceException, logs in inner catch, and rethrows (`throw;`).
  try {
    solveSubdomain(rank);
  } catch (const SolverConvergenceException& ex) {
    std::printf("[Rank %d/%d] Outer driver caught rethrown SolverConvergenceException: %s\n",
                rank, worldSize, ex.what());
  }

  MPI_Barrier(MPI_COMM_WORLD);
  std::printf("[Rank %d/%d] Catchpoint showcase finished cleanly (SIGUSR1 heartbeats=%d).\n",
              rank, worldSize, static_cast<int>(g_usr1HeartbeatCount));
  std::fflush(stdout);

  MPI_Finalize();
  return 0;
}
