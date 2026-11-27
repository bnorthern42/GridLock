! tutorial/stencil_omp.f90
! Hybrid MPI + OpenMP 2D column-major stencil tutorial for GridLock.
! Build with gfortran/OpenMPI:
!   mpif90 -g -O0 -fopenmp -Wall -Wextra -fcheck=bounds,do,mem tutorial/stencil_omp.f90 -o tutorial/stencil_omp
! Build with Intel ifx/Intel MPI:
!   mpiifx -g -O0 -qopenmp -warn all tutorial/stencil_omp.f90 -o tutorial/stencil_omp

program stencil_omp
  use iso_fortran_env, only: real64
  use mpi
  use omp_lib
  implicit none

  integer, parameter :: NROWS = 4
  integer, parameter :: NCOLS = 6

  real(real64) :: field(NROWS, NCOLS)
  real(real64) :: updated(NROWS, NCOLS)
  real(real64) :: local_sum, global_sum
  integer :: ierr, rank, nprocs, provided
  integer :: i, j, tid, nthreads

  call MPI_Init_thread(MPI_THREAD_FUNNELED, provided, ierr)
  call MPI_Comm_rank(MPI_COMM_WORLD, rank, ierr)
  call MPI_Comm_size(MPI_COMM_WORLD, nprocs, ierr)

  ! Deliberate, human-readable layout: field(row, col) = rank*1000 + col*100 + row.
  ! On Rank 0:
  !   Column 1: field(1:4, 1) = [101, 102, 103, 104]  (linear offsets 0..3)
  !   Column 2: field(1:4, 2) = [201, 202, 203, 204]  (linear offsets 4..7)
  do j = 1, NCOLS
    do i = 1, NROWS
      field(i, j) = real(rank * 1000 + j * 100 + i, real64)
      updated(i, j) = field(i, j)
    end do
  end do

  local_sum = 0.0_real64
  nthreads = 1

  !$omp parallel default(none) shared(field, updated, nthreads) private(i, j, tid) reduction(+:local_sum)
  tid = omp_get_thread_num()
  !$omp single
  nthreads = omp_get_num_threads()
  !$omp end single

  ! Outer loop iterates over columns so each worker accesses contiguous column-major memory.
  !$omp do schedule(static)
  do j = 2, NCOLS - 1
    do i = 2, NROWS - 1
      updated(i, j) = 0.2_real64 * (field(i, j) + field(i - 1, j) + field(i + 1, j) + &
                                    field(i, j - 1) + field(i, j + 1))
      local_sum = local_sum + updated(i, j)
    end do
  end do
  !$omp end do
  !$omp end parallel

  global_sum = 0.0_real64
  call MPI_Reduce(local_sum, global_sum, 1, MPI_DOUBLE_PRECISION, MPI_SUM, 0, MPI_COMM_WORLD, ierr)

  if (rank == 0) then
    write (*, '(A,I0,A,I0,A,F0.2,A)') 'GridLock Fortran MPI+OpenMP stencil complete (ranks=', &
      nprocs, ', threads/rank=', nthreads, ', global_sum=', global_sum, ')'
  end if

  call MPI_Finalize(ierr)
end program stencil_omp
