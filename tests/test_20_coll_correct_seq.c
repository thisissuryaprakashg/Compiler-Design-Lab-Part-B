// test_20_coll_correct_sequence.c
// NEGATIVE TEST: Multiple collectives in correct order on all ranks.
// Bcast → Allreduce → Barrier → Gather — same sequence everywhere.
// EXPECTED: No errors
// RANKS: 2

#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    int data = rank;
    int result = 0;
    int *gathered = NULL;
    if (rank == 0) gathered = (int*)malloc(size * sizeof(int));

    // Correct sequence on ALL ranks:
    MPI_Bcast(&data, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Allreduce(&data, &result, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
    MPI_Barrier(MPI_COMM_WORLD);
    MPI_Gather(&data, 1, MPI_INT, gathered, 1, MPI_INT, 0, MPI_COMM_WORLD);

    printf("Rank %d: bcast=%d allreduce=%d (correct sequence)\n", rank, data, result);

    if (rank == 0) free(gathered);
    MPI_Finalize();
    return 0;
}
