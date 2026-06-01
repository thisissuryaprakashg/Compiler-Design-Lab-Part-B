// test_15_collective_root_mismatch.c
// EDGE CASE: Both ranks call MPI_Bcast, but they specify different roots.
// Rank 0 says root=0, Rank 1 says root=1.
// EXPECTED ERROR: [COLLECTIVE_ORDER]
// RANKS: 2

#include <mpi.h>
#include <stdio.h>

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    int data = 42;

    // BUG: each rank uses itself as root — undefined behavior
    MPI_Bcast(&data, 1, MPI_INT, rank, MPI_COMM_WORLD);

    printf("Rank %d done\n", rank);
    MPI_Finalize();
    return 0;
}
