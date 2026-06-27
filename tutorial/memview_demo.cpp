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

    printf("MemView Demo Running. Inspect pointer: %p\n", (void*)memory_block);

    free(memory_block);
    MPI_Finalize();
    return 0;
}
