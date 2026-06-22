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

        std::vector<double> dummyData = {1.23, 4.56, 7.89, 10.11};
        uintptr_t address = reinterpret_cast<uintptr_t>(dummyData.data());

        // Send the address to the parent
        if (write(pipefd[1], &address, sizeof(address)) != sizeof(address)) {
            exit(1);
        }
        
        // Wait for a signal to exit
        while (true) {
            sleep(1);
        }
        
        exit(0);
    } else {
        // Parent process
        close(pipefd[1]); // Close write end
        
        uintptr_t address = 0;
        if (read(pipefd[0], &address, sizeof(address)) != sizeof(address)) {
            QFAIL("Failed to read address from child");
        }
        close(pipefd[0]);

        QVERIFY(address != 0);

        std::vector<double> result;
        try {
            result = NativeMemoryReader::readDoubles(pid, address, 4);
        } catch (const MemoryAccessException& e) {
            kill(pid, SIGTERM);
            waitpid(pid, nullptr, 0);
            QFAIL(e.what());
        }

        QCOMPARE(result.size(), 4);
        QCOMPARE(result[0], 1.23);
        QCOMPARE(result[1], 4.56);
        QCOMPARE(result[2], 7.89);
        QCOMPARE(result[3], 10.11);

        // Terminate child
        kill(pid, SIGTERM);
        waitpid(pid, nullptr, 0);
    }
#else
    QSKIP("NativeMemoryReader is only supported on Linux");
#endif
}

QTEST_GUILESS_MAIN(TestHpcMemory)
