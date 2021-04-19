/*
 * DevFS: Loadable Device File System
 *
 * Copyright (C) 2017 Sudarsun Kannan.  All rights reserved.
 *     Author: Sudarsun Kannan <sudarsun.kannan@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Derived from original ramfs:
 * TODO: DEVFS description
 */

/*TODO: Header cleanup*/
#include <linux/compat.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/devfs.h>
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
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include "pmfs.h"

#include <asm/xen/hypervisor.h>
#include <asm/xen/hypercall.h>
#include <xen/xen.h>
#include <xen/interface/xen.h>
#include <xen/interface/memory.h>
#include <xen/balloon.h>
#include <xen/heteromem.h>
#include <xen/features.h>
#include <xen/page.h>



//#define DRIVER_VERSION  "0.2"
//#define DRIVER_AUTHOR   "Sudarsun Kannan <sudarsun.kannan@gmail.com>"
//#define DRIVER_DESC     "DevFS filesystem"

#define DRIVER_VERSION  "0.2"
#define DRIVER_AUTHOR   "Alex Williamson <alex.williamson@redhat.com>"
#define DRIVER_DESC     "Type1 IOMMU driver for VFIO"

/*################## DEVFS ##################################*/


extern struct file_system_type devfs_fs_type;

static int g_devfs_init;
static struct devfs_inotree *g_inotree;
static __u8 isrdqueue = 1;
static __u8 iskernelio;
static __u32 g_qentrycnt;

int g_devfs_scheduler_init = 0;

//static int vfio_devfs_io_write (struct devfs_fstruct *rd);
//static int vfio_devfs_io_append (struct devfs_fstruct *rd);
static int devfs_free_file_queue(struct devfs_fstruct *rd);
static int vfio_creatfs_cmd(unsigned long arg);
static int vfio_creatq_cmd(unsigned long arg);
//static int devfs_submit_cmd(unsigned long arg, void *iommu_data);

static int devfs_init_file_queue(struct devfs_fstruct *rd){

	int i = 0;

	rd->fifo.buf = kzalloc(QUEUESZ(rd->qentrycnt), GFP_KERNEL);
	if (!rd->fifo.buf) {
		goto err_init_fqueue;
	}


	rd->fsblocks = kmalloc(rd->queuesize * sizeof(__u64), GFP_KERNEL);
	if ( !rd->fsblocks ) {
		printk(KERN_ALERT "DEBUG: Failed %s:%d \n",__FUNCTION__,__LINE__);
		goto err_init_fqueue;
	}

	//for( i= 0; i< rd->queuesize; i++) {
	for( i= 0; i< rd->qentrycnt; i++) {

#if defined(_DEVFS_MEMGMT)
		rd->fsblocks[i] = (void *)devfs_alloc_page(GFP_KERNEL, 0, 0);
#else 
		rd->fsblocks[i] = kmalloc(PAGE_SIZE, GFP_KERNEL);
#endif
		if ( !rd->fsblocks[i] ) {
			printk(KERN_ALERT "DEBUG: Failed %s:%d page %d \n",
					__FUNCTION__,__LINE__, i);
			goto err_init_fqueue;
		}
	}

	rd->qnumblocks = i;
	/*set queue buffer initialize to true*/
	rd->fqinit = 1;

	return 0;

err_init_fqueue:
	return -1;
}


/* DEVFS initialize function to initialize queue.
 * Command buffer is also initialized */
struct devfs_fstruct *devfs_init_file_struct(unsigned int qentrycnt){

	struct devfs_fstruct *rd =
			kzalloc(sizeof(struct devfs_fstruct), GFP_KERNEL);
	if (!rd) {
		printk(KERN_ALERT "Alloc failed %s:%d \n",__FUNCTION__,__LINE__);
		goto err_devfs_init;
	}

	if (qentrycnt)
		rd->qentrycnt = qentrycnt;
		//rd->qentrycnt = 16;
	else
		rd->qentrycnt =  NUM_QUEUE_CMDS;

#if defined(_DEVFS_DEBUG_RDWR)
	devfs_dbg("DEBUG: QSIZE %lu entries %u\n",
			QUEUESZ(rd->qentrycnt), rd->qentrycnt);
#endif

	if(rd->init_flg){
		goto err_devfs_init;
	}


	mutex_init(&rd->read_lock);
	init_waitqueue_head(&rd->fifo_event);

	rd->fqinit = 0;
	rd->num_entries = 0;
	//rd->queuesize = circ_space_to_end(&rd->fifo, rd->qentrycnt);
	rd->queuesize = QUEUESZ(rd->qentrycnt);
	rd->entrysize = sizeof(nvme_cmdrw_t);

#ifdef _DEVFS_FSTATS
	rd->rd_qhits = 0;
#endif
	rd->init_flg = 1;

	/* If Lazy alloc is defined, we allocate only during first write*/
#if !defined(_DEVFS_ONDEMAND_QUEUE)
	if(devfs_init_file_queue(rd))
		goto err_devfs_init;    	
#endif

	devfs_dbg("DEBUG: %s:%d fs queue buff num blocks %llu \n",
			__FUNCTION__,__LINE__, (__u64) rd->qnumblocks);
	return rd;

err_devfs_init:
	return NULL;
}


static int devfs_free_file_queue(struct devfs_fstruct *rd) {

	__u64 i = 0;
#if defined(_DEVFS_MEMGMT)
	struct page *page = NULL;
#endif

	if(!rd->fqinit) {
		/* No writes to file, and using ondemand allocation, 
		so skip deallocation */
		goto skip_qbuf_free;
	}

	if ( !rd->qnumblocks || rd->fifo.buf == NULL)
		return -1;

#ifdef _DEVFS_SCALABILITY
	devfs_scalability_flush_buffer(rd);
#else
	vfio_devfs_io_write (rd);
#endif

	if(rd->fifo.buf) {
		kfree(rd->fifo.buf);
		rd->fifo.buf = NULL;
	}
	rd->fifo.head = rd->fifo.tail = 0;

	for( i=0; i< rd->qnumblocks; i++) {

		if(rd->fsblocks[i]) {
#if defined(_DEVFS_MEMGMT)
			page = (struct page *)rd->fsblocks[i];
			devfs_free_pages(page);
#else
			kfree(rd->fsblocks[i]);
#endif
			rd->fsblocks[i] = NULL;
		}
	}

	if (rd->fsblocks) {
		kfree(rd->fsblocks);
		rd->fsblocks = NULL;
	}
	skip_qbuf_free:
	rd->fsblocks = NULL;
	rd->qnumblocks = 0;
	rd->num_entries = 0;
	rd->init_flg = 0;
	rd->fqinit = 0;

#if defined(_DEVFS_DEBUG)
	printk("DEBUG: Finished driver cleanup %s:%d \n",
			__FUNCTION__, __LINE__);
#endif
	return 0;
}	

/*
 * Responsible for writing to the kernel submission queue buffer
 * by copying data from the user-level buffer.
 * When the queue is full, IO is performed to the opened file
 */
int rd_write(struct devfs_fstruct *rd, void *buf, 
		int sz, int fd, int append)
{
	struct circ_buf *fifo = &rd->fifo;
	void *ptr = buf;
	nvme_cmdrw_t *cmd = (nvme_cmdrw_t *)buf;
	int retval;
	devfs_transaction_t *trans = NULL;

	struct inode *inode = NULL;
	struct devfs_inode *ei = NULL;
	struct file *file = rd->fp;

	loff_t index = 0;

	int init = 0;

	if(!file) {
		printk(KERN_ALERT "%s:%d Failed \n",__FUNCTION__, __LINE__);	
		retval = -EFAULT;
		goto rdwrite_err;
	}
	inode = file->f_inode;
	if(!inode) {
		printk(KERN_ALERT "%s:%d Failed \n",__FUNCTION__, __LINE__);	
		retval = -EFAULT;
		goto rdwrite_err;
	}

	ei = DEVFS_I(inode);
	if(!ei) {
		printk(KERN_ALERT "%s:%d Failed \n",__FUNCTION__, __LINE__);	
		retval = -EFAULT;
		goto rdwrite_err;
	}

	if(!buf) {
		printk(KERN_ALERT "DEBUG: Failed buf NULL \n");
		retval = -EFAULT;
		goto rdwrite_err;
	}


	/*check credential here */
	if(devfs_check_fs_cred(rd)) {
		printk(KERN_ALERT "%s:%d Write perm failed \n",__FUNCTION__, __LINE__);
		retval = -EFAULT;
		goto rdwrite_err;	
	}

	/* If it is a lazy queue allocation, then initialize buffers */
#if defined(_DEVFS_ONDEMAND_QUEUE)
	if(!rd->fqinit) {
		//Initialize queue buffers and also set fqinit
		if(devfs_init_file_queue(rd)) {
			printk(KERN_ALERT "%s:%d Queue buffer failed \n",
					__FUNCTION__, __LINE__);
			retval = -EFAULT;
			goto rdwrite_err;
		}
	}
#endif

	if(!rd || rd->fifo.buf == NULL || !sz) {
		printk(KERN_ALERT "%s:%d Failed buf NULL \n",__FUNCTION__, __LINE__);
		retval = -EFAULT;
		goto rdwrite_err;
	}


	while (sz > 0) {

		char *fptr;
		//int n;
		void *dest = NULL;

		//wait_event(rd->fifo_event, circ_space(&rd->fifo) > 0);
		//n = circ_space_to_end(&rd->fifo, rd->qentrycnt);
		//printk(KERN_ALERT "rd->queuesize=%d, rd->num_entries=%d\n", rd->queuesize, rd->num_entries);

#if defined(_DEVFS_RDQUEUE)
		//if( n < sz) {
		if (rd->fifo.head == rd->queuesize) {

			// Perform IO when the queue buff is full
			if(append) {
				vfio_devfs_io_append(rd);
			}else {
				vfio_devfs_io_write (rd);
			}

			fifo->head = fifo->tail = 0;
			rd->num_entries = 0;
#if defined(_DEVFS_DEBUG_RDWR)
			printk( KERN_ALERT "%s:%d circ_count %d f qentry cnt %u \n", __FUNCTION__,
					__LINE__,circ_count_to_end(&rd->fifo, rd->qentrycnt),
					rd->qentrycnt);
#endif
			//n = circ_space_to_end(&rd->fifo, rd->qentrycnt);
			//printk(KERN_ALERT "circ_buf is full\n");
		}
#endif

		fptr = &fifo->buf[fifo->head];

		/*if (copy_from_user(fptr, (void __user *)ptr, sz)) {
			printk( KERN_ALERT "Failed %s:%d %d %d\n",
					__FUNCTION__, __LINE__, fifo->head, sz);
			goto rdwrite_err;
		}*/

        if (init == 1) {
            if (copy_from_user(fptr, (void __user *)ptr, sz)) {
                printk( KERN_ALERT "Failed %s:%d %d %d\n",
                        __FUNCTION__, __LINE__, fifo->head, sz);
                goto rdwrite_err;
            }
        } else {
            memcpy(fptr, ptr, sz);
            init = 1;
        }

#if defined(_DEVFS_DEBUG_RDWR)
		printk(KERN_ALERT "Adding to queue ");
		DEBUGCMD(cmd);
#endif

		//fifo->head = (fifo->head + sz) & (rd->queuesize - 1);
		//sz  -= n;

		fifo->head = fifo->head + sz;
		sz = 0;

		ptr = (void *)cmd->common.prp2;

		if(ei->isjourn) {
			trans = devfs_new_ino_trans_log(ei, inode);
			if(!trans) {
				printk(KERN_ALERT "%s:%d Failed \n",__FUNCTION__, __LINE__);
				retval = -EFAULT;
				goto rdwrite_err;
			}
		}

		if (cmd && ptr) {
			/* enqueue per file pointer submission queue */
			dest = (void *)rd->fsblocks[rd->num_entries];
			if (!dest) {
				printk(KERN_ALERT "%s, %d destination %d null \n",
						__FUNCTION__, __LINE__, rd->num_entries);
				retval = -EFAULT;
				goto rdwrite_err;
			}

			/* Copy data from user to device */
			if (copy_from_user(dest, (void __user *)ptr, cmd->nlb)) {
				printk(KERN_ALERT "page copy failed %lu\n", (unsigned long)ptr);
				retval = -EFAULT;
				goto rdwrite_err;
			}

			/* Add index in per inode nv radix tree */
			index = (loff_t)cmd->slba >> PAGE_CACHE_SHIFT;

			printk(KERN_ALERT "insert index = %llu, dest = %llx\n", index, (__u64)dest);

			spin_lock(&ei->sq_tree_lock);
			radix_tree_insert(&ei->sq_tree, index, dest);
			spin_unlock(&ei->sq_tree_lock);

			//wake_up_all(&rd->fifo_event);
			
			/* we don't need the following update! It will screw inode size */
			//devfs_inode_update(inode, (loff_t)cmd->slba);

		} else {
			retval = -EFAULT;
			printk(KERN_ALERT "%s:%d Incorrect command or data buffer \n",
					__FUNCTION__, __LINE__);
			goto rdwrite_err;
		}
		rd->num_entries++;		

#if !defined(_DEVFS_RDQUEUE)
		// Perform IO when the queue buff is full
		if(append) {
			vfio_devfs_io_append(rd);	
		}else {
			vfio_devfs_io_write (rd);
		}

		fifo->head = fifo->tail = 0;
		rd->num_entries = 0;
#endif
		printk(KERN_ALERT "inode=%llx, inode->i_size=%llu, ei->i_size=%llu\n", 
			(__u64)inode, inode->i_size, ei->i_size);

#if defined(_DEVFS_DEBUG_RDWR)
		printk( KERN_ALERT "%s:%d circ_count %d f qentry cnt %u \n", __FUNCTION__,
				__LINE__,circ_count_to_end(&rd->fifo, rd->qentrycnt), rd->qentrycnt);
#endif
		//n = circ_space_to_end(&rd->fifo, rd->qentrycnt);

		if(ei->isjourn)
			devfs_commit_transaction(ei, trans);
	}

	return 0;

rdwrite_err:
	return retval;
}


