// test_23_combo_scatter_bugs.c
// REAL-WORLD: Simulated data distribution where root scatters sensor data.
// Multiple bugs:
//   1) TYPE_MISMATCH: root has double[] but scatters as MPI_INT
//   2) COLLECTIVE_ORDER: rank 1 calls Barrier before Scatter
// EXPECTED ERRORS: [TYPE_MISMATCH] + [COLLECTIVE_ORDER]
// RANKS: 2

#include <mpi.h>
#include <stdio.h>

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (rank == 0) {
        // BUG 1: sensor_data is double[] but we scatter as MPI_INT
        double sensor_data[4] = {1.5, 2.5, 3.5, 4.5};
        int recv[2];
        MPI_Scatter(sensor_data, 2, MPI_INT, recv, 2, MPI_INT, 0, MPI_COMM_WORLD);
        MPI_Barrier(MPI_COMM_WORLD);
        printf("Rank 0: got %d %d\n", recv[0], recv[1]);
    } else {
        // BUG 2: Barrier BEFORE Scatter — wrong order!
        MPI_Barrier(MPI_COMM_WORLD);
        int recv[2];
        MPI_Scatter(NULL, 0, MPI_INT, recv, 2, MPI_INT, 0, MPI_COMM_WORLD);
        printf("Rank 1: got %d %d\n", recv[0], recv[1]);
    }

    MPI_Finalize();
    return 0;
}
