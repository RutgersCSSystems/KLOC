FILE=$1

cd $SHARED_LIBS/initiator
cp "migration"_$1".c" migration.c
make clean 
make
sudo make install
