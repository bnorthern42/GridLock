#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    char* memory_block = (char*)malloc(256);
    memset(memory_block, 0xAA, 128);
    memset(memory_block + 128, 0xBB, 128);

    strcpy(memory_block + 16, "Hello, MemView!");

    // GridLock Tutorial: Memory View
    // ------------------------------
    // GridLock provides a hex dump view for raw memory inspection.
    // 
    // Instructions:
    // 1. Open the "Memory View" tab at the bottom.
    // 2. In the Variables view, find `memory_block` and copy its address (or use the context menu to send it to Memory View if available).
    // 3. Enter the pointer address into the Memory View address bar and set length to 256.
    // 4. You should clearly see the 0xAA padding, the 0xBB padding, and the "Hello, MemView!" ASCII string!

    printf("MemView Demo Running. Inspect pointer: %p\n", (void*)memory_block);

    free(memory_block);
    MPI_Finalize();
    return 0;
}
