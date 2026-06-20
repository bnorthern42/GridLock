#include "TestGenerator.hpp"
#include <QFile>
#include <QTextStream>
#include <QProcess>
#include <iostream>

namespace gridlock::backend {

bool generateAndCompileTestTarget() {
    const QString fileName = "mock_mpi_test.cpp";
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        std::cerr << "Failed to open " << fileName.toStdString() << " for writing.\n";
        return false;
    }

    QTextStream out(&file);
    out << "#include <mpi.h>\n"
        << "#include <iostream>\n"
        << "int main(int argc, char** argv) {\n"
        << "    MPI_Init(&argc, &argv);\n"
        << "    int rank;\n"
        << "    MPI_Comm_rank(MPI_COMM_WORLD, &rank);\n"
        << "    int calc = 0;\n" // Line 7
        << "    for(int i=0; i<10; ++i) {\n"
        << "        calc += i * rank;\n" // Line 9
        << "    }\n"
        << "    MPI_Finalize();\n"
        << "    return 0;\n"
        << "}\n";
    file.close();

    QProcess compiler;
    compiler.start("mpicxx", QStringList() << "-g" << fileName << "-o" << "mock_mpi_test_bin");
    if (!compiler.waitForStarted()) {
        std::cerr << "Failed to start mpicxx\n";
        return false;
    }
    
    if (!compiler.waitForFinished()) {
        std::cerr << "mpicxx compilation timed out\n";
        return false;
    }

    if (compiler.exitStatus() != QProcess::NormalExit || compiler.exitCode() != 0) {
        std::cerr << "Compilation failed:\n" << compiler.readAllStandardError().toStdString() << "\n";
        return false;
    }

    return true;
}

} // namespace gridlock::backend
