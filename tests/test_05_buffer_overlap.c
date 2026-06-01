// test_05_buffer_overlap.c
// BUG: Isend a buffer, then immediately use the SAME buffer in another Send
//      before calling MPI_Wait — the first buffer is still in-flight.
// EXPECTED ERROR: [BUFFER_OVERLAP]
// RANKS: 2

#include <mpi.h>
#include <stdio.h>

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    int buf[4] = {1, 2, 3, 4};

    if (rank == 0) {
        MPI_Request req;
        // Non-blocking send — buf is now in-flight
        MPI_Isend(buf, 4, MPI_INT, 1, 0, MPI_COMM_WORLD, &req);

        // BUG: reuse the same buf in another send BEFORE MPI_Wait
        MPI_Send(buf, 4, MPI_INT, 1, 1, MPI_COMM_WORLD);

        MPI_Wait(&req, MPI_STATUS_IGNORE);
    } else {
        MPI_Recv(buf, 4, MPI_INT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        MPI_Recv(buf, 4, MPI_INT, 0, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        printf("Rank 1 received both\n");
    }

    MPI_Finalize();
    return 0;
}
