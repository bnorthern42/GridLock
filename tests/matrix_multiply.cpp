
#include <mpi.h>
#include <iostream>
#include <vector>

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    int elements_per_proc = 100;
    std::vector<int> sub_matrix(elements_per_proc, rank);
    
    for (int i = 0; i < elements_per_proc; ++i) {
        sub_matrix[i] *= 2;
    }
    
    std::vector<int> gathered_matrix;
    if (rank == 0) {
        gathered_matrix.resize(elements_per_proc * size);
    }
    
    MPI_Gather(sub_matrix.data(), elements_per_proc, MPI_INT,
               gathered_matrix.data(), elements_per_proc, MPI_INT,
               0, MPI_COMM_WORLD);
               
    MPI_Finalize();
    return 0;
}
