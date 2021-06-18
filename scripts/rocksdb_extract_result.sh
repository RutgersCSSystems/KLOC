#!/bin/bash
#set -x

TARGET=$OUTPUTDIR
#APP="rocksdb"
APP="redis"
#TYPE="SSD"
#TYPE="NVM"
TYPE=""

STATTYPE="APP"
STATTYPE="KERNEL"
ZPLOT="$NVMBASE/graphs/zplot"

## Scaling Kernel Stats Graph
let SCALE_KERN_GRAPH=100000
let SCALE_FILEBENCH_GRAPH=1000
let SCALE_REDIS_GRAPH=1000
let SCALE_ROCKSDB_GRAPH=1000
let SCALE_CASSANDRA_GRAPH=100
let SCALE_SPARK_GRAPH=50000



let INCR_KERN_BAR_SPACE=3
let INCR_BREAKDOWN_BAR_SPACE=2
let INCR_FULL_BAR_SPACE=1
let INCR_ONE_SPACE=1


## declare an array variable
declare -a kernstat=("cache-miss" "buff-miss" "migrated")

##use this for storing some state
let slowmemhists=0

declare -a placearr=('slowmem-only' 'optimal-os-fastmem'  'naive-os-fastmem' 'nimble' 'slowmem-migration-only' 'slowmem-obj-affinity-nomig' 'slowmem-obj-affinity' 'slowmem-obj-affinity-prefetch' 'slowmem-obj-affinity-net')
declare -a placearrcontextsensitivity=('slowmem-only' 'optimal-os-fastmem'  'naive-os-fastmem' 'slowmem-migration-only' 'slowmem-obj-affinity-prefetch')

declare -a sensitive_arr=('APPSLOW-OSSLOW' 'APPFAST-OSFAST' 'APPFAST-OSSLOW' 'APPSLOW-OSFAST')
declare -a placearrcontextsensitivity=('slowmem-only' 'optimal-os-fastmem'  'naive-os-fastmem' 'slowmem-migration-only' 'slowmem-obj-affinity-prefetch')

declare -a pattern=("fillrandom" "readrandom" "fillseq" "readseq" "overwrite")
declare -a configarrbw=("BW500" "BW1000" "BW2000" "BW4000")
#declare -a configarr=("BW1000")
declare -a configarrcap=("CAP2048" "CAP4096" "CAP8192" "CAP10240")

declare -a mechnames=('naive-os-fastmem' 'optimal-os-fastmem' 'slowmem-migration-only' 'slowmem-obj-affinity-nomig'  'slowmem-obj-affinity' 'slowmem-obj-affinity-net' 'slowmem-only')
#declare -a devices=("SSD" "NVM")
declare -a devices=("NVM")
#declare -a devices=("SSD")

declare -a excludekernstat=("obj-affinity-NVM1")
declare -a excludefullstat=('slowmem-obj-affinity-prefetch' 'slowmem-obj-affinity-net' 'NVM1')
declare -a excludesensitivecontext=("NVM1" "obj-affinity-net")
declare -a excludebreakdown=("optimal" "NVM1" "nomig" "naive" "affinity-net" "slowmem-only" "optimal")
declare -a excluderedisbreakdown=("affinity-prefetch")

declare -a redispattern=("SET" "GET")
declare -a mech_redis_prefetch=('slowmem-obj-affinity' 'slowmem-obj-affinity-prefetch')



