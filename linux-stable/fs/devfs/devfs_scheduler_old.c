/*
 * devfs_scheduler.c
 *
 * Description: Scheduler related functions
 *
 */
#include <linux/fs.h>
#include <linux/devfs.h>
#include <linux/file.h>
#include <linux/iommu.h>
#include <linux/module.h>
#include <linux/nvme.h>
#include <linux/mm.h>

int devfs_device_thread_nr;
struct dev_thread_struct devfs_device_thread[DEVICE_THREAD_MAX];

// Initialize scheduler
int devfs_scheduler_init(int nr) {
	int retval = 0, i = 0;

	/* init in-device kthread handling I/O request */
	if (nr > DEVICE_THREAD_MAX) {
		printk(KERN_ALERT "DEBUG: device thread limit exceed! | %s:%d",
			__FUNCTION__, __LINE__);
	}
	devfs_device_thread_nr = nr;

	/* initialize device thread structure */
	for (i = 0; i < nr; ++i) {

		/* initialize file pointer queue list */
		INIT_LIST_HEAD(&devfs_device_thread[i].rd_list);

		/* initialize state */
		devfs_device_thread[i].state = 0;

		/* initialize spin lock */
		__SPIN_LOCK_UNLOCKED(&devfs_device_thread[i].lock);

		/* create kthread to simulate device thread */
		devfs_device_thread[i].kthrd = kthread_run(devfs_io_scheduler, &devfs_device_thread[i], "kdevio");
		if (IS_ERR(devfs_device_thread[i].kthrd)) {
			printk(KERN_ALERT "%s:%d Failed kthread create \n",
			__FUNCTION__,__LINE__);
			retval = -EFAULT;
			goto err_scheduler_init;
		}

		//printk(KERN_ALERT "##### kthread has started\n");
	}

err_scheduler_init:
	return retval;
}


// Clean per-file-pointer queue buffer, flush to storage
void devfs_scheduler_flush_buffer(struct devfs_fstruct *rd) {
	rd->kthrd_done = 1; 

	// wait for device thread to flush buffer to storage */
#ifdef _DEVFS_KTHRD_SEMAPHORE
	down_interruptible(&rd->ksem);
#else
	while(test_and_clear_bit(0, &rd->io_done) == 0);
#endif

#ifdef _DEVFS_SCALABILITY_DBG
    printk(KERN_ALERT "all IO has been done\n");
#endif
}
EXPORT_SYMBOL(devfs_scheduler_flush_buffer);


// Setup per-file-pointer scalability data structures when a file is opened
int devfs_scheduler_open_setup(struct devfs_inode *ei, struct devfs_fstruct *rd) {
	int retval = 0;

	/* init per file pointer queue */
	if (ei->rd_nr >= MAX_FP_QSIZE) {
		printk(KERN_ALERT "DEBUG: per file pointer queue limit excced | %s:%d",
			__FUNCTION__, __LINE__);
		retval = -EFAULT;
		goto err_open_setup;
	}

	ei->per_rd_queue[ei->rd_nr++] = rd;

	/* init submission queue radix tree */
	//FIXME fix here later
	if (ei->sq_tree_init != 1) {
		INIT_RADIX_TREE(&ei->sq_tree, GFP_ATOMIC);
		ei->sq_tree_init = 1;
	}

	rd->kthrd_done = 0;

	/* init io return bytes*/
	rd->iobytes = 0;

	/* init kthread semaphore */
	sema_init(&rd->ksem, 0);
	rd->io_done = 0;

	/* init rd state */
	rd->state = 0;

	//TODO
	// instead of creating kthread here
	// add rd to a device kthread list
	// should using rcu lock here
	int dev_thread_idx = rd->fd % devfs_device_thread_nr;
	struct dev_thread_struct *dev_thread = &devfs_device_thread[dev_thread_idx];

#ifdef _DEVFS_SCALABILITY_DBG
	printk(KERN_ALERT "######### &dev_thread->rd_list = %lx\n", &dev_thread->rd_list);
#endif

	INIT_LIST_HEAD(&rd->list);
	
	spin_lock(&dev_thread->lock);
	//list_add(&rd->list, &dev_thread->rd_list);
	list_add_rcu(&rd->list, &dev_thread->rd_list);
	spin_unlock(&dev_thread->lock);

	if (dev_thread->rd_list.next == &dev_thread->rd_list) {
		printk(KERN_ALERT "DEBUG: insert per-fp queue failed | %s:%d",
			__FUNCTION__, __LINE__);
		retval = -EFAULT;
		goto err_open_setup;
	}

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
	printk(KERN_ALERT "rd = %lx\n", rd);
#endif

err_open_setup:
	return retval;

}
EXPORT_SYMBOL(devfs_scheduler_open_setup);

