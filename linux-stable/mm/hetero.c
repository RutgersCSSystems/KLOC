/*
 * mm/mmap.c
 *
 * Written by obz.
 *
 * Address space accounting code	<alan@lxorguk.ukuu.org.uk>
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/backing-dev.h>
#include <linux/mm.h>
#include <linux/vmacache.h>
#include <linux/shm.h>
#include <linux/mman.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/syscalls.h>
#include <linux/capability.h>
#include <linux/init.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/personality.h>
#include <linux/security.h>
#include <linux/hugetlb.h>
#include <linux/shmem_fs.h>
#include <linux/profile.h>
#include <linux/export.h>
#include <linux/mount.h>
#include <linux/mempolicy.h>
#include <linux/rmap.h>
#include <linux/mmu_notifier.h>
#include <linux/mmdebug.h>
#include <linux/perf_event.h>
#include <linux/audit.h>
#include <linux/khugepaged.h>
#include <linux/uprobes.h>
#include <linux/rbtree_augmented.h>
#include <linux/notifier.h>
#include <linux/memory.h>
#include <linux/printk.h>
#include <linux/userfaultfd_k.h>
#include <linux/moduleparam.h>
#include <linux/pkeys.h>
#include <linux/oom.h>

#include <linux/btree.h>
#include <linux/radix-tree.h>

#include <linux/buffer_head.h>
#include <linux/jbd2.h>

#include <linux/uaccess.h>
#include <asm/cacheflush.h>
#include <asm/tlb.h>
#include <asm/mmu_context.h>
#include <linux/pfn_trace.h>
#include <net/sock.h>
#include <linux/migrate.h>
//#include <sys/time.h>
#include <linux/time64.h>

#include <linux/types.h>

#include "internal.h"

/* start_trace flag option */
#define COLLECT_TRACE 1
#define PRINT_GLOBAL_STATS 2
#define DUMP_STACK 3
#define PFN_TRACE 4
#define PFN_STAT 5
#define TIME_TRACE 6
#define TIME_STATS 7
#define TIME_RESET 8
#define COLLECT_ALLOCATE 9
#define PRINT_ALLOCATE 10
#define CLEAR_GLOBALCOUNT 100


/* 
Flags to enable kloc allocations.
Move this to header file later.
*/
#define KLOC_PGCACHE 11
#define KLOC_BUFFER 12
#define KLOC_JOURNAL 13
#define KLOC_RADIX 14
#define KLOC_FULLKERN 15
#define KLOC_SET_FASTMEM_NODE 16
#define KLOC_MIGRATE_FREQ 17
#define KLOC_KNODE 18
#define KLOC_DISABLE_MIGRATE 19
#define KLOC_MIGRATE_LISTCNT 20
#define KLOC_SET_CONTEXT 21
#define KLOC_NET 22
#define KLOC_PGCACHE_READAHEAD 23

/* 
 * Flags for kernel object profiling
 */
#define KLOC_KNODE_STAT
#define KLOC_KNODE_STAT_INFO 24
#define KLOC_KNODE_INODE 25
#define KLOC_KNODE_TRANS 26
#define KLOC_KNODE_BUFFHEAD 27
#define KLOC_KNODE_DCACHE 28
#define KLOC_KNODE_SOCKBUFF 29
#define KLOC_KNODE_BIO 30
/*
 * Migration related
 */
#define KLOC_MIGRATE_THRESH 31
#define KLOC_MIGRATE_THREADS 32

/*
 * Per-CPU List code enable
 */
//#define HETERO_CPULISTS

/* Collect life time of page 
*/
#define _ENABLE_HETERO_THREAD
#ifdef _ENABLE_HETERO_THREAD

/*We restruct max threads to 10 */
#define MAXTHREADS 10


struct migrate_threads {
	struct task_struct *thrd;
};
struct migrate_threads THREADS[MAXTHREADS] = {0};

volatile int thrd_idx = 0;
volatile int migration_thrd_active=0;
volatile int spinlock=0;
DEFINE_SPINLOCK(kthread_lock);
#endif

/* Hetero Stats information*/
int global_flag = 0;
int radix_cnt = 0;
int kloc_dbgmask = 0;

int enbl_kloc_pgcache=0;
int enbl_kloc_buffer=0;
int enbl_kloc_journal=0;
int enbl_kloc_radix=0;
int enbl_kloc_kernel=0;
int enbl_kloc_set_context=0;
int kloc_fastmem_node=0;
int enbl_knode=0;
int disabl_kloc_migrate=0;
int enbl_kloc_net=0;
int enbl_kloc_pgcache_readahead=0;
int enbl_thrd_migrate=0;
int kloc_migrate_thresh=0;

#define KLOC_KNODE_STAT

#ifdef KLOC_KNODE_STAT
int enbl_kloc_inode=0;
int enbl_kloc_buffhead=0;
int enbl_kloc_trans=0;
int enbl_kloc_dcache=0;
int enbl_kloc_bio=0;
int enbl_kloc_sockbuff=0;
#endif //KLOC_KNODE_STAT

//Number of inodes managed by KLOC
int kloc_stat_inodes = 0;
int kloc_stat_erased_inodes = 0;

//Frequency of migration
int g_migrate_freq=0;
//Migration list threshold
int min_migrate_cnt=0;
int kloc_pid=0;
int kloc_usrpg_cnt=0;
int kloc_kernpg_cnt=0;
long migrate_time=0;

unsigned long g_cachehits=0;
unsigned long g_cachemiss=0;
unsigned long g_buffhits=0;
unsigned long g_buffmiss=0;
unsigned long g_migrated=0;
unsigned long g_cachedel=0;
unsigned long g_buffdel=0;
int g_lock_step = 0;

#ifdef CONFIG_KLOC_STATS
unsigned long g_tot_cache_pages=0;
unsigned long g_tot_buff_pages=0;
unsigned long g_tot_app_pages=0;
unsigned long g_tot_vmalloc_pages=0;
#endif

#ifdef CONFIG_KLOC_KNODE
struct kloc_obj_list *kloc_struct=0;
struct list_head *pcpu_list_head;
int kloc_cleanedup=0;

unsigned long per_cpu_inode[NR_CPUS];

#endif



DEFINE_SPINLOCK(stats_lock);

#ifdef CONFIG_KLOC_KNODE
void inode_cleanup_rblarge(struct inode *inode);
void kloc_delete_inode(struct inode *inode);
#endif


#ifdef CONFIG_KLOC_RBTREE
bool kloc_rbinode_insert(struct rb_root *root, struct inode *inode);
void kloc_rb_remove_kaddr(struct rb_root *root, void *kaddr);
void inode_cleanup_rbsmall(struct inode *inode);
#endif



#ifdef CONFIG_KLOC_ENABLE

#ifdef CONFIG_KLOC_STATS
void incr_tot_cache_pages(void) 
{
	if(!is_kloc_pgcache_set())
		return;

	//kloc_spin_lock(&stats_lock);
	g_tot_cache_pages++;
	//kloc_spin_unlock(&stats_lock);
}

void incr_tot_buff_pages(void) 
{
	if(!is_kloc_buffer_set())
		return;

	//kloc_spin_lock(&stats_lock);
	g_tot_buff_pages++;
	//kloc_spin_unlock(&stats_lock);
}

void incr_tot_app_pages(void) 
{
	if(!is_kloc_pgcache_set()) 
		return;

	//kloc_spin_lock(&stats_lock);
	g_tot_app_pages++;
	/*if(g_tot_app_pages) {
		g_tot_app_pages = (g_tot_app_pages - g_tot_cache_pages  -
					g_tot_buff_pages);
	}*/
	//kloc_spin_unlock(&stats_lock);
}

inline 
void incr_tot_vmalloc_pages(void)
{
	g_tot_vmalloc_pages++;
}
#endif


