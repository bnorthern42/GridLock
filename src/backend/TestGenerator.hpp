/**
 * @file TestGenerator.hpp
 * @brief Helper utility for synthesizing and compiling mock MPI programs.
 *
 * Provides functions to generate on-the-fly MPI source files and invoke
 * the system MPI compiler (mpicxx) for debugging and integration tests.
 */

#pragma once

namespace gridlock::backend {

/**
 * @brief Generates a sample MPI test source file and compiles it into an executable.
 *
 * Writes a synthetic matrix-like calculation program to `mock_mpi_test.cpp` and invokes
 * `mpicxx -g mock_mpi_test.cpp -o mock_mpi_test_bin` synchronously.
 *
 * @return True if generation and compilation succeeded with exit code 0, false otherwise.
 */
bool generateAndCompileTestTarget();

}  // namespace gridlock::backend
