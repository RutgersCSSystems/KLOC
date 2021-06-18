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

#define READ_TIMEOUT	500		// 500us 
#define WRITE_TIMEOUT	5000	// 5000us

#define DEVFS_SCHED_ROUND_ROBIN	0
#define DEVFS_SCHED_READ_PRIO	1

#define DEV_THREAD_IDLE 0
#define DEV_THREAD_EXIT 1

/* Global Variable Definitions */
int devfs_device_thread_nr;
int devfs_scheduler_policy;
struct dev_thread_struct devfs_device_thread[DEVICE_THREAD_MAX];
struct devfs_fstruct g_dummy_rd;
struct list_head *g_rd_list;
//spinlock_t g_rd_list_lock;
DEFINE_SPINLOCK(g_rd_list_lock);
struct mutex g_mutex;

/* 
 * Initialize scheduler
 *
 * Called when file system is mounted
 */
int devfs_scheduler_init(int nr, int policy) {
	int retval = 0, i = 0;

	/* init in-device kthread handling I/O request */
	if (nr > DEVICE_THREAD_MAX) {
		printk(KERN_ALERT "DEBUG: device thread limit exceed! | %s:%d",
			__FUNCTION__, __LINE__);
	}
	devfs_device_thread_nr = nr;

	/* initialize scheduling policy */
	devfs_scheduler_policy = policy;

	/* initialize global rd list spin lock */
	//__SPIN_LOCK_UNLOCKED(g_rd_list_lock);
	mutex_init(&g_mutex);

	/* initialize dummy rd */
	g_dummy_rd.state = DEVFS_RD_BUSY;
	g_dummy_rd.closed = 0;

	/* initialize file pointer queue list */
	g_rd_list = &g_dummy_rd.list;
	INIT_LIST_HEAD(g_rd_list);

	devfs_dbg("g_rd_list = %llx, dev core cnt = %d, policy = %d\n", g_rd_list, devfs_device_thread_nr, policy);

	/* set global scheduler init flag */
	g_devfs_scheduler_init = 1;

	/* initialize device thread structure */
	for (i = 0; i < nr; ++i) {

		/* initialize file pointer queue list */
		INIT_LIST_HEAD(&devfs_device_thread[i].rd_list);

		/* initialize state */
		devfs_device_thread[i].state = DEV_THREAD_IDLE;

		/* initialize spin lock */
		__SPIN_LOCK_UNLOCKED(&devfs_device_thread[i].lock);

		/* initialize current serving queue */
		devfs_device_thread[i].current_rd_list = NULL;

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

void devfs_scheduler_exit(void) {
	int i = 0;
	struct list_head *cur = g_rd_list->next;
	struct devfs_fstruct *rd = NULL;

	/* initialize device thread structure */
	for (i = 0; i < devfs_device_thread_nr; ++i) {

		/* change device thread state to be EXIT */
		devfs_device_thread[i].state = DEV_THREAD_EXIT;

		/* stop kthreads */
		kthread_stop(devfs_device_thread[i].kthrd);
	}

	/* free rd struct */
	while (cur && cur->next != g_rd_list) {
		rd = list_entry(cur, struct devfs_fstruct, list); 
		cur = cur->next;
		if (rd) {
			kfree(rd);
			rd = NULL;
		}
	}

	/* set global scheduler init flag */
	g_devfs_scheduler_init = 0;

	devfs_dbg("Terminating device threads...\n");
}


/***************************************************************
 *******************	 Scheduler Code    ********************
 **************************************************************/

/* Round-robin scheduling policy */
static struct devfs_fstruct* devfs_sched_round_robin(struct dev_thread_struct *dev_thread_ctx) {
	struct devfs_fstruct *rd = NULL;
	struct devfs_fstruct *target = NULL;
	//struct list_head *cur = NULL;
	nvme_cmdrw_t *cmdrw = NULL;

	if (!dev_thread_ctx) {
		printk(KERN_ALERT "%s:%d Device thread context is NULL \n",
			__FUNCTION__,__LINE__);
		return NULL;
	}

	/* 
	 * Case 1 (Fast Path):
	 * If current serving is not NULL, picking next available rd queue
	 * until reaching the end of global rd list
	 */
	if (dev_thread_ctx->current_rd_list && dev_thread_ctx->current_rd_list->next != g_rd_list) {
	//if (dev_thread_ctx->current_rd_list) {

		//printk(KERN_ALERT "current list = %llx, head = %llx\n", dev_thread_ctx->current_rd_list, &g_rd_list);

		rd = list_entry(dev_thread_ctx->current_rd_list, struct devfs_fstruct, list);

		if (!rd && IS_ERR(rd)) {
			//rcu_read_unlock();
			goto rr_slow_path;
		}

#ifdef _DEVFS_SCHEDULER_RCU
		rcu_read_lock();
		list_for_each_entry_continue_rcu(rd, g_rd_list, list) {
#else
		mutex_lock(&g_mutex);		
		list_for_each_entry_continue(rd, g_rd_list, list) {
#endif
			if (rd && !IS_ERR(rd)) {

				//printk(KERN_ALERT "get next rd = %llx\n", rd);

				/* If it is a read operation, then just schedule it */
				if (test_and_set_bit(0, &rd->state) == DEVFS_RD_IDLE) {
#ifdef _DEVFS_SCALABILITY_DBG
					devfs_dbg("get rd in fast path, rd = %llx\n", rd);
#endif
					dev_thread_ctx->current_rd_list = &rd->list;
#ifdef _DEVFS_SCHEDULER_RCU
					rcu_read_unlock();
#else
					mutex_unlock(&g_mutex);
#endif
					return rd;
				}
			}
		}
#ifdef _DEVFS_SCHEDULER_RCU
		rcu_read_unlock();
#else
		mutex_unlock(&g_mutex);
#endif

	}

rr_slow_path:
	/* 
     * Case 2 (Slow Path):
	 * If current serving is NULL, walk through the global rd list from beginning
	 */
#ifdef _DEVFS_SCHEDULER_RCU
	rcu_read_lock();
	list_for_each_entry_rcu(rd, g_rd_list, list) {
#else
	mutex_lock(&g_mutex);		
	list_for_each_entry(rd, g_rd_list, list) {
#endif

		if (rd && !IS_ERR(rd)) {
			
			/* If it is a read operation, then just schedule it */
			if (test_and_set_bit(0, &rd->state) == DEVFS_RD_IDLE) {
#ifdef _DEVFS_SCALABILITY_DBG
				devfs_dbg("get rd in slow path, rd = %llx\n", rd);
#endif
				dev_thread_ctx->current_rd_list = &rd->list;
#ifdef _DEVFS_SCHEDULER_RCU
				rcu_read_unlock();
#else
				mutex_unlock(&g_mutex);
#endif
				return rd;
			}
		} 
	}
#ifdef _DEVFS_SCHEDULER_RCU
	rcu_read_unlock();
#else
	mutex_unlock(&g_mutex);
#endif

	return NULL;
}


/* Read prioritized scheduling policy */
static struct devfs_fstruct* devfs_sched_read_prioritized(struct dev_thread_struct *dev_thread_ctx) {
	struct devfs_fstruct *rd = NULL;
	struct devfs_fstruct *target = NULL;
	//struct list_head *cur = NULL;
	nvme_cmdrw_t *cmdrw = NULL;

	unsigned long cur_tsc = jiffies;
	unsigned long wait_time = 0;
	unsigned long longest = 0;

	if (!dev_thread_ctx) {
		printk(KERN_ALERT "%s:%d Device thread context is NULL \n",
			__FUNCTION__,__LINE__);
		return NULL;
	
	}
//#if 0
	/* 
	 * Case 1 (Fast Path):
	 * If current serving is not NULL, picking next available rd queue
	 * until reaching the end of global rd list
	 */
	if (dev_thread_ctx->current_rd_list && dev_thread_ctx->current_rd_list != g_rd_list) {

#ifdef _DEVFS_SCHEDULER_RCU
		rcu_read_lock();
		rd = list_entry_rcu(dev_thread_ctx->current_rd_list, struct devfs_fstruct, list);
		list_for_each_entry_continue_rcu(rd, g_rd_list, list) {
#else
		mutex_lock(&g_mutex);
		rd = list_entry(dev_thread_ctx->current_rd_list, struct devfs_fstruct, list);
		list_for_each_entry_continue(rd, g_rd_list, list) {
#endif
			if (rd && !IS_ERR(rd)) {
				/* 
				 * Check if the request is a read or write
				 * If it is a write operation that not yet reach the deadline
				 * (jiffies - rd->TSC < Threshold)
				 * Then just continue
				 * Otherwise, issue it */

				/* Only if rd is not scheduled by other device thread */
				if (rd->state == DEVFS_RD_IDLE) {
					if (rd->closed == 1) {
						/* This is a corner case
						 * If this rd (file pointer) is closed by host thread, then schedule it first */
						if (test_and_set_bit(0, &rd->state) == DEVFS_RD_IDLE) {
							dev_thread_ctx->current_rd_list = &rd->list;
#ifdef _DEVFS_SCHEDULER_RCU
							rcu_read_unlock();
#else
							mutex_unlock(&g_mutex);
#endif
							return rd;
						} else
							continue;
					}

					/* Fetch command from the head of rd request queue */
					cmdrw = rd->req;

					/* Checking queue empty */
					if (cmdrw == NULL) {
						//printk(KERN_ALERT "NULL IO cmd pointer | %s:%d", __FUNCTION__, __LINE__);
						continue;
					}

					/* 
					 * If it is a write(append) operation that not yet reach the deadline
					 * Then just continue, we could schedule it later
					 */
					if (cmdrw->common.opc == nvme_cmd_write ||
						cmdrw->common.opc == nvme_cmd_append ) {

						/* In case jiffie counter is overflown */
						if (time_after(cur_tsc, rd->tsc))	
							wait_time = jiffies_to_usecs(cur_tsc - rd->tsc);
						else
							wait_time = jiffies_to_usecs(ULONG_MAX - rd->tsc +  cur_tsc); 

						if (wait_time < WRITE_TIMEOUT) {
							if (wait_time > longest) {
								longest = wait_time;
								target = rd;
							}
							//target = rd;
							continue;
						}
					}	

					/*
					 * Reaching here, the request is either a read request,
					 * or a write request approaching deadline.
					 * Then schedule it.
					 */
					if (test_and_set_bit(0, &rd->state) == DEVFS_RD_IDLE) {
#ifdef _DEVFS_SCALABILITY_DBG
						devfs_dbg("get rd in pick_next, rd = %llx\n", rd);
#endif
						dev_thread_ctx->current_rd_list = &rd->list;
#ifdef _DEVFS_SCHEDULER_RCU
						rcu_read_unlock();
#else
						mutex_unlock(&g_mutex);
#endif
						return rd;
					}
				}
			}
		}
#ifdef _DEVFS_SCHEDULER_RCU
		rcu_read_unlock();
#else
		mutex_unlock(&g_mutex);
#endif
	}
//#endif

	/* 
     * Case 2 (Slow Path):
	 * If current serving is NULL, walk through the global rd list from beginning
	 */
#ifdef _DEVFS_SCHEDULER_RCU
	rcu_read_lock();
	list_for_each_entry_rcu(rd, g_rd_list, list) {
#else
	mutex_lock(&g_mutex);
	list_for_each_entry(rd, g_rd_list, list) {
#endif
		if (rd && !IS_ERR(rd)) {
			/* 
             * Check if the request is a read or write
			 * If it is a write operation that not yet reach the deadline
			 * (jiffies - rd->TSC < Threshold)
			 * Then just continue
			 * Otherwise, issue it */
			
			/* Only if rd is not scheduled by other device thread */
			if (rd->state == DEVFS_RD_IDLE) {
				if (rd->closed == 1) {
					/* This is a corner case
					 * If this rd (file pointer) is closed by host thread, then schedule it first */
					if (test_and_set_bit(0, &rd->state) == DEVFS_RD_IDLE) {
						dev_thread_ctx->current_rd_list = &rd->list;
#ifdef _DEVFS_SCHEDULER_RCU
						rcu_read_unlock();
#else
						mutex_unlock(&g_mutex);
#endif
						return rd;
					} else
						continue;
				}

				/* Fetch command from the head of rd request queue */
				cmdrw = rd->req;

				/* Checking queue empty */
				if (cmdrw == NULL) {
					//printk(KERN_ALERT "NULL IO cmd pointer | %s:%d", __FUNCTION__, __LINE__);
					continue;
				}

				/* 
				 * If it is a write(append) operation that not yet reach the deadline
				 * Then just continue, we could schedule it later
				 */
				if (cmdrw->common.opc == nvme_cmd_write ||
					cmdrw->common.opc == nvme_cmd_append ) {
			
					/* In case jiffie counter is overflown */
					if (time_after(cur_tsc, rd->tsc))	
						wait_time = jiffies_to_usecs(cur_tsc - rd->tsc);
					else
						wait_time = jiffies_to_usecs(ULONG_MAX - rd->tsc +  cur_tsc); 

					if (wait_time < WRITE_TIMEOUT) {
						if (wait_time > longest) {
							longest = wait_time;
							target = rd;
						}
						//target = rd;
						continue;
					}
				}	

				/*
				 * Reaching here, the request is either a read request,
				 * or a write request approaching deadline.
				 * Then schedule it.
				 */
				if (test_and_set_bit(0, &rd->state) == DEVFS_RD_IDLE) {
#ifdef _DEVFS_SCALABILITY_DBG
					devfs_dbg("get rd in pick_next, rd = %llx\n", rd);
#endif
					dev_thread_ctx->current_rd_list = &rd->list;
#ifdef _DEVFS_SCHEDULER_RCU
					rcu_read_unlock();
#else
					mutex_unlock(&g_mutex);
#endif
					return rd;
				}
			}
		} 
	}
#ifdef _DEVFS_SCHEDULER_RCU
	rcu_read_unlock();
#else
	mutex_unlock(&g_mutex);
#endif

pick_next_out:
	/* If there is no read request, pick a write request */
#ifdef _DEVFS_SCALABILITY_DBG
	printk(KERN_ALERT "target = %llx\n", target);
#endif 
	if (target && test_and_set_bit(0, &target->state) == DEVFS_RD_IDLE) {
#ifdef _DEVFS_SCALABILITY_DBG
		devfs_dbg("get rd in pick_next, target = %llx\n", target);
#endif
		dev_thread_ctx->current_rd_list = &target->list;
		return target;
	}
	return NULL;
}

/* Pick next file pointer queue to run */
static inline struct devfs_fstruct* devfs_sched_pick_next(struct dev_thread_struct *dev_thread_ctx, int policy) {
	switch (policy) {
		case DEVFS_SCHED_ROUND_ROBIN:
			return devfs_sched_round_robin(dev_thread_ctx);

		case DEVFS_SCHED_READ_PRIO:
			return devfs_sched_read_prioritized(dev_thread_ctx);

		default:
			return devfs_sched_round_robin(dev_thread_ctx);
	}
}


// In-device kthread I/O handler
int devfs_io_scheduler(void *data) {
	struct dev_thread_struct *dev_thread_ctx = data;
	struct devfs_fstruct *rd = NULL;
	void *ptr = NULL;
	nvme_cmdrw_t *cmdrw = NULL;
	int retval = 0;

#ifdef _DEVFS_OVERHEAD_KTHRD
	struct timespec start;
	struct timespec end;
#endif

	while (!kthread_should_stop()) {
#if 0
		/* If an exit flag is set, break this loop and return */
		if (dev_thread_ctx->state == DEV_THREAD_EXIT)
			break;
#endif

		/* Now picking next rd queue to schedule */
		rd = devfs_sched_pick_next(dev_thread_ctx, devfs_scheduler_policy);

		if (rd && !IS_ERR(rd)) {
#ifdef _DEVFS_SCALABILITY_DBG
			devfs_dbg("rd = %llx\n", rd);
#endif

			if (!rd || IS_ERR(rd))
				continue;

#ifdef _DEVFS_SCALABILITY_DBG
			devfs_dbg("rd = %llx | resuming kthread\n", rd);

			devfs_dbg("head=%d, tail=%d, entry=%d\n", 
				rd->fifo.head, rd->fifo.tail, rd->num_entries);

			devfs_dbg("rd->closed = %d\n", rd->closed);
#endif

			/*
			 * If host thread is closing this file descriptor 
			 * device thread need to do the following:
			 * 1) Flush all the data blocks in the buffer to storage
			 * 2) Notify host thread that all data are safely flushed
			 * 3) Remove list node from rd global linked list
			 */ 
			if (rd->closed == 1) {
				/* flush all the buffer to storage */
				vfio_devfs_io_write(rd);

				rd->closed = 0;
				rd->req = NULL;

			    /*  
				 * Remove current file pointer from the file pointer list
				 * Multiple host threads are closing concurrently,
				 * so need spin_lock here
				 *
				 * We need to delete list node here, because if we delete it
				 * later, dev_thread_ctx->current_rd_list will possibly point to
				 * a NULL pointer
				*/
#if 0
				//spin_lock(&g_rd_list_lock);
				mutex_lock(&g_mutex);
			    //list_del_rcu(&rd->list);
			    list_del(&rd->list);
			    //spin_unlock(&g_rd_list_lock);
				mutex_unlock(&g_mutex);
#endif

				/* notify host */
#ifdef _DEVFS_KTHRD_SEMAPHORE
				up(&rd->ksem);
#else
				test_and_set_bit(0, &rd->io_done);
#endif

				dev_thread_ctx->current_rd_list = NULL;

				/* clear rd->state */
				//test_and_clear_bit(0, &rd->state);

				continue;
			}

#ifdef _DEVFS_SCALABILITY_DBG
			devfs_dbg("rd = %llx | head=%d, tail=%d, entry=%d\n", 
				(__u64)rd, rd->fifo.head, rd->fifo.tail, rd->num_entries);
#endif

			//ptr = rd_read_tail(rd, sizeof(nvme_cmdrw_t), &rd->fifo.tail);
			//ptr = rd->fifo.buf[rd->fifo.tail]; 
			cmdrw = rd->req;

			/* Checking queue empty */
			if (cmdrw == NULL) {
				//printk(KERN_ALERT "NULL IO cmd pointer | %s:%d", __FUNCTION__, __LINE__);
				test_and_clear_bit(0, &rd->state);
				continue;
			}

			/* Set credential */
			devfs_set_cred(rd);

			if (cmdrw->common.opc == nvme_cmd_read) {

#ifdef _DEVFS_OVERHEAD_KTHRD
				getnstimeofday(&start);
#endif
				retval = vfio_devfs_io_read(rd, cmdrw, 1);

				/* Update return value of read op */
				rd->iobytes = retval;

				/* Clear rd request queue header */
				rd->req = NULL;

				/* Clear rd tsc */
				rd->tsc = 0;

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

			} else if (cmdrw->common.opc == nvme_cmd_write) {
				retval = vfio_devfs_io_write(rd);

				/* Update return value of write op */
				rd->iobytes = retval;

				/* Clear rd request queue header */
				rd->req = NULL;
	
				/* Clear rd tsc */
				rd->tsc = 0;
	
				/* notify host */
#ifdef _DEVFS_KTHRD_SEMAPHORE
				up(&rd->ksem);
#else
				test_and_set_bit(0, &rd->io_done);
#endif

			} else if (	cmdrw->common.opc == nvme_cmd_append ) {
				retval = vfio_devfs_io_append(rd);

				/* Update return value of write op */
				rd->iobytes = retval;

#ifdef _DEVFS_SCALABILITY_DBG
				devfs_dbg("after, rd = %llx | head=%d, tail=%d, entry=%d\n", 
					(__u64)rd, rd->fifo.head, rd->fifo.tail, rd->num_entries);
#endif
				
				/* Clear rd request queue header */
				rd->req = NULL;
	
				/* Clear rd tsc */
				rd->tsc = 0;

				/* notify host */
#ifdef _DEVFS_KTHRD_SEMAPHORE
				up(&rd->ksem);
#else
				test_and_set_bit(0, &rd->io_done);
#endif

			} else if ( cmdrw->common.opc == nvme_cmd_flush ) { 
				retval = vfio_devfs_io_fsync(rd);

				/* Update return value of write op */
				rd->iobytes = retval;

#ifdef _DEVFS_SCALABILITY_DBG
				devfs_dbg("after, rd = %llx | head=%d, tail=%d, entry=%d\n", 
					(__u64)rd, rd->fifo.head, rd->fifo.tail, rd->num_entries);
#endif
				
				/* Clear rd request queue header */
				rd->req = NULL;
	
				/* Clear rd tsc */
				rd->tsc = 0;

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
			test_and_clear_bit(0, &rd->state);
		}
	}
kthrd_out:
	return retval; 
}
EXPORT_SYMBOL(devfs_io_scheduler);