void incr_global_stats(unsigned long *counter){
	//kloc_spin_lock(&stats_lock);
	*counter = *counter + 1;	
	//kloc_spin_unlock(&stats_lock);
}

void print_global_stats(void) {

#ifdef CONFIG_KLOC_STATS
  	printk("ANALYSIS STAT CACHE-PAGES %lu, BUFF-PAGES %lu, APP-PAGES %lu VMALLOC %lu \n",
		g_tot_cache_pages, g_tot_buff_pages, g_tot_app_pages, g_tot_vmalloc_pages);

     /*  printk("FASTMEM CachePage hits %lu miss %lu " 
	      "KBUFF hits %lu miss %lu migrated %lu \n", 
		g_cachehits, g_cachemiss, g_buffhits, 
		g_buffmiss, g_migrated);*/
#endif
}
EXPORT_SYMBOL(print_global_stats);


struct mm_struct* getmm(struct task_struct *task) {
        struct mm_struct *mm = NULL;

        if(task->mm) {
                mm = task->mm;
	}
        else if(task->active_mm) {
                mm = task->active_mm;
	}
	return mm;
}


void kloc_stats(struct task_struct *task) {
#ifdef CONFIG_KLOC_STATS
	unsigned long buffpgs = 0;
	unsigned long cachepgs = 0;
	struct mm_struct *mm = NULL;

	mm = getmm(task);
	if(!mm)
		return;
#endif
}
EXPORT_SYMBOL(kloc_stats);


void reset_kloc_stats(struct task_struct *task) {
#ifdef CONFIG_KLOC_STATS
	g_cachehits = 0;
	g_cachemiss = 0;
	g_buffhits = 0;
	g_buffmiss = 0;
	g_migrated = 0;
	g_cachedel = 0;
	g_buffdel = 0;

	g_tot_cache_pages = 0;
	g_tot_buff_pages = 0;
	g_tot_app_pages = 0;
	g_tot_vmalloc_pages = 0;
#endif
}
EXPORT_SYMBOL(reset_kloc_stats);
#endif

unsigned long 
timediff (unsigned long start, unsigned long end) 
{
	return (end - start);
}


int 
check_listcnt_threshold (unsigned int count)
{
	if(min_migrate_cnt > count) 
		return 0;
	else
		return 1;
}
EXPORT_SYMBOL(check_listcnt_threshold);


#define K(x) ((x) << (PAGE_SHIFT - 10))
int check_node_memsize(int node_id) 
{
        struct sysinfo i;
        si_meminfo_node(&i, node_id);
	if(K(i.freeram) > kloc_migrate_thresh)
        	return -1;
	return 0;
}



/*
* Callers responsibility to check mm is not NULL
*/
int
kloc_check_parent (struct task_struct *task, struct mm_struct *mm) 
{
	struct task_struct *realp = NULL;
	struct task_struct *parent = NULL;
	struct task_struct *group_leader = NULL;
	struct mm_struct *parent_mm = NULL;

	realp  = task->real_parent;
	parent = task->parent;
	group_leader = task->group_leader;
	if(realp) {
		parent_mm = getmm(realp); 
	}/*else if (parent) {
		parent_mm = getmm(parent);
	}else if (group_leader) {
		parent_mm = getmm(group_leader);
	}*/

	if(strcmp(realp->comm, "java")) {
		return 0;
        }

	if(parent_mm && parent_mm->kloc_task == HETERO_PROC) {
		mm->kloc_task = HETERO_PROC;
		return 1;
	}
	return 0;
}


/*
 * Check whether is a kloc process 
 */
int 
kloc_check_proc (struct task_struct *task) 
{
    struct mm_struct *mm = NULL; 
    
    mm = getmm(task);
    if(!mm ) 
	return 0;

    if (mm->kloc_task == HETERO_PROC) {
	return 1;
    }

    if(!strcmp(task->comm, "java")) {
	mm->kloc_task = HETERO_PROC;
	return 1;
    }

    return 0; 	
}
EXPORT_SYMBOL(kloc_check_proc);


int 
kloc_check_page(struct mm_struct *mm, struct page *page) 
{
	int rc = -1;

	if(mm && (mm->kloc_task == HETERO_PROC) && page) {
		if(page->kloc == HETERO_PG_FLAG) {
			rc = 0;
		}
	}
	return rc;
}
EXPORT_SYMBOL(kloc_check_page);


static int 
stop_threads(struct task_struct *task, int force) 
{

#ifdef _ENABLE_HETERO_THREAD
	int idx = 0;

        //spin_lock(&kthread_lock);
        for(idx = 0; idx < MAXTHREADS; idx++) {
		/*if(force && THREADS[idx].thrd) {
			kthread_stop(THREADS[idx].thrd);
			THREADS[idx].thrd = NULL;
			thrd_idx--;
		}else if(THREADS[idx].thrd == task) {
			kthread_stop(THREADS[idx].thrd);
			THREADS[idx].thrd = NULL;
			if(thrd_idx > 0)
	                        thrd_idx--;
			break;
		}*/
		if(thrd_idx)
			thrd_idx--;
        }
        //spin_unlock(&kthread_lock);
#endif
	return 0;
}


/* 
* Exit function called during process exit 
*/
int 
is_kloc_exit(struct task_struct *task) 
{
    if(task && kloc_check_proc(task)) {
	kloc_stats(task);
#ifdef _ENABLE_HETERO_THREAD
	if(enbl_thrd_migrate) {
		//spin_lock(&kthread_lock);
		if(thrd_idx)
			thrd_idx--;
		//spin_unlock(&kthread_lock);
	}
#endif
    }
    return 0;
}
EXPORT_SYMBOL(is_kloc_exit);

void 
debug_kloc_obj(void *obj) 
{
#ifdef CONFIG_KLOC_DEBUG
        struct dentry *dentry, *curr_dentry = NULL;
	struct inode *inode = (struct inode *)obj;
	struct inode *currinode = (struct inode *)current->kloc_obj;

	if(obj == NULL)
		return;

	if(inode && inode->i_ino) {
		if(execute_ok(inode))
			return;
		dentry = d_find_any_alias(inode);
		printk(KERN_ALERT "%s:%d Inode %lu \n",
				__func__,__LINE__, inode->i_ino);
	}
#endif
}
EXPORT_SYMBOL(debug_kloc_obj);


int is_kloc_cacheobj(void *obj)
{
	return 1;

	if(!enbl_kloc_net)
		return 0;

	return enbl_kloc_net;
}
EXPORT_SYMBOL(is_kloc_cacheobj);


int 
is_kloc_obj(void *obj) 
{
	int ret = 0;
#ifdef CONFIG_KLOC_KNODE
        /*If we do not enable object affinity then we simply 
	return true for all the case*/
	if(!enbl_knode)
		ret = 1;
#endif
	if(obj && current && current->mm && 
		current->kloc_obj && current->kloc_obj == obj){
		ret = 1;
	}	
	return ret;
}
EXPORT_SYMBOL(is_kloc_obj);



/*
* Checked only for object affinity 
* when CONFIG_KLOC_KNODE is enabled
*/
int 
is_kloc_vma(struct vm_area_struct *vma) 
{
#ifdef CONFIG_KLOC_KNODE
	if(!enbl_knode)
		return 1;
        if(!vma || !vma->vm_file) {
                return 0;
        }
#endif
	return 1;
}


#ifdef KLOC_KNODE_STAT
int is_kloc_inode(void)
{
	return enbl_kloc_inode;
}
EXPORT_SYMBOL(is_kloc_inode);

//FIXME:
int is_kloc_buffhead(void)
{
	return 1;
        return enbl_kloc_buffhead;
}
EXPORT_SYMBOL(is_kloc_buffhead);


