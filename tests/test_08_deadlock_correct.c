// test_08_deadlock_correct.c
// NEGATIVE TEST: Rank 0 sends then recvs, Rank 1 recvs then sends.
// This is the CORRECT pattern — no deadlock should be reported.
// EXPECTED: No errors
// RANKS: 2

#include <mpi.h>
#include <stdio.h>

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    int data = rank;

    if (rank == 0) {
        // Correct: send first, then recv
        MPI_Send(&data, 1, MPI_INT, 1, 0, MPI_COMM_WORLD);
        MPI_Recv(&data, 1, MPI_INT, 1, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    } else {
        // Correct: recv first (matches rank 0's send), then send
        MPI_Recv(&data, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        MPI_Send(&data, 1, MPI_INT, 0, 1, MPI_COMM_WORLD);
    }

    printf("Rank %d done (no deadlock)\n", rank);
    MPI_Finalize();
    return 0;
}