/*
 * Read the kernel submission queue buffer
 */
nvme_cmdrw_t *rd_read(struct devfs_fstruct *rd, int sz)
{
	struct circ_buf *fifo = &rd->fifo;
	void *fptr;
	nvme_cmdrw_t *cmd;

	mutex_lock(&rd->read_lock);

	fptr = &fifo->buf[fifo->tail];
	cmd = (nvme_cmdrw_t *)fptr;
	//fifo->tail = (fifo->tail + rd->entrysize) & (rd->queuesize - 1);
	
	if (fifo->tail > fifo->head) {
		printk(KERN_ALERT "head = %d, tail = %d, num_entries = %d, tail exceed head! | %s:%d\n", 
				fifo->head, fifo->tail, rd->num_entries, __FUNCTION__, __LINE__);
		dump_stack();
		mutex_unlock(&rd->read_lock);
		return NULL;
	}
	
	fifo->tail = fifo->tail + rd->entrysize;

	//printk( KERN_ALERT "%s:%d %llu %d\n", 
	//	__FUNCTION__, __LINE__, cmd->slba, fifo->tail);
	
	mutex_unlock(&rd->read_lock);
	return (nvme_cmdrw_t *)fptr;
}

/*
 * Read the kernel submission queue buffer with tail
 */
nvme_cmdrw_t *rd_read_tail(struct devfs_fstruct *rd, int sz, int *tail)
{
	struct circ_buf *fifo = &rd->fifo;
	void *fptr;
	nvme_cmdrw_t *cmd;

	mutex_lock(&rd->read_lock);

	if (*tail > fifo->head) {
		printk(KERN_ALERT "head = %d, tail = %d, num_entries = %d, tail exceed head! | %s:%d\n", 
				fifo->head, fifo->tail, rd->num_entries, __FUNCTION__, __LINE__);
		//dump_stack();
		mutex_unlock(&rd->read_lock);
		return NULL;
	}

	fptr = &fifo->buf[*tail];
	cmd = (nvme_cmdrw_t *)fptr;

	/* We DON'T need to modify queue! 
	 * Because This is the read operation
	 * Just fetch data from queue without modifying it
	 */ 
	//*tail = (*tail + rd->entrysize) & (rd->queuesize - 1);

	//printk( KERN_ALERT "%s:%d %llu %d\n", 
	//	__FUNCTION__, __LINE__, cmd->slba, *tail);

	mutex_unlock(&rd->read_lock);

	return (nvme_cmdrw_t *)fptr;
}




static int devfs_match_ioq(nvme_cmdrw_t *cmdreq, nvme_cmdrw_t *cmdrw, 
		struct devfs_fstruct *rd, int entry){

	void *ptr = (void *)cmdreq->common.prp2;
	int retval = 0;

	if(!ptr) {
		printk(KERN_ALERT "%s:%d User data buffer null \n",
				__FUNCTION__, __LINE__);
		retval = -EFAULT;
		goto err_match_ioq;
	}

	if (cmdreq->slba == cmdrw->slba) {

		void *src = (void *)rd->fsblocks[entry];
		if (!src) {
			printk(KERN_ALERT "%s, %d destination %d null \n",
					__FUNCTION__, __LINE__, entry);
			retval = -EFAULT;
			goto err_match_ioq;
		}

		if (copy_to_user((void __user *)ptr, src, cmdreq->nlb)) {
			printk(KERN_ALERT "page copy failed %lu\n", (unsigned long)ptr);
			retval = -EFAULT;
			goto err_match_ioq;
		}

	}else {
		/* No match */
		return -1;
	}

#ifdef  _DEVFS_FSTATS
	rd->rd_qhits++;
#endif
	return 0;

	err_match_ioq:
	return retval;
}


long vfio_devfs_io_read (struct devfs_fstruct *rd, nvme_cmdrw_t *cmdreq, 
			u8 isappend){

	int num_entries = 0;
	long ret = 0;
	ssize_t rdbytes=0;
	struct file *fp = rd->fp;
	loff_t fpos = rd->fpos;	
	void *buf = (void *)cmdreq->common.prp2;
	ssize_t reqsz = cmdreq->nlb;
	int i = 0, j = 0;
	nvme_cmdrw_t *cmdrw = NULL;
	
	void *src = NULL;
	loff_t index = 0;

	struct inode *inode = NULL;
	struct devfs_inode *ei = NULL;

#ifdef _DEVFS_READ_DOUBLECOPY
	/*char *s = (char *)kzalloc(reqsz, GFP_KERNEL);
	char *p = (__force char __user *)s;
	cmdreq->common.drp1 = (__u64)s;*/

	if (!rd->kbuf) {
		printk(KERN_ALERT "Per file pointer kernel buffer is NULL | %s:%d\n",
				__FUNCTION__, __LINE__);
		goto io_read_err;
	}

	char *p = (__force char __user *)rd->kbuf;
	cmdreq->common.prp1 = (__u64)rd->kbuf;
#endif


	num_entries = rd->num_entries;

	inode = fp->f_inode;
	if(!inode) {
		printk(KERN_ALERT "%s:%d Failed \n",__FUNCTION__, __LINE__);	
		ret = -EFAULT;
		goto io_read_err;
	}

	ei = DEVFS_I(inode);
	if(!ei) {
		printk(KERN_ALERT "%s:%d Failed \n",__FUNCTION__, __LINE__);	
		ret = -EFAULT;
		goto io_read_err;
	}

	if(!cmdreq) {
		printk(KERN_ALERT "%s, %d request null \n", __FUNCTION__, __LINE__);
		ret = -EFAULT;
		goto io_read_err;
	}

	/*check credential here */
	if(devfs_check_fs_cred(rd)) {
		printk(KERN_ALERT "%s:%d Read perm failed \n",__FUNCTION__, __LINE__);
		ret = -EFAULT;
		goto io_read_err;
	}

//#if 0
	if(isrdqueue) {
#ifdef _DEVFS_SCALABILITY
		index = (loff_t)cmdreq->slba >> PAGE_CACHE_SHIFT;

		spin_lock(&ei->sq_tree_lock);
		src = radix_tree_lookup(&ei->sq_tree, index);
		spin_unlock(&ei->sq_tree_lock);
		if (src != NULL) {
			/* Target block found in submission queue */
#ifdef _DEVFS_SCALABILIYT_DBG
			printk(KERN_ALERT "radix tree hit\n");
#endif

			/*if (copy_to_user((void __user *)buf, src, cmdreq->nlb)) {
				printk(KERN_ALERT "page copy failed %lu\n", (unsigned long)buf);
				ret = -EFAULT;
				goto io_read_err;
			}*/

			//printk(KERN_ALERT "prp1 = %lx, src = %lx\n", cmdreq->common.prp1, src);
			//printk(KERN_ALERT "radix_hit, index = %d\n", index);

			memcpy((void*)cmdreq->common.prp1, src, cmdreq->nlb);

			ret = cmdreq->nlb;

			goto ioread_finish;
		}	

#else
		int tail = rd->fifo.tail;
		for ( i =0; i < num_entries; i++) {

			cmdrw = rd_read_tail(rd, sizeof(nvme_cmdrw_t), &tail);
			if(!cmdrw) {
				goto search_disk;
			}
			buf = (void *)cmdrw->common.prp2;

			/*TODO: Locking Just leaving without acquiring and releasing 
			 * locks is bad. Need to complete this after implementation of
			 * locking
			 */
			if (!devfs_match_ioq(cmdreq, cmdrw, rd, i)) {
				goto ioread_finish;
			}
		}
#endif
	}
//#endif

search_disk:

	/*TODO: Currently, we only read in page size. should be more than that */
	if (fp != NULL && buf) {
#ifndef _DEVFS_READ_DOUBLECOPY
		char __user *p;
		p = (__force char __user *)buf;
#endif

		/* offset of read/write should be handled in user */
		if (isappend) {
			//fpos = (loff_t)fp->f_pos;
			fpos = (loff_t)cmdreq->slba;
#if defined(_DEVFS_DEBUG_RDWR)
			printk(KERN_ALERT "%s:%u fpos %llu, reqsz %zu \n",
					__FUNCTION__, __LINE__, fpos, reqsz);
#endif
		}else{
			fpos = (loff_t)cmdreq->slba;
			//fpos = rd->fpos;
		}

#ifdef _DEVFS_SCALABILITY_DBG
		printk(KERN_ALERT "fp->f_pos = %llu, rd->fpos = %llu, cmdreq->slba = %llu\n", 
			fp->f_pos, rd->fpos, cmdreq->slba);
#endif

		rdbytes = devfs_read(fp, p, reqsz, &fpos);
		if(!rdbytes) {  //|| (rdbytes != reqsz)) {
			//ret = -EFAULT;
			//printk(KERN_ALERT "%s:%u Read failed for slba %llu, bytes read %zu \n",
			//		__FUNCTION__, __LINE__, (loff_t)cmdreq->slba, rdbytes);
			//DEBUGCMD(cmdreq);
			printk(KERN_ALERT "read failed, rdbytes = %d\n", rdbytes);
			memset(cmdreq->common.prp1, 0, PAGE_SIZE);
			ret = (long)rdbytes;
			//goto io_read_err;
			
		}else {
			//printk(KERN_ALERT "%s:%u rdbytes %zu buf %s \n", 
			//__FUNCTION__, __LINE__, rdbytes, p);
			ret = (long)rdbytes;	
	    }
#ifdef _DEVFS_SCALABILITY_DBG
		printk(KERN_ALERT "~~~~~~~ after read, cmd = %llx, prp1 = %llx, prp1 = %s\n", 
			cmdreq, cmdreq->common.prp1, (char*)cmdreq->common.prp1);
#endif
	}


	//printk(KERN_ALERT "rdbytes = %d\n", rdbytes);

	fp->f_pos = fp->f_pos + rdbytes;
	//rd->fpos = rd->fpos + rdbytes;

ioread_finish:
	return ret;

io_read_err:
	return ret;
}


