// test_21_coll_conditional_skip.c
// HARD CASE: Rank 0 calls Allreduce inside an if(rank==0) block.
// This is a common real-world bug — collectives MUST be called by ALL ranks.
// EXPECTED ERROR: [COLLECTIVE_ORDER] — seq count mismatch at Barrier
// RANKS: 2

#include <mpi.h>
#include <stdio.h>

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    int val = rank + 1;
    int sum = 0;

    // Correct: all ranks call this
    MPI_Allreduce(&val, &sum, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);

    if (rank == 0) {
        // BUG: only rank 0 calls this Allreduce — rank 1 skips it!
        int local = 10;
        int total = 0;
        MPI_Allreduce(&local, &total, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
        printf("Root extra allreduce: %d\n", total);
    }

    // Barrier to sync — this will detect the ordering mismatch
    MPI_Barrier(MPI_COMM_WORLD);

    printf("Rank %d done, sum=%d\n", rank, sum);
    MPI_Finalize();
    return 0;
}
