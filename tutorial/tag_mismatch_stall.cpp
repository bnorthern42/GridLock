/**
 * @file tag_mismatch_stall.cpp
 * @brief GridLock Tutorial: Point-to-Point Tag & Communicator Mismatch Stall
 *
 * This showcase program demonstrates a classic point-to-point MPI tag mismatch stall:
 * - Rank 0 sends a `TelemetryPacket` to Rank 1 using `send_tag = 100`.
 * - Rank 1 posts a blocking `MPI_Recv` from Rank 0 waiting specifically for `expected_tag = 200`
 *   (instead of `100` or `MPI_ANY_TAG`), causing Rank 1 to block indefinitely while Ranks 2..N-1
 *   complete their work and wait at the final `MPI_Finalize`.
 *
 * Recommended Breakpoint & Inspection Workflow:
 * 1. Set a breakpoint on line 50 (`MPI_Send(...)`) and line 57 (`MPI_Recv(...)`).
 * 2. Inspect `send_tag`, `expected_tag`, and `pkt` in the `VARIABLES` and `WATCH` tabs.
 * 3. Check the `MPI Diagnostics` tab in the bottom dock to see the unmatched point-to-point
 *    `MPI_Recv` tag (`200` vs `100`) and call stack for Rank 1.
 */

#include <mpi.h>
#include <cstdio>

struct TelemetryPacket {
    int sequence_id;
    int source_rank;
    int payload_tag;
    double timestamp_ms;
    double readings[4];
};

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    int rank = 0;
    int world_size = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);

    TelemetryPacket pkt{};
    pkt.sequence_id = 42;
    pkt.source_rank = rank;
    pkt.payload_tag = (rank == 0) ? 100 : 200;
    pkt.timestamp_ms = 12.5 * static_cast<double>(rank + 1);
    for (int i = 0; i < 4; ++i) {
        pkt.readings[i] = static_cast<double>(rank * 10 + i) * 1.25;
    }

    if (world_size >= 2) {
        if (rank == 0) {
            // Rank 0 transmits the packet to Rank 1 with tag 100.
            const int send_tag = 100;
            std::printf("[Rank 0] Sending TelemetryPacket (seq=%d) to Rank 1 with tag=%d\n",
                        pkt.sequence_id, send_tag);
            // BREAKPOINT TARGET (Line 53): Inspect send_tag (100) before transmission.
            MPI_Send(&pkt, sizeof(TelemetryPacket), MPI_BYTE, 1, send_tag, MPI_COMM_WORLD);
        } else if (rank == 1) {
            // BUG: Rank 1 listens for expected_tag = 200 instead of 100, stalling point-to-point progress!
            const int expected_tag = 200;
            MPI_Status status{};
            std::printf("[Rank 1] Waiting in MPI_Recv from Rank 0 with mismatched expected_tag=%d...\n",
                        expected_tag);
            // BREAKPOINT TARGET (Line 61): Inspect expected_tag (200) vs Rank 0's send_tag (100).
            MPI_Recv(&pkt, sizeof(TelemetryPacket), MPI_BYTE, 0, expected_tag, MPI_COMM_WORLD, &status);
            std::printf("[Rank 1] Received packet with status.MPI_TAG=%d\n", status.MPI_TAG);
        } else {
            // Ranks 2..N-1 perform independent local reduction and proceed to finalize.
            double sum = 0.0;
            for (double v : pkt.readings) sum += v;
            std::printf("[Rank %d/%d] Worker finished local pass (sum=%.2f)\n", rank, world_size, sum);
        }
    } else {
        std::printf("[Rank 0/1] Run with >= 2 ranks (-np 4 or -np 8) to trigger tag mismatch stall.\n");
    }

    MPI_Finalize();
    return 0;
}
