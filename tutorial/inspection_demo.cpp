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
    int rank = 0, size = 1; MPI_Comm_rank(MPI_COMM_WORLD, &rank); MPI_Comm_size(MPI_COMM_WORLD, &size);
    DeeplyNested data[3];
    for (int i = 0; i < 3; i++) {
        data[i].id = i;
        for (int j = 0; j < 5; j++) data[i].values[j] = (rank + 1) * (i * j);
        data[i].coords.x = (rank == 0) ? (i * 1.1f) : (i * 1.5f);
        data[i].coords.y = (rank == 0) ? (i * 2.2f) : (i * 2.5f);
        data[i].coords.z = (rank == 0) ? (i * 3.3f) : (i * 3.5f);
    }
    printf("[Rank %d/%d] Inspection Demo Running...\n", rank, size);
    MPI_Finalize();
    return 0;
}

/*
 * GridLock Tutorial: Variable Inspection & Deep Struct Hierarchy
 * --------------------------------------------------------------
 * This demo creates an array of deeply nested structures to demonstrate how
 * GridLock handles complex data types in its Variables view and Differential Grid.
 *
 * Data Layout Overview:
 *   struct DeeplyNested {
 *       int id;               // Rank-local element index [0..2]
 *       double values[5];     // Computed product table: (rank + 1) * (i * j)
 *       struct {
 *           float x, y, z;    // 3D spatial coordinates scaled per rank and element
 *       } coords;
 *   };
 *
 * Step-by-Step Walkthrough:
 * 1. Look at the Variables Dock Widget (docked on the right side of the workspace).
 * 2. Expand the `data` array to inspect individual `DeeplyNested` elements (`data[0]`, `data[1]`, `data[2]`).
 * 3. Notice how GridLock lazily fetches and renders fixed-size arrays (`values[5]`)
 *    and anonymous inner structs (`coords.x`, `coords.y`, `coords.z`) without freezing the UI.
 * 4. Switch between MPI ranks using the Scope selector or the Server Rack View slivers
 *    to verify rank-synchronized local state inspection.
 * 5. Try pinning `data[1].coords.z` or `data[1].id` to the Watch Expressions tab
 *    in the bottom dock to compare values across all active MPI ranks simultaneously.
 * 6. Use the Evaluator tab in the bottom dock to evaluate arbitrary expressions
 *    such as `data[2].values[4]` or `sizeof(data)` directly through the backend coordinator.
 *
 * Educational Note on GDB/MI & DAP Variable References:
 *   When execution pauses at the breakpoint below, GridLock queries the top-level
 *   stack frame locals and constructs a tree of variable references. Expanding any compound
 *   node issues an asynchronous child-fetch request for that specific `variablesReference`.
 *
 * Additional Debugging Tips:
 *   - Right-click any variable in the Variables Tree to copy its value or memory address.
 *   - If a variable holds a pointer or array base address, clicking it
 *     automatically streams a 256-byte hex dump into the Memory View dock tab.
 *   - Compare `data` across ranks to confirm rank-specific variable differences.
 *   - Use the conditional breakpoint dialog to break only on specific rank conditions.
 * --------------------------------------------------------------
 */
