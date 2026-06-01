// test_11_type_double_as_float.c
// EDGE CASE: double* buffer declared as MPI_FLOAT
// Both are floating-point but different sizes (8 vs 4 bytes).
// Sanitizer should catch this.
// EXPECTED ERROR: [TYPE_MISMATCH]
// RANKS: 2

#include <mpi.h>
#include <stdio.h>

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    double data[4] = {1.1, 2.2, 3.3, 4.4};

    if (rank == 0) {
        // BUG: buffer is double[] but we say MPI_FLOAT (4 bytes vs 8 bytes)
        MPI_Send(data, 4, MPI_FLOAT, 1, 0, MPI_COMM_WORLD);
    } else {
        double recv[4];
        MPI_Recv(recv, 4, MPI_DOUBLE, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        printf("Rank 1 received: %f\n", recv[0]);
    }

    MPI_Finalize();
    return 0;
}