EXTRACT_KERNINFO() {

        APP=$1
        dir=$2
	j=$3
        APPFILE=$4
	awkidx=$5
	stattype=$6
	localtype=$6
	file=$APPFILE
        resultdir=$ZPLOT/data/kernstat
        mkdir -p $resultdir

        outfile=$(basename $dir)
        outputfile=$APP-$outfile"-"$stattype".data"
        rm -rf $resultdir/$outputfile
        rm -rf "num.data"

        if [ "$APP" == "redis" ]
	then
		target=$dir/$APP"-kernel.out"
	else
		target=$dir/$file
	fi

	if [ -f $target ]; then

		search="$"$awkidx
	        if [ "$APP" == "redis" ]
		then
			let val=`cat $target | grep "HeteroProcname" &> orig.txt && sed 's/\s/,/g' orig.txt > modified.txt && cat modified.txt | awk -F, -v OFS=, "BEGIN {SUM=0}; {SUM=SUM+$search}; END {print SUM}"`  
		else

			if [[ $dir == *"slowmem-only"*  ]]; then
				if [[ $stattype == "cache-miss"  ]]; then
					localtype="cache-hits"
				fi	
			fi

			grep -r "HeteroProcname" $target  &> out.txt
			temp=`grep -Eo "$localtype([[:space:]]+[^[:space:]]+){1}" < out.txt`
			val=`echo $temp | awk '{print $2}'`

			if [[ $dir == *"optimal"*  ]]; then
				if [[ $localtype == *"cache-miss"*  ]]; then
					val=0
				fi	
			fi
		fi

		let scaled_value=$val/$SCALE_KERN_GRAPH
		echo $scaled_value &> $APP"kern.data"
		((j++))
		echo $j &> "num.data"
		paste "num.data" $APP"kern.data" &> $resultdir/$outputfile
		rm -rf "num.data" $APP"kern.data"
	fi
}


PULL_RESULT() {
	APP=$1
	dir=$2
        j=$3      
	APPFILE=$4
	GRAPHDATA=$5
	EXT=$6

	outfile=$(basename $dir)
	outputfile=$APP"-"$outfile$EXT".data"
	resultfile=$ZPLOT/data/$GRAPHDATA/$outputfile
	mkdir -p $ZPLOT/data/$GRAPHDATA
	rm -rf $resultfile
	rm -rf "num.data"


	if [ -f $dir/$APPFILE ]; then

		if [ "$APP" = 'redis' ]; 
		then
			val=`cat $dir/$APPFILE | grep -a "ET" | grep $access":" | awk 'BEGIN {SUM=0}; {SUM+=$2}; END {printf "%5.3f\n", SUM}'`
			echo $val $dir/$APPFILE
			scaled_value=$(echo $val $SCALE_REDIS_GRAPH | awk '{printf "%4.0f\n",$1/$2}')
			echo $scaled_value &> $APP".data"
		elif [  "$APP" = 'cassandra' ];
		then
			val=`cat $dir/$APPFILE | grep "Throughput" |  tail -1 | awk '{printf "%5.0f\n", $3}'`
			scaled_value=$(echo $val $SCALE_CASSANDRA_GRAPH | awk '{printf "%4.0f\n",$1/$2}')
			echo $scaled_value &> $APP".data"
		elif [  "$APP" = 'filebench' ]; 
		then
			val=`cat $dir/$APPFILE | grep "IO Summary:" | awk 'BEGIN {SUM=0}; {SUM=SUM+$6}; END {print SUM}'`
			scaled_value=$(echo $val $SCALE_FILEBENCH_GRAPH | awk '{printf "%4.0f\n",$1/$2}')
			echo $scaled_value &> $APP".data"

	       elif [  "$APP" = 'spark-bench' ];
	       then
                        val=`cat $dir/$APPFILE | grep "Elapsed" | awk '{print $8}' | awk -F: '{ print ($1 * 60) + ($2) + $3 }'`
                        scaled_value=$(echo $val $SCALE_SPARK_GRAPH | awk '{printf "%4.0f\n", $2/$1}')
			echo $dir/$APPFILE" "$val" "$scaled_value
                        echo $scaled_value &> $APP".data"
			#echo $dir/$APPFILE" "$APP".data"
			#cat $APP".data"
		else
			cp $dir/$APPFILE $dir/$APPFILE".txt"
			sed -i "/readseq/c\ " $dir/$APPFILE".txt"
			val=`cat $dir/$APPFILE".txt" | grep "ops/sec" | awk 'BEGIN {SUM=0}; {SUM=SUM+$5}; END {print SUM}'`
			echo $val
			scaled_value=$(echo $val $SCALE_ROCKSDB_GRAPH | awk '{printf "%4.0f\n",$1/$2}')
			echo $scaled_value &> $APP".data"
		fi
		((j++))
		echo $j &> "num.data"
		paste "num.data" $APP".data" &> $resultfile
		cat $resultfile
	fi
}