/* Debug: Read all the current buffer queue values 
 * The reader function holds the read lock buffer reading each 
 * read buffer values 
 */
int vfio_devfs_io_append (struct devfs_fstruct *rd) {

	nvme_cmdrw_t *cmdrw;
	void *ptr;
	int num_entries = 0, i = 0;
	int ret = 0;
	ssize_t wrbytes=0;
	struct file *fp = rd->fp;
	loff_t fpos = rd->fpos;	
	u8 isappend = 1;
	loff_t index = 0;

	struct inode *inode = NULL;
	struct devfs_inode *ei = NULL;

	num_entries = rd->num_entries;

#if defined(_DEVFS_DEBUG_RDWR)
	printk(KERN_ALERT "%s:%d num_entries %d \n",__FUNCTION__, __LINE__, num_entries);
#endif

	inode = fp->f_inode;
	if (!inode) {
		printk(KERN_ALERT "%s:%d Failed \n",__FUNCTION__, __LINE__);	
		ret = -EFAULT;
		goto appendio_err;
	}

	ei = DEVFS_I(inode);
	if (!ei) {
		printk(KERN_ALERT "%s:%d Failed \n",__FUNCTION__, __LINE__);	
		ret = -EFAULT;
		goto appendio_err;
	}

	//for (i = 0; i < num_entries; i++) {
	for (i = 0; rd->fifo.tail < rd->fifo.head; ++i) {

		void *src = NULL;
		const char __user *p;
		ptr = rd_read(rd, sizeof(nvme_cmdrw_t));
		cmdrw = (nvme_cmdrw_t *)ptr;

		if (!cmdrw) {
			printk(KERN_ALERT "FAILED %s:%d cmdrw is null \n",
					__FUNCTION__, __LINE__);
			ret = -EFAULT;
			goto appendio_err;
		}

		//src = (void *)rd->fsblocks[i];
		src = (void *)cmdrw->blk_addr;
		wrbytes = 0;	
		//cmdrw->slba = fp->f_pos;

#if defined(_DEVFS_DEBUG_RDWR)
		printk(KERN_ALERT "*****%s:%d block %llu buff idx %d "
				"num_entries %d***\n", __FUNCTION__, __LINE__,
				cmdrw->slba, i, num_entries);
#endif

		if (!src) {
			printk(KERN_ALERT "FAILED %s:%d block %llu buff idx %d opcode %d null \n",
					__FUNCTION__, __LINE__, cmdrw->slba, i, cmdrw->common.opc);
			ret = -EFAULT;
			goto appendio_err;
		}

#if defined(_DEVFS_DEBUG_RDWR)
		DEBUGCMD(cmdrw);
#endif
		if( fp != NULL && src) {

			fpos = (loff_t)cmdrw->slba;
			//fpos = rd->fpos;
			p = (__force const char __user *)src;

#ifdef _DEVFS_SCALABILITY_DBG
			printk(KERN_ALERT "cmdrw->slba=%llu, rd->fpos=%llu\n", cmdrw->slba, rd->fpos);
#endif
			wrbytes = devfs_kernel_write(fp, p, cmdrw->nlb, &fpos);
			
		    //wrbytes = devfs_direct_write(rd, cmdrw, isappend);

			if(!wrbytes || (wrbytes != cmdrw->nlb)) {
				ret = -EFAULT;

				printk("%s:%u Write failed fd %d slba %llu off %llu size %zu\n",
						__FUNCTION__, __LINE__, rd->fd, (loff_t)cmdrw->slba, fpos, wrbytes);

				goto appendio_err;
			}
#ifdef _DEVFS_FSTATS
			rd->rd_wrsize += cmdrw->nlb;
#endif		
		}
		rd->fpos = fpos;

		ret = wrbytes;
		
		/* remove entry in nv radix tree */
		index = (loff_t)cmdrw->slba >> PAGE_CACHE_SHIFT;
		spin_lock(&ei->sq_tree_lock);
		radix_tree_delete(&ei->sq_tree, index);
		spin_unlock(&ei->sq_tree_lock);

#ifdef _DEVFS_SCALABILITY_DBG
		printk(KERN_ALERT "delete index = %lld\n", index);
#endif
	}
appendio_err:
	return ret;
}



/* Debug: Read all the current buffer queue values 
 * The reader function holds the read lock buffer reading each 
 * read buffer values 
 */
int vfio_devfs_io_write (struct devfs_fstruct *rd) {

	nvme_cmdrw_t *cmdrw;
	void *ptr;
	int num_entries = 0, i = 0;
	int ret = 0;
	ssize_t wrbytes=0;
	struct file *fp = rd->fp;
	loff_t fpos = rd->fpos;	
	loff_t index = 0;

	struct inode *inode = NULL;
	struct devfs_inode *ei = NULL;

	num_entries = rd->num_entries;

	inode = fp->f_inode;
	if (!inode) {
		printk(KERN_ALERT "%s:%d Failed \n",__FUNCTION__, __LINE__);	
		ret = -EFAULT;
		goto writeio_err;
	}

	ei = DEVFS_I(inode);
	if (!ei) {
		printk(KERN_ALERT "%s:%d Failed \n",__FUNCTION__, __LINE__);	
		ret = -EFAULT;
		goto writeio_err;
	}

	/*printk(KERN_ALERT "in write, rd = %llx,  num_entries = %d\n", rd, num_entries);
	if (num_entries > 0) {
		ptr = rd_read_tail(rd, sizeof(nvme_cmdrw_t), &rd->fifo.tail);
		cmdrw = (nvme_cmdrw_t *)ptr;
		printk(KERN_ALERT "cmdrw->commom.opc=%d\n", cmdrw->common.opc);
	}*/

	//mutex_lock(&rd->read_lock);
	//for (i = 0; i < num_entries; i++) {
	for (i = 0; rd->fifo.tail < rd->fifo.head; ++i) {

		void *src = NULL;
		const char __user *p;
		ptr = rd_read(rd, sizeof(nvme_cmdrw_t));
		cmdrw = (nvme_cmdrw_t *)ptr;

		if (!cmdrw) {
			printk(KERN_ALERT "FAILED %s:%d cmdrw is null \n",
					__FUNCTION__, __LINE__);
			ret = -EFAULT;
			goto writeio_err;
		}

		//src = (void *)rd->fsblocks[i];
		src = (void *)cmdrw->blk_addr;
		wrbytes = 0;	

		if (!src) {
			printk(KERN_ALERT "FAILED %s:%d block %llu buff idx %d opcode %d null \n",
					__FUNCTION__, __LINE__, cmdrw->slba, i, cmdrw->common.opc);
			ret = -EFAULT;
			goto writeio_err;
		}

#if defined(_DEVFS_DEBUG_RDWR)
		DEBUGCMD(cmdrw);
#endif
		if( fp != NULL && src) {

			/* offset of read/write should be handled in user */
			// DOTO: will discuss it later
			fpos = (loff_t)cmdrw->slba;
			//fpos = rd->fpos;
			p = (__force const char __user *)src;

#ifdef _DEVFS_SCALABILITY_DBG
			printk(KERN_ALERT "cmdrw->slba=%llu, rd->fpos=%llu, cmdrw->commom.opc=%d\n", fpos, rd->fpos, cmdrw->common.opc);
#endif

			wrbytes = devfs_kernel_write(fp, p, cmdrw->nlb, &fpos);

			if(!wrbytes || (wrbytes != cmdrw->nlb)) {
				ret = -EFAULT;

				printk("%s:%u Write failed fd %d slba %llu off %llu size %zu\n",
						__FUNCTION__, __LINE__, rd->fd, (loff_t)cmdrw->slba, fpos, wrbytes);

				goto writeio_err;
			}
#ifdef _DEVFS_FSTATS
			rd->rd_wrsize += cmdrw->nlb;
#endif		
		}
		rd->fpos = fpos;

		ret = wrbytes;

		/* remove entry in nv radix tree */
		index = (loff_t)cmdrw->slba >> PAGE_CACHE_SHIFT;
		spin_lock(&ei->sq_tree_lock);
		radix_tree_delete(&ei->sq_tree, index);
		spin_unlock(&ei->sq_tree_lock);

#ifdef _DEVFS_SCALABILITY_DBG
		printk(KERN_ALERT "delete index = %lld\n", index);
#endif
	}
	//mutex_unlock(&rd->read_lock);
writeio_err:
	return ret;
}

long devfs_llseek(struct devfs_fstruct *rd, nvme_cmdrw_t *cmdrw)
{

	//BUG TODO: using slba for whence,
	//Ideally, we should be using a flag field in cmdrw flag.
	struct file *file = rd->fp;
	loff_t offset = (loff_t)cmdrw->nlb;
	loff_t currpos = 0;
	int whence = (int)cmdrw->slba;
	long pos = 0;

#if defined(_DEVFS_DEBUG_RDWR)
       printk("%s:%u Before offset %lu, fpos %llu currpos %zu \n",
                __FUNCTION__, __LINE__, offset, file->f_pos, currpos);
#endif

	currpos = default_llseek(file, offset, whence);

	pos = (long)currpos;

#if defined(_DEVFS_DEBUG_RDWR)
       printk("%s:%u Returns offset %lu, fpos %llu currpos %zu pos %ld \n",
                __FUNCTION__, __LINE__, offset, file->f_pos, currpos, pos);
#endif
	
	return pos;
}

/*
 * DevFS fsync
 */
int vfio_devfs_io_fsync(struct devfs_fstruct *rd) {

	nvme_cmdrw_t *cmdrw;
	void *ptr;
	int num_entries = 0, i = 0;
	int ret = 0;
	ssize_t wrbytes=0;
	struct file *fp = rd->fp;
	loff_t fpos = rd->fpos;	
	loff_t index = 0;

	struct inode *inode = NULL;
	struct devfs_inode *ei = NULL;
	struct devfs_fstruct *cur_rd;

	inode = fp->f_inode;
	if (!inode) {
		printk(KERN_ALERT "%s:%d Failed \n",__FUNCTION__, __LINE__);	
		ret = -EFAULT;
		goto io_fsync_err;
	}

	ei = DEVFS_I(inode);
	if (!ei) {
		printk(KERN_ALERT "%s:%d Failed \n",__FUNCTION__, __LINE__);	
		ret = -EFAULT;
		goto io_fsync_err;
	}

	//printk(KERN_ALERT "fsync start, rd = %llx.\n", rd);

	int rd_nr = ei->rd_nr;
//#if 0
	for (i = 0; i < rd_nr; ++i) {
		cur_rd = ei->per_rd_queue[i];
		if (!cur_rd) {
			printk(KERN_ALERT "In fsync, rd is NULL \n");	
			continue;
		}
		//if (test_and_set_bit(0, &cur_rd->fsyncing) == 0) {
		if (test_and_set_bit(0, &cur_rd->state) == 0) {
			if (cur_rd->num_entries == 0) {
				//test_and_clear_bit(0, &cur_rd->fsyncing);
				test_and_clear_bit(0, &cur_rd->state);
				continue;
			}

			if (cur_rd->num_entries > 0) {
				cmdrw = (nvme_cmdrw_t *)rd_read_tail(cur_rd, sizeof(nvme_cmdrw_t), &cur_rd->fifo.tail);

				//printk(KERN_ALERT "cmdrw->commom.opc=%d\n", cmdrw->common.opc);

				if (cmdrw && cmdrw->common.opc != nvme_cmd_append &&
					cmdrw->common.opc != nvme_cmd_write) {
					//test_and_clear_bit(0, &cur_rd->fsyncing);
					test_and_clear_bit(0, &cur_rd->state);
					continue;
				}
			}

			//printk(KERN_ALERT "rd_nr = %d, rd = %llx\n", rd_nr, cur_rd);
			vfio_devfs_io_write(cur_rd);

			mutex_lock(&cur_rd->read_lock);

			cur_rd->fifo.head = cur_rd->fifo.tail = 0;
			cur_rd->num_entries = 0;
		
			/*cur_rd->req = NULL;
			cur_rd->tsc = 0;*/
	
			mutex_unlock(&cur_rd->read_lock);

			//test_and_clear_bit(0, &cur_rd->fsyncing);
			test_and_clear_bit(0, &cur_rd->state);

			//printk(KERN_ALERT "fsync fin rd = %llx\n", cur_rd);
		} else {
			/* This rd is already in process of fsync by other device thread */
			continue;
		}
	}
//#endif
	//printk(KERN_ALERT "fsync done.\n");

io_fsync_err:
	return ret;
}



