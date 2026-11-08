/**
 * @file collective_mismatch.cpp
 * @brief GridLock Tutorial: MPI Collective Barrier / Broadcast Divergence
 *
 * This showcase program demonstrates an MPI collective call mismatch bug across ranks:
 * - Even ranks (Rank 0, 2, 4, ...) execute `MPI_Bcast` to distribute a configuration seed.
 * - Odd ranks (Rank 1, 3, 5, ...) branch into `MPI_Barrier(MPI_COMM_WORLD)` or skip the
 *   collective call when an edge condition triggers, stalling collective synchronization.
 *
 * Recommended Breakpoint & Inspection Workflow:
 * 1. Set a breakpoint on line 46 (`if (rank % 2 == 0)`) or line 50 (`MPI_Bcast(...)`).
 * 2. Open the `MPI Diagnostics` dock and `DeadlockAnalyzer` to inspect mismatched
 *    collective primitives across active ranks.
 * 3. Switch the right-hand `Disassembly` pane to `MAP (CFG)` to observe rank badges
 *    diverging onto separate basic blocks of the Control Flow Graph.
 */

#include <mpi.h>
#include <cstdio>
#include <vector>

struct CollectiveConfig {
    int iteration;
    int broadcast_seed;
    double tolerance;
    int rank_branch_tag;
};

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    int rank = 0;
    int world_size = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);

    // Initialize local configuration state before entering the collective phase.
    CollectiveConfig cfg{};
    cfg.iteration = 1;
    cfg.broadcast_seed = (rank == 0) ? 0x4242 : 0;
    cfg.tolerance = 1.0e-6 * static_cast<double>(rank + 1);
    cfg.rank_branch_tag = (rank % 2 == 0) ? 10 : 20;

    std::vector<double> local_work(16, static_cast<double>(rank) * 2.5);

    // BREAKPOINT TARGET (Line 46): Rank parity causes divergent collective calls!
    if (rank % 2 == 0) {
        // Even ranks participate in MPI_Bcast expecting all ranks in MPI_COMM_WORLD.
        cfg.rank_branch_tag = 100 + rank;
        MPI_Bcast(&cfg.broadcast_seed, 1, MPI_INT, 0, MPI_COMM_WORLD);
        for (size_t i = 0; i < local_work.size(); ++i) {
            local_work[i] += static_cast<double>(cfg.broadcast_seed);
        }
    } else {
        // Odd ranks take a divergent branch and enter MPI_Barrier instead of MPI_Bcast.
        cfg.rank_branch_tag = 200 + rank;
        for (size_t i = 0; i < local_work.size(); ++i) {
            local_work[i] *= 0.5;
        }
        MPI_Barrier(MPI_COMM_WORLD);
    }

    std::printf("[Rank %d/%d] Collective Mismatch Demo finished: seed=%d, branch=%d, work[0]=%.2f\n",
                rank, world_size, cfg.broadcast_seed, cfg.rank_branch_tag, local_work[0]);

    MPI_Finalize();
    return 0;
}
