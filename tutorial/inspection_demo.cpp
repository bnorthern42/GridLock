#include <mpi.h>
#include <stdio.h>

struct DeeplyNested {
    int id;
    double values[5];
    struct {
        float x, y, z;
    } coords;
};

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    DeeplyNested data[3];
    for (int i = 0; i < 3; i++) {
        data[i].id = i;
        for (int j = 0; j < 5; j++) data[i].values[j] = i * j;
        data[i].coords.x = i * 1.1f;
        data[i].coords.y = i * 2.2f;
        data[i].coords.z = i * 3.3f;
    }

    // Inspect 'data' here
    printf("Inspection Demo Running...\n");

    MPI_Finalize();
    return 0;
}
