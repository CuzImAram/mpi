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

    // --- 3. High Performance Sorting ---
    //
    // OPTIMIZATION IDEAS (keeping same algorithm & n_swaps):
    //
    // 1. Load balancing problem: Higher-ranked processes finish early because large
    //    elements bubble right quickly. Solution: Dynamic work stealing - idle processes
    //    could take over boundary comparisons from busy neighbors, or use a coordinator
    //    to reassign work dynamically based on where the "active front" currently is.
    //
    // 2. Further pipelining: Instead of pass-by-pass, we could track individual element
    //    positions and allow multiple "bubbles" to be in flight simultaneously across
    //    process boundaries, with careful tagging to maintain correctness.
    //
    // 3. Adaptive communication: For nearly-sorted data, use early termination detection
    //    via Allreduce on swap count (but this changes the algorithm slightly).
    //
    MPI_Barrier(MPI_COMM_WORLD);
    double start_time = MPI_Wtime();

    unsigned int my_swaps = 0;
    int my_end_idx = global_start + local_n - 1;

    // Persistent buffers and requests
    MPI_Request req_left_recv = MPI_REQUEST_NULL;
    MPI_Request req_left_send = MPI_REQUEST_NULL;
    MPI_Request req_right_recv = MPI_REQUEST_NULL;
    MPI_Request req_right_send = MPI_REQUEST_NULL;

    Element buf_left_recv;
    Element buf_left_send;
    Element buf_right_recv;
    Element buf_right_send;

    // Pre-post first left receive to hide initial latency
    if (rank > 0)
    {
        MPI_Irecv(&buf_left_recv, sizeof(Element), MPI_BYTE, rank - 1, 0, MPI_COMM_WORLD, &req_left_recv);
    }

    for (int pass = 0; pass < n - 1; ++pass)
    {
        int limit = n - 1 - pass;

        // Early exit: this process has no more work
        if (limit < global_start)
            break;

        int stop_in_me = (limit <= my_end_idx);
        int effective_limit = stop_in_me ? (limit - global_start) : (local_n - 1);

        // --- STEP A: DO LOCAL WORK FOR INDICES 1..local_n-2 (independent of boundaries) ---
        // This overlaps with waiting for left boundary data
        int inner_start = 1;
        int inner_end = (stop_in_me) ? effective_limit : (local_n - 2);

        for (int j = inner_start; j < inner_end; ++j)
        {
            if (local_a[j].val > local_a[j + 1].val)
            {
                Element tmp = local_a[j];
                local_a[j] = local_a[j + 1];
                local_a[j + 1] = tmp;
                my_swaps++;
            }
        }

        // --- STEP B: PROCESS LEFT BOUNDARY ---
        if (rank > 0)
        {
            // Wait for data from left neighbor (posted at end of previous pass or before loop)
            MPI_Wait(&req_left_recv, MPI_STATUS_IGNORE);

            Element incoming = buf_left_recv;

            // Compare incoming with local_a[0]
            if (incoming.val > local_a[0].val)
            {
                buf_left_send = local_a[0];
                local_a[0] = incoming;
                my_swaps++;
            }
            else
            {
                buf_left_send = incoming;
            }

            // Send result back (non-blocking)
            MPI_Isend(&buf_left_send, sizeof(Element), MPI_BYTE, rank - 1, 1, MPI_COMM_WORLD, &req_left_send);
        }

        // Now do the comparison at index 0 (which depends on left boundary result)
        if (effective_limit > 0)
        {
            if (local_a[0].val > local_a[1].val)
            {
                Element tmp = local_a[0];
                local_a[0] = local_a[1];
                local_a[1] = tmp;
                my_swaps++;
            }
        }

        // --- STEP C: FINALIZE RIGHT BOUNDARY FROM PREVIOUS PASS ---
        if (req_right_recv != MPI_REQUEST_NULL)
        {
            MPI_Wait(&req_right_recv, MPI_STATUS_IGNORE);
            local_a[local_n - 1] = buf_right_recv;
            req_right_recv = MPI_REQUEST_NULL;
        }

        // Do the last comparison (local_n-2, local_n-1) if needed
        if (!stop_in_me && local_n >= 2)
        {
            int j = local_n - 2;
            if (local_a[j].val > local_a[j + 1].val)
            {
                Element tmp = local_a[j];
                local_a[j] = local_a[j + 1];
                local_a[j + 1] = tmp;
                my_swaps++;
            }
        }

        // --- STEP D: INITIATE RIGHT BOUNDARY EXCHANGE FOR NEXT PASS ---
        if (rank < p - 1 && !stop_in_me)
        {
            buf_right_send = local_a[local_n - 1];
            MPI_Isend(&buf_right_send, sizeof(Element), MPI_BYTE, rank + 1, 0, MPI_COMM_WORLD, &req_right_send);
            MPI_Irecv(&buf_right_recv, sizeof(Element), MPI_BYTE, rank + 1, 1, MPI_COMM_WORLD, &req_right_recv);
        }

        // --- STEP E: PRE-POST LEFT RECEIVE FOR NEXT PASS ---
        // Check if next pass will still need left boundary exchange
        int next_limit = n - 2 - pass;
        if (rank > 0 && next_limit >= global_start)
        {
            MPI_Irecv(&buf_left_recv, sizeof(Element), MPI_BYTE, rank - 1, 0, MPI_COMM_WORLD, &req_left_recv);
        }

        // Cleanup sends from this pass
        if (req_right_send != MPI_REQUEST_NULL)
        {
            MPI_Wait(&req_right_send, MPI_STATUS_IGNORE);
            req_right_send = MPI_REQUEST_NULL;
        }
        if (req_left_send != MPI_REQUEST_NULL)
        {
            MPI_Wait(&req_left_send, MPI_STATUS_IGNORE);
            req_left_send = MPI_REQUEST_NULL;
        }
    }

    // Final Cleanup
    if (req_right_recv != MPI_REQUEST_NULL)
    {
        MPI_Wait(&req_right_recv, MPI_STATUS_IGNORE);
        local_a[local_n - 1] = buf_right_recv;
    }
    if (req_right_send != MPI_REQUEST_NULL)
        MPI_Wait(&req_right_send, MPI_STATUS_IGNORE);
    if (req_left_send != MPI_REQUEST_NULL)
        MPI_Wait(&req_left_send, MPI_STATUS_IGNORE);
    if (req_left_recv != MPI_REQUEST_NULL)
        MPI_Cancel(&req_left_recv);

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