int is_kloc_trans(void)
{
        return enbl_kloc_trans;
}
EXPORT_SYMBOL(is_kloc_trans);


int is_kloc_dcache(void)
{
	return enbl_kloc_dcache;
}
EXPORT_SYMBOL(is_kloc_dcache);


//FIXME:
int is_kloc_bio(void)
{
	return 1;

        return enbl_kloc_bio;
}
EXPORT_SYMBOL(is_kloc_bio);

int is_kloc_sockbuff(void)
{
        return enbl_kloc_sockbuff;
}
EXPORT_SYMBOL(is_kloc_sockbuff);
#endif


/* 
* Functions to test different allocation strategies 
*/
int 
is_kloc_pgcache_set(void)
{
        if(kloc_check_proc(current)) 
	        return enbl_kloc_pgcache;
        return 0;
}
EXPORT_SYMBOL(is_kloc_pgcache_set);

int 
is_kloc_pgcache_readahead_set(void)
{
	if(kloc_check_proc(current))
		return enbl_kloc_pgcache_readahead;
	return 0;
}
EXPORT_SYMBOL(is_kloc_pgcache_readahead_set);


int 
is_kloc_buffer_set(void)
{
        if(kloc_check_proc(current)) 
                return enbl_kloc_buffer;
    return 0;
}
EXPORT_SYMBOL(is_kloc_buffer_set);


/*
* Sets current task with kloc obj
*/
void set_curr_kloc_obj(void *obj) 
{
#ifdef CONFIG_KLOC_KNODE
        //current->mm->kloc_obj = obj;
	current->kloc_obj = obj;
#endif
}
EXPORT_SYMBOL(set_curr_kloc_obj);


/*
* Sets page with kloc obj
*/
void 
set_kloc_obj_page(struct page *page, void *obj)                          
{
#ifdef CONFIG_KLOC_KNODE
        page->kloc_obj = obj;
#endif
}
EXPORT_SYMBOL(set_kloc_obj_page);


#ifdef HETERO_CPULISTS
void initialize_kloc_lists(struct inode *inode) 
{
	if(!kloc_struct || !kloc_struct->init)
		init_kloc_list();

	if(!inode)
		return;

	if(!inode->hlist_entry_added) {
		/* Make inode active */
		inode->is_kloc_active = 1;

		if(!add_percpu_kloc_list(&inode->kloc_hlist_entry))
			inode->hlist_entry_added = 1;
		else
			inode->hlist_entry_added = 0;
	}
}
#endif


void set_inode_cpu(struct inode *inode) {
	per_cpu_inode[smp_processor_id()] = (unsigned long)inode;
}

int get_inode_cpu(struct inode *inode)
{
	int cpu  = -EINVAL;
	int ret = -EINVAL;

	for(cpu = 0; cpu < NR_CPUS; cpu++) {

		if(inode == (struct inode *)per_cpu_inode[smp_processor_id()]) {
			cpu = smp_processor_id();
			return cpu;
		}
	}
	return ret;
}


void  set_inode_kloc_obj(struct inode *inode)                                        
{

	if(!inode)
		return;

	inode->is_kloc = HETERO_INIT;

	set_inode_cpu(inode);

#ifdef CONFIG_KLOC_RBTREE
	//Initialize per-process inode RBtree
	if(current->kloc_rbinode_init != HETERO_INIT) {
		//spin_lock_init(&current->kloc_rblock);
		current->kloc_rbinode = RB_ROOT;
		current->kloc_rbinode_cnt = 0;
		current->kloc_rbinode_init = HETERO_INIT;
	}


	if(inode->kloc_rblarge_init != HETERO_INIT) {

		inode->kloc_rblarge = RB_ROOT;
		inode->kloc_rblarge_cnt = 0;
		inode->kloc_rblarge_init = HETERO_INIT;

		//spin_lock_init(&inode->kloc_rblock_large);

		//Add the inode to process' inode tree
		//FIXME: What about remove inode?
		kloc_rbinode_insert(&current->kloc_rbinode, inode);
		current->kloc_rbinode_cnt++;
	}

	if(inode->kloc_rbsmall_init != HETERO_INIT) {
		inode->kloc_rbsmall_init = HETERO_INIT;
		inode->kloc_rbsmall = RB_ROOT;
		inode->kloc_rbsmall_cnt = 0;

		//spin_lock_init(&inode->kloc_rblock_small);

		//Add the inode to process' inode tree
		//FIXME: What about remove inode?
		kloc_rbinode_insert(&current->kloc_rbinode, inode);
		current->kloc_rbinode_cnt++;
	}
#endif
	current->kloc_obj = (void *)inode;

#ifdef HETERO_CPULISTS
	initialize_kloc_lists(inode); 
#endif
}
EXPORT_SYMBOL(set_inode_kloc_obj);


void set_fsmap_kloc_obj(void *mapobj)                                        
{
        struct address_space *mapping = NULL;
	struct inode *inode = NULL;
	void *current_obj = current->kloc_obj;

	if(!current_obj)
		return;

#ifdef CONFIG_KLOC_DEBUG
	struct dentry *res = NULL;
#endif

#ifdef CONFIG_KLOC_KNODE
        /*
	 * If we do not enable object affinity then we simply 
	 * return true for all the case
	*/
	if(!enbl_knode)
		return;
#endif
	mapping = (struct address_space *)mapobj;
        mapping->kloc_obj = NULL;
	inode = (struct inode *)mapping->host;
	/*if(execute_ok(inode)) {
		mapping->kloc_obj = NULL;
		return;
	}*/
	if(!inode)
		return;

	inode->is_kloc = HETERO_INIT;

	set_inode_cpu(inode);

#ifdef CONFIG_KLOC_RBTREE
	//Initialize per-process inode RBtree
	if(current->kloc_rbinode_init != HETERO_INIT) 
	{
		current->kloc_rbinode = RB_ROOT;
		current->kloc_rbinode_cnt = 0;
		current->kloc_rbinode_init = HETERO_INIT;
	}

	if(inode->kloc_rblarge_init != HETERO_INIT) 
	{
		inode->kloc_rblarge_init = HETERO_INIT;
		inode->kloc_rblarge = RB_ROOT;
		inode->kloc_rblarge_cnt = 0;

		//Add the inode to process' inode tree
		//FIXME: What about remove inode?
		kloc_rbinode_insert(&current->kloc_rbinode, inode);
		current->kloc_rbinode_cnt++;
	}

	if(inode->kloc_rbsmall_init != HETERO_INIT) 
	{
		inode->kloc_rbsmall_init = HETERO_INIT;
		inode->kloc_rbsmall = RB_ROOT;
		inode->kloc_rbsmall_cnt = 0;
		//Add the inode to process' inode tree
		//FIXME: What about remove inode?
		kloc_rbinode_insert(&current->kloc_rbinode, inode);
		current->kloc_rbinode_cnt++;
	}
#endif

	if(current_obj && current_obj == (void *)inode)
		return;

        if((is_kloc_buffer_set() || is_kloc_pgcache_set())) {
                mapping->kloc_obj = (void *)inode;
                //current->mm->kloc_obj = (void *)inode;
		current->kloc_obj = (void *)inode;
        }
}
EXPORT_SYMBOL(set_fsmap_kloc_obj);


/* 
* Mark the socket to Hetero target object 
*/
void set_sock_kloc_obj(void *socket_obj, void *inode)                                        
{
        struct sock *sock = NULL;
	struct socket *socket = (struct socket *)socket_obj;
	sock = (struct sock *)socket->sk;

	if(!sock) {
		return;
	}

        if((is_kloc_buffer_set() || is_kloc_pgcache_set())){

		sock->kloc_obj = (void *)inode;
		//current->mm->kloc_obj = (void *)inode;
		current->kloc_obj = (void *)inode;
		sock->__sk_common.kloc_obj = (void *)inode;
	}
}
EXPORT_SYMBOL(set_sock_kloc_obj);


