/*
 * BRIEF DESCRIPTION
 *
 * Memory protection definitions for the PMFS filesystem.
 *
 * Copyright 2012-2013 Intel Corporation
 * Copyright 2010-2011 Marco Stornelli <marco.stornelli@gmail.com>
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __WPROTECT_H
#define __WPROTECT_H

#include <linux/pmfs_def.h>
#include <linux/fs.h>

/* pmfs_memunlock_super() before calling! */
static inline void pmfs_sync_super(struct pmfs_super_block *ps)
{
	u16 crc = 0;

	ps->s_wtime = cpu_to_le32(get_seconds());
	ps->s_sum = 0;
	crc = crc16(~0, (__u8 *)ps + sizeof(__le16),
			PMFS_SB_STATIC_SIZE(ps) - sizeof(__le16));
	ps->s_sum = cpu_to_le16(crc);
	/* Keep sync redundant super block */
	memcpy((void *)ps + PMFS_SB_SIZE, (void *)ps,
		sizeof(struct pmfs_super_block));
}

#if 1
/* pmfs_memunlock_inode() before calling! */
static inline void pmfs_sync_inode(struct pmfs_inode *pi)
{
	u16 crc = 0;

	//pi->i_sum = 0;
	crc = crc16(~0, (__u8 *)pi + sizeof(__le16), PMFS_INODE_SIZE -
		    sizeof(__le16));
	//pi->i_sum = cpu_to_le16(crc);
}
#endif

extern int pmfs_writeable(void *vaddr, unsigned long size, int rw);
extern int pmfs_xip_mem_protect(struct super_block *sb,
				 void *vaddr, unsigned long size, int rw);

extern spinlock_t pmfs_writeable_lock;
static inline int pmfs_is_protected(struct super_block *sb)
{
	struct pmfs_sb_info *sbi = (struct pmfs_sb_info *)sb->s_fs_info;

	return sbi->s_mount_opt & PMFS_MOUNT_PROTECT;
}

static inline int pmfs_is_protected_old(struct super_block *sb)
{
	struct pmfs_sb_info *sbi = (struct pmfs_sb_info *)sb->s_fs_info;

	return sbi->s_mount_opt & PMFS_MOUNT_PROTECT_OLD;
}

static inline int pmfs_is_wprotected(struct super_block *sb)
{
	return pmfs_is_protected(sb) || pmfs_is_protected_old(sb);
}

static inline void
__pmfs_memunlock_range(void *p, unsigned long len, int hold_lock)
{

#if 1
	/*
	 * NOTE: Ideally we should lock all the kernel to be memory safe
	 * and avoid to write in the protected memory,
	 * obviously it's not possible, so we only serialize
	 * the operations at fs level. We can't disable the interrupts
	 * because we could have a deadlock in this path.
	 */
	if (hold_lock)
		spin_lock(&pmfs_writeable_lock);
	pmfs_writeable(p, len, 1);
#endif
}

static inline void
__pmfs_memlock_range(void *p, unsigned long len, int hold_lock)
{
#if 1
	pmfs_writeable(p, len, 0);
	if (hold_lock)
		spin_unlock(&pmfs_writeable_lock);
#endif
}

static inline void pmfs_memunlock_range(struct super_block *sb, void *p,
					 unsigned long len)
{
#if 1
	if (pmfs_is_protected(sb))
		__pmfs_memunlock_range(p, len, 0);
	else if (pmfs_is_protected_old(sb))
		__pmfs_memunlock_range(p, len, 1);
#endif
}

static inline void pmfs_memlock_range(struct super_block *sb, void *p,
				       unsigned long len)
{

#if 1
	if (pmfs_is_protected(sb))
		__pmfs_memlock_range(p, len, 0);
	else if (pmfs_is_protected_old(sb))
		__pmfs_memlock_range(p, len, 1);
#endif
}

static inline void pmfs_memunlock_super(struct super_block *sb,
					 struct pmfs_super_block *ps)
{
#if 1
	if (pmfs_is_protected(sb))
		__pmfs_memunlock_range(ps, PMFS_SB_SIZE, 0);
	else if (pmfs_is_protected_old(sb))
		__pmfs_memunlock_range(ps, PMFS_SB_SIZE, 1);
#endif
}

static inline void pmfs_memlock_super(struct super_block *sb,
				       struct pmfs_super_block *ps)
{
#if 1
	pmfs_sync_super(ps);
	if (pmfs_is_protected(sb))
		__pmfs_memlock_range(ps, PMFS_SB_SIZE, 0);
	else if (pmfs_is_protected_old(sb))
		__pmfs_memlock_range(ps, PMFS_SB_SIZE, 1);
#endif
}

static inline void pmfs_memunlock_inode(struct super_block *sb,
					 struct pmfs_inode *pi)
{
#if 1
	if (pmfs_is_protected(sb))
		__pmfs_memunlock_range(pi, PMFS_SB_SIZE, 0);
	else if (pmfs_is_protected_old(sb))
		__pmfs_memunlock_range(pi, PMFS_SB_SIZE, 1);
#endif
}

static inline void pmfs_memlock_inode(struct super_block *sb,
				       struct pmfs_inode *pi)
{
#if 1
	/* pmfs_sync_inode(pi); */
	if (pmfs_is_protected(sb))
		__pmfs_memlock_range(pi, PMFS_SB_SIZE, 0);
	else if (pmfs_is_protected_old(sb))
		__pmfs_memlock_range(pi, PMFS_SB_SIZE, 1);
#endif
}

static inline void pmfs_memunlock_block(struct super_block *sb, void *bp)
{
#if 1
	if (pmfs_is_protected(sb))
		__pmfs_memunlock_range(bp, sb->s_blocksize, 0);
	else if (pmfs_is_protected_old(sb))
		__pmfs_memunlock_range(bp, sb->s_blocksize, 1);
#endif
}

static inline void pmfs_memlock_block(struct super_block *sb, void *bp)
{
#if 1
	if (pmfs_is_protected(sb))
		__pmfs_memlock_range(bp, sb->s_blocksize, 0);
	else if (pmfs_is_protected_old(sb))
		__pmfs_memlock_range(bp, sb->s_blocksize, 1);
#endif
}

#endif
