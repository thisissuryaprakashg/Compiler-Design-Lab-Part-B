// test_16_deadlock_4rank.c
// EDGE CASE: 4 ranks, each sends to (rank+1)%4 — circular deadlock.
//            0->1, 1->2, 2->3, 3->0
// EXPECTED ERROR: [DEADLOCK] with 4-node cycle
// RANKS: 4

#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>

#define BIGSIZE (1024 * 1024)

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (size != 4) {
        if (rank == 0) fprintf(stderr, "Run with exactly 4 ranks\n");
        MPI_Finalize();
        return 1;
    }

    int *buf = (int*)malloc(BIGSIZE * sizeof(int));
    int next = (rank + 1) % 4;
    int prev = (rank + 3) % 4;

    // BUG: all ranks do blocking Send before Recv — 4-rank circular deadlock
    MPI_Send(buf, BIGSIZE, MPI_INT, next, 0, MPI_COMM_WORLD);
    MPI_Recv(buf, BIGSIZE, MPI_INT, prev, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    printf("Rank %d done\n", rank);
    free(buf);
    MPI_Finalize();
    return 0;
}