PULL_RESULT_PATTERN() {

	APP=$1
	dir=$2
        j=$3      
	APPFILE=$4
	access=$5
	resultdir=$ZPLOT/data/patern
	mkdir -p $resultdir

	outfile=$(basename $dir)
	outputfile=$APP-$outfile
	rm -rf $resultdir/$outputfile
	rm -rf "num.data"
	resultfile="$APP"-"$access.data"

	if [ -f $dir/$APPFILE ]; then

		file=$dir/$APPFILE
		
		if [ "$APP" = 'redis' ]; then
			val=`cat $file | grep -a "$SEARCH" | grep $access":" | awk 'BEGIN {SUM=0}; {SUM+=$2}; END {printf "%5.3f\n", SUM}'`
			#if [ "$access" == "ET" ]; 
			#then
				scaled_value=$(echo $val 100 | awk '{printf "%4.0f\n",$1/$2}')
				echo $scaled_value &> $resultfile
			#else
				#echo $val &> $resultfile
			#fi
		else
			if [ "$access" = 'readseq' ]; then
				cat $file | grep $access" " | awk 'BEGIN {SUM=0}; {SUM=SUM+$7}; END {printf "%5.0d\n", SUM/10}' &> $resultfile
			else
				cat $file | grep $access" " | awk 'BEGIN {SUM=0}; {SUM=SUM+$7}; END {printf "%5.0f\n", SUM}' &> $resultfile
			fi
		fi

		((j++))
		echo $j &> "num.data"
		paste "num.data" $resultfile &> $resultdir/$outputfile"-"$access".data"
		rm -rf "num.data" $resultfile
	fi
}

function EXCLUDE_DIR  {

	exlude=$1
	dir=$2
	local -n list=$3

	for check in "${list[@]}"
	do
		if [[ $dir == *"$check"* ]]; then
			((exlude++))	
		fi
	done
}



