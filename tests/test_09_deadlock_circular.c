// test_09_deadlock_sendrecv.c
// BUG: 3 ranks, each sends to next and recvs from previous,
//      but ALL do blocking Send first — circular deadlock.
//      Rank 0 -> 1, Rank 1 -> 2, Rank 2 -> 0 (all Send before Recv)
// EXPECTED ERROR: [DEADLOCK]
// RANKS: 3

#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>

#define BIGSIZE (1024 * 1024)

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (size != 3) {
        if (rank == 0) fprintf(stderr, "Run with exactly 3 ranks\n");
        MPI_Finalize();
        return 1;
    }

    int *buf = (int*)malloc(BIGSIZE * sizeof(int));
    int next = (rank + 1) % 3;
    int prev = (rank + 2) % 3;

    // BUG: all ranks do blocking Send before Recv — circular deadlock
    MPI_Send(buf, BIGSIZE, MPI_INT, next, 0, MPI_COMM_WORLD);
    MPI_Recv(buf, BIGSIZE, MPI_INT, prev, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    printf("Rank %d done\n", rank);
    free(buf);
    MPI_Finalize();
    return 0;
}
