/*
 * change log:
 * alloc_pages_nvram() -> alloc_pages()
 */

#include <linux/compat.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/devfs.h>
#include <linux/devfs_def.h>

#include <linux/file.h>
#include <linux/iommu.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/rbtree.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/vfio.h>
#include <linux/workqueue.h>
#include <linux/circ_buf.h>
#include <linux/nvme.h>
#include <linux/syscalls.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/swap.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/backing-dev.h>
#include <linux/ramfs.h>
#include <linux/sched.h>
#include <linux/parser.h>
#include <linux/magic.h>
#include <linux/fsnotify.h>
#include <linux/mount.h>
#include <linux/kernel.h>
#include <linux/buffer_head.h>
#include <linux/namei.h>

extern void __iomem *ioremap_hpage_cache(resource_size_t phys_addr, unsigned long size);

struct page *devfs_alloc_page(gfp_t gfp, int order, int node)
{
        struct page *page;

        //page = alloc_pages_nvram(node, gfp, 0);
	page = alloc_pages(gfp, 0);
        if(!page) {
                goto pagemisses;
        }

        clear_user_highpage(page,0);
        //page->nvdirty = PAGE_MIGRATED;

        //hetero_add_to_nvlist_mm(page);
        return page;

pagemisses:
        return NULL;
}
EXPORT_SYMBOL(devfs_alloc_page);


void devfs_free_pages(struct page *page) {
	
	if(!page) {
		printk(KERN_ALERT "Failed %s:%d \n",__FUNCTION__,__LINE__);
		return;
	}
	__free_page(page);
}
EXPORT_SYMBOL(devfs_free_pages);


void *devfs_ioremap(struct super_block *sb, phys_addr_t phys_addr, 
			ssize_t size, const char* cachetype)
{
        void *retval;
        int protect, hugeioremap;

        if (sb) {
                protect = 0;
                hugeioremap = 0;
        } else {
                protect = 0;
                hugeioremap = 0;
        }
        /*
         * NOTE: Userland may not map this resource, we will mark the region so
         * /dev/mem and the sysfs MMIO access will not be allowed. This
         * restriction depends on STRICT_DEVMEM option. If this option is
         * disabled or not available we mark the region only as busy.
         */
        retval = request_mem_region_exclusive(phys_addr, size, cachetype);
        if (!retval) {
                printk(KERN_ALERT "FAILED %s:%d \n",__FUNCTION__,__LINE__);
                goto fail;
        }

        if (hugeioremap)
                retval = ioremap_hpage_cache(phys_addr, size);
        else
                retval = ioremap_cache(phys_addr, size);
fail:
        return retval;
}






#if 0

static struct kmem_cache *devfs_dentry_cache __read_mostly;
static DEFINE_PER_CPU(unsigned int, nr_dentry);

/* This must be called with d_lock held */
static inline void __d_devfs_get_dlock(struct dentry *dentry)
{
	dentry->d_count++;
}

static inline void __d_devfs_get(struct dentry *dentry)
{
	spin_lock(&dentry->d_lock);
	__d_devfs_get_dlock(dentry);
	spin_unlock(&dentry->d_lock);
}

/**
 * __d_alloc	-	allocate a dcache entry
 * @sb: filesystem it will belong to
 * @name: qstr of the name
 *
 * Allocates a dentry. It returns %NULL if there is insufficient memory
 * available. On a success the dentry is returned. The name passed in is
 * copied and the copy passed in may be reused after this call.
 */
 
struct dentry *__d_devfs_alloc(struct super_block *sb, const struct qstr *name)
{
	struct dentry *dentry;
	char *dname;

	dentry = kmem_cache_alloc(devfs_dentry_cache, GFP_KERNEL);
	if (!dentry)
		return NULL;

	/*
	 * We guarantee that the inline name is always NUL-terminated.
	 * This way the memcpy() done by the name switching in rename
	 * will still always have a NUL at the end, even if we might
	 * be overwriting an internal NUL character
	 */
	dentry->d_iname[DNAME_INLINE_LEN-1] = 0;
	if (name->len > DNAME_INLINE_LEN-1) {
		dname = kmalloc(name->len + 1, GFP_KERNEL);
		if (!dname) {
			kmem_cache_free(devfs_dentry_cache, dentry); 
			return NULL;
		}
	} else  {
		dname = dentry->d_iname;
	}	

	dentry->d_name.len = name->len;
	dentry->d_name.hash = name->hash;
	memcpy(dname, name->name, name->len);
	dname[name->len] = 0;

	/* Make sure we always see the terminating NUL character */
	smp_wmb();
	dentry->d_name.name = dname;

	dentry->d_count = 1;
	dentry->d_flags = 0;
	spin_lock_init(&dentry->d_lock);
	seqcount_init(&dentry->d_seq);
	dentry->d_inode = NULL;
	dentry->d_parent = dentry;
	dentry->d_sb = sb;
	dentry->d_op = NULL;
	dentry->d_fsdata = NULL;
	INIT_HLIST_BL_NODE(&dentry->d_hash);
	INIT_LIST_HEAD(&dentry->d_lru);
	INIT_LIST_HEAD(&dentry->d_subdirs);
	INIT_HLIST_NODE(&dentry->d_alias);
	INIT_LIST_HEAD(&dentry->d_u.d_child);
	//d_set_d_op(dentry, dentry->d_sb->s_d_op);
	this_cpu_inc(nr_dentry);

	return dentry;
}

/**
 * d_alloc	-	allocate a dcache entry
 * @parent: parent of entry to allocate
 * @name: qstr of the name
 *
 * Allocates a dentry. It returns %NULL if there is insufficient memory
 * available. On a success the dentry is returned. The name passed in is
 * copied and the copy passed in may be reused after this call.
 */
struct dentry *d_devfs_alloc(struct dentry * parent, const struct qstr *name)
{
	struct dentry *dentry = __d_devfs_alloc(parent->d_sb, name);
	if (!dentry)
		return NULL;

	spin_lock(&parent->d_lock);
	/*
	 * don't need child lock because it is not subject
	 * to concurrency here
	 */
	__d_devfs_get_dlock(parent);
	dentry->d_parent = parent;
	list_add(&dentry->d_u.d_child, &parent->d_subdirs);
	spin_unlock(&parent->d_lock);

	return dentry;
}
EXPORT_SYMBOL(d_devfs_alloc);

static void __init dcache_init(void)
{
	unsigned int loop;
	/* 
	 * A constructor could be added for stable state like the lists,
	 * but it is probably not worth it because of the cache nature
	 * of the dcache. 
	 */
	devfs_dentry_cache = KMEM_CACHE(dentry,
		SLAB_RECLAIM_ACCOUNT|SLAB_PANIC|SLAB_MEM_SPREAD);

}
#endif
