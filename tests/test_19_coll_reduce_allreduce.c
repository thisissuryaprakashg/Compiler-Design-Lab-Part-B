// test_19_coll_reduce_vs_allreduce.c
// EDGE CASE: Rank 0 calls MPI_Reduce, Rank 1 calls MPI_Allreduce.
// Both are reductions but they are different MPI calls — ordering violation.
// EXPECTED ERROR: [COLLECTIVE_ORDER]
// RANKS: 2

#include <mpi.h>
#include <stdio.h>

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    int val = rank + 1;
    int result = 0;

    if (rank == 0) {
        // Rank 0 calls Reduce (rooted, result only at root)
        MPI_Reduce(&val, &result, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
    } else {
        // BUG: Rank 1 calls Allreduce (non-rooted, result at all ranks)
        MPI_Allreduce(&val, &result, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
    }

    printf("Rank %d result=%d\n", rank, result);
    MPI_Finalize();
    return 0;
}
