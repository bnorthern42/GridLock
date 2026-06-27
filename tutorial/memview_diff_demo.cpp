#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    int* array = (int*)malloc(10 * sizeof(int));
    for (int i = 0; i < 10; i++) {
        array[i] = i;
    }

    if (rank == 1) {
        array[5] = 999;
    }

    // GridLock Tutorial: Differential Memory View
    // -------------------------------------------
    // Memory can often diverge silently across ranks. GridLock can highlight these differences.
    //
    // Instructions:
    // 1. Both ranks have allocated an array of 10 integers.
    // 2. However, rank 1 modifies index 5 to be 999, while rank 0 leaves it as 5.
    // 3. Open the "Memory View" tab.
    // 4. Enter the address of `array` for rank 0, and use the "Compare with Rank" feature, selecting rank 1.
    // 5. GridLock will perform a high-speed memory difference analysis and highlight the exact diverging bytes!

    printf("MemView Diff Demo Running on rank %d\n", rank);

    free(array);
    MPI_Finalize();
    return 0;
}
