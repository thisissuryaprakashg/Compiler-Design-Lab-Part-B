// test_01_type_mismatch.c
// BUG: Rank 0 sends a float array but declares it as MPI_INT.
//      Rank 1 receives with MPI_FLOAT (correct on its side).
//      Sanitizer should catch: send type (MPI_INT) != recv type (MPI_FLOAT)
// EXPECTED ERROR: [TYPE_MISMATCH]
// RANKS: 2

#include <mpi.h>
#include <stdio.h>

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    float buf[4] = {1.0f, 2.0f, 3.0f, 4.0f};

    if (rank == 0) {
        // BUG: buffer is float[] but we say MPI_INT
        MPI_Send(buf, 4, MPI_INT, 1, 0, MPI_COMM_WORLD);
    } else {
        MPI_Recv(buf, 4, MPI_FLOAT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        printf("Rank 1 received\n");
    }

    MPI_Finalize();
    return 0;
}
