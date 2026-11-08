/**
 * @file ghost_cell_drift.cpp
 * @brief GridLock Tutorial: 1D Stencil Halo / Ghost-Cell Exchange & Floating-Point NaN Injection
 *
 * This showcase program runs a 5-step 1D heat/diffusion stencil exchange (`MPI_Sendrecv`)
 * across a ring of MPI ranks. On iteration 3, Rank 2 (or the highest active rank when
 * world_size <= 2) injects an IEEE-754 quiet `NaN` into its right ghost boundary, which
 * rapidly contaminates neighboring domains during subsequent stencil updates.
 *
 * Recommended Breakpoint & Inspection Workflow:
 * 1. Set a breakpoint on line 66 (`stencil_center = 0.25 * ...`) inside the timestep loop.
 * 2. Add `left_ghost`, `right_ghost`, and `stencil_center` to the `WATCH` tab (`DifferentialGrid`)
 *    to watch the `NaN` anomaly appear on iteration 3 and propagate across ranks on iteration 4.
 * 3. Open `MEMORY VIEW` and click `DIFF RANKS` on `domain.data()` to highlight diverging stencil bytes.
 */

#include <mpi.h>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    int rank = 0;
    int world_size = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);

    // Periodic 1D ring neighbors for left and right ghost-cell halo exchanges.
    const int left_neighbor = (rank - 1 + world_size) % world_size;
    const int right_neighbor = (rank + 1) % world_size;
    const int faulty_rank = (world_size > 2) ? 2 : (world_size - 1);

    // Local 1D grid of 8 interior cells plus left/right ghost variables.
    std::vector<double> domain(8, 100.0 + static_cast<double>(rank) * 10.0);
    std::vector<double> next_domain = domain;
    double left_ghost = domain.front();
    double right_ghost = domain.back();
    double stencil_center = domain[domain.size() / 2];

    for (int step = 0; step < 5; ++step) {
        double send_left = domain.front();
        double send_right = domain.back();

        // Inject a corrupted NaN ghost-cell boundary value on iteration 3 on the faulty rank.
        if (step == 3 && rank == faulty_rank) {
            send_right = std::numeric_limits<double>::quiet_NaN();
        }

        // Exchange left and right boundary cells with periodic ring neighbors.
        MPI_Sendrecv(&send_right, 1, MPI_DOUBLE, right_neighbor, 10 + step,
                     &left_ghost, 1, MPI_DOUBLE, left_neighbor, 10 + step,
                     MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        MPI_Sendrecv(&send_left, 1, MPI_DOUBLE, left_neighbor, 20 + step,
                     &right_ghost, 1, MPI_DOUBLE, right_neighbor, 20 + step,
                     MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        // Compute 3-point 1D Jacobi heat diffusion update across interior cells.
        for (size_t i = 0; i < domain.size(); ++i) {
            const double left_val = (i == 0) ? left_ghost : domain[i - 1];
            const double right_val = (i + 1 == domain.size()) ? right_ghost : domain[i + 1];
            next_domain[i] = 0.25 * left_val + 0.50 * domain[i] + 0.25 * right_val;
        }
        domain.swap(next_domain);

        // BREAKPOINT TARGET (Line 69): Inspect left_ghost, right_ghost, and stencil_center across ranks.
        stencil_center = domain[domain.size() / 2];
        std::printf("[Rank %d/%d] Step %d: left_ghost=%.3f center=%.3f right_ghost=%.3f\n",
                    rank, world_size, step, left_ghost, stencil_center, right_ghost);
    }

    MPI_Finalize();
    return 0;
}
