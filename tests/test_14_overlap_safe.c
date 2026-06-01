// test_14_overlap_safe.c
// NEGATIVE TEST: Isend first half, Send second half — NO overlap.
// EXPECTED: No errors
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
        // Isend buf[0..4] (5 ints)
        MPI_Isend(&buf[0], 5, MPI_INT, 1, 0, MPI_COMM_WORLD, &req);

        // Send buf[5..9] (5 ints) — no overlap, different region
        MPI_Send(&buf[5], 5, MPI_INT, 1, 1, MPI_COMM_WORLD);

        MPI_Wait(&req, MPI_STATUS_IGNORE);
    } else {
        int tmp[5], tmp2[5];
        MPI_Recv(tmp, 5, MPI_INT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        MPI_Recv(tmp2, 5, MPI_INT, 0, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        printf("Rank 1 received both (safe, no overlap)\n");
    }

    MPI_Finalize();
    return 0;
}
