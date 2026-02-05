#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_FILES 100
#define MAX_LINE 256

typedef struct
{
    int number;
    double seq_time;
    double par_time_32;
    double par_time_16;
    double speedup_32;
    double speedup_16;
    double efficiency_32;
    double efficiency_16;
} SpeedupData;

// Function to extract time from a file
double extract_time(const char *filename)
{
    FILE *fp = fopen(filename, "r");
    if (!fp)
    {
        return -1.0;
    }

    char line[MAX_LINE];
    double time = -1.0;

    // Read first line which should contain "Time taken: X seconds"
    if (fgets(line, sizeof(line), fp))
    {
        if (sscanf(line, "Time taken: %lf seconds", &time) != 1)
        {
            time = -1.0;
        }
    }

    fclose(fp);
    return time;
}

// Comparison function for qsort
int compare_data(const void *a, const void *b)
{
    SpeedupData *da = (SpeedupData *)a;
    SpeedupData *db = (SpeedupData *)b;
    return da->number - db->number;
}

int main()
{
    SpeedupData data[MAX_FILES];
    int count = 0;

    // Create graph directory
    struct stat st = {0};
    if (stat("graph", &st) == -1)
    {
        mkdir("graph", 0755);
    }

    // Array of problem sizes (numbers from the filenames)
    int sizes[] = {100, 200, 300, 400, 500, 600, 700, 800, 900, 1000};
    int num_sizes = sizeof(sizes) / sizeof(sizes[0]);

    printf("Reading timing data...\n\n");

    for (int i = 0; i < num_sizes; i++)
    {
        int num = sizes[i];
        char seq_file[256], par_file_32[256], par_file_16[256];

        sprintf(seq_file, "out_plot/bubble_seq.out.%d", num);
        sprintf(par_file_32, "out_plot/bubble.out.%d", num);
        sprintf(par_file_16, "out_plot/bubble.out.%d_16", num);

        double seq_time = extract_time(seq_file);
        double par_time_32 = extract_time(par_file_32);
        double par_time_16 = extract_time(par_file_16);

        if (seq_time > 0 && par_time_32 > 0 && par_time_16 > 0)
        {
            data[count].number = num;
            data[count].seq_time = seq_time;
            data[count].par_time_32 = par_time_32;
            data[count].par_time_16 = par_time_16;
            data[count].speedup_32 = seq_time / par_time_32;
            data[count].speedup_16 = seq_time / par_time_16;
            data[count].efficiency_32 = (data[count].speedup_32 / 32.0) * 100.0;
            data[count].efficiency_16 = (data[count].speedup_16 / 16.0) * 100.0;

            printf("N=%d:\n", num);
            printf("  32 threads: Speedup=%.2fx, Efficiency=%.2f%%\n",
                   data[count].speedup_32, data[count].efficiency_32);
            printf("  16 threads: Speedup=%.2fx, Efficiency=%.2f%%\n",
                   data[count].speedup_16, data[count].efficiency_16);
            count++;
        }
        else
        {
            printf("Warning: Could not read complete data for N=%d\n", num);
        }
    }

    if (count == 0)
    {
        fprintf(stderr, "Error: No valid data found\n");
        return 1;
    }

    // Sort by problem size
    qsort(data, count, sizeof(SpeedupData), compare_data);

    // Write data to file for gnuplot
    FILE *data_fp = fopen("graph/speedup_data.txt", "w");
    if (!data_fp)
    {
        perror("Error creating graph/speedup_data.txt");
        return 1;
    }

    fprintf(data_fp, "# N Seq_Time Par32 Par16 Speedup32 Speedup16 Eff32(%%) Eff16(%%)\n");
    for (int i = 0; i < count; i++)
    {
        fprintf(data_fp, "%d %.6f %.6f %.6f %.6f %.6f %.6f %.6f\n",
                data[i].number, data[i].seq_time,
                data[i].par_time_32, data[i].par_time_16,
                data[i].speedup_32, data[i].speedup_16,
                data[i].efficiency_32, data[i].efficiency_16);
    }
    fclose(data_fp);
    printf("\nData written to graph/speedup_data.txt\n");

    // Create gnuplot script for SPEEDUP
    FILE *gnuplot_speedup = fopen("graph/plot_speedup.gnuplot", "w");
    if (!gnuplot_speedup)
    {
        perror("Error creating graph/plot_speedup.gnuplot");
        return 1;
    }

    fprintf(gnuplot_speedup, "set terminal png size 1400,800 font 'Arial,14'\n");
    fprintf(gnuplot_speedup, "set output 'graph/speedup.png'\n");
    fprintf(gnuplot_speedup, "set title 'Speedup: Sequential vs Parallel Bubble Sort' font 'Arial,16'\n");
    fprintf(gnuplot_speedup, "set xlabel 'Problem Size (N) in thousands' font 'Arial,14'\n");
    fprintf(gnuplot_speedup, "set ylabel 'Speedup (Sequential Time / Parallel Time)' font 'Arial,14'\n");
    fprintf(gnuplot_speedup, "set grid\n");
    fprintf(gnuplot_speedup, "set key top right\n");
    fprintf(gnuplot_speedup, "set ytics 8\n");
    fprintf(gnuplot_speedup, "set style line 1 lc rgb '#0060ad' lt 1 lw 2 pt 7 ps 1.5\n");
    fprintf(gnuplot_speedup, "set style line 2 lc rgb '#00a000' lt 1 lw 2 pt 9 ps 1.5\n");
    fprintf(gnuplot_speedup, "set style line 3 lc rgb '#dd181f' lt 2 lw 2\n");
    fprintf(gnuplot_speedup, "set style line 4 lc rgb '#ff9900' lt 2 lw 2\n");
    fprintf(gnuplot_speedup, "plot 'graph/speedup_data.txt' using 1:5 with linespoints ls 1 title '32 threads (Actual)', \\\n");
    fprintf(gnuplot_speedup, "     'graph/speedup_data.txt' using 1:6 with linespoints ls 2 title '16 threads (Actual)', \\\n");
    fprintf(gnuplot_speedup, "     32 with lines ls 3 title '32 threads (Ideal)', \\\n");
    fprintf(gnuplot_speedup, "     16 with lines ls 4 title '16 threads (Ideal)'\n");
    fclose(gnuplot_speedup);
    printf("Speedup gnuplot script written\n");

    // Create gnuplot script for EFFICIENCY
    FILE *gnuplot_efficiency = fopen("graph/plot_efficiency.gnuplot", "w");
    if (!gnuplot_efficiency)
    {
        perror("Error creating graph/plot_efficiency.gnuplot");
        return 1;
    }

    fprintf(gnuplot_efficiency, "set terminal png size 1400,800 font 'Arial,14'\n");
    fprintf(gnuplot_efficiency, "set output 'graph/efficiency.png'\n");
    fprintf(gnuplot_efficiency, "set title 'Parallel Efficiency' font 'Arial,16'\n");
    fprintf(gnuplot_efficiency, "set xlabel 'Problem Size (N) in thousands' font 'Arial,14'\n");
    fprintf(gnuplot_efficiency, "set ylabel 'Efficiency (%%)' font 'Arial,14'\n");
    fprintf(gnuplot_efficiency, "set grid\n");
    fprintf(gnuplot_efficiency, "set key top right\n");
    fprintf(gnuplot_efficiency, "set yrange [0:150]\n");
    fprintf(gnuplot_efficiency, "set style line 1 lc rgb '#0060ad' lt 1 lw 2 pt 7 ps 1.5\n");
    fprintf(gnuplot_efficiency, "set style line 2 lc rgb '#00a000' lt 1 lw 2 pt 9 ps 1.5\n");
    fprintf(gnuplot_efficiency, "set style line 3 lc rgb '#dd181f' lt 2 lw 2\n");
    fprintf(gnuplot_efficiency, "plot 'graph/speedup_data.txt' using 1:7 with linespoints ls 1 title '32 threads', \\\n");
    fprintf(gnuplot_efficiency, "     'graph/speedup_data.txt' using 1:8 with linespoints ls 2 title '16 threads', \\\n");
    fprintf(gnuplot_efficiency, "     100 with lines ls 3 title 'Ideal Efficiency (100%%)'\n");
    fclose(gnuplot_efficiency);
    printf("Efficiency gnuplot script written\n");

    // Generate plots
    printf("\nGenerating plots...\n");
    int ret1 = system("gnuplot graph/plot_speedup.gnuplot 2>/dev/null");
    int ret2 = system("gnuplot graph/plot_efficiency.gnuplot 2>/dev/null");

    if (ret1 == 0 && ret2 == 0)
    {
        printf("Plots generated successfully!\n");
        printf("  - graph/speedup.png\n");
        printf("  - graph/efficiency.png\n");

        // Clean up intermediate files
        printf("\nCleaning up intermediate files...\n");
        unlink("graph/speedup_data.txt");
        unlink("graph/plot_speedup.gnuplot");
        unlink("graph/plot_efficiency.gnuplot");
        printf("Done! Only PNG files remain in graph/ folder.\n");
    }
    else
    {
        printf("Error generating plots. To generate manually, run:\n");
        printf("  gnuplot graph/plot_speedup.gnuplot\n");
        printf("  gnuplot graph/plot_efficiency.gnuplot\n");
    }

    return 0;
}
