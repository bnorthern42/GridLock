#include <mpi.h>
#include <stdio.h>

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    volatile int a = 10;
    volatile int b = 20;
    volatile int c = 0;

    // GridLock Tutorial: CPU Registers
    // --------------------------------
    // In this tutorial, we will use inline assembly to force operations into CPU registers.
    // This lets you see the Registers view update in real-time.
    //
    // Instructions:
    // 1. Open the "Registers" tab at the bottom of the screen.
    // 2. You will see a breakpoint right before the assembly block. Take note of the `eax` register value.
    // 3. Click "Step Inst" (Step Instruction) on the toolbar to step through the assembly instructions one by one.
    // 4. Watch the `eax` register in the Registers tab highlight in red as its value changes!

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

    // Assembly operations complete. Observe the final value of c.
    printf("Register Demo: %d + %d = %d\n", a, b, c);

    MPI_Finalize();
    return 0;
}