void set_sock_kloc_obj_netdev(void *socket_obj, void *inode)                                        
{
#ifdef CONFIG_KLOC_NET
    struct sock *sock = NULL;
    struct socket *socket = (struct socket *)socket_obj;
    sock = (struct sock *)socket->sk;

    if(!sock) {
	return;
    }

    if((is_kloc_buffer_set() || is_kloc_pgcache_set())){
		sock->kloc_obj = (void *)inode;
		//current->mm->kloc_obj = (void *)inode;
		current->kloc_obj = (void *)inode;
		sock->__sk_common.kloc_obj = (void *)inode;
		if (sock->sk_dst_cache && sock->sk_dst_cache->dev) {
			if (!sock->sk_dst_cache->dev->kloc_sock)
				sock->sk_dst_cache->dev->kloc_sock = sock;
		}
	}
#endif
}
EXPORT_SYMBOL(set_sock_kloc_obj_netdev);

/*
 * RBTREE RELATED FUNCTIONALITIES
 */

#ifdef CONFIG_KLOC_RBTREE

/*
 * Clean up large rbinode elements
 */
void inode_cleanup_rblarge(struct inode *inode) {

    struct rb_node *node;
    unsigned int rbcnt =  inode->kloc_rblarge_cnt;

    if(!(&inode->kloc_rblarge))
	  return;

    for (node = rb_first(&inode->kloc_rblarge); node; node = rb_next(node)) {

        rb_erase(node, &inode->kloc_rblarge);
	if(node) {
                kfree(node);
                node = NULL;
        }
	rbcnt--;
    } 
}

/*
 * Clean up small rbinode elements
 */
void inode_cleanup_rbsmall(struct inode *inode) {

	struct rb_node *node;
    	unsigned int rbcnt =  inode->kloc_rbsmall_cnt;

    	if(!(&inode->kloc_rbsmall))
		  return;

	for (node = rb_first(&inode->kloc_rbsmall); node; node = rb_next(node)) {

        	rb_erase(node, &inode->kloc_rbsmall);
		if(node) {
        	        kfree(node);
                	node = NULL;
	        }

		if(rbcnt > 0)
			rbcnt--;
    	} 
   	inode->kloc_rbsmall_cnt = rbcnt;
}


/*
 * Clean up all inodes from process list
 */
void process_cleanup_rbtree(void) {

	struct rb_node *rb_inode = NULL;

       	for (rb_inode = rb_first(&current->kloc_rbinode);
                        rb_inode; rb_inode = rb_next(rb_inode)) {

                struct kloc_rbinode *kloc_rbinode =
                        rb_entry(rb_inode, struct kloc_rbinode, rbnode);
                if(!kloc_rbinode)
                        continue;

		struct inode *inode = NULL;
                inode = kloc_rbinode->inode;
                if(!inode)
                        continue;

		printk(KERN_ALERT "%s:%d \n", __func__, __LINE__);

		/* 
		 * Clean up large and small inode lists
		 */
		//spin_lock(&inode->kloc_rblock_large);
		inode_cleanup_rblarge(inode);
		//spin_unlock(&inode->kloc_rblock_large);

		//spin_lock(&inode->kloc_rblock_small);
		inode_cleanup_rbsmall(inode);
		//spin_unlock(&inode->kloc_rblock_small);

		inode->kloc_rblarge_init = 0;
		//inode->kloc_rblarge = NULL;
		inode->kloc_rblarge_cnt = 0;

		inode->kloc_rbsmall_init = 0;
		//inode->kloc_rbsmall = NULL;
		inode->kloc_rbsmall_cnt = 0;

		//spin_lock_init(&inode->kloc_rblock_large);
		//spin_lock_init(&inode->kloc_rblock_small);

		//spin_lock(&current->kloc_rblock);
	 	rb_erase(rb_inode, &current->kloc_rbinode);
		if(rb_inode) {
        	        kfree(rb_inode);
                	rb_inode = NULL;
	        }
		//spin_unlock(&current->kloc_rblock);
	}
}


/* 
 * Insert inode into a process KLOC list
 */
bool kloc_rbinode_insert(struct rb_root *root, struct inode *inode)
{

	return false;


	struct kloc_rbinode *data = 
		kmalloc(sizeof(struct kloc_rbinode), GFP_KERNEL);

	struct rb_node **link = &(root->rb_node), *parent=NULL;
	struct kloc_rbinode *this_node = NULL;

	data->inode = inode;
	
	while(*link) {
		parent = *link;
		this_node = rb_entry(parent, struct kloc_rbinode, rbnode);
		if(this_node->inode->i_ino > inode->i_ino) {
			link = &(*link)->rb_left;
		}
		else if (this_node->inode->i_ino == inode->i_ino) {
			goto duplicate;
		}
		else
			link = &(*link)->rb_right;
	}
	rb_link_node(&data->rbnode, parent, link);
	rb_insert_color(&data->rbnode, root);
	return true;

duplicate:
	return false;
}


/*
 * Searches and returns a page from pvt rb tree
 */
struct kloc_rbinode *kloc_rbinode_search 
        (struct rb_root *root, struct inode *inode)
{
	struct rb_node *node = root->rb_node;
	struct kloc_rbinode *this_node = NULL;

	if(root == NULL) {
		return NULL;
	}

	while(node) {
		this_node = rb_entry(node, struct kloc_rbinode, rbnode);

		if(this_node->inode->i_ino > inode->i_ino) {
			node = node->rb_left;
		}
		else if(this_node->inode->i_ino < inode->i_ino) {
			node = node->rb_right;
		}
		else /*==*/ {
			return this_node;
		}
	}
	return NULL;
}
EXPORT_SYMBOL(kloc_rbinode_search);


void kloc_rbinode_remove(struct rb_root *root, struct inode *inode)
{
	struct kloc_rbinode *node;
      
	node = kloc_rbinode_search(root, inode);
	if(node == NULL) {
		return;
	}
	rb_erase(&node->rbnode, root);

	cond_resched();
}

/* 
 * Inserts cache pages either into a large or small rbtree 
 * Assumes the caller has checked if page is NULL or not. 
 * If the page is already added/present to one of the rbtree's, 
 * the page is not added again.
 */
bool kloc_rb_insert_cache(struct rb_root *root, struct page *page)
{
	struct rb_node **link = &(root->rb_node), *parent=NULL;
	struct kloc_rbnode *this_node = NULL;
        struct kloc_rbnode *data = NULL;

	if(root == NULL){
		return false;
	}

	if(page->is_kloc_rbtree == HETERO_INIT) {
		return false;
	}	

	data = kmalloc(sizeof(struct kloc_rbnode), GFP_KERNEL);
	if(!data)
		BUG_ON(!data);

	data->page = page;

	while(*link) {

		parent = *link;
		this_node = rb_entry(parent, struct kloc_rbnode, lru_node);

		if(!this_node || !link) {
			goto rb_insert_fail;
		}

		if(page_to_virt(this_node->page) > page_to_virt(page)) {
			link = &(*link)->rb_left;
		}
		else if (page_to_virt(this_node->page) == page_to_virt(page)) {
			goto rb_insert_fail;
		}
		else {
			link = &(*link)->rb_right;
		}
	}

	rb_link_node(&data->lru_node, parent, link);
	rb_insert_color(&data->lru_node, root);
	/*Indicate that the page is now part of an rbtree 
	 * Ensure, it is reset during removal. 
	 */
	page->is_kloc_rbtree = HETERO_INIT;

	return true;

rb_insert_fail:
	kfree(data);
	data = NULL;
	return false;
}
EXPORT_SYMBOL(kloc_rb_insert_cache);


/*
 * Searches and returns a page from pvt rb tree
 */
