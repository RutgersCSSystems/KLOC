//#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <errno.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
//#include <numa.h>
#include <time.h>
#include <inttypes.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include "migration.h"

#define __NR_start_trace 333

#define CLEAR_GLOBAL_COUNT     0
#define COLLECT_TRACE 1
#define PRINT_GLOBAL_STATS 2
#define PFN_TRACE 4
#define PFN_STAT 5
#define TIME_TRACE 6
#define TIME_STATS 7
#define TIME_RESET 8
#define COLLECT_ALLOCATE 9
#define PRINT_ALLOCATE 10

#define HETERO_PGCACHE 11
#define HETERO_BUFFER 12
#define HETERO_JOURNAL 13
#define HETERO_RADIX 14
#define HETERO_FULLKERN 15
#define HETERO_SET_FASTMEM_NODE 16

#ifndef _SLOWONLY
#define HETERO_FASTMEM_NODE 0
#else
#define HETERO_FASTMEM_NODE 1
#endif

void main() {
	syscall(__NR_start_trace, PRINT_GLOBAL_STATS);
	syscall(__NR_start_trace, CLEAR_GLOBAL_COUNT, 0);
        syscall(__NR_start_trace, COLLECT_ALLOCATE, 0);
}
/*******************************************/
