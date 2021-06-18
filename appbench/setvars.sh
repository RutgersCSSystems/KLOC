#!/bin/bash
export APPBENCH=$PWD
export SHARED_DATA=$PWD/shared_data
export SHARED_LIBS=$PWD/shared_libs
export OUTPUTDIR=$APPBENCH/output
export GRAPHCHI_ROOT=$APPBENCH/graphchi/graphchi-cpp
mkdir $OUTPUTDIR
export QUARTZSCRIPTS=$SHARED_LIBS/quartz/scripts
export APPPREFIX=""
#export APPPREFIX="numactl --membind=1"
#export APPPREFIX=$QUARTZSCRIPTS/runenv.sh
