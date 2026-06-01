// test_06_collective_order.c
// BUG: Rank 0 calls Bcast then Barrier, but Rank 1 calls Barrier then Bcast.
//      Collectives must be called in the same order on all ranks.
// EXPECTED ERROR: [COLLECTIVE_ORDER]
// RANKS: 2

#include <mpi.h>
#include <stdio.h>

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    int data = 42;

    if (rank == 0) {
        // Rank 0: Bcast first, then Barrier
        MPI_Bcast(&data, 1, MPI_INT, 0, MPI_COMM_WORLD);
        MPI_Barrier(MPI_COMM_WORLD);
    } else {
        // BUG: Rank 1: Barrier first, then Bcast — wrong order!
        MPI_Barrier(MPI_COMM_WORLD);
        MPI_Bcast(&data, 1, MPI_INT, 0, MPI_COMM_WORLD);
    }

    printf("Rank %d done\n", rank);
    MPI_Finalize();
    return 0;
}
