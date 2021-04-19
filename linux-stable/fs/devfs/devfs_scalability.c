/*
 * devfs_scalability.c
 *
 * Description: Scalability related functions
 *
 */
#include <linux/fs.h>
#include <linux/devfs.h>
#include <linux/file.h>
#include <linux/iommu.h>
#include <linux/module.h>
#include <linux/nvme.h>
#include <linux/mm.h>


/* 
 * Clean per-file-pointer queue buffer, flush to storage
 *
 * Called when a host thread is closing this file pointer
 */
void devfs_scalability_flush_buffer(struct devfs_fstruct *rd) {
	rd->closed = 1; 

	// wait for device thread to flush buffer to storage */
#ifdef _DEVFS_KTHRD_SEMAPHORE
	down_interruptible(&rd->ksem);
#else
	while(test_and_clear_bit(0, &rd->io_done) == 0);
#endif

#ifdef _DEVFS_SCALABILITY_DBG
    devfs_dbg("all IO has been done\n");
#endif
}
EXPORT_SYMBOL(devfs_scalability_flush_buffer);


// Setup per-file-pointer scalability data structures when a file is opened
int devfs_scalability_open_setup(struct devfs_inode *ei, struct devfs_fstruct *rd) {
	int retval = 0;

	/* init submission queue radix tree */
	//FIXME fix here later
	if (ei->sq_tree_init != 1) {
		rwlock_init(&ei->i_meta_lock);
		INIT_RADIX_TREE(&ei->sq_tree, GFP_ATOMIC);
		ei->sq_tree_init = 1;
	}

	/* init per file pointer queue */
	read_lock(&ei->i_meta_lock);
	if (ei->rd_nr >= MAX_FP_QSIZE) {
		printk(KERN_ALERT "DEBUG: per file pointer queue limit excced | %s:%d",
			__FUNCTION__, __LINE__);
		retval = -EFAULT;
		read_unlock(&ei->i_meta_lock);
		goto err_open_setup;
	}
	read_unlock(&ei->i_meta_lock);

	write_lock(&ei->i_meta_lock);
	ei->per_rd_queue[ei->rd_nr++] = rd;
	write_unlock(&ei->i_meta_lock);


	/* init submission queue radix tree lock */
	__SPIN_LOCK_UNLOCKED(ei->sq_tree_lock);

	/* init rd close flag */
	rd->closed = 0;

	/* init io return bytes */
	rd->iobytes = 0;

	/* init kthread semaphore */
	sema_init(&rd->ksem, 0);
	rd->io_done = 0;

	/* init rd state */
	rd->state = DEVFS_RD_IDLE;

	/* initialize list structure of this rd */
	INIT_LIST_HEAD(&rd->list);

	/* initialize cmd queue pointer */
	rd->req = NULL;

	/* initialize fsyncing flag */
	rd->fsyncing = 0;

	/* 
	 * Add this rd to the global rd linked list
	 * The reason we need spin lock is that
	 * there could be multiple host threads tries to insert
	 * its rd struct to the global rd linked list
	 * at the same time
	 */
	//spin_lock(&g_rd_list_lock);
	mutex_lock(&g_mutex);
#ifdef _DEVFS_SCHEDULER_RCU
	list_add_rcu(&rd->list, g_rd_list);
	//list_add_tail_rcu(&rd->list, g_rd_list);
#else
	list_add(&rd->list, g_rd_list);
#endif
	/* Check if this rd is added successfully or not */
	if (g_rd_list->next != &rd->list) {
	//if (g_rd_list != rd->list.next) {
		printk(KERN_ALERT "DEBUG: insert per-fp queue failed | %s:%d",
			__FUNCTION__, __LINE__);
		retval = -EFAULT;
		//spin_unlock(&g_rd_list_lock);
		mutex_unlock(&g_mutex);
		goto err_open_setup;
	}
	//spin_unlock(&g_rd_list_lock);
	mutex_unlock(&g_mutex);

#ifdef _DEVFS_OVERHEAD_BREAKDOWN
	rd->t_read = 0;
	rd->t_double_copy = 0;
	rd->t_vfio_submitio_cmd = 0;
	rd->t_devfs_io_handler = 0;
	rd->read_op_count = 0;
#endif

#ifdef _DEVFS_READ_DOUBLECOPY
	rd->kbuf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!rd->kbuf) {
		printk(KERN_ALERT "%s:%d Failed per file pointer kernel buffer \n",
				__FUNCTION__,__LINE__);
		retval = -EFAULT;
		goto err_open_setup;
	}
#endif

#ifdef _DEVFS_SCALABILITY_DBG
	devfs_dbg("after setup, rd = %llx\n", rd);
#endif

err_open_setup:
	return retval;

}
EXPORT_SYMBOL(devfs_scalability_open_setup);

// Setup per-file-pointer scalability data structures when a file is closed
void devfs_scalability_close_setup(struct devfs_inode *ei, struct devfs_fstruct *rd) {

#ifdef _DEVFS_SCALABILITY_DBG
	devfs_dbg("About to delete list!\n");
#endif

	/* 
	 * Remove current file pointer from the file pointer list
	 * Multiple host threads are closing concurrently,
	 * so need spin_lock here */
	//spin_lock(&g_rd_list_lock);
	//list_del_rcu(&rd->list);
	//spin_unlock(&g_rd_list_lock);

    /* decrement inode rd count */
	write_lock(&ei->i_meta_lock);
	if (ei->rd_nr > 0) 
		--ei->rd_nr;
	write_unlock(&ei->i_meta_lock);

#ifdef _DEVFS_READ_DOUBLECOPY
	/* deallocate per file pointer kernel buffer */
	if (rd->kbuf) {
		kfree(rd->kbuf);
		rd->kbuf = NULL;
	}
#endif

}
EXPORT_SYMBOL(devfs_scalability_close_setup);