struct kloc_rbnode *kloc_rb_search(struct rb_root *root, struct page *page)
{
	if(root == NULL) {
		return NULL;
	}

	struct rb_node *node = root->rb_node;
	struct kloc_rbnode *this_node = NULL;

	while(node) {
		this_node = rb_entry(node, struct kloc_rbnode, lru_node);

		if(page_to_virt(this_node->page) > page_to_virt(page)) {
			node = node->rb_left;
		}
		else if(page_to_virt(this_node->page) < page_to_virt(page)) {

			node = node->rb_right;
		} else {
			return this_node;
		}
	}

	return NULL;
}
EXPORT_SYMBOL(kloc_rb_search);


void kloc_rb_remove(struct rb_root *root, struct page *page)
{
	struct kloc_rbnode *node = NULL;
       
	node = kloc_rb_search(root, page);
	if(node == NULL) {
		return;
	}

	rb_erase(&node->lru_node, root);
	/*Indicate that the page is now not a part of an rbtree 
	 * Ensure, it is reset during removal. 
	 */
	page->is_kloc_rbtree = 0;

	if(node) {
		kfree(node);
		node = NULL;
	}
}


unsigned long pageaddr[1024];
unsigned long virtaddr[1024];
int elements = 0;

int is_page_migratable(struct page *page)
{
	char *bad_reason = NULL;

	if (unlikely(atomic_read(&page->_mapcount) != -1))
		bad_reason = "nonzero mapcount";
        if (unlikely(page->mapping != NULL))
                bad_reason = "non-NULL mapping";
        if (unlikely(page_ref_count(page) != 0)) {
                bad_reason = "nonzero _refcount";
        }

        if (unlikely(page->flags & PAGE_FLAGS_CHECK_AT_FREE)) {
                bad_reason = "PAGE_FLAGS_CHECK_AT_FREE flag(s) set";
                //bad_flags = PAGE_FLAGS_CHECK_AT_FREE;
        }

        if(bad_reason != NULL) {
                return -ENODEV;
        }
	return 0;
}



/* The inodes have a per-inode rb-tree of cache and kernel buffer 
 * pages. This function adds the pages to the rbtree
 */
void 
insert_page_cache_rbtree(struct page *page, int is_pagecache)
{
	struct inode *inode = NULL;

	if(!page)
		return NULL;

	if(page->kloc_obj != NULL) {

		inode = (struct inode*)page->kloc_obj;

		if(inode) {

			if(!kloc_rbinode_search(&current->kloc_rbinode, inode)) {
				set_inode_kloc_obj(inode);
			}

			if(is_pagecache) {
				if(inode->kloc_rblarge_init != HETERO_INIT) {
					inode->kloc_rblarge_init = HETERO_INIT;
					inode->kloc_rblarge = RB_ROOT;
					inode->kloc_rblarge_cnt = 0;
					//spin_lock_init(&inode->kloc_rblock_large);
				}

				//spin_lock(&inode->kloc_rblock_large);

				if(kloc_rb_insert_cache(&inode->kloc_rblarge, page)
						== true) {
					inode->kloc_rblarge_cnt++;
				}

				//spin_unlock(&inode->kloc_rblock_large);
			}
		}
	}
}
EXPORT_SYMBOL(insert_page_cache_rbtree);


void remove_page_cache_rbtree(struct page *page, int is_pagecache)
{
	struct inode *inode;

	if(page->kloc_obj != NULL) {
		inode = (struct inode*)page->kloc_obj;
		if(inode) {
			if(is_pagecache) {

				//spin_lock(&inode->kloc_rblock_large);
				kloc_rb_remove(&inode->kloc_rblarge, page);
				//spin_unlock(&inode->kloc_rblock_large);

				if(inode->kloc_rblarge_cnt > 0)
					inode->kloc_rblarge_cnt--;
				page->kloc_obj = NULL;
			}
		}
	}

}
EXPORT_SYMBOL(remove_page_cache_rbtree);


void try_kernel_page_migration(struct inode *inode)
{
	struct rb_root kloc_rbsmall;
        int rb_pages_itr = 0;
        struct rb_node *rb_inode;
        void *kaddr;
        int pages_migrated = 0;
	int pages_erased = 0;
	int loop = 0;
	int num_inodes = 0;


	if(!inode)
		return;
	inode = NULL;

	cond_resched();

       	for (rb_inode = rb_first(&current->kloc_rbinode);
                        rb_inode; rb_inode = rb_next(rb_inode)) {

                if(!rb_inode)
                        continue;

		struct rb_node *node = NULL;
                struct kloc_rbinode *kloc_rbinode =
                        rb_entry(rb_inode, struct kloc_rbinode, rbnode);
                if(!kloc_rbinode)
                        continue;

                inode = kloc_rbinode->inode;
                if(!inode)
                        continue;

		int cpu = get_inode_cpu(inode);
		if(cpu != -EINVAL) {
			continue;
		}

		num_inodes++;

                kloc_rbsmall = inode->kloc_rbsmall;
                kaddr = NULL;

                if(!(&kloc_rbsmall))
                        continue;

                //spin_lock(&inode->kloc_rblock_small);
                for (node = rb_first(&kloc_rbsmall); node; node = rb_next(node))
                {
                        rb_pages_itr++;
			struct kloc_rbnode *rbnode = rb_entry(node, struct
					kloc_rbnode, lru_node);
			kaddr = NULL;
                        if(rbnode)
                                kaddr = rbnode->kaddr;

                        if(!kaddr)
                                continue;

			if(!is_vmalloc_addr(kaddr))
				continue;

			//spin_lock(&current->kloc_rblock);
			if(!migrate_vmalloc_pages(kaddr, NULL, NULL, get_slowmem_node(), NULL)) {
				pages_migrated += 1;
			}
			//spin_unlock(&current->kloc_rblock);
		}
		kloc_rbsmall = inode->kloc_rbsmall;
#if 0
                for (node = rb_first(&kloc_rbsmall); node; node = rb_next(node))
                {
			struct kloc_rbnode *rbnode = rb_entry(node, struct
					kloc_rbnode, lru_node);
                        if(!rbnode) {
				continue;
                        }
			rb_erase(&rbnode->lru_node, &inode->kloc_rbsmall);
			pages_erased++;

			if(inode->kloc_rbsmall_cnt > 0)
				inode->kloc_rbsmall_cnt--;
                }
#endif
                //spin_unlock(&inode->kloc_rblock_small);
        }
	cond_resched();
	return;
}


/* 
 * Inserts kernel address to small rbtree 
 * Assumes the caller has checked if address is NULL or not. 
 * If the address is already added/present to one of the rbtree's, 
 * the address is not added again.
 */
bool kloc_rb_insert_kaddr(struct rb_root *root, void *kaddr)
{
	struct rb_node **link = &(root->rb_node), *parent=NULL;
	struct kloc_rbnode *this_node = NULL;
        struct kloc_rbnode *data = NULL;

	data = kmalloc(sizeof(struct kloc_rbnode), GFP_KERNEL);
	if(!data) {
		//BUG_ON(!data);
		return false;
	}

	data->kaddr = (void *)kaddr;

	while(*link) {

		parent = *link;
		this_node = rb_entry(parent, struct kloc_rbnode, lru_node);

		if(!this_node || !link) {
			goto rb_insert_fail_kernel;
		}

		if((void *)this_node->kaddr > (void *)kaddr) {
			link = &(*link)->rb_left;
		}
		else if ((void *)this_node->kaddr == (void *)kaddr) {
			goto rb_insert_fail_kernel;
		}
		else {
			link = &(*link)->rb_right;
		}
	}

	rb_link_node(&data->lru_node, parent, link);
	rb_insert_color(&data->lru_node, root);

	return true;

rb_insert_fail_kernel:
	kfree(data);
	data = NULL;
	return false;
}
EXPORT_SYMBOL(kloc_rb_insert_kaddr);


