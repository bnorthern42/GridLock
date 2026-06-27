#include <mpi.h>
#include <stdio.h>

int main(int argc, char** argv) {
    printf("Before init\n");
    MPI_Init(&argc, &argv);
    printf("After init\n");
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    printf("Size: %d, Rank: %d\n", size, rank);
    MPI_Finalize();
    return 0;
}
