// test_17_ring_correct.c
// NEGATIVE TEST: Correct ring exchange using MPI_Sendrecv.
// Each rank sends to next and receives from previous simultaneously.
// MPI_Sendrecv is deadlock-safe by design.
// EXPECTED: No errors
// RANKS: 4

#include <mpi.h>
#include <stdio.h>

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    int send_data = rank * 10;
    int recv_data = -1;
    int next = (rank + 1) % size;
    int prev = (rank - 1 + size) % size;

    // Correct: MPI_Sendrecv avoids deadlock
    MPI_Sendrecv(&send_data, 1, MPI_INT, next, 0,
                 &recv_data, 1, MPI_INT, prev, 0,
                 MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    printf("Rank %d: sent %d, received %d from rank %d\n",
           rank, send_data, recv_data, prev);
    MPI_Finalize();
    return 0;
}