EXTRACT_BREAKDOWN_RESULT() {
	i=0
	j=0
	files=""
	rm $APP".data"
	let exlude=0

	APPFILE=""
	TYPE="NVM"

	for device in "${devices[@]}"
	do
		TYPE=$device
		APPFILE=$APP".out-"$device

		for accesstype in "${pattern[@]}"
		do
			for dir in $TARGET/*$device*
			do
                                exlude=0
                                EXCLUDE_DIR $exlude $dir excludebreakdown
                                if [ $exlude -ge 1 ]; then
                                        echo "EXCLUDING" $dir
                                        continue;
				else
					echo "NOT EXCLUDING" $dir
                                fi
				PULL_RESULT_PATTERN $APP $dir $j $basename $APPFILE $accesstype
			done
		j=$((j+$INCR_BREAKDOWN_BAR_SPACE))
		done
	done
}




EXTRACT_KERNSTAT() {

	j=0
	exlude=0
	APP=$1
	rm $APP".data"
	rm "num.data"

	for device in "${devices[@]}"
	do
		TYPE=$device
		APPFILE=$APP".out-"$device

		 if [ $TYPE == "SSD" ]; then
			awkidx=10
		 else
			awkidx=11
		 fi

		if [ "$APP" == "redis" ]
		then
			awkidx=8
		fi

		for stattype in "${kernstat[@]}"
		do

                        for placement in "${placearr[@]}"
                        do
                                for dir in $TARGET/*$placement*$device
                                do

					exlude=0
					EXCLUDE_DIR $exlude $dir excludekernstat
					if [ $exlude -ge 1 ]; then
						continue;
					fi
					EXTRACT_KERNINFO $APP $dir $j $APPFILE $awkidx $stattype
				done
			
			done
			if [ "$stattype" == "buff-miss" ];
			then
				((awkidx++))
				((awkidx++))
			else
				((awkidx++))
				((awkidx++))
				((awkidx++))
				((awkidx++))
			fi

			j=$((j+$INCR_KERN_BAR_SPACE))

		done
	done
}


REDIS_CONSOLIDATE_RESULT() {

        dir=$1
        APP=$2
	let instances=4

        rm $dir/$APP"-all.out-"$device

        for file in $dir/$APP*.txt
        do
                search=$APP
                if [[ $file == *"$search"*".txt" ]];
                then
			#rm -rf $dir/$APP"-all.out-"$device
                        cat $file | grep "ET:" &> tmp.txt
                        sed -i 's/\r/\n/g' tmp.txt
                        cat tmp.txt | grep "SET:" | tail -1 &>> $dir/$APP"-all.out-"$device
                        cat tmp.txt | grep "GET:" | tail -1 &>> $dir/$APP"-all.out-"$device
			rm -rf tmp.txt
                fi
        done

	for file in $dir/$APP".out-"*
	do
		if [ -f $file ]; then
			text="Currname\sredis-server"
			awkidx=10
			rm -rf $dir/$APP"-kernel.out"
			for i in $(seq 1 $instances);
			do
				 cat $file | sed 's/\[[^]]*\]//g' | sed 's/ Curr /Curr /g' |  grep $text$i | tail -1 &>> $dir/$APP"-kernel.out"
			done
		fi
	done
}


EXTRACT_REDIS_BREAKDOWN_RESULT() {
        j=0
        files=""
	APP=$1
        rm $APP".data"
        APPFILE=""

	for device in "${devices[@]}"
        do
                TYPE=$device
                APPFILE=$APP".out-"$device

		for array in "${placearr[@]}"
		do

			for dir in $TARGET/$array"-"$device
			do
				exlude=0
				EXCLUDE_DIR $exlude $dir excluderedisbreakdown
				if [ $exlude -ge 1 ]; then
					echo "EXCLUDING" $dir
					continue;
				fi
				REDIS_CONSOLIDATE_RESULT $dir $APP
			done
		done
	done

        for device in "${devices[@]}"
        do
                TYPE=$device
                APPFILE=$APP".out-"$device

		for accesstype in "${redispattern[@]}"
		do
			for array in "${placearr[@]}"
			do
				for dir in $TARGET/$array"-"$device
				do
					exlude=0
					EXCLUDE_DIR $exlude $dir excluderedisbreakdown
					if [ $exlude -ge 1 ]; then
						echo "EXCLUDING" $dir
						continue;
					fi
					PULL_RESULT_PATTERN $APP $dir $j $APP"-all.out-"$TYPE $accesstype
				done
			done
			((j++))
		done
	done
}



EXTRACT_REDIS_PREFETCH_BREAKDOWN_RESULT() {
        j=0
        files=""
	APP=$1
        rm $APP".data"
        APPFILE=""

	for device in "${devices[@]}"
        do
                TYPE=$device
                APPFILE=$APP".out-"$device

		for array in "${mech_redis_prefetch[@]}"
		do
			for dir in $TARGET/$array"-"$device
			do
				exlude=0
				EXCLUDE_DIR $exlude $dir excludebreakdown
				if [ $exlude -ge 1 ]; then
					echo "EXCLUDING" $dir
					continue;
				fi

				REDIS_CONSOLIDATE_RESULT $dir $APP
			done
		done
	done

        for device in "${devices[@]}"
        do
                TYPE=$device
                APPFILE=$APP".out-"$device

		for accesstype in "${redispattern[@]}"
		do
			for array in "${mech_redis_prefetch[@]}"
			do
				for dir in $TARGET/$array"-"$device
				do
					exlude=0
					EXCLUDE_DIR $exlude $dir excludebreakdown
					if [ $exlude -ge 1 ]; then
						echo "EXCLUDING" $dir
						continue;
					fi
					echo "NOT EXCLUDING" $dir
					PULL_RESULT_PATTERN $APP $dir $j $APP"-all.out-"$TYPE $accesstype
				done
			done
			((j++))
		done
	done
}


EXTRACT_RESULT() {
	rm $APP".data"
	rm "num.data"
	exclude=0
	ADD=$1

	for device in "${devices[@]}"
	do
		TYPE=$device
		APPFILE=""

		for placement in "${placearr[@]}"
		do
			for dir in $TARGET/$placement-$device$ADD
			do
				exlude=0
				EXCLUDE_DIR $exlude $dir excludefullstat
				if [ $exlude -ge 1 ]; then
					continue;
				fi
				
				if [ "$APP" = 'redis' ]; then
					APPFILE=$APP"-all.out-"$TYPE
				else
					APPFILE=$APP".out-"$TYPE
				fi
				echo $APPFILE
				PULL_RESULT $APP $dir $j $APPFILE ""
			done
		done
	done
	j=$((j+$INCR_FULL_BAR_SPACE))
}


EXTRACT_RESULT_SENSITIVE_MOTIVATE() {
        rm $APP".data"
        rm "num.data"
        exclude=0

        for device in "${devices[@]}"
        do
                TYPE=$device
                APPFILE=""

                for BW in "${configarr[@]}"
                do
                        for placement in "${sensitive_arr[@]}"
                        do
                                for dir in $TARGET/$BW*/*$placement*$device
                                do
                                        exlude=0
                                        EXCLUDE_DIR $exlude $dir excludefullstat
                                        if [ $exlude -ge 1 ]; then
                                                echo "EXCLUDING" $dir
                                                continue;
                                        fi

                                        if [ "$APP" = 'redis' ]; then
                                                APPFILE=$APP"-all.out-"$TYPE
                                        else
                                                APPFILE=$APP".out-"$TYPE
                                        fi
                                        PULL_RESULT $APP $dir $j $APPFILE "motivate" "-"$BW
                                done
                        done
                        j=$((j+$INCR_ONE_SPACE))
                done
        done
}

