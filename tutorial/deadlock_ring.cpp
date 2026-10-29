/**
 * @file deadlock_ring.cpp
 * @brief MPI Ring Deadlock Demonstration for GridLock Parallel Debugger.
 *
 * Illustrates a classic circular wait-for deadlock across N MPI ranks where
 * every rank posts a synchronous blocking MPI_Send to its right neighbor
 * ((rank + 1) % size) before posting the matching MPI_Recv from its left
 * neighbor ((rank + size - 1) % size).
 */

#include <mpi.h>
#include <stdio.h>
#include <vector>

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    int rank = 0;
    int size = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    const int next_rank = (rank + 1) % size;
    const int prev_rank = (rank + size - 1) % size;
    const int tag = 100;

    // Allocate a payload exceeding the eager-to-rendezvous threshold so
    // blocking MPI_Send stalls until the destination rank posts MPI_Recv.
    std::vector<int> send_buf(16384, rank * 10 + 1);
    std::vector<int> recv_buf(16384, 0);

    // Circular wait-for deadlock: every rank blocks inside MPI_Send waiting
    // for next_rank to enter MPI_Recv.
    MPI_Send(send_buf.data(), static_cast<int>(send_buf.size()), MPI_INT,
             next_rank, tag, MPI_COMM_WORLD);
    MPI_Recv(recv_buf.data(), static_cast<int>(recv_buf.size()), MPI_INT,
             prev_rank, tag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    printf("[Rank %d/%d] Received payload=%d from Rank %d\n",
           rank, size, recv_buf[0], prev_rank);

    MPI_Finalize();
    return 0;
}
