#include <mpi.h>
#include <stdio.h>

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (size < 2) {
        printf("Requires at least 2 ranks.\n");
        MPI_Finalize();
        return 0;
    }

    // GridLock Tutorial: Deadlock Detection
    // -------------------------------------
    // In this demo, rank 0 and rank 1 will intentionally create a deadlock.
    // Both ranks use MPI_Ssend (Synchronous Send), which blocks until a matching receive is posted.
    // Since both ranks execute Ssend first, neither can ever reach their Recv call.
    // This creates a classic circular wait.
    // 
    // Instructions:
    // 1. Observe that both ranks hit the breakpoint below.
    // 2. Click "Continue" to let them proceed. The debugger will hang because the MPI program is deadlocked.
    // 3. Open the "MPI Diagnostics" tab at the bottom to see GridLock's Deadlock Analyzer.
    // 4. It should detect the circular dependency and highlight the ranks involved!

    int token = rank;
    if (rank == 0) {
        MPI_Ssend(&token, 1, MPI_INT, 1, 0, MPI_COMM_WORLD);
        MPI_Recv(&token, 1, MPI_INT, 1, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    } else if (rank == 1) {
        MPI_Ssend(&token, 1, MPI_INT, 0, 0, MPI_COMM_WORLD);
        MPI_Recv(&token, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    }

    MPI_Finalize();
    return 0;
}
