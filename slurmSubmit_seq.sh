#!/bin/bash
####### Mail Notify / Job Name / Comment #######
#SBATCH --job-name="bubble_seq.c"

####### Partition #######
#SBATCH --partition=pub23

####### Ressources #######
#SBATCH --time=0-00:05:00
#SBATCH --mem-per-cpu=1000

####### Node Info #######
#SBATCH --exclusive
#SBATCH --nodes=1

####### Output #######
#SBATCH --output=out/bubble_seq.out.%j
#SBATCH --error=out/error/bubble_seq.err.%j

#path/to/binary
out/bubble_seq 200000 123