// Setup per-file-pointer scalability data structures when a file is closed
void devfs_scheduler_close_setup(struct devfs_inode *ei, struct devfs_fstruct *rd) {

	int dev_thread_idx = rd->fd % devfs_device_thread_nr;
	struct dev_thread_struct *dev_thread = &devfs_device_thread[dev_thread_idx];

	/* Remove current file pointer from the file pointer list
	 * Multiple host threads are closing concurrently,
	 * so need spin_lock here */
#ifdef _DEVFS_SCALABILITY_DBG
	printk(KERN_ALERT "@@@@@@@ about to delete list!\n");
#endif
	spin_lock(&dev_thread->lock);
	//list_del(&rd->list);
	list_del_rcu(&rd->list);
	spin_unlock(&dev_thread->lock);

    /* decrement inode rd count */
	if (ei->rd_nr > 0) 
		ei->rd_nr--;

#ifdef _DEVFS_READ_DOUBLECOPY
	/* deallocate per file pointer kernel buffer */
	if (rd->kbuf) {
		kfree(rd->kbuf);
		rd->kbuf = NULL;
	}
#endif

}
EXPORT_SYMBOL(devfs_scheduler_close_setup);


/***************************************************************
 *******************	 Scheduler Code    ********************
 **************************************************************/

// Pick next available queue to schedule
/*static struct devfs_fstruct pick_next() {
	struct devfs_fstruct *rd = NULL;

	rcu_read_lock();
	list_for_each_entry_rcu(rd, &g_rd_list, list) {
		if (test_and_set_bit(0, rd->state) == 1)
			continue;
		return rd; 
	}
	rcu_read_unlock();

	return rd;
}*/


