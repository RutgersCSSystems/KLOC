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


// Clean per-file-pointer queue buffer, flush to storage
void devfs_scalability_flush_buffer(struct devfs_fstruct *rd) {
	if (rd->num_entries > 0) {
#ifdef _DEVFS_SCALABILITY_DBG
		//printk(KERN_ALERT "rd->sem_empty = %d, rd->sem_full = %d\n", rd->sem_empty.count, rd->sem_full.count);
#endif

#if defined(_DEVFS_KTHRD_SEMAPHORE)
		up(&rd->sem_empty);
		down_interruptible(&rd->sem_full);
#elif defined(_DEVFS_KTHRD_WAKEUP)
		test_and_set_bit(0, &rd->come);
		wake_up_process(rd->kthrd);
		//up(&rd->ksem);
		while(test_and_clear_bit(0, &rd->write_done) == 0); 
#else
		test_and_set_bit(0, &rd->come);
		while(test_and_clear_bit(0, &rd->write_done) == 0); 
#endif
    }

#ifdef _DEVFS_SCALABILITY_DBG
    printk(KERN_ALERT "all IO has been done\n");
#endif
}
EXPORT_SYMBOL(devfs_scalability_flush_buffer);


// Setup per-file-pointer scalability data structures when a file is opened
int devfs_scalability_open_setup(struct devfs_inode *ei, struct devfs_fstruct *rd) {
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
	rd->read_done = 0;

	/* init io return bytes*/
	rd->iobytes = 0;

#ifdef _DEVFS_KTHRD_SEMAPHORE
	/* init kthread semaphore */
	sema_init(&rd->sem_empty, 0);
	sema_init(&rd->sem_full, 0);
#else
	rd->write_done = 0;
	rd->come = 0;
	rd->kthrd_fin = 0;
	rd->kthrd_rdy = 0;

	sema_init(&rd->ksem, 0);
#endif

	/* init in-device kthread handling I/O req */
	rd->kthrd = kthread_run(devfs_io_handler, rd, "kdevio");
	if (IS_ERR(rd->kthrd)) {
		printk(KERN_ALERT "%s:%d Failed kthread create \n",
		__FUNCTION__,__LINE__);
		retval = -EFAULT;
		goto err_open_setup;
	}

	//printk(KERN_ALERT "##### kthread has started\n");
    
#ifdef _DEVFS_KTHRD_SEMAPHORE
	/* wait for kthread to be waken up */
	down_interruptible(&rd->sem_full);
#else
	while (test_and_clear_bit(0, &rd->kthrd_rdy) == 0);
#endif

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

	//printk(KERN_ALERT "##### should reach here\n");

err_open_setup:
	return retval;

}
EXPORT_SYMBOL(devfs_scalability_open_setup);

