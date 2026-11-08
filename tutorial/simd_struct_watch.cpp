/**
 * @file simd_struct_watch.cpp
 * @brief GridLock Tutorial: SIMD / Matrix Buffer & Raw Memory Export Showcase
 *
 * This showcase program populates a 16-byte aligned packet header and 4x4 transformation
 * matrix (`alignas(16) SimdMatrixPacket`) with deterministic byte signatures (`0xCA, 0xFE, rank, ...`)
 * and rank-specific diagonal/off-diagonal matrix elements.
 *
 * Recommended Breakpoint & Inspection Workflow:
 * 1. Set a breakpoint on line 61 (`MPI_Allreduce(...)`) after `packet` is populated.
 * 2. Select `packet` in the `VARIABLES` tree or enter `&packet` in `MEMORY VIEW` to inspect
 *    the raw 16-byte aligned header (`0xCAFE...`) and IEEE-754 float matrix bytes.
 * 3. Click `DIFF RANKS` in `MEMORY VIEW` to highlight rank-specific byte offsets, and click
 *    `EXPORT` to dump the aligned binary slice to disk for offline analysis.
 */

#include <mpi.h>
#include <cstdint>
#include <cstdio>

struct alignas(16) SimdMatrixPacket {
    uint8_t magic[4];       // Byte signature: {0xCA, 0xFE, rank, world_size}
    uint32_t packet_id;     // Little-endian packet sequence identifier
    uint64_t flags;         // Bitmask flags for SIMD lane activity
    float matrix[4][4];     // 4x4 row-major single-precision transformation matrix
    double lane_Norms[4];   // Per-row Euclidean norm accumulator
};

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    int rank = 0;
    int world_size = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);

    SimdMatrixPacket packet{};
    packet.magic[0] = 0xCA;
    packet.magic[1] = 0xFE;
    packet.magic[2] = static_cast<uint8_t>(rank & 0xFF);
    packet.magic[3] = static_cast<uint8_t>(world_size & 0xFF);
    packet.packet_id = 0x1000U + static_cast<uint32_t>(rank);
    packet.flags = 0xABCD000000000000ULL | static_cast<uint64_t>(rank);

    // Populate the 4x4 matrix and row norms with rank-distinct floating-point values.
    for (int r = 0; r < 4; ++r) {
        double row_sum_sq = 0.0;
        for (int c = 0; c < 4; ++c) {
            const float val = (r == c)
                                  ? static_cast<float>((rank + 1) * 10 + r)
                                  : static_cast<float>(rank) * 0.25f + static_cast<float>(r * 4 + c);
            packet.matrix[r][c] = val;
            row_sum_sq += static_cast<double>(val) * static_cast<double>(val);
        }
        packet.lane_Norms[r] = row_sum_sq;
    }

    // BREAKPOINT TARGET (Line 60): Inspect &packet in MemView, toggle DIFF RANKS, and test EXPORT!
    double local_trace =
        packet.matrix[0][0] + packet.matrix[1][1] + packet.matrix[2][2] + packet.matrix[3][3];
    double global_trace = 0.0;
    MPI_Allreduce(&local_trace, &global_trace, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);

    std::printf("[Rank %d/%d] SIMD Matrix Packet @ %p: magic=%02X%02X%02X%02X trace=%.2f global_trace=%.2f\n",
                rank, world_size, static_cast<const void*>(&packet),
                packet.magic[0], packet.magic[1], packet.magic[2], packet.magic[3],
                local_trace, global_trace);

    MPI_Finalize();
    return 0;
}
