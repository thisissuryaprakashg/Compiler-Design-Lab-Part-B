// test_07_deadlock.c
// BUG: Both ranks do blocking Send to each other BEFORE Recv.
//      Uses large buffer to prevent MPI eager protocol buffering.
// EXPECTED ERROR: [DEADLOCK]
// RANKS: 2

#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>

#define BIGSIZE (1024 * 1024)  // 1MB — too large for eager protocol

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    int *data = (int*)malloc(BIGSIZE * sizeof(int));
    for (int i = 0; i < BIGSIZE; i++) data[i] = rank;

    if (rank == 0) {
        // BUG: rank 0 sends to 1 (blocking) — rank 1 also sends to 0
        MPI_Send(data, BIGSIZE, MPI_INT, 1, 0, MPI_COMM_WORLD);
        MPI_Recv(data, BIGSIZE, MPI_INT, 1, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    } else {
        // BUG: deadlock — both sides sending before either receives
        MPI_Send(data, BIGSIZE, MPI_INT, 0, 0, MPI_COMM_WORLD);
        MPI_Recv(data, BIGSIZE, MPI_INT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    }

    printf("Rank %d done\n", rank);
    free(data);
    MPI_Finalize();
    return 0;
}
