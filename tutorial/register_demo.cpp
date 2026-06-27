#include <mpi.h>
#include <stdio.h>

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    volatile int a = 10;
    volatile int b = 20;
    volatile int c = 0;

    // Use some inline assembly to force registers
    #if defined(__x86_64__) || defined(__i386__)
    __asm__ __volatile__(
        "mov %1, %%eax;"
        "add %2, %%eax;"
        "mov %%eax, %0;"
        : "=r" (c)
        : "r" (a), "r" (b)
        : "%eax"
    );
    #else
    c = a + b;
    #endif

    printf("Register Demo: %d + %d = %d\n", a, b, c);

    MPI_Finalize();
    return 0;
}
