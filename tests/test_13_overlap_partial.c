// test_13_overlap_partial.c
// EDGE CASE: Isend first half of array, then Send overlapping middle portion
//            before Wait. The ranges partially overlap.
// EXPECTED ERROR: [BUFFER_OVERLAP]
// RANKS: 2

#include <mpi.h>
#include <stdio.h>

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    int buf[10] = {0,1,2,3,4,5,6,7,8,9};

    if (rank == 0) {
        MPI_Request req;
        // Isend buf[0..5] (6 ints, 24 bytes)
        MPI_Isend(&buf[0], 6, MPI_INT, 1, 0, MPI_COMM_WORLD, &req);

        // BUG: Send buf[4..7] (4 ints) — overlaps with in-flight buf[0..5]
        MPI_Send(&buf[4], 4, MPI_INT, 1, 1, MPI_COMM_WORLD);

        MPI_Wait(&req, MPI_STATUS_IGNORE);
    } else {
        int tmp[6], tmp2[4];
        MPI_Recv(tmp, 6, MPI_INT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        MPI_Recv(tmp2, 4, MPI_INT, 0, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        printf("Rank 1 received both\n");
    }

    MPI_Finalize();
    return 0;
}
