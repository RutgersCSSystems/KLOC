#! /bin/bash
set -x

PARAFS=../..
DBPATH=/mnt/pmemdir
DEVFSSRC=../../devfs_client
VALUSESIZE=100
BENCHMARK="fillrandom,readrandom"
WORKLOADDESCR="fillrandom-readrandom"

# Create output directories
if [ ! -d "result-ext4dax-merged" ]; then
	mkdir result-ext4dax-merged
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
	mkdir -p result-ext4dax/$WORKLOADDESCR_$1_$2
	./db_bench --db=$DBPATH --num_levels=6 --key_size=20 --prefix_size=20 --memtablerep=prefix_hash --bloom_bits=10 --bloom_locality=1 --benchmarks=$BENCHMARK --use_existing_db=0 --num=500000 --compression_type=none --value_size=$2 --threads=$1  &> result-ext4dax-merged/$WORKLOADDESCR"_"$1"_"$2".txt"
	sleep 2
}

if mount | grep $FSPATH > /dev/null; then
	echo "ext4 dax already mounted"
else
	$DEVFSSRC/scripts/mountext4dax.sh
fi

declare -a sizearr=("100" "512" "1024" "4096") 
declare -a threadarr=("1" "2" "4" "8")
for size in "${sizearr[@]}"
do
	for thrd in "${threadarr[@]}"
	do
		CLEAN
		FlushDisk
		RUN $thrd $size
	done        
done   