void kloc_rb_remove_kaddr(struct rb_root *root, void *kaddr)
{
	struct kloc_rbnode *node = NULL;
       
	node = kloc_rb_search_kaddr(root, kaddr);
	if(node == NULL) {
		return;
	}

	rb_erase(&node->lru_node, root);
	if(node) {
		kfree(node);
		node = NULL;
	}
}


/*
 * Searches and returns a kern addr
 */
struct kloc_rbnode *kloc_rb_search_kaddr(struct rb_root *root, void *kaddr)
{
	if(root == NULL) {
		return NULL;
	}

	struct rb_node *node = root->rb_node;
	struct kloc_rbnode *this_node = NULL;

	while(node) {
		this_node = rb_entry(node, struct kloc_rbnode, lru_node);

		if((void *)this_node->kaddr > (void *)kaddr) {
			node = node->rb_left;
		}
		else if((void *)this_node->kaddr < (void *)kaddr) {

			node = node->rb_right;
		} else {
			return this_node;
		}
	}
	return NULL;
}
EXPORT_SYMBOL(kloc_rb_search_kaddr);




/* The inodes have a per-inode rb-tree of kernel buffer 
 * pages. This function adds the vaddr to the rbtree
 */
int insert_kaddr_rbtree(struct page *page, void *kaddr)
{
	struct inode *inode = NULL;

	if(!kaddr)
		return NULL;

	if(!page)
		return NULL;

	if(page->kloc_obj != NULL) {

		inode = (struct inode*)page->kloc_obj;
		if(inode) {
			if(inode->kloc_rbsmall_init != HETERO_INIT) {
				inode->kloc_rbsmall_init = HETERO_INIT;
				inode->kloc_rbsmall = RB_ROOT;
				inode->kloc_rbsmall_cnt = 0;
				//spin_lock_init(&inode->kloc_rblock_small);
			}

			//spin_lock(&inode->kloc_rblock_small);
			if(kloc_rb_insert_kaddr(&inode->kloc_rbsmall, kaddr)
					== true) {
				inode->kloc_rbsmall_cnt++;
			}
			//spin_unlock(&inode->kloc_rblock_small);
		}
	}
}
EXPORT_SYMBOL(insert_kaddr_rbtree);


void remove_kaddr_rbtree(struct page *page, void *kaddr)
{
	struct inode *inode;

	if(page->kloc_obj != NULL) {

		inode = (struct inode*)page->kloc_obj;
		if(inode) {

			if(inode->kloc_rbsmall_init == HETERO_INIT)
				return;

			//spin_lock(&inode->kloc_rblock_small);
			kloc_rb_remove_kaddr(&inode->kloc_rbsmall, kaddr);
			//spin_unlock(&inode->kloc_rblock_small);
			if(inode->kloc_rbsmall_cnt > 0)
				inode->kloc_rbsmall_cnt--;
			return;
		}
	}

	if(inode->kloc_rbsmall_cnt % 200 == 0) {
		//try_kernel_page_migration(inode);
	}

}
EXPORT_SYMBOL(remove_kaddr_rbtree);


int delete_element(void *kaddr) {

	int i = 0;

	for(i=0; i < elements; i++) {
		if(virtaddr[i] == (unsigned long)kaddr)	
			virtaddr[i] = 0;
	}

}



void *kloc_kmalloc_wrap(unsigned int size, void *kloc_obj) {
#ifdef CONFIG_KLOC_MIGRATE
        /* Insert the page to KLOC RBTREE */
        struct page *page = NULL;
        void *s = NULL;

	s = kloc_kmalloc_migrate(size, GFP_KERNEL);
        if(s) {
                page = vmalloc_to_page(s);
                if(page && page->is_kloc_vmalloc == HETERO_INIT) {
                        page->kloc_obj = kloc_obj;
                        insert_kaddr_rbtree(page, s);
                }
		return s;	
        }else {
		return NULL;
	}
#endif
}
EXPORT_SYMBOL(kloc_kmalloc_wrap);


void kloc_vfree_page(void *addr) {

	struct page *page = vmalloc_to_page(addr);

	if(!is_vmalloc_addr(addr))
		return;

	if(page && page->is_kloc_vmalloc == HETERO_INIT) {
		//delete_element(addr);
		remove_kaddr_rbtree(page, addr);
		kloc_vfree(addr);
		//set_page_count(page, 0);
		//spin_unlock(&current->kloc_rblock);
	}
	//spin_unlock(&current->kloc_rblock);
	//cond_resched();
}
EXPORT_SYMBOL(kloc_vfree_page);
#endif


/* Update STAT
* TODO: Currently not setting HETERO_PG_FLAG for testing
*/
void 
kloc_update_pgcache(int nodeid, struct page *page, int delpage) 
{
	int correct_node = 0; 
	struct mm_struct *mm = NULL;
	/* Always enabled for page cache pages */
	int is_pagecache= 1; 

	if(!page) 
		return;

	if(page_to_nid(page) == nodeid)
		correct_node = 1;

	if (!current)
		return;

	mm = getmm(current);
	if(!mm)
		return;

	if(page->kloc != HETERO_PG_FLAG)
		return;

	/*Check if page is in the correct node and 
	we are not deleting and only inserting the page*/
	if(correct_node && !delpage) {
		mm->pgcache_hits_cnt += 1;
		page->kloc = HETERO_PG_FLAG;
		incr_global_stats(&g_cachehits);
#ifdef CONFIG_KLOC_KNODE_DISABLE
		insert_page_cache_rbtree(page, is_pagecache);
#endif
	} else if(!correct_node && !delpage) {
		mm->pgcache_miss_cnt += 1;
		page->kloc = 0;
		incr_global_stats(&g_cachemiss);
	}else if(correct_node && (page->kloc == HETERO_PG_FLAG) 
			&& delpage) {
		mm->pgcachedel++;
		incr_global_stats(&g_cachedel);
#ifdef CONFIG_KLOC_KNODE_DISABLE
		remove_page_cache_rbtree(page, is_pagecache);
#endif
	}
	/* Either if object affinity is disabled or page node is 
	incorrect, then return */
	if(!correct_node || !enbl_knode)
		goto ret_pgcache_stat;

ret_pgcache_stat:
	return;
}
EXPORT_SYMBOL(kloc_update_pgcache);


/* 
* Update STAT 
* TODO: Currently not setting HETERO_PG_FLAG for testing 
*/
void kloc_update_pgbuff_stat(int nodeid, struct page *page, int delpage) 
{
	int correct_node = 0; 
	struct mm_struct *mm = NULL;

	if(!page) 
		return;

	if(page_to_nid(page) == nodeid)
		correct_node = 1;

	mm = getmm(current);
	if(!mm)
		return;

	if(page->kloc != HETERO_PG_FLAG)
		return;

	//Check if page is in the correct node and 
	//we are not deleting and only inserting the page
	if(correct_node && !delpage) {
		mm->pgbuff_hits_cnt += 1;
		incr_global_stats(&g_buffhits);
		page->kloc = HETERO_PG_FLAG;
	}else if(!correct_node && !delpage) {
		incr_global_stats(&g_buffmiss);
		mm->pgbuff_miss_cnt += 1;
		page->kloc = 0;
	}else if(correct_node && (page->kloc == HETERO_PG_FLAG) 
			&& delpage) {
#ifdef CONFIG_KLOC_STATS
		mm->pgbuffdel++;
#endif		
	}
	//Either if object affinity is disabled or page node is 
	//incorrect, then return
	if(!correct_node || !enbl_knode)
		goto ret_pgbuff_stat;

ret_pgbuff_stat:
	return;
}
EXPORT_SYMBOL(kloc_update_pgbuff_stat);


