#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

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
            printf("Error: p must divide n\n");
        MPI_Finalize();
        return 1;
    }

    int local_n = n / p;
    // Align memory can help with vectorization, strictly standardized malloc is used here though.
    Element *local_a = (Element *)malloc(local_n * sizeof(Element));

    // --- 1. Initialization ---
    int global_start = rank * local_n;
    for (int k = 0; k < local_n; ++k)
    {
        int i = global_start + k;
        srand(seed * (i + 5));
        local_a[k].nr = i;
        local_a[k].val = (rand() % 100) / 10.0;
    }

    // --- 2. Output Input (Only for n<=20) ---
    // Using simple byte-wise gather for simplicity and speed
    if (n <= 20)
    {
        Element *full = NULL;
        if (rank == 0)
            full = (Element *)malloc(n * sizeof(Element));
        // Use MPI_BYTE to avoid Type construction overhead for simple gathering
        MPI_Gather(local_a, local_n * sizeof(Element), MPI_BYTE,
                   full, local_n * sizeof(Element), MPI_BYTE, 0, MPI_COMM_WORLD);

        if (rank == 0)
        {
            printf("Eingabe:");
            for (int i = 0; i < n; ++i)
                printf(" (%d, %.1f)", full[i].nr, full[i].val);
            printf("\n");
            fflush(stdout);
            free(full);
        }
    }

    // --- 3. Parallel Bubble Sort ---
    // Strict left-to-right comparison order required for correctness
    // Optimization potential is limited by the sequential nature of bubble sort
    //
    // LOAD BALANCING: Due to large elements bubbling up quickly to the right,
    // higher-ranked processes finish their work earlier than lower-ranked ones.
    // Example: Element (142, 9.8) moves right faster than (7, 0.3) moves left.
    // After several passes, P0 still compares (7, 0.3) with (25, 1.5) while
    // P3 may already be idle as all large values like (142, 9.8) are settled.
    // Possible approaches to address this imbalance:
    // 1) Dynamic redistribution: Idle processes take over work from still-active
    //    processes (requires complex communication and data migration).
    // 2) Odd-Even Transposition Sort: Alternates between odd/even phases,
    //    enabling better parallelism per iteration with more uniform workload.
    // 3) Work-stealing: Idle processes request elements from busy processes.
    //
    // FURTHER OPTIMIZATIONS:
    // - Early termination: Use a global flag to detect when no swaps occurred
    //   in an iteration and terminate all processes early.
    //   Example: If pass 85 has no swaps across all processes, stop immediately.
    // - Pre-sorting locally: Sort each local block before communication rounds
    //   to reduce the number of inter-process swaps.
    //   Example: P1's [(50, 7.2), (51, 3.1), (52, 8.9)] -> [(51, 3.1), (50, 7.2), (52, 8.9)]
    // - Non-blocking communication: Use MPI_Isend/Irecv to overlap computation
    //   with communication where possible.
    // - Pipelined communication: Start sending boundary elements as soon as
    //   they are ready instead of waiting for all local comparisons.
    //   Example: Send (39, 4.8) from P1 to P2 immediately after local_a[local_n-1] is updated.

    MPI_Barrier(MPI_COMM_WORLD);
    double start_time = MPI_Wtime();

    unsigned int my_swaps = 0;
    int my_end_idx = global_start + local_n - 1;

    Element buf_send, buf_recv;

    for (int pass = 0; pass < n - 1; ++pass)
    {
        int limit = n - 1 - pass;

        // Early exit: this process has no more work
        if (limit < global_start)
            break;

        int stop_in_me = (limit <= my_end_idx);
        int local_limit = stop_in_me ? (limit - global_start) : (local_n - 1);

        // Step 1: Receive element from left neighbor and handle boundary comparison
        if (rank > 0)
        {
            MPI_Recv(&buf_recv, sizeof(Element), MPI_BYTE, rank - 1, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            if (buf_recv.val > local_a[0].val)
            {
                buf_send = local_a[0];
                local_a[0] = buf_recv;
                my_swaps++;
            }
            else
            {
                buf_send = buf_recv;
            }

            MPI_Send(&buf_send, sizeof(Element), MPI_BYTE, rank - 1, 1, MPI_COMM_WORLD);
        }

        // Step 2: All local comparisons (left to right)
        for (int j = 0; j < local_limit; ++j)
        {
            if (local_a[j].val > local_a[j + 1].val)
            {
                Element tmp = local_a[j];
                local_a[j] = local_a[j + 1];
                local_a[j + 1] = tmp;
                my_swaps++;
            }
        }

        // Step 3: Send rightmost to right neighbor and receive result
        if (rank < p - 1 && !stop_in_me)
        {
            buf_send = local_a[local_n - 1];
            MPI_Send(&buf_send, sizeof(Element), MPI_BYTE, rank + 1, 0, MPI_COMM_WORLD);
            MPI_Recv(&buf_recv, sizeof(Element), MPI_BYTE, rank + 1, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            local_a[local_n - 1] = buf_recv;
        }
    }

    MPI_Barrier(MPI_COMM_WORLD);
    double end_time = MPI_Wtime();
    if (rank == 0)
        printf("Time taken: %.6f seconds\n", end_time - start_time);

    // --- 4. Output Logic ---
    unsigned int total_swaps = 0;
    MPI_Reduce(&my_swaps, &total_swaps, 1, MPI_UNSIGNED, MPI_SUM, 0, MPI_COMM_WORLD);

    if (rank == 0)
    {
        printf("n_swaps = %u\n", total_swaps);
        fflush(stdout);
    }

    for (int r = 0; r < p; ++r)
    {
        MPI_Barrier(MPI_COMM_WORLD);
        if (rank == r)
        {
            printf("P%d: (%d, %.1f) (%d, %.1f)\n",
                   rank, local_a[0].nr, local_a[0].val,
                   local_a[local_n - 1].nr, local_a[local_n - 1].val);
            fflush(stdout);
        }
    }
    MPI_Barrier(MPI_COMM_WORLD);

    if (n <= 20)
    {
        Element *full = NULL;
        if (rank == 0)
            full = (Element *)malloc(n * sizeof(Element));
        MPI_Gather(local_a, local_n * sizeof(Element), MPI_BYTE,
                   full, local_n * sizeof(Element), MPI_BYTE, 0, MPI_COMM_WORLD);

        if (rank == 0)
        {
            printf("Ausgabe:");
            for (int i = 0; i < n; ++i)
                printf(" (%d, %.1f)", full[i].nr, full[i].val);
            printf("\n");
            fflush(stdout);
            free(full);
        }
    }

    free(local_a);
    MPI_Finalize();
    return 0;
}