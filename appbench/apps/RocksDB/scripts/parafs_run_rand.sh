#! /bin/bash
set -x

PARAFS=$OFFLOADBASE
DBPATH=/mnt/ram
BENCHMARK=fillrandom,readrandom
WORKLOADDESCR="fillrandom-readrandom"

RESULTDIR=$1

# Create output directories
if [ ! -d "$RESULTDIR" ]; then
	mkdir $RESULTDIR
fi

CLEAN() {
	rm -rf /mnt/ram/*
	sudo killall "db_bench"
	sudo killall "db_bench"
	echo "KILLING Rocksdb db_bench"
}

FlushDisk() {
	sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"
	sudo sh -c "sync"
	sudo sh -c "sync"
	sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"
}

RUN() {
	export PARAFSENV=parafs
	export DEVCORECNT=4
	export SCHEDPOLICY=2
	cd $ROCKSDB
	LD_PRELOAD=$DEVFSCLIENT/libshim/shim.so $ROCKSDB/db_bench --db=$DBPATH --num_levels=6 --key_size=20 --prefix_size=20 --memtablerep=prefix_hash --bloom_bits=10 --bloom_locality=1 --benchmarks=$BENCHMARK --use_existing_db=0 --num=500000 --compression_type=none --value_size=$2 --threads=$1  &> $RESULTDIR/$WORKLOADDESCR"_"$1"_"$2".txt"
	unset PARAFSENV
	sleep 2
}

declare -a sizearr=("100" "512" "1024" "4096")
#declare -a threadarr=("1" "2" "4")
declare -a threadarr=("8" "16")
for size in "${sizearr[@]}"
do
	for thrd in "${threadarr[@]}"
	do
	        CLEAN
		FlushDisk
		RUN $thrd $size
	done
done
