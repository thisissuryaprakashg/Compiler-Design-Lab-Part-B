// test_24_combo_type_overlap.c
// REAL-WORLD: Simulation code that sends boundary data.
// Multiple bugs in ONE program:
//   1) TYPE_MISMATCH: float buffer sent as MPI_DOUBLE
//   2) BUFFER_OVERLAP: Isend buffer reused in another Send before Wait
// EXPECTED ERRORS: [TYPE_MISMATCH] + [BUFFER_OVERLAP]
// RANKS: 2

#include <mpi.h>
#include <stdio.h>

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    float boundary[8] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};

    if (rank == 0) {
        MPI_Request req;

        // BUG 1: float[] buffer sent as MPI_DOUBLE (wrong element size!)
        MPI_Isend(boundary, 4, MPI_DOUBLE, 1, 0, MPI_COMM_WORLD, &req);

        // BUG 2: reuse same boundary buffer before Wait
        MPI_Send(boundary, 4, MPI_FLOAT, 1, 1, MPI_COMM_WORLD);

        MPI_Wait(&req, MPI_STATUS_IGNORE);
    } else {
        double dbuf[4];
        float fbuf[4];
        MPI_Recv(dbuf, 4, MPI_DOUBLE, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        MPI_Recv(fbuf, 4, MPI_FLOAT, 0, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        printf("Rank 1 received both messages\n");
    }

    MPI_Finalize();
    return 0;
}