// Setup per-file-pointer scalability data structures when a file is closed
void devfs_scalability_close_setup(struct devfs_inode *ei, struct devfs_fstruct *rd) {
	/* Wake up per file pointer kthread to finish*/
	// FIXME bug here

#if defined(_DEVFS_KTHRD_SEMAPHORE)
	rd->kthrd_done = 1; 

	up(&rd->sem_empty); 
	down_interruptible(&rd->sem_full);
#elif defined(_DEVFS_KTHRD_WAKEUP)
	test_and_set_bit(0, &rd->kthrd_done);
	test_and_set_bit(0, &rd->come);
	wake_up_process(rd->kthrd);
	//up(&rd->ksem);
	while(test_and_clear_bit(0, &rd->kthrd_fin) == 0);
#else
	test_and_set_bit(0, &rd->kthrd_done);
	test_and_set_bit(0, &rd->come);
	while(test_and_clear_bit(0, &rd->kthrd_fin) == 0);
#endif

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
EXPORT_SYMBOL(devfs_scalability_close_setup);



// In-device kthread I/O handler
int devfs_io_handler(struct devfs_fstruct *rd) {
	void *ptr = NULL;
	nvme_cmdrw_t *cmdrw = NULL;
	int retval = 0;
	int read_fin = 0;
	int write_fin = 0;

#ifdef _DEVFS_OVERHEAD_KTHRD
	struct timespec start;
	struct timespec end;
#endif

	while (1) {
		/*if (kthread_should_stop())
			break;*/

#ifdef _DEVFS_SCALABILITY_DBG
		printk(KERN_ALERT "rd = %llx | entering kthread\n", rd);
#endif
			
#if defined(_DEVFS_KTHRD_SEMAPHORE)
		/* If no request comes, using semaphore to wake up host thread
		 * and block kthread itself
		 */
		up(&rd->sem_full);
		down_interruptible(&rd->sem_empty);
#elif defined(_DEVFS_KTHRD_WAKEUP)
		/* If no request comes, using schedule() to block kthread itself */
		test_and_set_bit(0, &rd->kthrd_rdy);
		
		set_current_state(TASK_INTERRUPTIBLE);
		if (test_and_clear_bit(0, &rd->come) == 0)
			schedule();
		set_current_state(TASK_RUNNING);
		test_and_clear_bit(0, &rd->come);

		//down_interruptible(&rd->ksem);
#else
		/* If no request comes, spinning */
		test_and_set_bit(0, &rd->kthrd_rdy);
		while(test_and_clear_bit(0, &rd->come) == 0);
#endif
			
#ifdef _DEVFS_SCALABILITY_DBG
		printk(KERN_ALERT "rd = %llx | resuming kthread\n", rd);

		printk(KERN_ALERT "head=%d, tail=%d, entry=%d\n", 
			rd->fifo.head, rd->fifo.tail, rd->num_entries);
#endif

		/* If host thread is closing this file descriptor */
#if defined(_DEVFS_KTHRD_SEMAPHORE)
		if (rd->kthrd_done == 1)
			goto kthrd_out;
#else
		if (test_and_clear_bit(0, &rd->kthrd_done) == 1)
			goto kthrd_out;
#endif
	

#ifdef _DEVFS_SCALABILITY_DBG
		printk(KERN_ALERT "rd = %llx | head=%d, tail=%d, entry=%d\n", 
			(__u64)rd, rd->fifo.head, rd->fifo.tail, rd->num_entries);
#endif

		/* Set credential */
		devfs_set_cred(rd);

		ptr = rd_read_tail(rd, sizeof(nvme_cmdrw_t), &rd->fifo.tail);
		//ptr = rd->fifo.buf[rd->fifo.tail]; 
		cmdrw = (nvme_cmdrw_t *)ptr;

		/* Checking queue empty */
		if (cmdrw == NULL) {
			printk(KERN_ALERT "NULL IO cmd pointer | %s:%d", __FUNCTION__, __LINE__);
			break;
		}

		if ( cmdrw->common.opc == nvme_cmd_read ) {

#ifdef _DEVFS_OVERHEAD_KTHRD
			getnstimeofday(&start);
#endif

			retval = vfio_devfs_io_read(rd, cmdrw, 1);

			/* Notifying host thread read has finished */
#if defined(_DEVFS_KTHRD_SEMAPHORE)
			rd->read_done = 1;
#else
			test_and_set_bit(0, &rd->read_done);
#endif

#ifdef _DEVFS_OVERHEAD_KTHRD
			getnstimeofday(&end);
			rd->t_devfs_io_handler += ((end.tv_sec*1000000000 + end.tv_nsec) -
				(start.tv_sec*1000000000 + start.tv_nsec));
#endif

		} else if ( cmdrw->common.opc == nvme_cmd_write ) {
			retval = vfio_devfs_io_write(rd);

			/* Notifying host thread write has finished */
#if ! defined(_DEVFS_KTHRD_SEMAPHORE)
			test_and_set_bit(0, &rd->write_done);
#endif
		} else if (	cmdrw->common.opc == nvme_cmd_append ) {
			retval = vfio_devfs_io_append(rd);

			/* Notifying host thread write has finished */
#if ! defined(_DEVFS_KTHRD_SEMAPHORE)
			test_and_set_bit(0, &rd->write_done);
#endif

#ifdef _DEVFS_SCALABILITY_DBG
			printk(KERN_ALERT "after, rd = %llx | head=%d, tail=%d, entry=%d\n", 
				(__u64)rd, rd->fifo.head, rd->fifo.tail, rd->num_entries);
#endif

		} else if ( cmdrw->common.opc == nvme_cmd_lseek ) {
			// TODO
			//retval =  (long) devfs_llseek (rd,(void *)cmdrw);
		} else {
			printk(KERN_ALERT "%s:%d Invalid command op code\n",
					__FUNCTION__,__LINE__);
			// TODO
			printk(KERN_ALERT "head=%d, tail=%d, entry=%d\n", 
				rd->fifo.head, rd->fifo.tail, rd->num_entries);
			break;

		}
		rd->iobytes = retval;
	}
kthrd_out:
#if defined(_DEVFS_KTHRD_SEMAPHORE)
	up(&rd->sem_full);
#else
	//printk(KERN_ALERT "######### should arrive here! | rd = %llx\n", rd);
	test_and_set_bit(0, &rd->kthrd_fin);
#endif

	return retval; 
}
EXPORT_SYMBOL(devfs_io_handler);

