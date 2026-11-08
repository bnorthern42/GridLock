/**
 * @file heap_imbalance.cpp
 * @brief GridLock Tutorial: Asymmetric Heap Allocation & Stack Depth Telemetry Showcase
 *
 * This showcase program creates intentional per-rank memory and call-stack imbalance:
 * - Each rank allocates a `std::vector<double>` whose size scales quadratically with
 *   `(rank + 1)`, producing distinct heap footprints per rank.
 * - Each rank recurses to a rank-dependent call-stack depth `(2 + rank * 2)` before
 *   reaching the leaf inspection frame and synchronizing at `MPI_Barrier`.
 *
 * Recommended Breakpoint & Inspection Workflow:
 * 1. Set a breakpoint on line 43 (`const double leaf_checksum = ...`) inside `recurseWithStackFrame`.
 * 2. Observe the live `STACK: ... | HEAP: ... | RAM: ... | CPU: ...` telemetry in the bottom
 *    status bar ribbon as you switch between ranks in `SERVER RACK`.
 * 3. Inspect `heap_buffer.data()` in `MEMORY VIEW` to view the rank-scaled heap allocation.
 */

#include <mpi.h>
#include <cstdint>
#include <cstdio>
#include <numeric>
#include <vector>

/**
 * @brief Recursively builds stack frames with local arrays to demonstrate per-rank stack telemetry.
 */
double recurseWithStackFrame(int rank, int current_depth, int target_depth,
                             const std::vector<double>& heap_buffer) {
    // Allocate a 256-byte stack frame buffer per recursion level to grow active stack usage.
    double stack_scratch[32];
    for (int i = 0; i < 32; ++i) {
        stack_scratch[i] = static_cast<double>(rank * 100 + current_depth * 10 + i);
    }

    if (current_depth < target_depth) {
        return stack_scratch[0] * 0.01 +
               recurseWithStackFrame(rank, current_depth + 1, target_depth, heap_buffer);
    }

    // BREAKPOINT TARGET (Line 41): Compare stack depth, RSP, and heap_buffer size across ranks!
    const double leaf_checksum =
        std::accumulate(heap_buffer.begin(), heap_buffer.begin() + 16, stack_scratch[31]);
    return leaf_checksum;
}

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    int rank = 0;
    int world_size = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);

    // Scale heap buffer size asymmetrically: Rank 0 allocates 4K doubles, higher ranks allocate much more.
    const size_t element_count = static_cast<size_t>(4096) * static_cast<size_t>((rank + 1) * (rank + 1));
    std::vector<double> heap_buffer(element_count, static_cast<double>(rank) + 0.5);
    for (size_t i = 0; i < 64 && i < heap_buffer.size(); ++i) {
        heap_buffer[i] = static_cast<double>(rank * 1000 + static_cast<int>(i));
    }

    // Recurse to a rank-dependent stack depth before synchronizing across the cluster.
    const int target_stack_depth = 2 + rank * 2;
    const double total = recurseWithStackFrame(rank, 0, target_stack_depth, heap_buffer);

    MPI_Barrier(MPI_COMM_WORLD);
    std::printf("[Rank %d/%d] Heap Imbalance Demo: elements=%zu (%zu KB), depth=%d, checksum=%.2f\n",
                rank, world_size, heap_buffer.size(),
                (heap_buffer.size() * sizeof(double)) / 1024, target_stack_depth, total);

    MPI_Finalize();
    return 0;
}
