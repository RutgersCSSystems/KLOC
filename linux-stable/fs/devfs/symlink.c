/*
 * BRIEF DESCRIPTION
 *
 * Symlink operations
 *
 * Copyright 2012-2013 Intel Corporation
 * Copyright 2009-2011 Marco Stornelli <marco.stornelli@gmail.com>
 * Copyright 2003 Sony Corporation
 * Copyright 2003 Matsushita Electric Industrial Co., Ltd.
 * 2003-2004 (c) MontaVista Software, Inc. , Steve Longerbeam
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 *
 * change log:
 *
 * vfs_readlink() -> readlink_copy()
 *
 */

#include <linux/fs.h>
#include "pmfs.h"

int pmfs_block_symlink(struct inode *inode, const char *symname, int len)
{
	struct super_block *sb = inode->i_sb;
	u64 block;
	char *blockp;
	int err;

	err = pmfs_alloc_blocks(NULL, inode, 0, 1, false);
	if (err)
		return err;

	block = pmfs_find_data_block(inode, 0);
	blockp = pmfs_get_block(sb, block);

	pmfs_memunlock_block(sb, blockp);
	memcpy(blockp, symname, len);
	blockp[len] = '\0';
	pmfs_memlock_block(sb, blockp);
	pmfs_flush_buffer(blockp, len+1, false);
	return 0;
}

static int pmfs_readlink(struct dentry *dentry, char *buffer, int buflen)
{
	struct inode *inode = dentry->d_inode;
	struct super_block *sb = inode->i_sb;
	u64 block;
	char *blockp;

	block = pmfs_find_data_block(inode, 0);
	blockp = pmfs_get_block(sb, block);
	//return vfs_readlink(dentry, buffer, buflen, blockp);
	return readlink_copy(buffer, buflen, blockp);
}

static void *pmfs_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	struct inode *inode = dentry->d_inode;
	struct super_block *sb = inode->i_sb;
	off_t block;
	int status;
	char *blockp;

	block = pmfs_find_data_block(inode, 0);
	blockp = pmfs_get_block(sb, block);
	//FIXME
	// vfs_follow_link is deprecated in Kernel 4.8.12
	//status = vfs_follow_link(nd, blockp);
	return ERR_PTR(status);
}

const struct inode_operations pmfs_symlink_inode_operations = {
	.readlink	= pmfs_readlink,
	//.follow_link	= pmfs_follow_link,
	.setattr	= pmfs_notify_change,
};
