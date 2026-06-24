#include "test_hpc_memory.hpp"
#include "../src/core/hpc/NativeMemoryReader.hpp"
#include <QTest>
#include <vector>

#ifdef __linux__
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#endif

void TestHpcMemory::testDirectRead() {
#ifdef __linux__
    int pipefd[2];
    QVERIFY(pipe(pipefd) == 0);

    pid_t pid = fork();
    QVERIFY(pid >= 0);

    if (pid == 0) {
        // Child process
        close(pipefd[0]); // Close read end
        
        // Redirect stdout to pipe
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);

        std::vector<double> dummyData = {1.0, 2.5, 3.14};
        uintptr_t address = reinterpret_cast<uintptr_t>(dummyData.data());

        // Print PID and address to stdout
        printf("%d %lu\n", getpid(), static_cast<unsigned long>(address));
        fflush(stdout);

        // Wait for a signal to exit
        while (true) {
            sleep(1);
        }
        
        exit(0);
    } else {
        // Parent process
        close(pipefd[1]); // Close write end
        
        FILE* stream = fdopen(pipefd[0], "r");
        int child_pid = 0;
        unsigned long address_val = 0;
        if (fscanf(stream, "%d %lu", &child_pid, &address_val) != 2) {
            QFAIL("Failed to read PID and address from child stdout");
        }
        fclose(stream);
        
        uintptr_t address = static_cast<uintptr_t>(address_val);

        QVERIFY(address != 0);

        std::vector<double> result;
        try {
            result = NativeMemoryReader::readDoubles(pid, address, 3);
        } catch (const std::runtime_error& e) {
            kill(pid, SIGTERM);
            waitpid(pid, nullptr, 0);
            QFAIL(e.what());
        }

        QCOMPARE(result.size(), 3UL);
        QCOMPARE(result[0], 1.0);
        QCOMPARE(result[1], 2.5);
        QCOMPARE(result[2], 3.14);

        // Terminate child
        kill(pid, SIGTERM);
        waitpid(pid, nullptr, 0);
    }
#else
    QSKIP("NativeMemoryReader is only supported on Linux");
#endif
}

QTEST_GUILESS_MAIN(TestHpcMemory)