EXTRACT_RESULT_SENSITIVE_CONTEXT() {
        rm $APP".data"
        rm "num.data"
        exclude=0

        for device in "${devices[@]}"
        do
                TYPE=$device
                APPFILE=""

                for BW in "${configarr[@]}"
                do
                        for placement in "${placearrcontextsensitivity[@]}"
                        do
                                for dir in $TARGET/$BW*/*$placement*$device
                                do
                                        exlude=0
                                        EXCLUDE_DIR $exlude $dir excludesensitivecontext
                                        if [ $exlude -ge 1 ]; then
                                                echo "EXCLUDING" $dir
                                                continue;
                                        fi

                                       if [ "$APP" = 'redis' ]; then
                                                APPFILE=$APP"-all.out-"$TYPE
                                        else
                                                APPFILE=$APP".out-"$TYPE
                                        fi
                                        PULL_RESULT $APP $dir $j $APPFILE "result-sensitivity" "-"$BW
                                done
                        done
                        j=$((j+$INCR_ONE_SPACE))
                done
        done
}

EXTRACT_RESULT_COMPARE() {
        rm $APP".data"
        rm "num.data"
        exclude=0

        for device in "${devices[@]}"
        do
                TYPE=$device
                APPFILE=""

                for BW in "${configarr[@]}"
                do
                        for placement in "${sensitive_arr[@]}"
                        do
                                for dir in $TARGET/$BW*/*$placement*$device
                                do
                                        exlude=0
                                        EXCLUDE_DIR $exlude $dir excludefullstat
                                        if [ $exlude -ge 1 ]; then
                                                echo "EXCLUDING" $dir
                                                continue;
                                        fi

                                        if [ "$APP" = 'redis' ]; then
						REDIS_CONSOLIDATE_RESULT $dir $APP
						APPFILE=$APP"-all.out-"$TYPE
                                        else
                                                APPFILE=$APP".out-"$TYPE
                                        fi
                                        PULL_RESULT $APP $dir $j $APPFILE "motivate" "-"$BW
                                done
                        done
                        j=$((j+$INCR_ONE_SPACE))
                done
        done
}

