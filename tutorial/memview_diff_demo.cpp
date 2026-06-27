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

    printf("MemView Diff Demo Running on rank %d\n", rank);

    free(array);
    MPI_Finalize();
    return 0;
}
