#!/bin/bash
set -x

RUNNOW=1
RUNSCRIPT=run.sh
mkdir $OUTPUTDIR
sudo dmesg -c &> del.txt

USAGE(){
echo "./app \$maxhotpage \$BW \$outputdir \$app"
}

RUNAPP(){
  #rm $OUTPUTDIR/$APP
  FLAGPATH=$NVMBASE"/flags/"$APP
  let value=`cat "$FLAGPATH"`
  if [ $value == 0 ]; then
	  echo "$APP NOW RUNNING"
	  cd $APPBASE
	  $APPBASE/$RUNSCRIPT $RUNNOW $OUTPUTDIR/$APP &> $OUTPUTDIR/$APP
	  echo "******************"  &>> $OUTPUTDIR/$APP
	  echo "KERNEL  DMESG"  &>> $OUTPUTDIR/$APP
	  echo "******************"  &>> $OUTPUTDIR/$APP 	
	  echo "  "  &>> $OUTPUTDIR/$APP
	  sudo dmesg -c &>> $OUTPUTDIR/$APP
	  echo 1 > $FLAGPATH
  else
	  echo "$APP ALREADY RUN"	
  fi
}


intexit() {
    # Kill all subprocesses (all processes in the current process group)
    kill -HUP -$$
}

hupexit() {
    # HUP'd (probably by intexit)
    echo
    echo "Interrupted"
    exit
}

trap hupexit HUP
trap intexit INT

#if [ -z "$1" ]
# then	
#  USAGE 
#  exit
#fi

#$NVMBASE/scripts/copy_data.sh

if [ -z "$4" ]
  then
        APPBASE=$APPBENCH/apps/filebench
        APP=filebench
        RUNAPP
	$NVMBASE/scripts/reset.sh
	exit	

	APPBASE=$APPBENCH/redis-3.0.0/src
	APP=redis
	echo "running $APP..."
	RUNAPP
	$NVMBASE/scripts/reset.sh

	APPBASE=$APPBENCH/apps/rocksdb
	APP=db_bench
	RUNAPP
	$NVMBASE/scripts/reset.sh
	exit

	APPBASE=$APPBENCH/apps/memcached_client
	APP=memcached
	RUNAPP
	export LD_PRELOAD=$SHARED_LIBS/construct/libmigration.so
	/bin/ls
	export LD_PRELOAD=""
        $NVMBASE/scripts/reset.sh
        exit




	
	APPBASE=$APPBENCH/apps/leveldb
	APP=leveldb
	echo "running $APP..."
	RUNAPP



	
	#APPBASE=$APPBENCH/apps/fio
	#APP=fio
	#echo "running $APP ..."
	#RUNAPP

	APPBASE=$APPBENCH/graphchi
	APP=graphchi
	RUNAPP
	# We need data files
	$SCRIPTS/createdata.sh
	rm $SHARED_DATA/com-orkut.ungraph.txt.*

	APPBASE=$APPBENCH/Metis
	APP=Metis
	$SCRIPTS/createdata.sh
	RUNAPP

	#APPBASE=$APPBENCH/memcached/memtier_benchmark
	#APP=memcached
	#echo "running $APP ..."
	#RUNAPP
	#exit

	#RUNSCRIPT="runfcreate.sh"
        #APP=fcreate
        #RUNAPP
        #RUNSCRIPT=run.sh

	#APPBASE=$APPBENCH/apps/mongo-perf
	#APP=mongodb
	#RUNAPP


	#APPBASE=$APPBENCH/xstream_release
	#APP=xstream_release
	#scp -r $HOSTIP:$SHARED_DATA*.ini $APPBASE
        #cp $APPBASE/*.ini $SHARED_DATA
	#RUNAPP

fi

finish:
$NVMBASE/scripts/reset.sh
#currentDate=`date +"%D %T"`
set +x
