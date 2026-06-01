// test_22_deadlock_halo.c
// REAL-WORLD BUG: Bidirectional halo exchange where neighbors
// send to EACH OTHER with blocking Sends before posting Recvs.
// This creates true cycles: 0↔1, 1↔2, 2↔3.
// EXPECTED ERROR: [DEADLOCK]
// RANKS: 4

#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>

#define HALO (1024 * 256)  // large to prevent eager buffering

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    double *send_left  = (double*)calloc(HALO, sizeof(double));
    double *send_right = (double*)calloc(HALO, sizeof(double));
    double *recv_left  = (double*)calloc(HALO, sizeof(double));
    double *recv_right = (double*)calloc(HALO, sizeof(double));

    int left  = (rank > 0)        ? rank - 1 : MPI_PROC_NULL;
    int right = (rank < size - 1) ? rank + 1 : MPI_PROC_NULL;

    // BUG: Send to BOTH neighbors before receiving from either.
    // Ranks 1 and 2 each send to their right neighbor (blocking).
    // But right neighbor is also sending to THEM (blocking).
    // → Cycle: rank 1 sends to rank 2, rank 2 sends to rank 1.

    // Send right first
    if (right != MPI_PROC_NULL)
        MPI_Send(send_right, HALO, MPI_DOUBLE, right, 0, MPI_COMM_WORLD);
    // Send left
    if (left != MPI_PROC_NULL)
        MPI_Send(send_left, HALO, MPI_DOUBLE, left, 1, MPI_COMM_WORLD);
    // Recv from left
    if (left != MPI_PROC_NULL)
        MPI_Recv(recv_left, HALO, MPI_DOUBLE, left, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    // Recv from right
    if (right != MPI_PROC_NULL)
        MPI_Recv(recv_right, HALO, MPI_DOUBLE, right, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    printf("Rank %d: halo exchange done\n", rank);
    free(send_left); free(send_right); free(recv_left); free(recv_right);
    MPI_Finalize();
    return 0;
}
