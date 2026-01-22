#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>

typedef struct
{
    int nr;
    double val;
} Element;

int main(int argc, char **argv)
{
    MPI_Init(&argc, &argv);

    int rank, p;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &p);

    if (argc < 3)
    {
        if (rank == 0)
            printf("Usage: mpirun -n <p> %s <n> <seed>\n", argv[0]);
        MPI_Finalize();
        return 1;
    }

    int n = atoi(argv[1]);
    int seed = atoi(argv[2]);

    if (n % p != 0)
    {
        if (rank == 0)
            printf("Error: p must divide n (n=%d, p=%d)\n", n, p);
        MPI_Finalize();
        return 1;
    }

    int local_n = n / p;
    Element *local_a = (Element *)malloc(local_n * sizeof(Element));

    // MPI Datatype
    MPI_Datatype mpi_elem_type;
    int blocklengths[2] = {1, 1};
    MPI_Aint offsets[2];
    offsets[0] = offsetof(Element, nr);
    offsets[1] = offsetof(Element, val);
    MPI_Datatype types[2] = {MPI_INT, MPI_DOUBLE};
    MPI_Type_create_struct(2, blocklengths, offsets, types, &mpi_elem_type);
    MPI_Type_commit(&mpi_elem_type);

    // 1. Parallel Initialization
    int global_start = rank * local_n;
    for (int k = 0; k < local_n; ++k)
    {
        int i = global_start + k;
        srand(seed * (i + 5));
        local_a[k].nr = i;
        local_a[k].val = (rand() % 100) / 10.0;
    }

    // 2. Output Input
    Element *full_array = NULL;
    if (n <= 20)
    {
        if (rank == 0)
            full_array = (Element *)malloc(n * sizeof(Element));
        MPI_Gather(local_a, local_n, mpi_elem_type,
                   full_array, local_n, mpi_elem_type, 0, MPI_COMM_WORLD);

        if (rank == 0)
        {
            printf("Eingabe:");
            for (int i = 0; i < n; ++i)
            {
                printf(" (%d, %.1f)", full_array[i].nr, full_array[i].val);
            }
            printf("\n");
            fflush(stdout); // Force output
            free(full_array);
        }
    }

    // 3. Sorting
    unsigned int my_swaps = 0;
    int my_end_idx = global_start + local_n - 1;

    // time measurement start
    MPI_Barrier(MPI_COMM_WORLD);
    double start_time = MPI_Wtime();

    for (int pass = 0; pass < n - 1; ++pass)
    {
        int limit = n - 1 - pass;

        // Corrected Loop Exit Condition
        if (limit < global_start)
        {
            break;
        }

        // Receive from left
        if (rank > 0)
        {
            Element incoming;
            MPI_Recv(&incoming, 1, mpi_elem_type, rank - 1, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            if (incoming.val > local_a[0].val)
            {
                Element to_return = local_a[0];
                local_a[0] = incoming;
                MPI_Send(&to_return, 1, mpi_elem_type, rank - 1, 0, MPI_COMM_WORLD);
                my_swaps++;
            }
            else
            {
                MPI_Send(&incoming, 1, mpi_elem_type, rank - 1, 0, MPI_COMM_WORLD);
            }
        }

        // Local Scan
        int scan_len = local_n - 1;
        int stop_in_me = (limit <= my_end_idx);

        if (stop_in_me)
        {
            scan_len = limit - global_start;
        }

        for (int j = 0; j < scan_len; ++j)
        {
            if (local_a[j].val > local_a[j + 1].val)
            {
                Element tmp = local_a[j];
                local_a[j] = local_a[j + 1];
                local_a[j + 1] = tmp;
                my_swaps++;
            }
        }

        // Send to right
        if (rank < p - 1 && !stop_in_me)
        {
            Element my_last = local_a[local_n - 1];
            MPI_Send(&my_last, 1, mpi_elem_type, rank + 1, 0, MPI_COMM_WORLD);

            Element returned;
            MPI_Recv(&returned, 1, mpi_elem_type, rank + 1, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            local_a[local_n - 1] = returned;
        }
    }

    // time measurement end
    MPI_Barrier(MPI_COMM_WORLD);
    double end_time = MPI_Wtime();

    if (rank == 0)
    {
        printf("Time taken: %.6f seconds\n", end_time - start_time);
    }

    // 4. Output Logic

    // Reduce swaps
    unsigned int total_swaps = 0;
    MPI_Reduce(&my_swaps, &total_swaps, 1, MPI_UNSIGNED, MPI_SUM, 0, MPI_COMM_WORLD);

    if (rank == 0)
    {
        printf("n_swaps = %u\n", total_swaps);
        fflush(stdout);
    }

    // Process-wise bounds output (Ordered by rank)
    for (int r = 0; r < p; ++r)
    {
        MPI_Barrier(MPI_COMM_WORLD);
        if (rank == r)
        {
            printf("P%d: (%d, %.1f) (%d, %.1f)\n",
                   rank,
                   local_a[0].nr, local_a[0].val,
                   local_a[local_n - 1].nr, local_a[local_n - 1].val);
            fflush(stdout); // IMPORTANT: Force flush to keep order
        }
    }

    // IMPORTANT: Wait for all P0..P3 prints to finish before P0 prints "Ausgabe"
    MPI_Barrier(MPI_COMM_WORLD);

    // Final Sorted Output for n <= 20
    if (n <= 20)
    {
        if (rank == 0)
            full_array = (Element *)malloc(n * sizeof(Element));
        MPI_Gather(local_a, local_n, mpi_elem_type,
                   full_array, local_n, mpi_elem_type, 0, MPI_COMM_WORLD);

        if (rank == 0)
        {
            printf("Ausgabe:");
            for (int i = 0; i < n; ++i)
            {
                printf(" (%d, %.1f)", full_array[i].nr, full_array[i].val);
            }
            printf("\n");
            fflush(stdout);
            free(full_array);
        }
    }

    MPI_Type_free(&mpi_elem_type);
    free(local_a);
    MPI_Finalize();
    return 0;
}