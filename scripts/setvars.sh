export BASE=$PWD
export NVMBASE=$BASE
export OUTPUTDIR=$NVMBASE/results
######## DO NOT CHANGE BEYOUND THIS ###########
#Pass the release name
export OS_RELEASE_NAME=$1
export KERN_SRC=$NVMBASE/linux-stable
#CPU parallelism
export PARA="-j32"
export VER="4.17.0"
#export VER="4.18.0-2-amd64"

export TEST_TMPDIR=/mnt/pmemdir
export GITBRANCH="master"

#QEMU
export QEMU_IMG=$NVMBASE
#export QEMU_IMG_FILE=$QEMU_IMG/qemu-image.img
export QEMU_IMG_FILE=$QEMU_IMG/qemu-image-fresh.img
export MOUNT_DIR=$QEMU_IMG/mountdir
export QEMUMEM="80G"
export QEMUNODE1="40G"
export QEMUNODE2="40G"


export KERNEL=$NVMBASE/KERNEL

#BENCHMARKS AND LIBS
export LINUX_SCALE_BENCH=$NVMBASE/linux-scalability-benchmark
export APPBENCH=$NVMBASE/appbench
export SHARED_LIBS=$NVMBASE/shared_libs
export QUARTZ=$SHARED_LIBS/quartz

#SCRIPTS
export SCRIPTS=$NVMBASE/scripts
export INPUTXML=$SCRIPTS/input.xml
export QUARTZSCRIPTS=$SHARED_LIBS/quartz/scripts

#APP SPECIFIC and APPBENCH
#export GRAPHCHI_ROOT=$APPBENCH/graphchi/graphchi-cpp
export SHARED_DATA=$APPBENCH/shared_data
#export SHARED_DATA=/mnt/pmemdir

export APPPREFIX="" #"numactl --preferred=0 /usr/bin/time -v"
#export APPPREFIX="perf record -e instructions,mem-loads,mem-stores --vmlinux=/lib/modules/4.17.0/build/vmlinux -I 1000"
#export APPPREFIX="numactl --membind=1"
#export APP_PREFIX="numactl --membind=1"



#Commands
mkdir $OUTPUTDIR
mkdir $KERNEL
