#include <stdio.h>
#include <stdlib.h>
#include <time.h>

typedef struct
{
    int nr;
    double val;
} Element;

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        printf("Usage: %s <n> <seed>\n", argv[0]);
        return 1;
    }

    int n = atoi(argv[1]);
    int seed = atoi(argv[2]);

    // Allocation
    Element *a = (Element *)malloc(n * sizeof(Element));
    if (!a)
    {
        perror("Malloc failed");
        return 1;
    }

    // The srand call is inside the loop to ensure specific values per index
    for (int i = 0; i < n; ++i)
    {
        srand(seed * (i + 5));
        a[i].nr = i;
        a[i].val = (rand() % 100) / 10.0;
    }

    // Output Input for n <= 20
    if (n <= 20)
    {
        printf("Eingabe:");
        for (int i = 0; i < n; ++i)
        {
            printf(" (%d, %.1f)", a[i].nr, a[i].val);
        }
        printf("\n");
    }

    clock_t start = clock();

    // Sorting
    // Strict adherence to the provided loop structure to count swaps exactly.
    unsigned int n_swaps = 0;
    for (int i = n - 1; i > 0; --i)
    {
        for (int j = 0; j < i; ++j)
        {
            if (a[j].val > a[j + 1].val)
            {
                // Swap
                Element temp = a[j];
                a[j] = a[j + 1];
                a[j + 1] = temp;
                n_swaps++;
            }
        }
    }

    clock_t end = clock();
    double time_taken = (double)(end - start) / CLOCKS_PER_SEC;
    printf("Time taken: %.6f seconds\n", time_taken);

    // Output n_swaps
    printf("n_swaps = %u\n", n_swaps);

    // Output Sorted Array for n <= 20
    if (n <= 20)
    {
        printf("Ausgabe:");
        for (int i = 0; i < n; ++i)
        {
            printf(" (%d, %.1f)", a[i].nr, a[i].val);
        }
        printf("\n");
    }

    free(a);
    return 0;
}