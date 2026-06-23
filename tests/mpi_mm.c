/******************************************************************************
 * FILE: mpi_mm_deadlock.c
 * DESCRIPTION:
 *   MPI Matrix Multiply - C Version
 *
 *   This version intentionally injects a deterministic MPI deadlock so GridLock
 *   can test deadlock detection.
 *
 *   Deadlock pattern:
 *     rank 0 synchronous-sends to rank 1
 *     rank 1 synchronous-sends to rank 2
 *     rank 2 synchronous-sends to rank 0
 *
 *   Since MPI_Ssend does not complete until the matching receive is posted,
 *   all three ranks block forever before any receive is reached.
 *
 * BUILD:
 *   mpicc -O0 -g -Wall -Wextra -o mpi_mm_deadlock mpi_mm_deadlock.c
 *
 * RUN:
 *   mpirun -np 3 ./mpi_mm_deadlock
 ******************************************************************************/

#include "mpi.h"
#include <stdio.h>
#include <stdlib.h>

#define MATSIZE 500
#define NRA MATSIZE   /* number of rows in matrix A */
#define NCA MATSIZE   /* number of columns in matrix A */
#define NCB MATSIZE   /* number of columns in matrix B */
#define MASTER 0      /* taskid of first task */
#define FROM_MASTER 1 /* setting a message type */
#define FROM_WORKER 2 /* setting a message type */

#define ENABLE_PURPOSEFUL_DEADLOCK 1
#define DEADLOCK_TAG 999

static void inject_purposeful_deadlock(int taskid, int numtasks) {
  int token = taskid;
  int recv_token = -1;
  MPI_Status status;

  if (numtasks != 3) {
    if (taskid == MASTER) {
      fprintf(stderr, "This deadlock test expects exactly 3 MPI ranks.\n"
                      "Run with:\n"
                      "  mpirun -np 3 ./mpi_mm_deadlock\n");
    }

    MPI_Abort(MPI_COMM_WORLD, 1);
    exit(1);
  }

  MPI_Barrier(MPI_COMM_WORLD);

  if (taskid == 0) {
    printf(
        "[rank 0] DEADLOCK TEST: MPI_Ssend to rank 1, then recv from rank 2\n");
    fflush(stdout);

    MPI_Ssend(&token, 1, MPI_INT, 1, DEADLOCK_TAG, MPI_COMM_WORLD);

    /* Never reached */
    MPI_Recv(&recv_token, 1, MPI_INT, 2, DEADLOCK_TAG, MPI_COMM_WORLD, &status);
  } else if (taskid == 1) {
    printf(
        "[rank 1] DEADLOCK TEST: MPI_Ssend to rank 2, then recv from rank 0\n");
    fflush(stdout);

    MPI_Ssend(&token, 1, MPI_INT, 2, DEADLOCK_TAG, MPI_COMM_WORLD);

    /* Never reached */
    MPI_Recv(&recv_token, 1, MPI_INT, 0, DEADLOCK_TAG, MPI_COMM_WORLD, &status);
  } else if (taskid == 2) {
    printf(
        "[rank 2] DEADLOCK TEST: MPI_Ssend to rank 0, then recv from rank 1\n");
    fflush(stdout);

    MPI_Ssend(&token, 1, MPI_INT, 0, DEADLOCK_TAG, MPI_COMM_WORLD);

    /* Never reached */
    MPI_Recv(&recv_token, 1, MPI_INT, 1, DEADLOCK_TAG, MPI_COMM_WORLD, &status);
  }
}

int main(int argc, char *argv[]) {
  int numtasks,              /* number of tasks in partition */
      taskid,                /* a task identifier */
      numworkers,            /* number of worker tasks */
      source,                /* task id of message source */
      dest,                  /* task id of message destination */
      mtype,                 /* message type */
      rows,                  /* rows of matrix A sent to each worker */
      averow, extra, offset, /* used to determine rows sent to each worker */
      i, j, k, rc = 0;       /* misc */

  double a[NRA][NCA], /* matrix A to be multiplied */
      b[NCA][NCB],    /* matrix B to be multiplied */
      c[NRA][NCB];    /* result matrix C */

  MPI_Status status;

  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &taskid);
  MPI_Comm_size(MPI_COMM_WORLD, &numtasks);

