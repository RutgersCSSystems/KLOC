all: migration

CXX=g++
CC=gcc
LIBS=-lpthread -lrt
override CFLAGS += -Wall -fpic

migration: migration.c
	$(CC) $(CFLAGS) -c migration.c $(LIBS) -D_STOPMIGRATION #-D_STOPONDEMAND
	$(CC) reset.c -o reset $(LIBS)
	$(CC) $(CFLAGS) -shared -o libmigration.so migration.o $(LIBS)

install:
	sudo cp *.so /usr/local/lib
	sudo cp *.so /usr/lib
	sudo cp *.h /usr/local/include
	sudo cp *.h /usr/include
	sudo cp *.h /usr/lib/

clean:
	rm -rf *.o
	rm -rf *.so
	rm -rf reset
