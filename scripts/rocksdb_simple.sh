#!/bin/bash
set -x
cd $NVMBASE
APP=""
TYPE="NVM"
CAPACITY=2000

APP="rocksdb.out"

SETUP(){
	$NVMBASE/scripts/clear_cache.sh
	cd $SHARED_LIBS/initiator
	make clean
}

THROTTLE() {
        source scripts/setvars.sh
        cp $SCRIPTS/nvmemul-throttle.ini $QUARTZ/nvmemul.ini
        $SCRIPTS/install_quartz.sh
        #$SCRIPTS/throttle.sh
        #$SCRIPTS/throttle.sh
}

DISABLE_THROTTLE() {
        source scripts/setvars.sh
        cp $SCRIPTS/nvmemul-nothrottle.ini $QUARTZ/nvmemul.ini
        $SCRIPTS/throttle.sh
        #$SCRIPTS/throttle.sh
}

SETUPEXTRAM() {
	sudo dmesg -c
        sudo rm -rf  /mnt/ext4ramdisk/*
	$SCRIPTS/umount_ext4ramdisk.sh
	sudo rm -rf  /mnt/ext4ramdisk/*
	sudo rm -rf  /mnt/ext4ramdisk/
	
	NUMAFREE=`numactl --hardware | grep "node 0 free:" | awk '{print $4}'`
	let DISKSZ=$NUMAFREE-$CAPACITY
	echo $DISKSZ"*************"
	$SCRIPTS/umount_ext4ramdisk.sh
	$SCRIPTS/mount_ext4ramdisk.sh $DISKSZ

	#Enable for Ramdisk
	if [ "NVM" = "$TYPE" ]
	then
		echo "Running for NVM"
		sudo ln -s /mnt/ext4ramdisk $APPBENCH/shared_data
	else
		#Enable for SSD
		echo "Running for SSD"
		mkdir $APPBENCH/shared_data
	fi
}

COMPILE_SHAREDLIB() {
	#Compile shared libs
	cd $SHARED_LIBS/initiator
	make clean
	make CFLAGS=$DEPFLAGS
	sudo make install
}

RUNAPP() {

	echo $OUTPUT
        #Run application
        cd $NVMBASE
        if [ "$APP" = "rocksdb.out" ]
        then
                $APPBENCH/apps/RocksDB/run.sh &> $OUTPUT
		echo "Writing output to "$OUTPUT
		mv $OUTPUTDIR/redis* 
        fi
}


SET_RUN_APP() {	
	BASE=$OUTPUTDIR
	mkdir $OUTPUTDIR/$1
	export OUTPUTDIR=$OUTPUTDIR/$1

	if [ "NVM" = "$TYPE" ]
	then
		echo "Running for NVM"
		OUTPUT="$OUTPUTDIR/$APP-NVM"
	else
		echo "Running for SSD"
		OUTPUT="$OUTPUTDIR/$APP-SSD"
	fi

	echo $OUTPUTDIR

        $NVMBASE/scripts/clear_cache.sh
        cd $SHARED_LIBS/initiator
        make clean
	make CFLAGS="$2"
	sudo make install

	RUNAPP
	$SCRIPTS/rocksdb_extract_result.sh
	$SCRIPTS/clear_cache.sh

	#cp -r $OUTPUTDIR $BASE/"CAP"$CAPACITY-$TYPE/$1
	export OUTPUTDIR=$BASE


	set +x
}

#### WITH PREFETCH #############
SETUPEXTRAM

#export APPPREFIX="numactl  --preferred=1"
$NVMBASE/scripts/clear_cache.sh
SET_RUN_APP "slowmem-obj-affinity-prefetch-threads-$TYPE" "-D_MIGRATE -D_PREFETCH -D_OBJAFF -D_NET -D_MIGRATE_THREADS"
$NVMBASE/scripts/clear_cache.sh
exit

#export APPPREFIX="numactl  --preferred=1"
$NVMBASE/scripts/clear_cache.sh
SET_RUN_APP "slowmem-obj-affinity-prefetch-$TYPE" "-D_MIGRATE -D_PREFETCH -D_OBJAFF -D_NET"
$NVMBASE/scripts/clear_cache.sh
exit




#### NAIVE PLACEMENT #############
export APPPREFIX="numactl --preferred=0 "
$NVMBASE/scripts/clear_cache.sh
SET_RUN_APP "naive-nonuma-os-fastmem-$TYPE" "-D_DISABLE_MIGRATE"
exit


THROTTLE


export APPPREFIX="numactl  --preferred=0"
$NVMBASE/scripts/clear_cache.sh
SET_RUN_APP "slowmem-migration-only-$TYPE" "-D_MIGRATE -D_NET"



#### NAIVE PLACEMENT #############
export APPPREFIX="numactl  --preferred=0"
$NVMBASE/scripts/clear_cache.sh
SET_RUN_APP "naive-os-fastmem-$TYPE" "-D_DISABLE_MIGRATE"



$SCRIPTS/umount_ext4ramdisk.sh
$SCRIPTS/mount_ext4ramdisk.sh 24000
DISABLE_THROTTLE
export APPPREFIX="numactl --membind=0"
SET_RUN_APP "optimal-os-fastmem-$TYPE-node0" "-D_DISABLE_HETERO  -D_DISABLE_MIGRATE"



exit




$SCRIPTS/umount_ext4ramdisk.sh
$SCRIPTS/mount_ext4ramdisk.sh 24000
DISABLE_THROTTLE
export APPPREFIX="numactl --membind=0"
SET_RUN_APP "optimal-os-fastmem-$TYPE" "-D_DISABLE_HETERO  -D_DISABLE_MIGRATE"
exit




export APPPREFIX="numactl --membind=1"
$SCRIPTS/umount_ext4ramdisk.sh
$SCRIPTS/mount_ext4ramdisk.sh 24000
SET_RUN_APP "slowmem-only-$TYPE" "-D_SLOWONLY -D_DISABLE_MIGRATE -D_NET"
exit

#### OBJAFF NO PREFETCH #############
export APPPREFIX="numactl  --preferred=0"
SETUPEXTRAM
$NVMBASE/scripts/clear_cache.sh
SET_RUN_APP "slowmem-obj-affinity-nomig-$TYPE" "-D_DISABLE_MIGRATE -D_OBJAFF"
exit









#### OBJ AFFINITY NO MIGRATION NO PREFETCH #############

$SCRIPTS/umount_ext4ramdisk.sh
$SCRIPTS/mount_ext4ramdisk.sh 24000
DISABLE_THROTTLE
export APPPREFIX="numactl --membind=0"
SET_RUN_APP "optimal-os-fastmem-$TYPE" "-D_DISABLE_HETERO  -D_DISABLE_MIGRATE"


exit



#### WITHOUT PREFETCH #############
export APPPREFIX="numactl  --preferred=0"
SETUPEXTRAM
$NVMBASE/scripts/clear_cache.sh
SET_RUN_APP "slowmem-obj-affinity-$TYPE" "-D_MIGRATE -D_OBJAFF -D_NET"
$NVMBASE/scripts/clear_cache.sh





mkdir $OUTPUTDIR/slowmem-only
OUTPUT="slowmem-only/$APP"
SETUP
make CFLAGS="-D_SLOWONLY"
export APPPREFIX="numactl --membind=1"
$SCRIPTS/umount_ext4ramdisk.sh
$SCRIPTS/mount_ext4ramdisk.sh 24000
RUNAPP 
$SCRIPTS/rocksdb_extract_result.sh
$SCRIPTS/clear_cache.sh
exit

#Don't do any migration




#mkdir $OUTPUTDIR/fastmem-only
exit
#Disable hetero for fastmem only mode
#make CFLAGS="-D_DISABLE_HETERO"
