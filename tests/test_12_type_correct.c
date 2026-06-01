// test_12_type_correct.c
// NEGATIVE TEST: All types match correctly.
// int buffer with MPI_INT, double buffer with MPI_DOUBLE.
// EXPECTED: No errors
// RANKS: 2

#include <mpi.h>
#include <stdio.h>

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    int idata[4] = {1, 2, 3, 4};
    double ddata[4] = {1.0, 2.0, 3.0, 4.0};

    if (rank == 0) {
        MPI_Send(idata, 4, MPI_INT, 1, 0, MPI_COMM_WORLD);
        MPI_Send(ddata, 4, MPI_DOUBLE, 1, 1, MPI_COMM_WORLD);
    } else {
        MPI_Recv(idata, 4, MPI_INT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        MPI_Recv(ddata, 4, MPI_DOUBLE, 0, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        printf("Rank 1: int=%d double=%f (correct types)\n", idata[0], ddata[0]);
    }

    MPI_Finalize();
    return 0;
}
