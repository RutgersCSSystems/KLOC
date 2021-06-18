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

#define CLEAR_COUNT 100
#define COLLECT_TRACE 1
#define PRINT_STATS 2
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

/*
Flags to enable hetero allocations.
Move this to header file later.
*/
#define HETERO_KOBJ_STAT_INFO 24
#define HETERO_KOBJ_INODE 25
#define HETERO_KOBJ_TRANS 26
#define HETERO_KOBJ_BUFFHEAD 27
#define HETERO_KOBJ_DCACHE 28
#define HETERO_KOBJ_SOCKBUFF 29
#define HETERO_KOBJ_BIO 30

#define HETERO_MIGRATE_THREADS 32

#ifndef _SLOWONLY
#define HETERO_FASTMEM_NODE 0
#else
#define HETERO_FASTMEM_NODE 1
#endif



static int setinit;

void sig_handler(int);



static void con() __attribute__((constructor)); 
static void dest() __attribute__((destructor));


#define HETERO_MIGRATE_FREQ 17
#define FREQ 100000
#define HETERO_OBJ_AFF 18
#define HETERO_DISABLE_MIGRATE 19
#define HETERO_MIGRATE_LISTCNT 20
#define HETERO_SET_CONTEXT 21
#define HETERO_NET 22
#define HETERO_PGCACHE_READAHEAD 23
#define MIGRATE_LIST_CNT 5000


void set_migration_freq() {
#ifdef _MIGRATE
    syscall(__NR_start_trace, HETERO_MIGRATE_FREQ, FREQ);
#else
    syscall(__NR_start_trace, HETERO_MIGRATE_FREQ, 9000000000);
#endif
}

void set_migrate_list_cnt() {
#ifdef _MIGRATE
        syscall(__NR_start_trace, HETERO_MIGRATE_LISTCNT, MIGRATE_LIST_CNT);
#else
        syscall(__NR_start_trace, HETERO_MIGRATE_LISTCNT, 9000000000);
#endif
}

void enable_object_affn() {
#ifdef _OBJAFF
    syscall(__NR_start_trace, HETERO_OBJ_AFF, HETERO_OBJ_AFF);
#endif
}

void disable_migration() {
#ifdef _DISABLE_MIGRATE
    syscall(__NR_start_trace, HETERO_DISABLE_MIGRATE, HETERO_DISABLE_MIGRATE);
#endif
}

void enbl_hetero_net() {
#ifdef _NET
    syscall(__NR_start_trace, HETERO_NET, HETERO_NET);
#endif	
}

int enbl_hetero_pgcache_readahead_set(void)
{
#ifdef _PREFETCH
	syscall(__NR_start_trace, HETERO_PGCACHE_READAHEAD, HETERO_PGCACHE_READAHEAD);
#endif
}




 
void enable_hetero_inode(void)
{
#ifdef _INODE_PLACE
        syscall(__NR_start_trace, HETERO_KOBJ_STAT_INFO, HETERO_KOBJ_INODE);
#endif
}

 
void enable_hetero_bufferhead(void)
{
#ifdef _BUFFHEAD_PLACE
        syscall(__NR_start_trace, HETERO_KOBJ_STAT_INFO, HETERO_KOBJ_BUFFHEAD);
#endif
}

void enbl_threaded_migration(void)
{
#ifdef _MIGRATE_THREADS
        syscall(__NR_start_trace, HETERO_MIGRATE_THREADS, 1);
#endif
}

void enable_hetero_trans(void)
{
#ifdef _TRANS_PLACE
        syscall(__NR_start_trace, HETERO_KOBJ_STAT_INFO, HETERO_KOBJ_TRANS);
#endif
}



void enable_hetero_dcache(void)
{
#ifdef _DCACHE_PLACE
        syscall(__NR_start_trace, HETERO_KOBJ_STAT_INFO, HETERO_KOBJ_DCACHE);
#endif
}



void enable_hetero_bio(void)
{
#ifdef _BIO_PLACE
        syscall(__NR_start_trace, HETERO_KOBJ_STAT_INFO, HETERO_KOBJ_BIO);
#endif
}


void enable_hetero_sockbuff(void)
{
#ifdef _SOCKBUFF_PLACE
        syscall(__NR_start_trace, HETERO_KOBJ_STAT_INFO, HETERO_KOBJ_SOCKBUFF);
#endif
}



void enable_kernstat_info(void)
{
	enable_hetero_bio();
	enable_hetero_sockbuff();
	enable_hetero_trans();
	enable_hetero_inode();
	enable_hetero_dcache();
	enable_hetero_bufferhead();
}


void dest() {
    fprintf(stderr, "application termination...\n");
    syscall(__NR_start_trace, PRINT_STATS);
    /*a = syscall(__NR_start_trace, CLEAR_COUNT);
    a = syscall(__NR_start_trace, PFN_STAT);
    a = syscall(__NR_start_trace, TIME_STATS);
    a = syscall(__NR_start_trace, TIME_RESET);
    syscall(__NR_start_trace, PRINT_ALLOCATE, 0);
    syscall(__NR_start_trace, CLEAR_COUNT, 0);*/
}

void con() {
  
    struct sigaction action;
    pid_t pid = getpid();

    /* Do not enable hetero if HETERO disabled */
#ifndef _DISABLE_HETERO
    if(!setinit) {
        fprintf(stderr, "initiating tracing...\n");
	/*syscall(__NR_start_trace, CLEAR_COUNT, 0);
        syscall(__NR_start_trace, COLLECT_ALLOCATE, 0);*/
        syscall(__NR_start_trace, HETERO_PGCACHE, 0);
        syscall(__NR_start_trace, HETERO_BUFFER, 0);
        syscall(__NR_start_trace, HETERO_JOURNAL, 0);
        syscall(__NR_start_trace, HETERO_RADIX, 0);
        syscall(__NR_start_trace, HETERO_FULLKERN, 0);
        syscall(__NR_start_trace, HETERO_SET_FASTMEM_NODE, HETERO_FASTMEM_NODE);
	syscall(__NR_start_trace, (int)pid);
	syscall(__NR_start_trace, HETERO_SET_FASTMEM_NODE, HETERO_FASTMEM_NODE);

	set_migration_freq();
	enable_object_affn();
	disable_migration();
	set_migrate_list_cnt();
	enbl_hetero_net();
	enbl_hetero_pgcache_readahead_set();
	enbl_threaded_migration();

	/*detailed kernel object placement*/
	//enable_kernstat_info();

        //Register KILL
        memset(&action, 0, sizeof(struct sigaction));
        action.sa_handler = sig_handler;
        sigaction(SIGKILL, &action, NULL);
        //setinit = 1;
     } 
#endif
 	
}

void init_allocs() {
	con();
}

void sig_handler(int sig) {
  
    switch (sig) {
        case SIGKILL:
            dest();
        default:
	    return;
    }
}
/*******************************************/
