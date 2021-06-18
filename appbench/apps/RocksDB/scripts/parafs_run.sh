#! /bin/bash
set -x

RESULTDIR=result-parafs-$1

# Run fillseq, readseq banchmark
#./parafs_run_seq.sh $RESULTDIR

sleep 2

# Run fillrandom, readrandom banchmark
./parafs_run_rand.sh $RESULTDIR


