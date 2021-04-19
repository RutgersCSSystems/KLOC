/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_HETERO_H
#define _LINUX_HETERO_H

#include <linux/vmalloc.h>
//#include <linux/types.h>
//#include <linux/percpu.h>

struct kloc_obj_list {
#ifdef CONFIG_HETERO_PERCPU
        struct hlist_head __percpu *kloclist;
#else
	struct list_head *kloclist;
#endif
	int init;
};

#ifdef CONFIG_HETERO_PERCPU
struct hlist_head __percpu *get_kloc_struct_list(void);
#else
struct list_head *get_kloc_struct_list(void);
#endif


#ifdef CONFIG_KLOC_ENABLE

#define HETERO_PG_FLAG 1
#define HETERO_PG_DEL_FLAG 2
#define HETERO_PG_MIG_FLAG 3

#define _USE_HETERO_PG_FLAG
#define NUMA_FAST_NODE 0
#define NUMA_HETERO_NODE 1
#define HETERO_PROC 4

//for initializing hetero variables
#define HETERO_INIT 999

#ifdef CONFIG_KLOC_STATS

enum {
        HETERO_APP_PAGE    = 1000,
        HETERO_KBUFF_PAGE  = 1001,
        HETERO_CACHE_PAGE  = 1002
};
#endif

/* Page cache allocation */
#define _ENABLE_PAGECACHE
/* Buffer allocation */
#define CONFIG_HETERO_ENABLE_BUFFER
/* Journal allocation */
#define _ENABLE_JOURNAL
/* Radix tree allocation */
#define CONFIG_HETERO_ENABLE_RADIX
/* Page table allocation */
#define CONFIG_HETERO_ENABLE_PGTBL
#else
#define NUMA_HETERO_NODE 0
#endif

#define FASTFREE_THRESH 200000

/*
 * Debug code
 */
#ifdef pr_fmt
#undef pr_fmt
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#endif

extern int kloc_dbgmask;

#define kloc_dbg(s, args ...)              \
        ((1 & kloc_dbgmask) ? pr_warning(s, ## args) : 0)
#define kloc_force_dbg(s, args ...)     pr_warning(s, ## args)
#define kloc_err(sb, s, args ...)       pmfs_error_mng(sb, s, ## args)
#define kloc_warn(s, args ...)          pr_warning(s, ## args)
#define kloc_info(s, args ...)          pr_info(s, ## args)


#ifdef CONFIG_KLOC_RBTREE
struct kloc_rbinode {
        struct inode *inode;
        struct rb_node rbnode;
};

struct kloc_rbnode {
        struct page *page;
        void *kaddr;
        struct rb_node lru_node;
};

struct kloc_rbinode *kloc_rbinode_search
        (struct rb_root *root, struct inode *inode);

bool kloc_rb_insert(struct rb_root *root, struct page *page);
struct kloc_rbnode *kloc_rb_search(struct rb_root *root, struct page *page);
#endif

struct vm_area_struct;
int kloc_check_proc (struct task_struct *task);
int check_listcnt_threshold(unsigned int listcount);
extern int page_list_count(struct list_head *pagelist);

int is_kloc_pgcache_set(void);
int is_kloc_buffer_set(void);
int is_kloc_journ_set(void);
int is_kloc_radix_set(void);
int is_kloc_kernel_set(void);
int is_kloc_pgtbl_set(void);
int is_kloc_exit(struct task_struct *task);
int is_kloc_obj(void *obj);
int is_kloc_cacheobj(void *obj);
int is_kloc_vma(struct vm_area_struct *);
int is_kloc_page(struct page *page, int nodeid);
int is_kloc_pgcache_readahead_set(void);

int get_fastmem_node(void);
int get_slowmem_node(void);

void set_curr_kloc_obj(void *obj);
void set_fsmap_kloc_obj(void *mapobj);
void  set_inode_kloc_obj(struct inode *inode);

void set_kloc_obj_page(struct page *page, void *obj);
void debug_kloc_obj(void *obj);
void print_kloc_stats(struct task_struct *task);
void incr_global_stats(unsigned long *counter);
void set_sock_kloc_obj(void *socket, void *inode);
int kloc_init_rbtree(struct task_struct *task);
void set_sock_kloc_obj_netdev(void *socket, void *inode);


int kloc_check_page(struct mm_struct *mm, struct page *page);
void kloc_update_pgcache(int node, struct page *, int delpage);
void kloc_update_pgbuff_stat(int nodeid, struct page *page, int delpage);
void kloc_try_migration(void *map, gfp_t gfp_mask, int iskernel);

/* rbtree */
int 
kloc_insert_cpage_rbtree(struct task_struct *task, struct page *page);
int 
kloc_insert_kpage_rbtree(struct task_struct *task, struct page *page);
void kloc_erase_cpage_rbtree(struct task_struct *task, struct page *page);
void kloc_erase_kpage_rbtree(struct task_struct *task, struct page *page);
struct page *kloc_search_pg_rbtree(struct task_struct *task, 
				     struct page *page);


/* Erase full rbtree */
void kloc_erase_cache_rbree(struct task_struct *task);
void kloc_erase_kbuff_rbree(struct task_struct *task);
int kloc_reset_rbtree(struct task_struct *task);
int migrate_pages_slowmem(struct task_struct *task);

void
kloc_replace_cache(gfp_t gfp_mask, struct page *oldpage);

void kloc_delete_inode(struct inode *inode);
void kloc_inactive_inode(struct inode *inode);

void insert_page_cache_rbtree(struct page *page, int is_pagecache);
void remove_page_cache_rbtree(struct page *page, int is_pagecache);


/* ktree related functions */
int insert_kaddr_rbtree(struct page *page, void *kaddr);
struct kloc_rbnode *kloc_rb_search_kaddr(struct rb_root *root, void *kaddr);
void remove_kaddr_rbtree(struct page *page, void *kaddr);
void kloc_vfree_page(void *addr);
void *kloc_kmalloc_wrap(unsigned int size, void *kloc_obj);

#ifdef CONFIG_KLOC_STATS
void incr_tot_cache_pages(void);
void incr_tot_buff_pages(void);
void incr_tot_app_pages(void);
void incr_tot_vmalloc_pages(void);
#endif

inline int is_kloc_inode(void);
inline int is_kloc_buffhead(void);
inline int is_kloc_trans(void);
inline int is_kloc_dcache(void);
inline int is_kloc_bio(void);
inline int is_kloc_sockbuff(void);

void print_kloc_deadlock(void);

#endif /* _LINUX_NUMA_H */


