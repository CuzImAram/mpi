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
    // MPI initialisieren
    MPI_Init(&argc, &argv);

    int rank, p;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &p);

    // Kommandozeilenargumente prüfen
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
    // Speicherausrichtung kann bei Vektorisierung helfen, hier wird jedoch streng standardisiertes malloc verwendet.
    Element *local_a = (Element *)malloc(local_n * sizeof(Element));

    // --- 1. Initialisierung ---
    int global_start = rank * local_n;
    for (int k = 0; k < local_n; ++k)
    {
        int i = global_start + k;
        srand(seed * (i + 5));
        local_a[k].nr = i;
        local_a[k].val = (rand() % 100) / 10.0;
    }

    // --- 2. Eingabe ausgeben (Nur für n<=20) ---
    if (n <= 20)
    {
        Element *full = NULL;
        if (rank == 0)
            full = (Element *)malloc(n * sizeof(Element));
        // MPI_BYTE um Overhead bei Typkonstruktion für einfaches gathering zu vermeiden
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

    // --- 3. Paralleler Bubble Sort ---
    // LOAD BALANCING:
    // Da große Elemente schnell nach rechts aufsteigen,
    // beenden Prozesse mit höherem Rang ihre Arbeit früher als Prozesse mit niedrigerem Rang.
    // Beispiel: Das Element (142, 9,8) bewegt sich schneller nach rechts als (7, 0,3) nach links.
    // Nach mehreren Durchläufen vergleicht P0 immer noch (7, 0,3) mit (25, 1,5), während
    // P3 möglicherweise bereits im Leerlauf ist, da alle großen Werte wie (142, 9,8) bereits zugeordnet sind.
    // Möglicher Ansatz, um dieses Ungleichgewicht zu beheben:
    // Dynamische Umverteilung: Im Leerlauf befindliche Prozesse übernehmen die Arbeit von noch aktiven
    // Prozessen (erfordert komplexe Kommunikation und Datenmigration).

    // WEITERE OPTIMIERUNGEN:
    // - Vorzeitige Beendigung (Global Flag):
    // Idee: Ein MPI_Allreduce in jeder Iteration, um zu prüfen, ob global 0 Swaps stattgefunden haben.
    // Realität im Code: Es gibt keine MPI_Allreduce-Operation innerhalb der Schleife. Die Schleife läuft stur durch,
    // bis pass < n - 1 erreicht ist oder der lokale Bereich (limit < global_start) indexbasiert nicht mehr betroffen
    // ist. Es wird nicht geprüft, ob tatsächlich noch getauscht wird.

    // - Nicht blockierende Kommunikation: Verwenden von MPI_Isend/Irecv, um die Berechnung
    //   nach Möglichkeit mit der Kommunikation zu überlappen.

    MPI_Barrier(MPI_COMM_WORLD);
    double start_time = MPI_Wtime();

    unsigned int my_swaps = 0;
    int my_end_idx = global_start + local_n - 1;

    Element buf_send, buf_recv;

    // Test Values (without posistion indices):
    // P0: [10, 50]
    // P1: [30, 20]
    // P2: [ 5,  1]

    for (int pass = 0; pass < n - 1; ++pass)
    {
        int limit = n - 1 - pass;

        // Vorzeitiger Abbruch: Dieser Prozess hat keine Arbeit mehr
        if (limit < global_start)
            break;

        int stop_in_me = (limit <= my_end_idx);
        int local_limit = stop_in_me ? (limit - global_start) : (local_n - 1);

        // --- 3. Paralleler Bubble Sort (Verfolgung Pass 0) ---

        // Schritt 1: Randprüfung mit linkem Nachbarn (P1 und P2 warten hier)
        if (rank > 0)
        {
            // CHAIN P1 (waiting for P0): Recv(50) -> Compare(50 > 30) -> Swap: P1 becomes [50, 20] -> Send(30) back to P0
            // CHAIN P2 (waiting for P1): This happens later in this loop once P1 reaches Step 3.
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

        // Schritt 2: Lokale Vergleiche ("Bubble" an den rechten Rand jedes Prozesses bewegen)
        for (int j = 0; j < local_limit; ++j)
        {
            if (local_a[j].val > local_a[j + 1].val)
            {
                // CHAIN P0: [10, 30] -> No swap. Result: [10, 30]
                // CHAIN P1: [50, 20] -> Swap(50, 20). Result: [20, 50] (Bubble 50 is now at the edge)
                // CHAIN P2: [ 5,  1] -> Swap( 5,  1). Result: [ 1,  5]
                Element tmp = local_a[j];
                local_a[j] = local_a[j + 1];
                local_a[j + 1] = tmp;
                my_swaps++;
            }
        }

        // Schritt 3: Randprüfung mit rechtem Nachbarn (Aktives Schieben nach rechts)
        if (rank < p - 1 && !stop_in_me)
        {
            // CHAIN P0: Send(50) to P1 -> Recv(30) from P1. Final P0 State: [10, 30]
            // CHAIN P1: Send(50) to P2 -> P2 (in Step 1) compares(50 > 1) -> P2 Swaps: [50, 5] -> P2 Sends(1) back.
            //           P1 Recv(1) from P2. Final P1 State: [20, 1]
            buf_send = local_a[local_n - 1];
            MPI_Send(&buf_send, sizeof(Element), MPI_BYTE, rank + 1, 0, MPI_COMM_WORLD);
            MPI_Recv(&buf_recv, sizeof(Element), MPI_BYTE, rank + 1, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            local_a[local_n - 1] = buf_recv;
        }
        // --- ENDE VON PASS 0 ZUSAMMENFASSUNG ---
        // P0: [10, 30]
        // P1: [20,  1]
        // P2: [50,  5]
    }

    MPI_Barrier(MPI_COMM_WORLD);
    double end_time = MPI_Wtime();
    if (rank == 0)
        printf("Time taken: %.6f seconds\n", end_time - start_time);

    // --- 4. Ausgabe-Logik ---
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