// In-device kthread I/O handler
int devfs_io_scheduler(struct dev_thread_struct *dev_thread_ctx) {
	struct devfs_fstruct *rd = NULL;
	void *ptr = NULL;
	nvme_cmdrw_t *cmdrw = NULL;
	int retval = 0;
	int read_fin = 0;
	int write_fin = 0;

#ifdef _DEVFS_OVERHEAD_KTHRD
	struct timespec start;
	struct timespec end;
#endif

#ifdef _DEVFS_SCALABILITY_DBG
	printk(KERN_ALERT "######### &dev_thread_ctx->rd_list = %lx\n", &dev_thread_ctx->rd_list);
#endif

	while (1) {
		/*now do a round-robin way to schedule among queues */
		rcu_read_lock();
		list_for_each_entry_rcu(rd, &dev_thread_ctx->rd_list, list) {
#ifdef _DEVFS_SCALABILITY_DBG
			printk(KERN_ALERT "rd = %llx\n", rd);
#endif

			if (!rd || IS_ERR(rd))
				continue;

#ifdef _DEVFS_SCALABILITY_DBG
			printk(KERN_ALERT "rd = %llx | resuming kthread\n", rd);

			printk(KERN_ALERT "head=%d, tail=%d, entry=%d\n", 
				rd->fifo.head, rd->fifo.tail, rd->num_entries);

			printk(KERN_ALERT "rd->kthrd_done = %d\n", rd->kthrd_done);
#endif

			/* If host thread is closing this file descriptor */
			if (rd->kthrd_done == 1) {
				/* flush all the buffer to storage */
				vfio_devfs_io_write(rd);
				
				/* remove itself from rd_list */

				rd->kthrd_done = 0;
				rd->req = NULL;

				/* notify host */
#ifdef _DEVFS_KTHRD_SEMAPHORE
				up(&rd->ksem);
#else
				test_and_set_bit(0, &rd->io_done);
#endif
				continue;
			}

#ifdef _DEVFS_SCALABILITY_DBG
			printk(KERN_ALERT "rd = %llx | head=%d, tail=%d, entry=%d\n", 
				(__u64)rd, rd->fifo.head, rd->fifo.tail, rd->num_entries);
#endif

			/* Set credential */
			devfs_set_cred(rd);

			//ptr = rd_read_tail(rd, sizeof(nvme_cmdrw_t), &rd->fifo.tail);
			//ptr = rd->fifo.buf[rd->fifo.tail]; 
			cmdrw = rd->req;

			/* Checking queue empty */
			if (cmdrw == NULL) {
				//printk(KERN_ALERT "NULL IO cmd pointer | %s:%d", __FUNCTION__, __LINE__);
				continue;
			}

			if ( cmdrw->common.opc == nvme_cmd_read ) {

#ifdef _DEVFS_OVERHEAD_KTHRD
				getnstimeofday(&start);
#endif
				retval = vfio_devfs_io_read(rd, cmdrw, 1);

				rd->iobytes = retval;

				rd->req = NULL;

				/* Notifying host thread read has finished */
#ifdef _DEVFS_KTHRD_SEMAPHORE
				up(&rd->ksem);
#else
				test_and_set_bit(0, &rd->io_done);
#endif

#ifdef _DEVFS_OVERHEAD_KTHRD
				getnstimeofday(&end);
				rd->t_devfs_io_handler += ((end.tv_sec*1000000000 + end.tv_nsec) -
					(start.tv_sec*1000000000 + start.tv_nsec));
#endif

			} else if ( cmdrw->common.opc == nvme_cmd_write ) {
				retval = vfio_devfs_io_write(rd);

				rd->iobytes = retval;

				rd->req = NULL;
	
				/* notify host */
#ifdef _DEVFS_KTHRD_SEMAPHORE
				up(&rd->ksem);
#else
				test_and_set_bit(0, &rd->io_done);
#endif

			} else if (	cmdrw->common.opc == nvme_cmd_append ) {
				retval = vfio_devfs_io_append(rd);

				rd->iobytes = retval;

#ifdef _DEVFS_SCALABILITY_DBG
				printk(KERN_ALERT "after, rd = %llx | head=%d, tail=%d, entry=%d\n", 
					(__u64)rd, rd->fifo.head, rd->fifo.tail, rd->num_entries);
#endif
				rd->req = NULL;

				/* notify host */
#ifdef _DEVFS_KTHRD_SEMAPHORE
				up(&rd->ksem);
#else
				test_and_set_bit(0, &rd->io_done);
#endif

			} else if ( cmdrw->common.opc == nvme_cmd_lseek ) {
				// TODO
				//retval =  (long) devfs_llseek (rd,(void *)cmdrw);
			} else {
				//printk(KERN_ALERT "%s:%d Invalid command op code\n",
				//		__FUNCTION__,__LINE__);
				// TODO
				//printk(KERN_ALERT "head=%d, tail=%d, entry=%d\n", 
				//	rd->fifo.head, rd->fifo.tail, rd->num_entries);
			}
		}
		rcu_read_unlock();
	}
kthrd_out:
	return retval; 
}
EXPORT_SYMBOL(devfs_io_scheduler);


