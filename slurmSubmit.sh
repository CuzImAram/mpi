#!/bin/bash
####### Mail Notify / Job Name / Comment #######
#SBATCH --job-name="bubble.c"

####### Partition #######
#SBATCH --partition=pub23

####### Ressources #######
#SBATCH --time=0-00:05:00
#SBATCH --mem-per-cpu=1000

####### Node Info #######
#SBATCH --exclusive
#SBATCH --nodes=1

####### Output #######
#SBATCH --output=out/bubble.out.%j
#SBATCH --error=out/error/bubble.err.%j

#path/to/binary
mpirun -n 32 out/bubble 900000 123
#mpirun -n 4 perf stat -e L1-dcache-load-misses,L1-dcache-loads,LLC-load-misses,LLC-loads out/bubble 100000 123