/* Direct write or append - Directly writes from the user buffer to DevFS file
 */
long devfs_direct_write(struct devfs_fstruct *rd, nvme_cmdrw_t *cmdrw, u8 isappend)
{
	long ret = 0;
	struct file *fp = rd->fp;
	loff_t fpos = cmdrw->slba;
	size_t count = 0, wrbytes = 0, rem= 0;
	const char __user *p = (void *)cmdrw->common.prp2;

	count = cmdrw->nlb;
	rem = count;

	if(isappend){
		fpos = fp->f_pos;
	}else {
		fpos = cmdrw->slba;
	}

#if defined(_DEVFS_DEBUG_RDWR)
	printk(KERN_ALERT "%s:%u Attempting Write fd %d fpos %llu size %zu isappend %d\n",
		__FUNCTION__, __LINE__, rd->fd, fpos, count, isappend);
#endif

	if( fp != NULL && p) {

		while (rem) {	

			wrbytes = devfs_kernel_write(fp, p, rem, &fpos);
			if(!wrbytes) {
				ret = -EFAULT;
				printk(KERN_ALERT "%s:%u Write failed fd %d fpos %llu size %zu wrote %zu\n",
						__FUNCTION__, __LINE__, rd->fd, fpos, count, wrbytes);
				goto perfio_err;
			}
			rem = rem - wrbytes;
#if defined(_DEVFS_DEBUG_RDWR)
      			  printk(KERN_ALERT "%s:%u Wrote fd %d fpos %llu size %zu isappend %d\n",
			                __FUNCTION__, __LINE__, rd->fd, fpos, count, isappend);
#endif
		}

#ifdef _DEVFS_FSTATS
		rd->rd_wrsize += count;
#endif
	}
	rd->fpos = fpos;

	//Update file pointer
        fp->f_pos = fpos;

	//Return value
	ret = (long)count;

#if defined(_DEVFS_DEBUG_RDWR)
        printk("%s:%u After Write fd %d fpos %llu size %zu isappend %d wrote %zu\n",
                __FUNCTION__, __LINE__, rd->fd, fp->f_pos, count, isappend, wrbytes);
#endif

perfio_err:
	return (long)wrbytes;
}



struct devfs_fstruct *fd_to_queuebuf(int fd) {

	struct devfs_fstruct *rd = NULL;
	struct file *fp = NULL;

	if(fd < 0) {
		printk(KERN_ALERT "%s, %d incorrect file descriptor %d \n",
				__FUNCTION__, __LINE__, fd);
		goto err_queuebuf;
	}

	fp = fget(fd);
	if(!fp) {
		printk(KERN_ALERT "%s, %d failed to get file pointer \n",
				__FUNCTION__, __LINE__);
		goto err_queuebuf;
	}

	rd = fp->cmdbuf;
	if(!rd) {
		printk(KERN_ALERT "DEBUG: Failed %s:%d for fd %d \n",
				__FUNCTION__,__LINE__, fd);
		goto err_queuebuf;
	}
	return rd;

	err_queuebuf:
	return NULL;
}
EXPORT_SYMBOL(fd_to_queuebuf);


#if 0
int init_file_struct(__u32 qentrycnt, int fd, struct file *fp) 
{
    struct devfs_fstruct *rd = NULL;
    int ret = 0;	

     /* allocate file queues and other structures*/	
     rd = devfs_init_file_struct(qentrycnt);	
     if (!rd) {
	printk(KERN_ALERT "Failed init file struct %s:%d\n",
		__FUNCTION__, __LINE__);
	ret = -EFAULT;
	goto err_fstruct;
     }

     fp->cmdbuf = (void *)rd;
     
     /*Create back reference*/
     rd->fp = fp;	 	
     rd->fd = fd;	

     /* update security credentials*/
     devfs_set_cred(rd); 

err_fstruct:
     return ret;	
}
#endif


/* Create DEVFS file and initalize file in-memory file structures */
//static 
int vfio_creatfile_cmd(unsigned long arg, int kernel_call){

	unsigned long minsz;
	struct vfio_devfs_creatfp_cmd map;
	int retval = 0;
	int flen = 0;
	int flags;
	umode_t mode;
	struct file *fp = NULL;	
	int fd = -1;
	struct devfs_fstruct *rd = NULL;
	struct inode *inode = NULL;
	struct devfs_inode *ei = NULL;
	__u8 isjourn = 0;
	unsigned int qentrycnt = 0;
	struct vfio_devfs_creatfp_cmd *mapptr = (struct vfio_devfs_creatfp_cmd *)arg;

#ifdef _DEVFS_SCALABILITY_DBG
	printk(KERN_ALERT "DEBUG: Calling %s:%d \n",__FUNCTION__,__LINE__);
#endif

	minsz = offsetofend(struct vfio_devfs_creatfp_cmd, isrdqueue);
	/*If emulating DevFS I/O calls from within kernel*/
	if (kernel_call) {
		mapptr = (struct vfio_devfs_creatfp_cmd *)arg;
		map = *mapptr;
		if(!strlen(map.fname)) {
			printk(KERN_ALERT "DEBUG: Failed %s:%d \n",
				__FUNCTION__,__LINE__);
			retval = -EFAULT;
			goto err_creatfp;
		}	

	} else if (copy_from_user(&map, (void __user *)arg, minsz)) {
		printk(KERN_ALERT "DEBUG: Failed %s:%d \n",__FUNCTION__,__LINE__);
		retval = -EFAULT;
		goto err_creatfp;
	}
	flen = strlen(map.fname);
	flags = (int)map.flags;
	mode = (umode_t)map.mode;
	isjourn = (__u8)map.isjourn;
	//isrdqueue = (__u8)map.isrdqueue;
	iskernelio = (__u8)map.iskernelio;
	qentrycnt = (unsigned int)map.qentrycnt;

	if(flen < 0) {
		printk(KERN_ALERT "DEBUG: Failed %s:%d filename "
				"incorrect len %d \n",__FUNCTION__,__LINE__, flen);
		retval = -EFAULT;
		goto err_creatfp;
	}
	pmfs_dbg_verbose("%s:%d create file %s isjourn %d "
			"isrdqueue %d iskernelio %d qentrycnt %u\n", 
			__FUNCTION__,__LINE__, map.fname, isjourn, 
			isrdqueue, iskernelio, qentrycnt);

	fp = devfs_create_file(map.fname, flags | O_LARGEFILE, mode, &fd);
	if(fp == NULL) {
		printk(KERN_ALERT "%s:%d create file failed \n", 
				__FUNCTION__,__LINE__);
		retval = fd;
		goto err_creatfp;
	}

	map.fd = fd;
	fp->isdevfs = 1;
	inode = fp->f_inode;
	if(!inode) {
		printk(KERN_ALERT "%s:%d inode NULL \n", 
				__FUNCTION__,__LINE__);
		retval = -EFAULT;
		goto err_creatfp;
	}

	inode->isdevfs = 1;

	/*printk(KERN_ALERT "inode = %llx, inode->isdevfs = %d | %s:%d\n", 
			inode, inode->isdevfs, __FUNCTION__, __LINE__);	*/

	/*Get DevFS inode info from inode*/
	ei = DEVFS_I(inode);
	if(!ei) {
		printk(KERN_ALERT "%s:%d DevFS inode info NULL \n", 
				__FUNCTION__,__LINE__);
		retval = -EFAULT;
		goto err_creatfp;
	}

	if(!inode->page_tree_init){
		INIT_RADIX_TREE(&inode->page_tree, GFP_ATOMIC);
		inode->page_tree_init = 1;
	}

	if(isjourn) {
		ei->isjourn = isjourn;
		/* This condition should be false*/
		if(unlikely(ei->cachep_init == CACHEP_INIT)){
			printk(KERN_ALERT "%s:%d Failed \n",
					__FUNCTION__,__LINE__);
			retval = -EFAULT;
			goto err_creatfp;
		}
		if(init_journal(inode)) {
			printk(KERN_ALERT "%s:%d init_journal Failed \n",
					__FUNCTION__,__LINE__);
			retval = -EFAULT;
			goto err_creatfp;
		}
	}else {

	      ei->isjourn = 0;
	      pmfs_dbg_verbose("%s:%d journal disabled, \n",
			 __FUNCTION__,__LINE__);
        }
	if(!g_inotree || !ei) {
		printk(KERN_ALERT "%s:%d g_inotree or ei NULL\n",
				__FUNCTION__,__LINE__);
		goto err_creatfp;
	}

	/*Insert devfs inode info into rbtree*/
	//retval = insert_inode_rbtree(&g_inotree->inode_list, ei);
	if(retval) {
		printk(KERN_ALERT "%s:%d Failed rbtree insert \n",
				__FUNCTION__,__LINE__);
		retval = -EFAULT;
		goto err_creatfp;
	}
	/* allocate file queues and other structures*/
	rd = devfs_init_file_struct(qentrycnt);
	if (!rd) {
		printk(KERN_ALERT "Failed init file struct %s:%d\n",
				__FUNCTION__, __LINE__);
		retval = -EFAULT;
		goto err_creatfp;
	}
	fp->cmdbuf = (void *)rd;

	/*Create back reference*/
	rd->fp = fp;
	rd->fd = fd;

	/* update security credentials*/
	devfs_set_cred(rd);

	if (!kernel_call && copy_to_user((void __user *)arg, &map, minsz)) {
		printk(KERN_ALERT "DEBUG: Failed %s:%d %lu\n",
				__FUNCTION__, __LINE__, minsz);
		retval = -EFAULT;
		goto err_creatfp;
	}else if (kernel_call) {
		mapptr->fd = fd;
	}
	pmfs_dbg_verbose("%s:%d file descriptor %d success\n", 
			__FUNCTION__,__LINE__, fd);

#ifdef _DEVFS_SCALABILITY
	retval = devfs_scalability_open_setup(ei, rd);
	if (retval != 0)
		goto err_creatfp;
#endif

	return 0;

err_creatfp:
	return retval;
}
EXPORT_SYMBOL(vfio_creatfile_cmd);


static inline void w32(nvme_device_t* dev, u32* addr, u32 val)
{
	//DEBUG("w32 %#lx %#x", (u64) addr - (u64) dev->reg, val);
	*addr = val;
}

#if 0