FORMAT_RESULT_REDIS() {
        files=""
	APP=$1
        rm $APP".data"
        APPFILE=""
        DIRECTORY=$TARGET
	ADD=$2

	for device in "${devices[@]}"
        do
                TYPE=$device
                APPFILE=$APP".out-"$device

		for placement in "${placearr[@]}"
                do
			for dir in $TARGET/$placement-$TYPE*
			do
				echo $dir$ADD
				REDIS_CONSOLIDATE_RESULT $dir$ADD $APP
			done
		done
	done
}



###################### NUMA STUDY RESULT EXTRACTION ######################################
#placearr=('numa-preferred-0' 'numa-preferred-0-MEMHOG-node0' 'numa-preferred-1-DISABLE-AUTONUMA' 'numa-preferred-NONUMA' 'numa-preferred-NONUMA-MEMHOG-node0' 'numa-preferred-0-DISABLE-AUTONUMA' 'numa-preferred-1' 'numa-preferred-1-MEMHOG-node1' 'numa-preferred-NONUMA-DISABLE-AUTONUMA')



placearr=('numa-preferred-NONUMA')
j=0
APP='redis'
OUTPUTDIR="/localhome/sudarsun/NVM/results/NUMASTUDY/output-numastudy-trial1-1million-NVM"
TARGET=$OUTPUTDIR	
FORMAT_RESULT_REDIS "redis" "/slowmem-obj-affinity-prefetch-NVM"
EXTRACT_RESULT "/slowmem-obj-affinity-prefetch-NVM"
exit

placearr=('numa-preferred-0' 'numa-preferred-1' 'numa-preferred-0-DISABLE-AUTONUMA' 'numa-preferred-1-DISABLE-AUTONUMA' 'numa-preferred-NONUMA' 'numa-preferred-NONUMA-DISABLE-AUTONUMA' 'numa-preferred-0-MEMHOG-node0' 'numa-preferred-1-MEMHOG-node1')
j=0
APP='redis'
OUTPUTDIR="/localhome/sudarsun/NVM/results/NUMASTUDY/output-numastudy-trial1-1million-NVM"
TARGET=$OUTPUTDIR	
FORMAT_RESULT_REDIS "redis" "/naive-os-fastmem-NVM"
EXTRACT_RESULT "/naive-os-fastmem-NVM"

exit


j=0
APP='rocksdb'
OUTPUTDIR="/localhome/sudarsun/NVM/appbench/output-numastudy-trial1-1million-SSD"
TARGET=$OUTPUTDIR
placearr=('numa-preferred-0' 'numa-preferred-0-MEMHOG-node0' 'numa-preferred-1-DISABLE-AUTONUMA' 'numa-preferred-NONUMA' 'numa-preferred-NONUMA-MEMHOG-node0' 'numa-preferred-0-DISABLE-AUTONUMA' 'numa-preferred-1' 'numa-preferred-1-MEMHOG-node1' 'numa-preferred-NONUMA-DISABLE-AUTONUMA')
EXTRACT_RESULT "rocksdb"
exit







j=0
APP='filebench'
#OUTPUTDIR="/proj/fsperfatscale-PG0/sudarsun/context/results/m-rocksdb_sensitivity"
OUTPUTDIR="/users/skannan/ssd/NVM/results/output-Aug11-allapps"
TARGET=$OUTPUTDIR
EXTRACT_RESULT "filebench"

APP='redis'
FORMAT_RESULT_REDIS "redis"
EXTRACT_RESULT "redis"

