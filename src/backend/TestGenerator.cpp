/**
 * @file TestGenerator.cpp
 * @brief Implementation of synthetic test code generation and compilation routines.
 *
 * Implements the file writing and mpicxx compiler execution logic required
 * to generate standalone test binaries for multi-rank integration testing.
 */

#include "TestGenerator.hpp"

#include <QFile>
#include <QProcess>
#include <QTextStream>
#include <iostream>

namespace gridlock::backend {

/**
 * @brief Synthesizes and compiles an MPI test executable.
 * @return True if compilation was successful and output binary created.
 *
 * Writes a complete C++ MPI program with rank computation to disk, invokes
 * mpicxx with debug flags (`-g`), and verifies clean compiler completion.
 */
bool generateAndCompileTestTarget() {
    const QString fileName = "mock_mpi_test.cpp";
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        std::cerr << "Failed to open " << fileName.toStdString()
                  << " for writing.\n";
        return false;
    }

    // Stream C++ source code with MPI initialization and loop calculations.
    QTextStream out(&file);
    out << "#include <mpi.h>\n"
        << "#include <iostream>\n"
        << "int main(int argc, char** argv) {\n"
        << "    MPI_Init(&argc, &argv);\n"
        << "    int rank;\n"
        << "    MPI_Comm_rank(MPI_COMM_WORLD, &rank);\n"
        << "    int calc = 0;\n"  // Line 7
        << "    for(int i=0; i<10; ++i) {\n"
        << "        calc += i * rank;\n"  // Line 9
        << "    }\n"
        << "    MPI_Finalize();\n"
        << "    return 0;\n"
        << "}\n";
    file.close();

    // Invoke system MPI C++ compiler to generate debug binary.
    QProcess compiler;
    compiler.start("mpicxx", QStringList() << "-g" << fileName << "-o"
                                           << "mock_mpi_test_bin");
    if (!compiler.waitForStarted()) {
        std::cerr << "Failed to start mpicxx\n";
        return false;
    }

    // Await compilation completion.
    if (!compiler.waitForFinished()) {
        std::cerr << "mpicxx compilation timed out\n";
        return false;
    }

    // Check for non-zero exit code or process crash.
    if (compiler.exitStatus() != QProcess::NormalExit ||
        compiler.exitCode() != 0) {
        std::cerr << "Compilation failed:\n"
                  << compiler.readAllStandardError().toStdString() << "\n";
        return false;
    }

    return true;
}

}  // namespace gridlock::backend