/**
 * Create an IO queue pair of completion and submission.
 * @param   dev         device context
 * @param   id          queue id
 * @param   qsize       queue size
 * @param   sqbuf       submission queue buffer
 * @param   sqpa        submission queue IO physical address
 * @param   cqbuf       completion queue buffer
 * @param   cqpa        admin completion IO physical address
 * @return  pointer to the created io queue or NULL if failure.
 */
nvme_queue_t* nvme_create_ioq(nvme_device_t* dev, int id, int qsize,
		void* sqbuf, u64 sqpa, void* cqbuf, u64 cqpa)
{
	nvme_queue_t* ioq = kmalloc(sizeof(*ioq), GFP_KERNEL);
	ioq->dev = dev;
	ioq->id = id;
	ioq->size = qsize;
	ioq->sq = sqbuf;
	ioq->cq = cqbuf;
	ioq->sq_doorbell = dev->reg->sq0tdbl + (2 * id * dev->dbstride);
	ioq->cq_doorbell = ioq->sq_doorbell + dev->dbstride;

	if (nvme_acmd_create_cq(ioq, cqpa) || nvme_acmd_create_sq(ioq, sqpa)) {
		free(ioq);
		return NULL;
	}
	return ioq;
}
#endif


/**
 * Check a completion queue and return the completed command id and status.
 * @param   q           queue
 * @param   stat        completion status returned
 * @return  the completed command id or -1 if there's no completion.
 */
int devfs_check_completion(nvme_queue_t* q, int* stat)
{
	nvme_cq_entry_t* cqe = &q->cq[q->cq_head];
	*stat = 0;

	if (cqe->p == q->cq_phase) return -1;

	q->sq_head = cqe->sqhd;
	*stat = cqe->psf & 0xfe;
	if (++q->cq_head == q->size) {
		q->cq_head = 0;
		q->cq_phase = !q->cq_phase;
	}
	w32(q->dev, q->cq_doorbell, q->cq_head);

	if (*stat == 0) {
		printk(KERN_ALERT "q=%d cid=%#x (C) \n", q->id, cqe->cid);
	} else {
		printk(KERN_ALERT "%s:%d q=%d cid=%#x stat=%#x "
				"(dnr=%d m=%d sct=%d sc=%#x) (C)\n",
				__FUNCTION__,__LINE__, q->id, cqe->cid,
				*stat, cqe->dnr, cqe->m, cqe->sct, cqe->sc);
	}
	return cqe->cid;
}


/**
 * NVMe submit a read write command.
 * @param   ioq         io queue
 * @param   opc         op code
 * @param   cid         command id
 * @param   nsid        namespace
 * @param   slba        startling logical block address
 * @param   nlb         number of logical blocks
 * @param   prp1        PRP1 address
 * @param   prp2        PRP2 address
 * @return  0 if ok else -1.
 */
int devfs_create_rw_cmd(nvme_queue_t* ioq, nvme_cmdrw_t* rqcmd)
{
	nvme_cmdrw_t* cmd = &ioq->sq[ioq->sq_tail].rw;
	memset(cmd, 0, sizeof (*cmd));
	cmd->common.opc = rqcmd->common.opc;
	cmd->common.cid = rqcmd->common.cid;
	cmd->common.nsid = rqcmd->common.nsid;
	cmd->common.prp1 = rqcmd->common.prp1;
	cmd->common.prp2 = rqcmd->common.prp2;
	cmd->slba = rqcmd->slba;
	cmd->nlb = rqcmd->nlb - 1;

	/*printk(KERN_ALERT "q=%d t=%d cid=%#x nsid=%d lba=%llu nb=%d (%c)\n", ioq->id,
    	ioq->sq_tail, rqcmd->common.cid, rqcmd->common.nsid, rqcmd->slba, rqcmd->nlb, 
	cmd->common.opc == NVME_CMD_READ? 'R' : 'W');*/

	return 0;
}


