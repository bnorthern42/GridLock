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

    // GridLock Tutorial: Variable Inspection & Deep Struct Hierarchy
    // --------------------------------------------------------------
    // This demo creates an array of deeply nested structures to demonstrate how
    // GridLock handles complex data types in its Variables view and Differential Grid.
    //
    // Data Layout Overview:
    //   struct DeeplyNested {
    //       int id;               // Rank-local element index [0..2]
    //       double values[5];     // Computed product table: i * j
    //       struct {
    //           float x, y, z;    // 3D spatial coordinates scaled per element
    //       } coords;
    //   };
    //
    // Step-by-Step Walkthrough:
    // 1. Look at the Variables Dock Widget (docked on the right side of the workspace).
    // 2. Expand the `data` array to inspect individual `DeeplyNested` elements (`data[0]`, `data[1]`, `data[2]`).
    // 3. Notice how GridLock lazily fetches and renders fixed-size arrays (`values[5]`)
    //    and anonymous inner structs (`coords.x`, `coords.y`, `coords.z`) without freezing the UI.
    // 4. Switch between MPI ranks using the Scope selector or the Server Rack View slivers
    //    to verify rank-synchronized local state inspection.
    // 5. Try pinning `data[1].coords.z` or `data[1].id` to the Watch Expressions tab
    //    in the bottom dock to compare values across all active MPI ranks simultaneously.
    // 6. Use the Evaluator tab in the bottom dock to evaluate arbitrary expressions
    //    such as `data[2].values[4]` or `sizeof(data)` directly through the backend coordinator.
    //
    // Educational Note on GDB/MI & DAP Variable References:
    //   When execution pauses at the breakpoint below (line 76), GridLock queries the top-level
    //   stack frame locals and constructs a tree of variable references. Expanding any compound
    //   node issues an asynchronous child-fetch request for that specific `variablesReference`.
    //
    // Expected Values at Breakpoint (Line 76):
    //   - data[0]: id = 0, values = {0, 0, 0, 0, 0}, coords = {x = 0.0, y = 0.0, z = 0.0}
    //   - data[1]: id = 1, values = {0, 1, 2, 3, 4}, coords = {x = 1.1, y = 2.2, z = 3.3}
    //   - data[2]: id = 2, values = {0, 2, 4, 6, 8}, coords = {x = 2.2, y = 4.4, z = 6.6}
    //
    // Press "Continue" or "Step Inst" on the main toolbar once you have finished inspecting `data`.
    //
    // Additional Debugging Tips:
    //   - Right-click any variable in the Variables Tree to copy its value or memory address.
    //   - If a variable holds a pointer or array base address (e.g., `0x7ffc...`), clicking it
    //     automatically streams a 256-byte hex dump into the Memory View dock tab.
    //   - You can also inspect the raw assembly instructions for the nested loop initialization
    //     in the Disassembly View pane located at the top of the right-hand splitter.
    //   - Compare `data` across ranks to confirm deterministic initialization across all MPI processes.
    //   - Use the conditional breakpoint dialog (right-click line number gutter) to break only on
    //     specific rank conditions or variable thresholds during iterative loops.
    // --------------------------------------------------------------

    // Breakpoint Target:
    // Inspect 'data' here (Tutorial Breakpoint Line 76)
    // Verify `data[0]..data[2]` in the Variables Tree:
    printf("Inspection Demo Running...\n");

    MPI_Finalize();
    return 0;
}