/* 
* Simple miss increment; called specifically from 
* functions that do not explicity aim to place pages 
* on heterogeneous memory
*/
void update_kloc_pgbuff_stat_miss(void) 
{
        current->mm->pgbuff_miss_cnt += 1;
}
EXPORT_SYMBOL(update_kloc_pgbuff_stat_miss);


/* 
 * Check if the designed node and current page location 
 * match. Responsibility of the requester to pass nodeid
 */
int is_kloc_page(struct page *page, int nodeid){

   if(page_to_nid(page) == nodeid) {
	return 1;
   }
   return 0;
}
EXPORT_SYMBOL(is_kloc_page);


int is_kloc_journ_set(void){

    //if(kloc_pid && current->pid == kloc_pid)
    return enbl_kloc_journal;
    return 0;
}
EXPORT_SYMBOL(is_kloc_journ_set);


int is_kloc_radix_set(void){
    if(kloc_check_proc(current))
    	return enbl_kloc_radix;
    return 0;
}
EXPORT_SYMBOL(is_kloc_radix_set);


int is_kloc_kernel_set(void){
    return enbl_kloc_kernel;
    return 1;
}
EXPORT_SYMBOL(is_kloc_kernel_set);

int get_fastmem_node(void) {
        return kloc_fastmem_node;
}

int get_slowmem_node(void) {
        return NUMA_HETERO_NODE;
}


/* This function will be enabled only when threaded 
 * migration is enabled.
 */
static int migration_thread_fn(void *arg) {

        unsigned long count = 0;
        struct mm_struct *mm = (struct mm_struct *)arg;

        if(!mm) {
#ifdef _ENABLE_HETERO_THREAD
		//spin_lock(&kthread_lock);
		thrd_idx--;
		//spin_unlock(&kthread_lock);
#endif
                return 0;
        }
        count = kloc_migrate_to_node(mm, get_fastmem_node(),
                        get_slowmem_node(),MPOL_MF_MOVE_ALL);

#ifdef _ENABLE_HETERO_THREAD
        //spin_lock(&kthread_lock);
	if(thrd_idx)
		thrd_idx--;
        //spin_unlock(&kthread_lock); 
#endif
        //do_gettimeofday(&end);
        //migrate_time += timediff(&start, &end);
	//stop_threads(current, 0);
	//if(kthread_should_stop()) {
	//	do_exit(0);
	//}
	//spin_lock(&kthread_lock);
	//spin_unlock(&kthread_lock);
	//printk(KERN_ALERT "%s:%d THREAD %d EXITING %d\n", 
	//	__func__, __LINE__, current->pid, thrd_idx);
        return 0;
}


void 
kloc_try_migration(void *map, gfp_t gfp_mask, int iskernel) {

	unsigned long *target=0;
	unsigned long *cachemiss=0;
        unsigned long *buffmiss=0;
	unsigned long freemem_pages = 0;

	if(disabl_kloc_migrate) {
		return;
	}

	if(!current->mm || (current->mm->kloc_task != HETERO_PROC))
		return;

	if(!g_cachemiss) {
		return;
	}

	/* Check free space availability. If we have 
	 * lot of space in the node left, then don't 
	 * begin migration.
	 */
	//if(check_node_memsize(get_fastmem_node()))
	//	return;
	cachemiss = &current->mm->pgcache_miss_cnt;
	buffmiss = &current->mm->pgbuff_miss_cnt;
	target = &current->mm->migrate_attempt;

	/* Check if we are well with in the threshold*/
	if((*cachemiss +  *buffmiss) <  *target) {
		return;
	}else {
		*target = *target + g_migrate_freq;
	}

#ifdef _ENABLE_HETERO_THREAD
	if(!enbl_thrd_migrate)
		goto sync_migrate;

	if(MAXTHREADS <= thrd_idx)
		return;
	THREADS[thrd_idx].thrd = kthread_run(migration_thread_fn,
				current->mm, "HETEROTHRD");	

	//spin_lock(&kthread_lock);
	thrd_idx++;
	//spin_unlock(&kthread_lock);
#endif

sync_migrate:
	kloc_stats(current);
	
	//kloc_migrate_to_node(current->mm, get_fastmem_node(),
	//			get_slowmem_node(), MPOL_MF_MOVE_ALL);

        return;
}
EXPORT_SYMBOL(kloc_try_migration);


void reset_all_flags(void)
{
    /*reset kloc allocate flags */
    enbl_kloc_pgcache = 0;
    enbl_kloc_buffer = 0; 
    enbl_kloc_radix = 0;
    enbl_kloc_journal = 0; 
    enbl_kloc_kernel = 0;
    enbl_kloc_net = 0;
    enbl_kloc_pgcache_readahead=0;
    /* Enable application defined context */
    enbl_kloc_set_context = 0;
    enbl_knode = 0;	
    kloc_pid = 0;
    kloc_kernpg_cnt = 0;
    kloc_usrpg_cnt = 0;

    /* Reset KOBJ Stats */
    enbl_kloc_trans = 0;
    enbl_kloc_inode = 0;
    enbl_kloc_dcache = 0;
    enbl_kloc_sockbuff = 0;
    enbl_kloc_bio = 0;
    enbl_kloc_buffer = 0;
    kloc_migrate_thresh = 0;
}



/* start trace system call */
SYSCALL_DEFINE2(start_trace, int, flag, int, val)
{

#ifdef _ENABLE_HETERO_THREAD
	int idx = 0;
#endif

#ifdef CONFIG_KLOC_ENABLE
    switch(flag) {
	case CLEAR_GLOBALCOUNT:
	    printk("flag set to clear count %d\n", flag);
	    global_flag = CLEAR_GLOBALCOUNT;
	    process_cleanup_rbtree();
	    reset_all_flags();
	    reset_kloc_stats(current);	
#ifdef CONFIG_KLOC_KNODE
	    kloc_struct = NULL;
#endif
#ifdef HETERO_CPULISTS
	    cleanup_kloclist();
	    free_kloc_list();
#endif
	    break;

	case COLLECT_TRACE:
	    global_flag = COLLECT_TRACE;
	    return global_flag;
	    break;
	case PRINT_GLOBAL_STATS:
	    global_flag = PRINT_GLOBAL_STATS;
	    print_global_stats();
	    break;
	case PFN_TRACE:
	    global_flag = PFN_TRACE;
	    return global_flag;
	    break;
	case PFN_STAT:
	    print_pfn_hashtable();
	    break;
	case TIME_TRACE:
	    global_flag = TIME_TRACE;
	    return global_flag;
	    break;
	case TIME_STATS:
	    global_flag = TIME_STATS;
	    print_rbtree_time_stat();
	    break;
	case TIME_RESET:
	    rbtree_reset_time();
	    break;
	case COLLECT_ALLOCATE:
	    global_flag = COLLECT_ALLOCATE;
	    return global_flag;
	    break;
	case PRINT_ALLOCATE:
	    global_flag = PRINT_ALLOCATE;
	    print_global_stats();	
	    break;
	case KLOC_PGCACHE:
	    enbl_kloc_pgcache = 1;
	    break;
	case KLOC_BUFFER:
	    enbl_kloc_buffer = 1;
	    break;
	case KLOC_JOURNAL:
	    enbl_kloc_journal = 1;
	    break;
	case KLOC_RADIX:
	    enbl_kloc_radix = 1;
	    break;
	case KLOC_FULLKERN:
	    enbl_kloc_kernel = 1;
	    break;
	case KLOC_SET_FASTMEM_NODE:
	    kloc_fastmem_node = val;
	    break;
	case KLOC_MIGRATE_FREQ:
	     g_migrate_freq = val;
	     break;	
	case KLOC_KNODE:
#ifdef CONFIG_KLOC_KNODE
	    enbl_knode = 1;
#endif 
	    break;	
	case KLOC_DISABLE_MIGRATE:
	     disabl_kloc_migrate = 1;
	     break;	
	case KLOC_MIGRATE_LISTCNT:
	     min_migrate_cnt = val;
	     break;	

	/* Set current file context */
	case KLOC_SET_CONTEXT:
	     enbl_kloc_set_context = 1;
	     break;

	case KLOC_NET:
	     enbl_kloc_net = 1;
	     break;		

	case KLOC_PGCACHE_READAHEAD:
	     enbl_kloc_pgcache_readahead = 1;	
	     break;	

#ifdef KLOC_KNODE_STAT
	case KLOC_KNODE_STAT_INFO:
	     if(val == KLOC_KNODE_INODE)
	     	enbl_kloc_inode=1;
	     if(val == KLOC_KNODE_BUFFHEAD)
	     	enbl_kloc_buffhead=1;
      	     if(val == KLOC_KNODE_TRANS)
	     	enbl_kloc_trans=1;
	     if(val == KLOC_KNODE_DCACHE)
	     	enbl_kloc_dcache=1;
	     if(val == KLOC_KNODE_SOCKBUFF)
	     	enbl_kloc_sockbuff=1;
	     if(val == KLOC_KNODE_BIO)
		enbl_kloc_bio=1;
	     break;
#endif
	case KLOC_MIGRATE_THRESH:
		kloc_migrate_thresh = val;
		break;

	case KLOC_MIGRATE_THREADS:
		enbl_thrd_migrate = val;
		break;

	default:
#ifdef CONFIG_KLOC_DEBUG
	   kloc_dbgmask = 1;	
#endif
	    kloc_pid = flag;
	    current->mm->kloc_task = HETERO_PROC;
	    break;
    }
#endif

    if(!kloc_migrate_thresh)
	kloc_migrate_thresh = KLOC_MIGRATE_THRESH;

    return 0;
}