/* Perform IO using the file descriptor */
long vfio_submitio_cmd(unsigned long arg){

	unsigned long minsz;
	struct vfio_devfs_rw_cmd map;
	unsigned long start_addr;
	nvme_cmdrw_t *cmdrw = NULL;
	long retval = 0;
	int i = 0, buf_idx = 0;
	uint32_t mask = VFIO_DMA_MAP_FLAG_READ |
			VFIO_DMA_MAP_FLAG_WRITE;
	int fd = -1;
	struct devfs_fstruct *rd = NULL;

#ifdef _DEVFS_SCALABILITY
	nvme_cmdrw_t kcmdrw;

	struct circ_buf *fifo = NULL;
	void *ptr = NULL;
	struct inode *inode = NULL;
	struct devfs_inode *ei = NULL;
	struct file *file = NULL;
	loff_t index = 0;
	char *fptr = NULL;
	void *dest = NULL;
#endif

#ifdef _DEVFS_OVERHEAD_BREAKDOWN
	struct timespec start;
	struct timespec end;
	struct timespec read_start;
	struct timespec read_end;
	struct timespec double_copy_start;
	struct timespec double_copy_end;
#endif

#ifdef _DEVFS_OVERHEAD_BREAKDOWN
	getnstimeofday(&start);
#endif


#ifdef _DEVFS_DEBUG_ENT
	printk(KERN_ALERT "DEBUG: Calling %s:%d \n",__FUNCTION__,__LINE__);
#endif

	minsz = offsetofend(struct vfio_devfs_rw_cmd, cmd_count);

	if (copy_from_user(&map, (void __user *)arg, minsz)) {
		printk(KERN_ALERT "DEBUG: Failed %s:%d \n",__FUNCTION__,__LINE__);
		retval = -EFAULT;
		goto err_submit_io;
	}

	start_addr = (unsigned long)map.vaddr;
	if(!start_addr) {
		printk(KERN_ALERT "DEBUG: Failed %s:%d \n",__FUNCTION__,__LINE__);
		retval = -EFAULT;
		goto err_submit_io;
	}

    /*cmdrw = kmalloc(sizeof(nvme_cmdrw_t), GFP_KERNEL);
	if (!cmdrw) {
		printk(KERN_ALERT "DEBUG: Failed %s:%d \n",__FUNCTION__,__LINE__);
		retval = -EFAULT;               
        goto err_submit_io;             
    }*/
	cmdrw = &kcmdrw;

	fd = map.fd;
	rd = fd_to_queuebuf(fd);

	//file structure does not exist for exisiting files opened first time
	if(!rd) {
  	      rd = devfs_init_file_struct(g_qentrycnt);
	}

	if(!rd) {
		printk(KERN_ALERT "DEBUG: Failed %s:%d \n",__FUNCTION__,__LINE__);
		goto err_submit_io;
	}

	if(!rd->fp) {
		rd->fp = fget(fd);
	}
	if(!rd->fp) {
		printk(KERN_ALERT "Null file pointer %s:%d \n",__FUNCTION__,__LINE__);
		goto err_submit_io;
	}

	if (map.argsz < minsz || map.flags & ~mask) {
		printk(KERN_ALERT "DEBUG: Failed %s:%d \n",__FUNCTION__,__LINE__);
		retval = -EFAULT;
		goto err_submit_io;
	}

	for( i=0; i< map.cmd_count; i++) {

		//cmdrw = (nvme_cmdrw_t *) start_addr;

        if (copy_from_user(cmdrw, (void __user*)start_addr, sizeof(nvme_cmdrw_t))) {
                    printk(KERN_ALERT "DEBUG: Failed %s:%d \n",__FUNCTION__,__LINE__);
                    retval = -EFAULT;
                    goto err_submit_io;
        }

		/*TODO: Add TSC to the commands */
		//cmdrw->cmdtsc = rdtsc();

	#ifdef _DEVFS_DEBUG
		DEBUGCMD(cmdrw);
	#endif

		/* Check the command opcode type
		 * For reads, we currently do an immediate read by copying the
		 * the data into user space buffer.
		 * Multiple commands can be supported at the same time
		 */
#ifdef _DEVFS_SCALABILITY
		if ( cmdrw->common.opc == nvme_cmd_read ||
			 cmdrw->common.opc == nvme_cmd_write || 
			 cmdrw->common.opc == nvme_cmd_append) {

			fifo = &rd->fifo;
			file = rd->fp;

			index = 0;

			if(!file) {
				printk(KERN_ALERT "%s:%d Failed \n",__FUNCTION__, __LINE__);	
				retval = -EFAULT;
				goto err_submit_io;
			}
			inode = file->f_inode;
			if(!inode) {
				printk(KERN_ALERT "%s:%d Failed \n",__FUNCTION__, __LINE__);	
				retval = -EFAULT;
				goto err_submit_io;
			}

			ei = DEVFS_I(inode);
			if(!ei) {
				printk(KERN_ALERT "%s:%d Failed \n",__FUNCTION__, __LINE__);	
				retval = -EFAULT;
				goto err_submit_io;
			}

			/* If it is a lazy queue allocation, then initialize buffers */
#if defined(_DEVFS_ONDEMAND_QUEUE)
			if(!rd->fqinit) {
				//Initialize queue buffers and also set fqinit
				if(devfs_init_file_queue(rd)) {
					printk(KERN_ALERT "%s:%d Queue buffer failed \n",
							__FUNCTION__, __LINE__);
					retval = -EFAULT;
					goto err_submit_io;
				}
			}
#endif

			if(!rd || rd->fifo.buf == NULL) {
				printk(KERN_ALERT "%s:%d Failed buf NULL \n",__FUNCTION__, __LINE__);
				retval = -EFAULT;
				goto err_submit_io;
			}

			//printk(KERN_ALERT "rd->queuesize=%d, rd->num_entries=%d\n", rd->queuesize, rd->num_entries);

			if (rd->fifo.head == rd->queuesize) {

				/* Perform IO when the queue buff is full
				 * Wake up per file pointer kthread to handle IO
				 */
#ifdef _DEVFS_SCALABILITY_DBG
				printk(KERN_ALERT "^^ rd->fifo.head = %d\n", rd->fifo.head);
#endif
				
				if (cmdrw->common.opc != nvme_cmd_read) {
					rd->req = cmdrw;

#ifdef _DEVFS_KTHRD_SEMAPHORE
					down_interruptible(&rd->ksem);
#else
					while(test_and_clear_bit(0, &rd->io_done) == 0);
#endif

#ifdef _DEVFS_SCALABILITY_DBG
					printk(KERN_ALERT "resume main thread\n");
#endif
				}
				mutex_lock(&rd->read_lock);

				fifo->head = fifo->tail = 0;
				rd->num_entries = 0;
			
				mutex_unlock(&rd->read_lock);
			}

			mutex_lock(&rd->read_lock);

			/* insert new request to the rd queue */
			buf_idx = fifo->head / rd->entrysize;
			//printk(KERN_ALERT "buf_idx = %d\n", buf_idx);
			cmdrw->blk_addr = (u64)rd->fsblocks[buf_idx];

			fptr = &fifo->buf[fifo->head];
			memcpy(fptr, cmdrw, sizeof(nvme_cmdrw_t));
			fifo->head = fifo->head + rd->entrysize;

			/* update TSC for current jiffies */
			rd->tsc = jiffies;

			ptr = (void *)cmdrw->common.prp2;

			if (cmdrw->common.opc == nvme_cmd_write ||
				cmdrw->common.opc == nvme_cmd_append) {

					if (cmdrw && ptr) {
						/* enqueue per file pointer submission queue */
						//if (rd->num_entries >= rd->qentrycnt) {
						if (buf_idx >= rd->qentrycnt) {
							printk(KERN_ALERT "%s, %d buffer is full, idx = %d \n", __FUNCTION__, __LINE__, buf_idx);
							retval = -EFAULT;
							goto err_submit_io;
						}

						//dest = (void *)rd->fsblocks[rd->num_entries];
						dest = (void *)cmdrw->blk_addr;
						if (!dest) {
							printk(KERN_ALERT "%s, %d destination %d null \n",
									__FUNCTION__, __LINE__, rd->num_entries);
							retval = -EFAULT;
							goto err_submit_io;
						}

						/* Copy data from user to device */
						if (copy_from_user(dest, (void __user *)ptr, cmdrw->nlb)) {
							printk(KERN_ALERT "page copy failed %lu\n", (unsigned long)ptr);
							retval = -EFAULT;
							goto err_submit_io;
						}

						/* Add index in per inode nv radix tree */
						index = (loff_t)cmdrw->slba >> PAGE_CACHE_SHIFT;

#ifdef _DEVFS_SCALABILITY_DBG
						printk(KERN_ALERT "rd = %llx | insert index = %lld, dest = %llx\n",
							(__u64)rd, index, (__u64)dest);
#endif
						spin_lock(&ei->sq_tree_lock);
						radix_tree_insert(&ei->sq_tree, index, dest);
						spin_unlock(&ei->sq_tree_lock);

						//wake_up_all(&rd->fifo_event);
							
						/* we don't need the following update! It will screw inode size */
						//devfs_inode_update(inode, (loff_t)cmd->slba);
						retval = cmdrw->nlb;
					} else {
						retval = -EFAULT;
						printk(KERN_ALERT "%s:%d Incorrect command or data buffer \n",
								__FUNCTION__, __LINE__);
						goto err_submit_io;
					}
			}
			rd->num_entries++;

			mutex_unlock(&rd->read_lock);

#ifdef _DEVFS_SCALABILITY_DBG
			printk(KERN_ALERT "after increment, rd->num_entries=%d\n", rd->num_entries);	
#endif

			/* wait until read finishes */
			if (cmdrw->common.opc == nvme_cmd_read) {
#ifdef _DEVFS_OVERHEAD_READ
				getnstimeofday(&read_start);
#endif

#ifdef _DEVFS_SCALABILITY_DBG
				printk(KERN_ALERT "uptr = %llx\n", cmdrw->common.prp2);
#endif
				rd->req = (nvme_cmdrw_t*)rd_read_tail(rd, sizeof(nvme_cmdrw_t), &rd->fifo.tail);

#ifdef _DEVFS_KTHRD_SEMAPHORE
				down_interruptible(&rd->ksem);
#else
				while(test_and_clear_bit(0, &rd->io_done) == 0);
#endif

#ifdef _DEVFS_OVERHEAD_READ
				getnstimeofday(&read_end);
				rd->t_read += ((read_end.tv_sec*1000000000 + read_end.tv_nsec) -
                   (read_start.tv_sec*1000000000 + read_start.tv_nsec));
#endif


#ifdef _DEVFS_SCALABILITY_DBG
				printk(KERN_ALERT "read is done\n");
#endif
				retval = rd->iobytes;
				
				/* Last thing is do data copy to user buffer */
				cmdrw = (nvme_cmdrw_t *)rd_read(rd, sizeof(nvme_cmdrw_t));

#ifdef _DEVFS_SCALABILITY_DBG
				printk(KERN_ALERT "~~~~~~~ main process read, cmd = %llx, prp1 = %llx, prp1 = %s\n", 
					(__u64)cmdrw, cmdrw->common.prp1, (char *)cmdrw->common.prp1);
#endif

				/* Reaching end of file */
				if (cmdrw->common.prp1 && *(char*)cmdrw->common.prp1 == '\0') {
					printk(KERN_ALERT "reaching EOF, slba = %d, bytes = %d\n", cmdrw->slba, rd->iobytes);
					rd->num_entries--;
					retval = 0;
					goto err_submit_io;
				}

#ifdef _DEVFS_OVERHEAD_COPY
				getnstimeofday(&double_copy_start);
#endif

				if (copy_to_user((void __user *)cmdrw->common.prp2, (void *)cmdrw->common.prp1, cmdrw->nlb)) {
					rd->num_entries--;
					printk(KERN_ALERT "DEBUG: Failed %s:%d \n",__FUNCTION__,__LINE__);
					retval = -EFAULT;
					goto err_submit_io;
				}

#ifdef _DEVFS_OVERHEAD_COPY
				getnstimeofday(&double_copy_end);
				rd->t_double_copy += ((double_copy_end.tv_sec*1000000000 + double_copy_end.tv_nsec) -
                   (double_copy_start.tv_sec*1000000000 + double_copy_start.tv_nsec));
#endif

				//fifo->head = fifo->tail = 0;
				//rd->num_entries = 0
				//fifo->tail = fifo->tail + rd->entrysize;

				/*if (fifo->tail == rd->queuesize)
					fifo->tail = 0;*/

				rd->num_entries--;


			}

		} /*else if ( cmdrw->common.opc == nvme_cmd_read ) { 
			retval = vfio_devfs_io_read(rd, cmdrw, 1);
		}*/ else if ( cmdrw->common.opc == nvme_cmd_lseek ) {
			// TODO
			//retval =  (long) devfs_llseek (rd,(void *)cmdrw);
		} else {
			printk(KERN_ALERT "%s:%d Invalid command op code\n",
					__FUNCTION__,__LINE__);
			// TODO
		}

#else
		if ( cmdrw->common.opc == nvme_cmd_read ) {
			retval = vfio_devfs_io_read (rd, cmdrw, 1);

			if(iskernelio) 
				devfs_rwtest(rd, (void *)cmdrw, 
						sizeof(nvme_cmdrw_t), fd, 0);

		} else if ( cmdrw->common.opc == nvme_cmd_write ) {

			start_addr += sizeof(nvme_cmdrw_t);

	#if defined(_DEVFS_DIRECTIO)
			retval =  devfs_direct_write (rd,(void *)cmdrw, 0);
	#else
			retval = rd_write(rd, (void *)cmdrw, sizeof(nvme_cmdrw_t), fd, 0);
	#endif
			if(iskernelio) 
				devfs_rwtest(rd, (void *)cmdrw, 
						sizeof(nvme_cmdrw_t), fd, 0);

		}else if ( cmdrw->common.opc == nvme_cmd_append ) {

			start_addr += sizeof(nvme_cmdrw_t);

	#if defined(_DEVFS_DIRECTIO)
			retval =  devfs_direct_write (rd,(void *)cmdrw, 1);
	#else
			retval = rd_write(rd, (void *)cmdrw, sizeof(nvme_cmdrw_t), fd, 1);
	#endif
			if(iskernelio) 
				devfs_rwtest(rd, (void *)cmdrw, 
						sizeof(nvme_cmdrw_t), fd, 1);

		}else if ( cmdrw->common.opc == nvme_cmd_lseek ) {

			start_addr += sizeof(nvme_cmdrw_t);

	#if defined(_DEVFS_DIRECTIO)
			retval =  (long) devfs_llseek (rd,(void *)cmdrw);
	#else
			//TODO: QUEUE lskeep command
			//retval = rd_write(rd, (void *)cmdrw, sizeof(nvme_cmdrw_t), fd, 1);
	#endif
			//if(iskernelio) 
			//	devfs_rwtest(rd, (void *)cmdrw, 
			//			sizeof(nvme_cmdrw_t), fd, 1);

#if defined(_DEVFS_DEBUG_RDWR)
			 printk(KERN_ALERT "%s:%d Returning devfs_llseek %ld \n",
					__FUNCTION__, __LINE__, retval);
#endif
		}
		else {
			printk(KERN_ALERT "%s:%d Invalid command op code\n",
					__FUNCTION__,__LINE__);
			retval = -EFAULT;
			goto err_submit_io;
		}
#endif
	}
err_submit_io:

#ifdef _DEVFS_OVERHEAD_BREAKDOWN
	if (cmdrw->common.opc == nvme_cmd_read) {
		getnstimeofday(&end);
		rd->t_vfio_submitio_cmd += ((end.tv_sec*1000000000 + end.tv_nsec) -
            (start.tv_sec*1000000000 + start.tv_nsec));
		++rd->read_op_count;
	}
#endif	

	/*if (cmdrw)
		kfree(cmdrw);*/

	return retval;
}
EXPORT_SYMBOL(vfio_submitio_cmd);


#if 0
int copy_dentry(struct dentry *src, struct dentry *dest){


	//memcpy(dest, src, sizeof(struct dentry));
	dest->d_parent = src->d_parent;
	dest->d_inode = src->d_inode;
	dest->d_sb = src->d_sb;
	dest->d_fsdata = src->d_fsdata;
	dest->d_lock = src->d_lock;
	dest->d_inode = src->d_inode;
	dest->d_subdirs = src->d_subdirs;
	devfs_dbgv(KERN_ALERT "dest->d_name.name %s \n", dest->d_name.name);
	return 0;
#if 0
	/* RCU lookup touched fields */
	unsigned int d_flags;           /* protected by d_lock */
	seqcount_t d_seq;               /* per dentry seqlock */
	struct hlist_bl_node d_hash;    /* lookup hash list */
	struct dentry *d_parent;        /* parent directory */
	struct qstr d_name;
	struct inode *d_inode;          /* Where the name belongs to - NULL is
	 * negative */
	unsigned char d_iname[DNAME_INLINE_LEN];        /* small names */

	/* Ref lookup also touches following */
	unsigned int d_count;           /* protected by d_lock */
	spinlock_t d_lock;              /* per dentry lock */
	const struct dentry_operations *d_op;
	struct super_block *d_sb;       /* The root of the dentry tree */
	unsigned long d_time;           /* used by d_revalidate */
	void *d_fsdata;                 /* fs-specific data */

	struct list_head d_lru;         /* LRU list */
	/*
	 * d_child and d_rcu can share memory
	 */
	union {
		struct list_head d_child;       /* child of parent list */
		struct rcu_head d_rcu;
	} d_u;
	struct list_head d_subdirs;     /* our children */
	struct hlist_node d_alias;      /* inode alias list */
#endif
}
#endif


