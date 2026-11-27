# Hybrid MPI + OpenMP Fortran tutorial Makefile for GridLock
#
# GNU Fortran (default):
#   make
#
# Intel Fortran (ifx / Intel MPI):
#   make FC=mpiifx FFLAGS="-g -O0 -qopenmp -warn all"

FC ?= mpif90
FFLAGS ?= -g -O0 -fopenmp -Wall -Wextra -fcheck=bounds,do,mem
TARGET ?= stencil_omp
SRC := stencil_omp.f90

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(FC) $(FFLAGS) $(SRC) -o $@

run: $(TARGET)
	OMP_NUM_THREADS=4 mpirun -np 2 ./$(TARGET)

clean:
	rm -f $(TARGET)
