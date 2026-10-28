/**
 * @file test_hpc_memory.cpp
 * @brief Implementation of Linux direct virtual memory reader unit tests.
 *
 * Spawns a child process holding floating-point telemetry buffers and uses
 * NativeMemoryReader (wrapping process_vm_readv) to read the memory directly.
 */

#include "test_hpc_memory.hpp"

#include <vector>

#include <QTest>

#include "../src/core/hpc/NativeMemoryReader.hpp"

#ifdef __linux__
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

/**
 * @brief Tests reading double-precision floating-point arrays directly via process_vm_readv.
 */
void TestHpcMemory::testDirectRead() {
#ifdef __linux__
    int pipefd[2];
    QVERIFY(pipe(pipefd) == 0);

    // Fork a child process to host target memory.
    pid_t pid = fork();
    QVERIFY(pid >= 0);

    if (pid == 0) {
        // Child process execution path.
        close(pipefd[0]); // Close read end
        
        // Redirect stdout to communication pipe.
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);

        // Allocate test vector in child's address space.
        std::vector<double> dummyData = {1.0, 2.5, 3.14};
        uintptr_t address = reinterpret_cast<uintptr_t>(dummyData.data());

        // Emit PID and buffer virtual address to pipe.
        printf("%d %lu\n", getpid(), static_cast<unsigned long>(address));
        fflush(stdout);

        // Sleep until parent signals termination.
        while (true) {
            sleep(1);
        }
        
        exit(0);
    } else {
        // Parent process execution path.
        close(pipefd[1]); // Close write end
        
        // Read child process PID and buffer address from pipe.
        FILE* stream = fdopen(pipefd[0], "r");
        int child_pid = 0;
        unsigned long address_val = 0;
        if (fscanf(stream, "%d %lu", &child_pid, &address_val) != 2) {
            QFAIL("Failed to read PID and address from child stdout");
        }
        fclose(stream);
        
        uintptr_t address = static_cast<uintptr_t>(address_val);
        QVERIFY(address != 0);

        // Read 3 doubles directly from child's address space via process_vm_readv.
        std::vector<double> result;
        try {
            result = NativeMemoryReader::readDoubles(pid, address, 3);
        } catch (const std::runtime_error& e) {
            kill(pid, SIGTERM);
            waitpid(pid, nullptr, 0);
            QFAIL(e.what());
        }

        // Verify values read match child data buffer.
        QCOMPARE(result.size(), 3UL);
        QCOMPARE(result[0], 1.0);
        QCOMPARE(result[1], 2.5);
        QCOMPARE(result[2], 3.14);

        // Terminate and reap child process.
        kill(pid, SIGTERM);
        waitpid(pid, nullptr, 0);
    }
#else
    QSKIP("NativeMemoryReader is only supported on Linux");
#endif
}

QTEST_GUILESS_MAIN(TestHpcMemory)