/* Close DEVFS file and its in-memory file structures */
int devfs_close(struct inode *inode, struct file *fp){

	int retval = 0;
	struct devfs_fstruct *rd = NULL;
	struct devfs_inode *ei = NULL;
#if defined(_DEVFS_DENTRY_OFFLOAD)
	struct dentry *dentry = NULL;
	struct dentry *host_dentry = NULL;
#endif
#if defined(_DEVFS_INODE_OFFLOAD)
	struct super_block *sb = NULL;
#endif


#if defined(_DEVFS_DEBUG_ENT)
	printk(KERN_ALERT "DEBUG: Calling %s:%d \n",__FUNCTION__,__LINE__);
#endif
	rd = fp->cmdbuf;
	if(!rd) {
		printk(KERN_ALERT "DEBUG: Failed %s:%d \n",__FUNCTION__,__LINE__);
		retval = -EFAULT;
		goto err_rel;
	}

	/*Get DevFS inode info from inode*/
	ei = DEVFS_I(fp->f_inode);
	if(!ei) {
		printk(KERN_ALERT "%s:%d DevFS inode info NULL \n",
				__FUNCTION__,__LINE__);
		retval = -EFAULT;
		goto err_rel;
	}

#ifdef _DEVFS_OVERHEAD_BREAKDOWN
	if (rd->read_op_count > 0) {
		printk(KERN_ALERT "Count = %lu, Average latency for vfio_submitio_cmd = %lu ns\n", 
			rd->read_op_count, rd->t_vfio_submitio_cmd / rd->read_op_count);

#ifdef _DEVFS_OVERHEAD_KTHRD
		printk(KERN_ALERT "Count = %lu, Average latency for devfs_io_handler = %lu ns / %lu\n", 
			rd->read_op_count, rd->t_devfs_io_handler / rd->read_op_count, 
			rd->t_vfio_submitio_cmd / rd->read_op_count);
#endif

#ifdef _DEVFS_OVERHEAD_READ
		printk(KERN_ALERT "Count = %lu, Average latency for read = %lu ns / %lu\n", 
			rd->read_op_count, rd->t_read / rd->read_op_count, 
			rd->t_vfio_submitio_cmd / rd->read_op_count);
#endif

		/*printk(KERN_ALERT "Count = %lu, Average latency for synchronization = %lu us / %lu\n", 
			rd->read_op_count, (rd->t_read - rd->t_devfs_io_handler) / rd->read_op_count, 
			rd->t_vfio_submitio_cmd / rd->read_op_count);*/
#ifdef _DEVFS_OVERHEAD_COPY
		printk(KERN_ALERT "Count = %lu, Average latency for double copy  = %lu ns / %lu\n", 
			rd->read_op_count, rd->t_double_copy / rd->read_op_count, 
			rd->t_vfio_submitio_cmd / rd->read_op_count);
#endif
	}
#endif


#ifdef  _DEVFS_FSTATS
	printk(KERN_ALERT "Read queue hits %llu \n", rd->rd_qhits);
	printk(KERN_ALERT "Total writes %llu \n", rd->rd_wrsize);
#endif
	/* Release in-memory file structures */
	if (devfs_free_file_queue(rd)) {
		printk(KERN_ALERT "Failed: DevFS cleanup %s:%d \n",
				__FUNCTION__, __LINE__);
		retval = -EFAULT;
		goto err_rel;
	}

#if defined(_DEVFS_DEBUG)
	printk(KERN_ALERT "Finished devfs_free_file_queue %s:%d \n",
			__FUNCTION__, __LINE__);
#endif

#ifdef _DEVFS_SCALABILITY
	devfs_scalability_close_setup(ei, rd);
#endif

	//Free the file structure during close
	/*if (rd) {
		kfree(rd);
		fp->cmdbuf = NULL;
		rd = NULL;
	}*/

#if defined(_DEVFS_DELETE_JOURN_COLD)
	if(ei->isjourn)
		free_journal(fp->f_inode);
#endif

#if defined(_DEVFS_INODE_OFFLOAD)
	dentry = fp->f_path.dentry;
	sb = fp->f_inode->i_sb;

	BUG_ON(!sb);
	BUG_ON(!dentry);
	BUG_ON(!fp->f_inode);

	inode_offld_to_host(sb, fp->f_inode, dentry);
#endif

#if defined(_DEVFS_DENTRY_OFFLOAD)
	struct devfs_sb_info *sbi = DEVFS_SB(inode->i_sb);

	if(!sbi->dentrysize) {
		printk(KERN_ALERT "%s:%d FAILED Invalid dentrysize \n",
				__FUNCTION__,__LINE__);
		goto skip_offload;
	}

	if(!sbi->d_host_addr) {
		printk(KERN_ALERT "%s:%d FAILED Invalid d_host_addr \n",
				__FUNCTION__,__LINE__);
		goto skip_offload;
	}
	printk(KERN_ALERT "%s:%d dentry off load \n",  __FUNCTION__,__LINE__);
	dentry = fp->f_path.dentry;

#if defined(_DEVFS_SLAB_ALLOC)
	BUG_ON(!sbi->d_host_slab);
	host_dentry = (struct dentry *)devfs_slab_alloc(sbi, sbi->d_host_slab,
			&sbi->d_host_addr, &sbi->d_host_off);
#else
	host_dentry = (struct dentry *)(sbi->d_host_addr + sbi->d_host_off);
	sbi->d_host_off += sizeof(struct dentry);
#endif

	if(!host_dentry) {
		printk(KERN_ALERT "%s:%d FAILED invalid dest host_dentry\n",
				__FUNCTION__,__LINE__);
	}

	//TODO: Incorrect return type. Takes host_dentry and returns the same
	host_dentry = devfs_dentry_alloc(dentry->d_parent, &dentry->d_name,
				host_dentry, dentry);

	if(devfs_dentry_move_host(dentry, host_dentry)) {
		printk(KERN_ALERT "%s:%d FAILED dentry_free \n",
				__FUNCTION__,__LINE__);
	}
	//Enable isdevfs so that dcache does not try to 
	//use slab allocator to delete it
	host_dentry->devfs_host = 1;
	fp->f_path.dentry = host_dentry;
	skip_offload:
#endif


	//fsnotify_close(fp);
	//fput(fp);
	fp->f_pos = 0;
	filp_close(fp, NULL);

	//#if !defined(_DEVFS_ONDEMAND_QUEUE)
	/*Remove the DevFS inode info structure from the rbtree*/
	//del_inode_rbtree(g_inotree, ei);
	//#endif

	/*Evict dentry on a file close if using aggresive mode 
	Assumes that inode is not NUL
	 */
#if defined(_DEVFS_AGGR_DENTRY_EVICT)
	dentry = d_find_any_alias(inode);
	if(!dentry) {
		printk(KERN_ALERT "%s:%d dentry NULL \n",__FUNCTION__,__LINE__);
	}
	else {
		/*Set dentry flag with delete operation */
		dentry->d_flags |= DCACHE_OP_DELETE;
		dput(dentry);
	}
#endif

#if defined(_DEVFS_DEBUG)
	printk(KERN_ALERT "%s:%d Success \n",__FUNCTION__,__LINE__);
#endif

	return 0;
err_rel:
	return retval;
}



/* Close DEVFS file and its in-memory file structures */
int vfio_close_cmd(unsigned long arg){

	unsigned long minsz;
	struct vfio_devfs_closefd_cmd map;
	int retval = 0, fd = -1;
	struct file *fp = NULL;
	struct devfs_sb_info *sbi = NULL;

#if defined(_DEVFS_DEBUG_ENT)
	printk(KERN_ALERT "DEBUG: Calling %s:%d \n",__FUNCTION__,__LINE__);
#endif

	minsz = offsetofend(struct vfio_devfs_closefd_cmd, fd);

	if (copy_from_user(&map, (void __user *)arg, minsz)) {
		printk(KERN_ALERT "DEBUG: Failed %s:%d \n",__FUNCTION__,__LINE__);
		retval = -EFAULT;
		goto err_closefd;
	}

	fd = map.fd;
	fp = fget(fd);

	BUG_ON(!fp->isdevfs);
	BUG_ON(!fp->f_inode);

	sbi = DEVFS_SB(fp->f_inode->i_sb);

	devfs_dbgv("%s:%d fd %d, inodeoffz %lu\n", __FUNCTION__,__LINE__, 
			fd, sbi->inodeoffsz);

#ifdef _DEVFS_SCALABILITY_DBG
	struct inode *inode = fp->f_inode;
	printk(KERN_ALERT "inode=%llx, inode->i_size=%llu\n", (__u64)inode, inode->i_size);
#endif


	if(devfs_close(fp->f_inode, fp)) {
		printk(KERN_ALERT "DEBUG: Failed %s:%d \n",__FUNCTION__,__LINE__);
		retval = -EFAULT;
		goto err_closefd;
	}


err_closefd:
	return retval;
}

/* 
 * initialize firmware level file system 
 */
static int vfio_creatfs_cmd(unsigned long arg){

	unsigned long minsz;
	struct vfio_devfs_creatfs_cmd map;
	int retval = 0;
	uint32_t mask = VFIO_DMA_MAP_FLAG_READ |
			VFIO_DMA_MAP_FLAG_WRITE;
#ifdef _DEVFS_DEBUG_ENT
	printk(KERN_ALERT "DEBUG: Calling %s:%d \n",__FUNCTION__,__LINE__);
#endif

	minsz = offsetofend(struct vfio_devfs_creatfs_cmd, sched_policy);

	if (copy_from_user(&map, (void __user *)arg, minsz)) {
		printk(KERN_ALERT "DEBUG: Failed %s:%d \n",__FUNCTION__,__LINE__);
		retval = -EFAULT;
		goto err_creatfs;
	}

	/*start_addr = (unsigned long)map.vaddr;
		if(!start_addr) {
		      printk(KERN_ALERT "DEBUG: Failed %s:%d \n",__FUNCTION__,__LINE__);
		      retval = -EFAULT;
		      goto err_creatfs;
		}*/

	if (map.argsz < minsz || map.flags & ~mask) {
		printk(KERN_ALERT "DEBUG: Failed %s:%d \n",__FUNCTION__,__LINE__);
		retval = -EFAULT;
		goto err_creatfs;
	}

	if (!g_devfs_scheduler_init) {	
		/* 
		 * Initialize I/O scheduler
		 * Create device threads to handle I/O requests
		 */
		devfs_scheduler_init(map.dev_core_cnt, map.sched_policy);
	}
	
	if(g_devfs_init) {
#if defined(_DEVFS_DEBUG)
		printk(KERN_ALERT "DEBUG: %s:%d DevFS already initialized \n",
			__FUNCTION__,__LINE__);
#endif
		goto err_creatfs;
	}

	/*Create inode tree list */
	if(!g_devfs_init)
		g_inotree = devfs_inode_list_create();


	/*Initialize DevFS file system emulation*/
	g_devfs_init = 1;

#ifdef _DEVFS_SCALABILITY_DBG
	printk(KERN_ALERT "DEBUG: Success %s:%d \n",__FUNCTION__,__LINE__);
#endif

err_creatfs:
	return retval;
}


/*
 * nvmed_get_buffer_addr
 * translate virtual address to physical address, set reserved flags
 */
