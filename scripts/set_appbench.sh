#!/bin/bash
set -x

INSTALL_SYSTEM_LIBS(){
	sudo apt-get install -y git
	sudo apt-get install -y software-properties-common
	sudo apt-get install -y python3-software-properties
	sudo apt-get install -y python-software-properties
	sudo apt-get install -y unzip
	sudo apt-get install -y python-setuptools python-dev build-essential
	sudo easy_install -y pip
        sudo apt install -y  python-pip
	sudo pip install zplot
	sudo apt-get install -y numactl
	sudo apt-get install -y libsqlite3-dev
	sudo apt-get install -y libnuma-dev
	sudo apt-get install -y cmake
	sudo apt-get install -y build-essential
	sudo apt-get install -y maven
	sudo apt-get install -y fio
	sudo apt-get install -y libbfio-dev
	sudo apt-get install -y libboost-dev
	sudo apt-get install -y libboost-thread-dev
	sudo apt-get install -y libboost-system-dev
	sudo apt-get install -y libboost-program-options-dev
	sudo apt-get install -y libconfig-dev
	sudo apt-get install -y uthash-dev
	sudo apt-get install -y cscope
	sudo apt-get install -y msr-tools
	sudo apt-get install -y msrtool
	sudo pip install -y psutil
	sudo apt-get install -y libmpich-dev
	sudo apt-get install -y libzstd-dev
	sudo apt-get install -y liblz4-dev
	sudo apt-get install -y libsnappy-dev
	sudo apt-get install -y libncurses-dev
	sudo apt-get install -y mosh
	#sudo pip install thrift_compiler
	#INSTALL_JAVA
	sudo add-apt-repository ppa:webupd8team/java
	sudo apt-get update
	sudo apt-get install -y openjdk-8-jdk
	sudo apt-get install -y build-essential
	sudo apt-get install -y libssl-dev
	sudo apt-get install -y libgflags-dev
	sudo apt-get install -y zlib1g-dev
	sudo apt-get install -y libbz2-dev
	sudo apt-get install -y libevent-dev
	sudo apt-get install -y systemd
	#sudo apt-get install memcached
	sudo apt-get install -y libaio*
	sudo apt-get install -y software-properties-common
}

INSTALL_CMAKE(){
    cd $SHARED_LIBS
    wget https://cmake.org/files/v3.7/cmake-3.7.0-rc3.tar.gz
    tar zxvf cmake-3.7.0-rc3.tar.gz
    cd cmake-3.7.0-rc3
    rm -rf CMakeCache*
    ./configure
    ./bootstrap
    make -j16
    sudo make install
}

INSTALL_MIGRATION_LIB(){
	cd $SHARED_LIBS/construct
	make -j4
	sudo make install
}

INSTALL_KERNEL(){
	source scripts/setvars.sh
	$SCRIPTS/compile_deb.sh #&> out.txt
	$SCRIPTS/compile_nokvm.sh #&>> out.txt
}


INSTALL_SYSBENCH() {
        curl -s https://packagecloud.io/install/repositories/akopytov/sysbench/script.deb.sh | sudo bash
        sudo apt -y install sysbench
}

INSTALL_MYSQL() {
        sudo apt-get install mysql-server-5.7

	# change datadir to ssd	
	sudo systemctl stop mysql
	sudo rsync -av /var/lib/mysql $SSD/mysql
	sudo mv /var/lib/mysql /var/lib/mysql.bak
	sed -i '/datadir/d' /etc/mysql/mysql.conf.d/mysqld.cnf | cat -n
	echo 'datadir = $SSD/mysql/mysql' >> /etc/mysql/mysql.conf.d/mysqld.cnf
	echo 'alias /var/lib/mysql/ -> $SSD/mysql,' >> /etc/apparmor.d/tunables/alias
	sudo systemctl restart apparmor
	sudo mkdir /var/lib/mysql/mysql -p
	sudo systemctl start mysql
}


INSTALL_SPARK_DOCKER_CLOUDSUITE(){
	sudo service docker stop
	sudo apt-get -y remove docker docker.io
	sudo rm -rf /var/lib/docker $APPBENCH/docker
	mkdir $APPBENCH/docker
	sudo apt-get -y install docker docker.io
	sudo service docker stop
	sudo cp scripts/docker_new.service /lib/systemd/system/docker.service
	sudo systemctl daemon-reload
	sudo service docker start
	sudo docker pull cloudsuite/graph-analytics
	sudo docker pull cloudsuite/twitter-dataset-graph
}


INSTALL_CASSANDRA() {
	cd $APPBENCH/apps
	git clone https://github.com/SudarsunKannan/butterflyeffect
	cd butterflyeffect/code
	source scripts/setvars.sh
	scripts/install_cassandra.sh
	source $CODE/scripts/setvars.sh
	cp $CODE/cassandra.sh $CSRC/bin/cassandra
}

INSTALL_ROCKSDB() {
	cd $APPBENCH/apps
	#git clone https://github.com/facebook/rocksdb
	cd RocksDB
	#DEBUG_LEVEL=0 make shared_lib db_bench -j16
	./build_rocksdb.sh
	cp $APPBENCH/apps/rocks-script/run_rocksdb.sh $APPBENCH/apps/rocksdb/run.sh
}

INSTALL_FILEBENCH() {
	cd $APPBENCH/apps
	cd filebench
	./configure
	make clean && make $PARA
}

INSTALL_GFLAGS(){
	cd $SHARED_LIBS
	git clone https://github.com/gflags/gflags.git
	cd gflags
	rm -rf CMakeCache.txt
	export CXXFLAGS="-fPIC" && cmake . -DCMAKE_INSTALL_PREFIX=/usr/local -DBUILD_SHARED_LIBS=ON && make -j16 
	sudo make install
	sudo cp /usr/local/lib/libgflags* /usr/lib/
}

INSTALL_REDIS(){
	cd $APPBENCH/apps
	cd redis-5.0.5
	make clean
	make -j16
}


#Get Other Apps not in out Repo
GETAPPS(){
	mkdir $APPBENCH
	cd $APPBENCH
	git clone https://github.com/SudarsunKannan/leveldb
	mkdir $APPBENCH/apps
	cd $APPBENCH/apps
	git clone https://github.com/SudarsunKannan/fio
	cd $APPBENCH/apps
	git clone https://github.com/memcached/memcached.git
}

INSTALL_MIGRATION_LIB
#INSTALL_SYSTEM_LIBS
#INSTALL_KERNEL
#GETAPPS
#INSTALL_CMAKE
#INSTALL_GFLAGS
#INSTALL_ROCKSDB
exit