#if ENABLE_PURPOSEFUL_DEADLOCK
  inject_purposeful_deadlock(taskid, numtasks);
#endif

  if (numtasks < 2) {
    printf("Need at least two MPI tasks. Quitting...\n");
    MPI_Abort(MPI_COMM_WORLD, rc);
    exit(1);
  }

  numworkers = numtasks - 1;

  /**************************** master task
   * ************************************/
  if (taskid == MASTER) {
    printf("mpi_mm has started with %d tasks.\n", numtasks);

    for (i = 0; i < NRA; i++)
      for (j = 0; j < NCA; j++)
        a[i][j] = i + j;

    for (i = 0; i < NCA; i++)
      for (j = 0; j < NCB; j++)
        b[i][j] = i * j;

    double start = MPI_Wtime();

    /* Send matrix data to the worker tasks */
    averow = NRA / numworkers;
    extra = NRA % numworkers;
    offset = 0;
    mtype = FROM_MASTER;

    for (dest = 1; dest <= numworkers; dest++) {
      rows = (dest <= extra) ? averow + 1 : averow;

      MPI_Send(&offset, 1, MPI_INT, dest, mtype, MPI_COMM_WORLD);
      MPI_Send(&rows, 1, MPI_INT, dest, mtype, MPI_COMM_WORLD);
      MPI_Send(&a[offset][0], rows * NCA, MPI_DOUBLE, dest, mtype,
               MPI_COMM_WORLD);
      MPI_Send(&b, NCA * NCB, MPI_DOUBLE, dest, mtype, MPI_COMM_WORLD);

      offset = offset + rows;
    }

    /* Receive results from worker tasks */
    mtype = FROM_WORKER;

    for (i = 1; i <= numworkers; i++) {
      source = i;

      MPI_Recv(&offset, 1, MPI_INT, source, mtype, MPI_COMM_WORLD, &status);
      MPI_Recv(&rows, 1, MPI_INT, source, mtype, MPI_COMM_WORLD, &status);
      MPI_Recv(&c[offset][0], rows * NCB, MPI_DOUBLE, source, mtype,
               MPI_COMM_WORLD, &status);
    }

    double finish = MPI_Wtime();
    printf("Done in %f seconds.\n", finish - start);
  }

  /**************************** worker task
   * ************************************/
  if (taskid > MASTER) {
    mtype = FROM_MASTER;

    MPI_Recv(&offset, 1, MPI_INT, MASTER, mtype, MPI_COMM_WORLD, &status);
    MPI_Recv(&rows, 1, MPI_INT, MASTER, mtype, MPI_COMM_WORLD, &status);
    MPI_Recv(&a, rows * NCA, MPI_DOUBLE, MASTER, mtype, MPI_COMM_WORLD,
             &status);
    MPI_Recv(&b, NCA * NCB, MPI_DOUBLE, MASTER, mtype, MPI_COMM_WORLD, &status);

    for (k = 0; k < NCB; k++)
      for (i = 0; i < rows; i++) {
        c[i][k] = 0.0;

        for (j = 0; j < NCA; j++)
          c[i][k] = c[i][k] + a[i][j] * b[j][k];
      }

    mtype = FROM_WORKER;

    MPI_Send(&offset, 1, MPI_INT, MASTER, mtype, MPI_COMM_WORLD);
    MPI_Send(&rows, 1, MPI_INT, MASTER, mtype, MPI_COMM_WORLD);
    MPI_Send(&c, rows * NCB, MPI_DOUBLE, MASTER, mtype, MPI_COMM_WORLD);
  }

  MPI_Finalize();
  return 0;
}