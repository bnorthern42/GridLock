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

    // GridLock Tutorial: Variable Inspection
    // --------------------------------------
    // This demo creates an array of deeply nested structures to demonstrate how
    // GridLock handles complex data types in its Variables view.
    //
    // Instructions:
    // 1. Look at the Variables Dock Widget (usually on the right side).
    // 2. Expand the `data` array to inspect the elements.
    // 3. Notice how GridLock handles arrays and nested structs gracefully.
    // 4. Try pinning `data[1]` to the Watch Expressions tab using the context menu.

    // Inspect 'data' here
    printf("Inspection Demo Running...\n");

    MPI_Finalize();
    return 0;
}
