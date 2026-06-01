// test_10_deadlock_small_buf.c
// EDGE CASE: Same send-send deadlock as test_07 but with SMALL buffer.
// MPI may use eager protocol and not actually deadlock,
// but the PATTERN is still wrong — sanitizer should still warn.
// EXPECTED ERROR: [DEADLOCK]
// RANKS: 2

#include <mpi.h>
#include <stdio.h>

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    int data = rank;

    if (rank == 0) {
        // BUG: both ranks send before recv — deadlock pattern
        MPI_Send(&data, 1, MPI_INT, 1, 0, MPI_COMM_WORLD);
        MPI_Recv(&data, 1, MPI_INT, 1, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    } else {
        MPI_Send(&data, 1, MPI_INT, 0, 0, MPI_COMM_WORLD);
        MPI_Recv(&data, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    }

    printf("Rank %d done\n", rank);
    MPI_Finalize();
    return 0;
}
