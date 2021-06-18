HOGNODE=$1
cd $NVMBASE/bench/test-tlb

#LOAD=2147483648
LOAD=2147483640
numactl --membind=$HOGNODE --physcpubind=$HOGNODE ./test-tlb $LOAD 4096 &
numactl --membind=$HOGNODE --physcpubind=$HOGNODE ./test-tlb $LOAD 4096 &
numactl --membind=$HOGNODE --physcpubind=$HOGNODE ./test-tlb $LOAD 4096 &
numactl --membind=$HOGNODE --physcpubind=$HOGNODE ./test-tlb $LOAD 4096 &
numactl --membind=$HOGNODE --physcpubind=$HOGNODE ./test-tlb $LOAD 4096 &
numactl --membind=$HOGNODE --physcpubind=$HOGNODE ./test-tlb $LOAD 4096 &
numactl --membind=$HOGNODE --physcpubind=$HOGNODE ./test-tlb $LOAD 4096 

#numactl --membind=1 ./test-tlb $LOAD 4096 &
#numactl --membind=1 ./test-tlb $LOAD 4096 &
#numactl --membind=1 ./test-tlb $LOAD 4096 &
#numactl --membind=1 ./test-tlb $LOAD 4096 &
#numactl --membind=1 ./test-tlb $LOAD 4096 &
#numactl --membind=1 ./test-tlb $LOAD 4096 &
#numactl --membind=1 ./test-tlb $LOAD 4096

cd $NVMBASE
pkill test-tlb
pkill -9 test-tlb
pkill test-tlb