APP='rocksdb'
TARGET=$OUTPUTDIR
EXTRACT_RESULT "rocksdb"

APP='cassandra'
TARGET=$OUTPUTDIR
EXTRACT_RESULT "cassandra"


APP='spark-bench'
TARGET=$OUTPUTDIR
EXTRACT_RESULT "spark-bench"

cd $ZPLOT
python2.7 $NVMBASE/graphs/zplot/scripts/e-allapps-total.py
exit


################################################################################
j=0
configarr=("${configarrbw[@]}")
APP='rocksdb'
OUTPUTDIR="/proj/fsperfatscale-PG0/sudarsun/context/results/rocksdb-sensitivity-context"
TARGET=$OUTPUTDIR
EXTRACT_RESULT_SENSITIVE_CONTEXT $APP

j=$((j+$INCR_FULL_BAR_SPACE))

APP='spark-bench'
OUTPUTDIR="/proj/fsperfatscale-PG0/sudarsun/context/results/spark-sensitivity-context"
TARGET=$OUTPUTDIR
EXTRACT_RESULT_SENSITIVE_CONTEXT $APP

cd $ZPLOT
python $NVMBASE/graphs/zplot/scripts/e-rocksdb-sensitivity-BW.py "BW"
exit



####################CAP STAT ################################
configarr=("${configarrbw[@]}") 

j=0
APP='rocksdb'
OUTPUTDIR="/proj/fsperfatscale-PG0/sudarsun/context/results/rocksdb_sensitivity"
TARGET=$OUTPUTDIR
EXTRACT_RESULT_SENSITIVE_MOTIVATE "rocksdb"

j=$((j+$INCR_FULL_BAR_SPACE))
j=$((j+$INCR_FULL_BAR_SPACE))

APP='spark-bench'
OUTPUTDIR="/proj/fsperfatscale-PG0/sudarsun/context/results/sparkbench-sensitivity"
TARGET=$OUTPUTDIR
EXTRACT_RESULT_SENSITIVE_MOTIVATE "spark-bench"

cd $ZPLOT
python2.7 $NVMBASE/graphs/zplot/scripts/m-rocksdb-sensitivity-BW.py "BW"


configarr=("${configarrcap[@]}")

j=0
APP='rocksdb'
OUTPUTDIR="/proj/fsperfatscale-PG0/sudarsun/context/results/rocksdb_sensitivity"
TARGET=$OUTPUTDIR
EXTRACT_RESULT_SENSITIVE_MOTIVATE "rocksdb"

j=$((j+$INCR_FULL_BAR_SPACE))
j=$((j+$INCR_FULL_BAR_SPACE))

APP='spark-bench'
OUTPUTDIR="/proj/fsperfatscale-PG0/sudarsun/context/results/sparkbench-sensitivity"
TARGET=$OUTPUTDIR
EXTRACT_RESULT_SENSITIVE_MOTIVATE "spark-bench"

cd $ZPLOT
python2.7 $NVMBASE/graphs/zplot/scripts/m-rocksdb-sensitivity-BW.py "CAP"
exit

####################MOTIVATION ANALYSIS########################
j=0
APP='rocksdb'
OUTPUTDIR="/proj/fsperfatscale-PG0/sudarsun/context/results/rocksdb_sensitivity"
TARGET=$OUTPUTDIR
EXTRACT_RESULT_COMPARE "rocksdb"

APP='redis'
OUTPUTDIR="/proj/fsperfatscale-PG0/sudarsun/context/results/redis-sensitivity"
TARGET=$OUTPUTDIR
EXTRACT_RESULT_COMPARE "redis"


APP='filebench'
TARGET=$OUTPUTDIR
EXTRACT_RESULT_COMPARE "filebench"

APP='cassandra'
#OUTPUTDIR="/users/skannan/ssd/NVM/appbench/output"
OUTPUTDIR="/proj/fsperfatscale-PG0/sudarsun/context/results/redis-sensitivity"
TARGET=$OUTPUTDIR
EXTRACT_RESULT_COMPARE "cassandra"

