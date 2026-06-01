// test_18_coll_extra.c
// EDGE CASE: Rank 0 calls two Bcasts, Rank 1 calls only one.
// The second Bcast on rank 0 has no matching collective on rank 1.
// EXPECTED ERROR: [COLLECTIVE_ORDER] — sequence count mismatch
// RANKS: 2

#include <mpi.h>
#include <stdio.h>

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    int data = 42;

    // First Bcast — both ranks participate (correct)
    MPI_Bcast(&data, 1, MPI_INT, 0, MPI_COMM_WORLD);

    if (rank == 0) {
        // BUG: Rank 0 does a second Bcast, Rank 1 does not
        MPI_Bcast(&data, 1, MPI_INT, 0, MPI_COMM_WORLD);
    }

    // This Barrier should catch that rank 0 has 2 collectives, rank 1 has 1
    MPI_Barrier(MPI_COMM_WORLD);

    printf("Rank %d done\n", rank);
    MPI_Finalize();
    return 0;
}
