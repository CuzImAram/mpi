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

    // Call arguments
    if (argc < 3)
    {
        if (rank == 0)
            printf("Usage: mpirun -n <p> %s <n> <seed>\n", argv[0]);
        MPI_Finalize();
        return 1;
    }

    int n = atoi(argv[1]);
    int seed = atoi(argv[2]);

    // Validate input constraint p|n
    if (n % p != 0)
    {
        if (rank == 0)
            printf("Error: p must divide n (n=%d, p=%d)\n", n, p);
        MPI_Finalize();
        return 1;
    }

    int local_n = n / p;
    Element *local_a = (Element *)malloc(local_n * sizeof(Element));

    // Create MPI Datatype for struct Element
    // Although manual packing is possible, Type_struct is cleaner.
    MPI_Datatype mpi_elem_type;
    int blocklengths[2] = {1, 1};
    MPI_Aint offsets[2];
    offsets[0] = offsetof(Element, nr);
    offsets[1] = offsetof(Element, val);
    MPI_Datatype types[2] = {MPI_INT, MPI_DOUBLE};
    MPI_Type_create_struct(2, blocklengths, offsets, types, &mpi_elem_type);
    MPI_Type_commit(&mpi_elem_type);

    // 1. Parallel Initialization
    // Global index calculation: P0 gets 0..local_n-1, P1 gets local_n..2*local_n-1, etc.
    int global_start = rank * local_n;
    for (int k = 0; k < local_n; ++k)
    {
        int i = global_start + k; // Global index
        srand(seed * (i + 5));    // Specific seed per index
        local_a[k].nr = i;
        local_a[k].val = (rand() % 100) / 10.0;
    }

    // 2. Output Input for n <= 20
    // We gather to P0 for simple printing (ordered).
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
            // Don't free full_array yet, we might reuse or free now.
            // Freeing now to keep clean, re-alloc later.
            free(full_array);
        }
    }

    // 3. Sorting with Exact Comparisons
    // Sequential loop: for (i = n-1; i > 0; --i) ...
    // This defines n-1 passes. In pass 'pass', the bubble goes from 0 to 'limit'.
    unsigned int my_swaps = 0;
    int my_end_idx = global_start + local_n - 1;

    for (int pass = 0; pass < n - 1; ++pass)
    {
        int limit = n - 1 - pass; // The target index for the bubble

        // Optimization: If the target limit is to the left of my start,
        // this process is done with all remaining passes.
        if (limit <= global_start)
        {
            break;
        }

        // A. Boundary Receive (Left)
        // If we are not the first process, we receive the bubble from the left.
        if (rank > 0)
        {
            Element incoming;
            MPI_Recv(&incoming, 1, mpi_elem_type, rank - 1, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            // Compare incoming (left) vs local_a[0] (right)
            if (incoming.val > local_a[0].val)
            {
                // Swap needed: we keep the larger value (incoming), return smaller (local_a[0])
                Element to_return = local_a[0];
                local_a[0] = incoming;
                MPI_Send(&to_return, 1, mpi_elem_type, rank - 1, 0, MPI_COMM_WORLD);
                // Note: The 'swap' count is usually associated with the comparison.
                // We performed the comparison logic here, so we count it.
                my_swaps++;
            }
            else
            {
                // No swap: we return incoming back to left, keep local_a[0]
                MPI_Send(&incoming, 1, mpi_elem_type, rank - 1, 0, MPI_COMM_WORLD);
            }
        }

        // B. Local Scan
        // We scan from j=0 to j < local_limit.
        // If 'limit' falls inside our block, we stop scanning at the corresponding offset.
        // If 'limit' is beyond our block, we scan the whole local array (carrying bubble to end).

        int scan_len = local_n - 1;
        int stop_in_me = (limit <= my_end_idx);

        if (stop_in_me)
        {
            // The bubble stops at global index 'limit'.
            // This corresponds to local index 'limit - global_start'.
            // The comparisons are (j, j+1). The last comparison is (limit-1, limit).
            // So j goes up to limit - global_start - 1.
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

        // C. Boundary Send (Right)
        // If we are not the last process AND the bubble didn't stop in us:
        if (rank < p - 1 && !stop_in_me)
        {
            // We send our last element to the right neighbor for comparison
            Element my_last = local_a[local_n - 1];
            MPI_Send(&my_last, 1, mpi_elem_type, rank + 1, 0, MPI_COMM_WORLD);

            // We wait for the "smaller" value to return
            Element returned;
            MPI_Recv(&returned, 1, mpi_elem_type, rank + 1, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            local_a[local_n - 1] = returned;
            // Note: The swap count for this boundary is handled by rank+1 (who did the if/else).
        }
    }

    // 4. Output Logic

    // Reduce swaps
    unsigned int total_swaps = 0;
    MPI_Reduce(&my_swaps, &total_swaps, 1, MPI_UNSIGNED, MPI_SUM, 0, MPI_COMM_WORLD);

    if (rank == 0)
    {
        printf("n_swaps = %u\n", total_swaps);
    }

    // Process-wise bounds output (Ordered by rank)
    // To ensure order P0, P1..., we use a token passing approach or simple Barrier loop.
    // Barrier loop is sufficient for simple output requirements.
    for (int r = 0; r < p; ++r)
    {
        MPI_Barrier(MPI_COMM_WORLD);
        if (rank == r)
        {
            printf("P%d: (%d, %.1f) (%d, %.1f)\n",
                   rank,
                   local_a[0].nr, local_a[0].val,
                   local_a[local_n - 1].nr, local_a[local_n - 1].val);
        }
    }

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
            free(full_array);
        }
    }

    // Cleanup
    MPI_Type_free(&mpi_elem_type);
    free(local_a);
    MPI_Finalize();
    return 0;
}