APP='spark-bench'
OUTPUTDIR="/proj/fsperfatscale-PG0/sudarsun/context/results/sparkbench-sensitivity"
TARGET=$OUTPUTDIR
EXTRACT_RESULT_COMPARE "spark-bench"

cd $ZPLOT
python2.7 $NVMBASE/graphs/zplot/scripts/m-allapps-total.py
exit

#####################REDIS NETWORK##############################
j=0
APP='redis'
OUTPUTDIR="/users/skannan/ssd/NVM/results/redis-results-Aug11"
TARGET=$OUTPUTDIR
EXTRACT_REDIS_BREAKDOWN_RESULT "redis"
cd $ZPLOT
python2.7 $NVMBASE/graphs/zplot/scripts/e-redis-breakdown.py

exit

#####################REDIS NETWORK##############################
j=0
APP='redis'
OUTPUTDIR="/users/skannan/ssd/NVM/results/redis-results-Aug11"
TARGET=$OUTPUTDIR
EXTRACT_REDIS_BREAKDOWN_RESULT "redis"
cd $ZPLOT
python2.7 $NVMBASE/graphs/zplot/scripts/e-redis-breakdown.py

exit

####################KERNEL STAT ################################
j=0
APP='rocksdb'
OUTPUTDIR="results/output-Aug11-allapps"
TARGET=$OUTPUTDIR
EXTRACT_KERNSTAT "rocksdb"
cd $ZPLOT
python2.7 $NVMBASE/graphs/zplot/scripts/e-rocksdb-kernstat.py -o "e-rocksdb-kernstat" -a "rocksdb" -y 400 -r 50 -s "NVM"
exit


j=0
APP='rocksdb'
OUTPUTDIR="results/output-Aug11-allapps"
TARGET=$OUTPUTDIR
EXTRACT_KERNSTAT "rocksdb"
cd $ZPLOT
python2.7 $NVMBASE/graphs/zplot/scripts/e-rocksdb-kernstat.py -o "e-rocksdb-kernstat" -a "rocksdb" -y 400 -r 50 -s "NVM"
exit
#######################ROCKSDB PREFETCH#########################
j=0
APP='rocksdb'
#OUTPUTDIR="results/output-Aug8-allapps"
OUTPUTDIR="/users/skannan/ssd/NVM/results/rocksdb-results-prefetch-Aug13"
TARGET=$OUTPUTDIR
EXTRACT_BREAKDOWN_RESULT "rocksdb"
cd $ZPLOT
python2.7 $NVMBASE/graphs/zplot/scripts/e-rocks-prefetch-breakdown.py
exit
#EXTRACT_KERNSTAT "redis"
#cd $ZPLOT
#python $NVMBASE/graphs/zplot/scripts/e-rocksdb-kernstat.py -i "" -o "e-redis-kernstat" -a "redis" -y 80 -r 10 -s "SSD"
######################################################
j=0
APP='redis'
OUTPUTDIR="/users/skannan/ssd/NVM/appbench/output"
TARGET=$OUTPUTDIR
EXTRACT_REDIS_PREFETCH_BREAKDOWN_RESULT "redis"
cd $ZPLOT
python2.7 $NVMBASE/graphs/zplot/scripts/e-redis-prefetch-breakdown.py
exit


######################################################

j=0
APP='redis'
OUTPUTDIR="/users/skannan/ssd/NVM/results/redis-results-july30th"
TARGET=$OUTPUTDIR
#FORMAT_RESULT_REDIS
EXTRACT_REDIS_BREAKDOWN_RESULT "redis"
cd $ZPLOT
python $NVMBASE/graphs/zplot/scripts/e-redis-breakdown.py
exit

EXTRACT_RESULT
cd $ZPLOT
python $NVMBASE/graphs/zplot/scripts/e-rocksdb-total.py
exit