#ifdef HETERO_CPULISTS
/*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%*/
/*
 * Implementation related to per-CPU lists
 */
void cleanup_kloclist(void)
{
	struct list_head *list_entries, *head;
	int cpu=0, num_inodes=0;
	struct inode *inode = NULL;

#ifdef CONFIG_HETERO_PERCPU
	struct list_head __percpu *pcpu_list;
#else
	struct list_head *pcpu_list, *tmp = NULL;
#endif
	if(kloc_cleanedup) {
		return 0;
	}

	if(!kloc_struct) {
		return 0;
	}

	if(!kloc_struct->init) {
		return 0;
	}

        pcpu_list = kloc_struct->kloclist;
	if(!pcpu_list) {
                return 0;
        }
#ifdef CONFIG_HETERO_PERCPU
        for_each_possible_cpu(cpu) {
		list_entries = per_cpu_ptr(pcpu_list, cpu);
#else
		list_entries = pcpu_list;
#endif
		if(!list_entries)
			return 0;

		list_for_each_safe(head, tmp, list_entries) {

	                inode = list_entry(head, struct inode, kloc_hlist_entry);

			inode->kloc_rblarge_init = 0;
			inode->kloc_rblarge = RB_ROOT;

			kloc_delete_inode(inode);
			kloc_stat_erased_inodes++;
        	}
		//FIXME: This is not required.
#ifdef CONFIG_HETERO_PERCPU
	}
#endif
	current->kloc_rbinode_init = 0;
	kloc_cleanedup = 1;
	return;
}


void kloc_inactive_inode(struct inode *inode)
{
	if(!inode)
		return;

	inode->is_kloc_active = 0;
	return;
}
EXPORT_SYMBOL(kloc_inactive_inode);


void kloc_delete_inode(struct inode *inode)
{
	if(!inode)
		return;

	if(inode->hlist_entry_added) {
		list_del(&inode->kloc_hlist_entry);
		inode->hlist_entry_added = 0;

		if(kloc_stat_inodes) {
			kloc_stat_inodes--;
		}
	}
	/* Make inode in-active */
	inode->is_kloc_active = 0;
	return;
}
EXPORT_SYMBOL(kloc_delete_inode);


void init_kloc_list(void) 
{
	int cpu;
#ifdef CONFIG_HETERO_PERCPU
	struct list_head __percpu *list;
	list = alloc_percpu(struct list_head);
#else

	struct list_head *list;
	list = kmalloc(sizeof(struct list_head), GFP_KERNEL);
#endif
	if(!list) {
        	printk(KERN_ALERT "%s:%d kloc_objs list NULL \n",
                                __func__, __LINE__);
		VM_BUG_ON(!list);
	}

#ifdef CONFIG_HETERO_PERCPU
	for_each_possible_cpu(cpu)
		INIT_HLIST_HEAD(per_cpu_ptr(list, cpu));
#else
	INIT_LIST_HEAD(list);
#endif
	kloc_struct = kmalloc(sizeof(struct kloc_obj_list), GFP_KERNEL);
	if(!kloc_struct) {
		return;
	}

	kloc_struct->kloclist = list;
	kloc_struct->init = 1;
	pcpu_list_head = kloc_struct->kloclist;
	kloc_cleanedup = 0;
	printk(KERN_ALERT "%s:%d Finished initialization \n",
			__func__, __LINE__);
}


void free_kloc_list(void) 
{
	if(!kloc_struct)
		return;

	if(kloc_struct->kloclist)
		kfree(kloc_struct->kloclist);

	kloc_struct->init = 0;

	if(kloc_struct) {
		kfree(kloc_struct);
		pcpu_list_head = NULL;
	}
}


void iterate_cpulist(void)
{
	struct list_head *list_entries, *head;
	int cpu=0, num_inodes=0;
	struct inode *inode = NULL;

#ifdef CONFIG_HETERO_PERCPU
	struct list_head __percpu *pcpu_list;
#else
	struct list_head *pcpu_list;
#endif
	if(!kloc_struct)
		return 0;

        pcpu_list = kloc_struct->kloclist;

#ifdef CONFIG_HETERO_PERCPU
        for_each_possible_cpu(cpu) {

		//list_entries = this_cpu_ptr(pcpu_list);
		list_entries = per_cpu_ptr(pcpu_list, cpu);
#else
		list_entries = pcpu_list;
#endif
		if(!list_entries)
			return 0;

		list_for_each_entry(inode, list_entries, kloc_hlist_entry) {
			num_inodes++;
		}

#ifdef CONFIG_HETERO_PERCPU
	}
#endif
	return;
}


#ifdef CONFIG_HETERO_PERCPU
struct list_head __percpu *get_kloc_struct_list(void)
#else
struct list_head *get_kloc_struct_list(void)
#endif
{
	if(!kloc_struct)
		return NULL;

	return kloc_struct->kloclist;
}
EXPORT_SYMBOL(get_kloc_struct_list);


int add_percpu_kloc_list (struct list_head *entry) 
{ 

#ifdef CONFIG_HETERO_PERCPU
	struct list_head __percpu *pcpu_list;
#else
	struct list_head *pcpu_list;
#endif
       	struct list_head *list;

	if(!kloc_struct)
		return -1;

       	pcpu_list = kloc_struct->kloclist;

       	if (WARN_ON_ONCE(!pcpu_list))
       		return -1;
#ifdef CONFIG_HETERO_PERCPU
       	list = this_cpu_ptr(pcpu_list);
#else
	list = pcpu_list;
#endif
	list_add(entry, list);

	//iterate_cpulist();
	/* 
	 * FIXME: Enable the flag back. 
	 * Also, just an approximate count for stat purposes, 
	 * as we do not do it atomically. 
	 */
#ifdef CONFIG_KLOC_STATS
	kloc_stat_inodes++;
#endif
	return 0;
}
#endif //HETERO_CPULISTS
/*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%*/

