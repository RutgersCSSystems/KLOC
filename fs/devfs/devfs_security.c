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

static const struct cred *get_const_curr_cred(void){

	const struct cred *cred = current_cred();

	if (unlikely(!cred)) {
    		printk(KERN_ALERT "%s:%d fs NULL \n",__FUNCTION__,__LINE__);
	    	return ERR_PTR(-ENOMEM);
	}
	return get_cred(cred);
}

/*Set the credential for current file structure*/
int devfs_set_cred(struct devfs_fstruct *fs) {

	if(!fs) {
		printk(KERN_ALERT "%s:%d fs NULL \n",__FUNCTION__,__LINE__);
		return -EINVAL;
	}
	fs->f_cred = get_const_curr_cred();

#if defined(_DEVFS_DEBUG)
	printk(KERN_ALERT "%s:%d Setting credentials for fs \n",
		__FUNCTION__,__LINE__);
#endif
	return 0;
}
EXPORT_SYMBOL(devfs_set_cred);

/*Get the credential from current file structure*/
const struct cred *devfs_get_cred(struct devfs_fstruct *fs) {

	if(!fs) {
		printk(KERN_ALERT "%s:%d fs NULL \n",__FUNCTION__,__LINE__);
		return ERR_PTR(-ENOMEM);
	}
	return fs->f_cred;
}
EXPORT_SYMBOL(devfs_get_cred);


/* Check the credential if the file structure and current process 
*  credential match. If not return error.  
*  DevFs.c will take care of handling it.
*/
int devfs_check_fs_cred(struct devfs_fstruct *fs) {
	
	int retval = 0;

	if (unlikely(!fs)) {
   		printk(KERN_ALERT "%s:%d fs NULL \n",__FUNCTION__,__LINE__);
       		retval = -EINVAL;
   	}

	if (unlikely(!fs-> f_cred)) {
   		printk(KERN_ALERT "%s:%d fs NULL \n",__FUNCTION__,__LINE__);
		retval = -EINVAL;
	}

	if (fs-> f_cred != get_const_curr_cred()) {
		retval = -EINVAL;
		printk(KERN_ALERT "%s:%d perm mismatch\n",__FUNCTION__,__LINE__);	
	}
	return retval;
}
EXPORT_SYMBOL(devfs_check_fs_cred);
