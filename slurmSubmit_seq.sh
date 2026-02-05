#!/bin/bash
####### Mail Notify / Job Name / Comment #######
#SBATCH --job-name="bubble_seq.c"

####### Partition #######
#SBATCH --partition=pub23

####### Ressources #######
#SBATCH --time=0-05:00:00
#SBATCH --mem-per-cpu=1000

####### Node Info #######
#SBATCH --exclusive
#SBATCH --nodes=1

####### Output #######
#SBATCH --output=out/bubble_seq.out.%j
#SBATCH --error=out/error/bubble_seq.err.%j

#path/to/binary
out/bubble_seq 900000 123
#perf stat -e L1-dcache-load-misses,L1-dcache-loads,LLC-load-misses,LLC-loads out/bubble_seq 100000 123