static int vfio_creatq_cmd(unsigned long arg){

	unsigned long minsz;
	struct vfio_iommu_type1_queue_map map;
	struct task_struct *task = current;
	struct mm_struct *mm;
	unsigned long vaddr, start_addr;
	int i=0, retval=0;
	__u64* qpfnlist;
	struct page *page_info;

	pgd_t *pgd;
	pte_t *ptep, pte;
	pud_t *pud;
	pmd_t *pmd;
	p4d_t *p4d;

	uint32_t mask = VFIO_DMA_MAP_FLAG_READ |
			VFIO_DMA_MAP_FLAG_WRITE;

	pmfs_dbg("Enter Calling %s:%d \n",__FUNCTION__,__LINE__);

	minsz = offsetofend(struct vfio_iommu_type1_queue_map, qpfns);

	if (copy_from_user(&map, (void __user *)arg, minsz)) {
		printk(KERN_ALERT "DEBUG: Failed %s:%d \n",__FUNCTION__,__LINE__);
		retval = -EFAULT;
		goto ret_qmap;
	}

	start_addr = (unsigned long)map.vaddr;
	if(!start_addr) {
		printk(KERN_ALERT "DEBUG: Failed %s:%d \n",__FUNCTION__,__LINE__);
		retval = -EFAULT;
		goto ret_qmap;
	}

	if (map.argsz < minsz || map.flags & ~mask) {
		printk(KERN_ALERT "DEBUG: Failed %s:%d \n",__FUNCTION__,__LINE__);
		retval = -EFAULT;
		goto ret_qmap;
	}
	if (map.qpfns > _DEVFS_QUEUE_PAGES) {
		printk(KERN_ALERT "DEBUG: Failed %s:%d %llu\n",__FUNCTION__,__LINE__, map.qpfns);
		retval = -EFAULT;
		goto ret_qmap;
	}

	qpfnlist = kzalloc(sizeof(__u64) * map.qpfns, GFP_KERNEL);
	if(!qpfnlist) {
		printk(KERN_ALERT "DEBUG: Failed %s:%d %llu\n",__FUNCTION__,__LINE__, map.qpfns);
		retval = -EFAULT;
		goto ret_qmap;
	}
	mm = task->mm;
	if(!mm) {
		printk(KERN_ALERT "DEBUG: Failed %s:%d \n",__FUNCTION__,__LINE__);
		retval = -EFAULT;
		goto ret_qmap;
	}
	down_read(&mm->mmap_sem);

	pmfs_dbg("vfio_creatq_cmd page mapping pages %llu\n",map.qpfns);

	for(i=0; i<map.qpfns; i++) {

		vaddr = start_addr + (PAGE_SIZE * i);

		pgd = pgd_offset(mm, vaddr);
		if(pgd_none(*pgd) || pgd_bad(*pgd)) {
			printk(KERN_ALERT "DEBUG: Failed %s:%d \n",__FUNCTION__,__LINE__);
			retval = -EFAULT;
			break;
		}

		p4d = p4d_offset(pgd, vaddr);
	        if (!p4d_present(*p4d)) {
			printk(KERN_ALERT "DEBUG: Failed %s:%d \n",__FUNCTION__,__LINE__);
			retval = -EFAULT;		
        	        break;
		}

		pud = pud_offset(p4d, vaddr);
		if(pud_none(*pud) || pud_bad(*pud)) {
			printk(KERN_ALERT "DEBUG: Failed %s:%d \n",__FUNCTION__,__LINE__);
			retval = -EFAULT;
			break;
		}
		pmd = pmd_offset(pud, vaddr);
		if(!pmd_none(*pmd) &&
				(pmd_val(*pmd) & (_PAGE_PRESENT|_PAGE_PSE)) != _PAGE_PRESENT) {
			pte = *(pte_t *)pmd;
			qpfnlist[i] = (pte_pfn(pte) << PAGE_SHIFT) + (vaddr & ~PMD_MASK);
			continue;
		}
		else if (pmd_none(*pmd) || pmd_bad(*pmd)) {
			retval = -EFAULT;
			printk(KERN_ALERT "DEBUG: Failed %s:%d \n",__FUNCTION__,__LINE__);
			break;
		}

		ptep = pte_offset_map(pmd, vaddr);
		if(!ptep) {
			printk(KERN_ALERT "DEBUG: Failed %s:%d \n",__FUNCTION__,__LINE__);
			retval = -EFAULT;
			break;
		}

		pte = *ptep;
		page_info = pte_page(pte);
		qpfnlist[i] = pte_pfn(pte) << PAGE_SHIFT;
		pte_unmap(ptep);
	}
	up_read(&mm->mmap_sem);

	/*if(!retval && qpfnlist) {
		    map.qpfnlist = qpfnlist;
		}*/

	if (copy_to_user(map.qpfnlist, qpfnlist, sizeof(__u64)*map.qpfns)) {
		printk(KERN_ALERT "DEBUG: Failed %s:%d %llu\n",__FUNCTION__,__LINE__,
				sizeof(__u64)*map.qpfns);
		retval = -EFAULT;
	}
	if (qpfnlist) {
		kfree(qpfnlist);
		qpfnlist = NULL;
	}
ret_qmap:
	return retval;
}

/* 
 * close firmware level file system 
 * exit all device threads
 */
static int vfio_closefs_cmd(unsigned long arg){

	unsigned long minsz;
	struct vfio_devfs_closefs_cmd map;
	int retval = 0;

#ifdef _DEVFS_DEBUG_ENT
	printk(KERN_ALERT "DEBUG: Calling %s:%d \n",__FUNCTION__,__LINE__);
#endif

	minsz = offsetofend(struct vfio_devfs_closefs_cmd, argsz);

	if (copy_from_user(&map, (void __user *)arg, minsz)) {
		printk(KERN_ALERT "DEBUG: Failed %s:%d \n",__FUNCTION__,__LINE__);
		retval = -EFAULT;
		goto err_closefs;
	}

	if (map.argsz < minsz) {
		printk(KERN_ALERT "DEBUG: Failed %s:%d \n",__FUNCTION__,__LINE__);
		retval = -EFAULT;
		goto err_closefs;
	}

	/* Exit device threads that handles I/O requests */
	devfs_scheduler_exit();

#ifdef _DEVFS_SCALABILITY_DBG
	printk(KERN_ALERT "DEBUG: Success %s:%d \n",__FUNCTION__,__LINE__);
#endif

err_closefs:
	return retval;
}


/* DEVFS fsync */
int vfio_fsync_cmd(unsigned long arg){

	unsigned long minsz;
	struct vfio_devfs_fsync_cmd map;
	int retval = 0, fd = -1;
	struct file *fp = NULL;
	struct devfs_fstruct *rd = NULL;
	struct circ_buf *fifo = NULL;
	char *fptr = NULL;

	nvme_cmdrw_t kcmdrw;
	nvme_cmdrw_t *cmdrw = &kcmdrw;

	cmdrw->common.opc = nvme_cmd_flush;	

#if defined(_DEVFS_DEBUG_ENT)
	printk(KERN_ALERT "DEBUG: Calling %s:%d \n",__FUNCTION__,__LINE__);
#endif

	minsz = offsetofend(struct vfio_devfs_fsync_cmd, fd);

	if (copy_from_user(&map, (void __user *)arg, minsz)) {
		printk(KERN_ALERT "DEBUG: Failed %s:%d \n",__FUNCTION__,__LINE__);
		retval = -EFAULT;
		goto err_fsync;
	}

	fd = map.fd;
	fp = fget(fd);

	BUG_ON(!fp->isdevfs);
	BUG_ON(!fp->f_inode);

	rd = fd_to_queuebuf(fd);

	if (!rd) {
		printk(KERN_ALERT "DEBUG: Failed %s:%d \n",__FUNCTION__,__LINE__);
		goto err_fsync;
	}

	if (!rd->fp) {
		rd->fp = fget(fd);
	}
	if (!rd->fp) {
		printk(KERN_ALERT "Null file pointer %s:%d \n",__FUNCTION__,__LINE__);
		goto err_fsync;
	}

#ifdef _DEVFS_SCALABILITY_DBG
	struct inode *inode = fp->f_inode;
	printk(KERN_ALERT "inode=%llx, inode->i_size=%llu\n", (__u64)inode, inode->i_size);
#endif

	/*if (devfs_fsync(rd)) {
		printk(KERN_ALERT "DEBUG: Failed %s:%d \n",__FUNCTION__,__LINE__);
		retval = -EFAULT;
		goto err_fsync;
	}*/

	if(!rd || rd->fifo.buf == NULL) {
		printk(KERN_ALERT "%s:%d Failed buf NULL \n",__FUNCTION__, __LINE__);
		retval = -EFAULT;
		goto err_fsync;
	}

	fifo = &rd->fifo;

	//printk(KERN_ALERT "rd->queuesize=%d, rd->num_entries=%d\n", rd->queuesize, rd->num_entries);

	if (rd->fifo.head == rd->queuesize) {

		/* Perform IO when the queue buff is full
		 * Wake up per file pointer kthread to handle IO
		 */
#ifdef _DEVFS_SCALABILITY_DBG
		printk(KERN_ALERT "^^ rd->fifo.head = %d\n", rd->fifo.head);
#endif

		rd->req = (nvme_cmdrw_t *)&fifo->buf[fifo->tail];

		//printk(KERN_ALERT "entry = %d, rd->req->common.opc = %d\n", rd->num_entries, rd->req->common.opc);

#ifdef _DEVFS_KTHRD_SEMAPHORE
		down_interruptible(&rd->ksem);
#else
		while(test_and_clear_bit(0, &rd->io_done) == 0);
#endif


#ifdef _DEVFS_SCALABILITY_DBG
		printk(KERN_ALERT "resume main thread\n");
#endif
		mutex_lock(&rd->read_lock);

		fifo->head = fifo->tail = 0;
		rd->num_entries = 0;

		mutex_unlock(&rd->read_lock);
	}

	/* insert new request to the rd queue */
	/*fptr = &fifo->buf[fifo->head];
	memcpy(fptr, cmdrw, sizeof(nvme_cmdrw_t));
	fifo->head = fifo->head + rd->entrysize;*/

	rd->req = cmdrw;

    //devfs_dbg("before, rd = %llx, rd->req = %llx | head=%d, tail=%d, entry=%d\n",
	//			(__u64)rd, rd->req, rd->fifo.head, rd->fifo.tail, rd->num_entries);


	/* update TSC for current jiffies */
	rd->tsc = jiffies;

	//rd->num_entries++;

#ifdef _DEVFS_KTHRD_SEMAPHORE
	down_interruptible(&rd->ksem);
#else
	while(test_and_clear_bit(0, &rd->io_done) == 0);
#endif

	//printk(KERN_ALERT "fsync return\n");

	/*
	fifo->head = fifo->tail = 0;
	rd->num_entries = 0;
	test_and_clear_bit(0, &rd->fsyncing);
	*/

    //devfs_dbg("after, rd = %llx, rd->req = %llx | head=%d, tail=%d, entry=%d\n",
	//			(__u64)rd, rd->req, rd->fifo.head, rd->fifo.tail, rd->num_entries);

err_fsync:
	return retval;
}


long devfs_ioctl(struct file *fp, unsigned int cmd, unsigned long arg)
{
	//New version of the OS would not allow using 
	void *iommu_data = (struct file *)fp;

	if (cmd == VFIO_IOMMU_GET_QUEUE_ADDR) {
		pmfs_dbg("DEVFS: ioctl VFIO_IOMMU_GET_QUEUE_ADDR\n");
		return vfio_creatq_cmd(arg);
	} else if (cmd ==  VFIO_DEVFS_RW_CMD) {
		return vfio_submitio_cmd(arg);
	} else if (cmd == VFIO_DEVFS_FSYNC_CMD) {
		return vfio_fsync_cmd(arg);
	} else if (cmd ==  VFIO_DEVFS_CREATFS_CMD) {
		pmfs_dbg("DEVFS: ioctl VFIO_DEVFS_CREATFS_CMD\n");
		return vfio_creatfs_cmd(arg);
	} else if (cmd == VFIO_DEVFS_CREATFILE_CMD) {
		return vfio_creatfile_cmd(arg, 0);
	} else if (cmd == VFIO_DEVFS_CLOSEFILE_CMD) {
		return vfio_close_cmd(arg);
	} else if (cmd == VFIO_DEVFS_CLOSEFS_CMD) {
		return vfio_closefs_cmd(arg);
	} else if (cmd == VFIO_DEVFS_SUBMIT_CMD) {
		//return devfs_submit_cmd(arg, iommu_data);
	}
	return 0;
}
EXPORT_SYMBOL(devfs_ioctl);


static int __init init_devfs_fs(void)
{
	int err = init_inodecache();
	if (err)
		goto out1;

	//if (test_and_set_bit(0, &once))
	printk(KERN_ALERT "init_devfs_fs called \n");


	/*Create inode tree list */
	//g_inotree = devfs_inode_list_create();


	err = register_filesystem(&devfs_fs_type);
	if (err)
		goto out;

	/*if(!g_inotree) {
		printk(KERN_ALERT "Failed %s:%d \n",__FUNCTION__,__LINE__);
		goto out1;	
	}*/	
	printk(KERN_ALERT "%s:%d Success \n", __FUNCTION__,__LINE__);

	return 0;
	out:
	//destroy_inodecache();
	out1:
	printk(KERN_ALERT "%s:%d failed \n", __FUNCTION__,__LINE__);
	return err;
}
module_init(init_devfs_fs);

//EXPORT_SYMBOL(init_devfs_fs);
//fs_initcall(init_devfs_fs);

static void __exit unregister_devfs_fs(void)
{
	if (unregister_filesystem(&devfs_fs_type)) {
		printk(KERN_ALERT "%s:%d Failed \n", __FUNCTION__,__LINE__);
	}
	destroy_inodecache();
}
//EXPORT_SYMBOL(unregister_devfs_fs);
module_exit(unregister_devfs_fs);

MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);




