// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2014-2021 Junjiro R. Okajima
 */

/*
 * posix acl operations
 */

#include <linux/fs.h>
#include "aufs.h"

struct posix_acl *aufs_get_acl(struct inode *inode, int type, bool rcu)
{
	struct posix_acl *acl;
	int err;
	aufs_bindex_t bindex;
	struct inode *h_inode;
	struct super_block *sb;

	acl = ERR_PTR(-ECHILD);
	if (rcu)
		goto out;

	acl = NULL;
	sb = inode->i_sb;
	si_read_lock(sb, AuLock_FLUSH);
	ii_read_lock_child(inode);
	if (!(sb->s_flags & SB_POSIXACL))
		goto unlock;

	bindex = au_ibtop(inode);
	h_inode = au_h_iptr(inode, bindex);
	if (unlikely(!h_inode
		     || ((h_inode->i_mode & S_IFMT)
			 != (inode->i_mode & S_IFMT)))) {
		err = au_busy_or_stale();
		acl = ERR_PTR(err);
		goto unlock;
	}

	/* always topmost only */
	acl = get_acl(h_inode, type);
	if (IS_ERR(acl))
		forget_cached_acl(inode, type);
	else
		set_cached_acl(inode, type, acl);

unlock:
	ii_read_unlock(inode);
	si_read_unlock(sb);

out:
	AuTraceErrPtr(acl);
	return acl;
}

int aufs_set_acl(struct user_namespace *userns, struct inode *inode,
		 struct posix_acl *acl, int type)
{
	int err;
	ssize_t ssz;
	struct dentry *dentry;
	struct au_sxattr arg = {
		.type = AU_ACL_SET,
		.u.acl_set = {
			.acl	= acl,
			.type	= type
		},
	};

	IMustLock(inode);

	if (inode->i_ino == AUFS_ROOT_INO)
		dentry = dget(inode->i_sb->s_root);
	else {
		dentry = d_find_alias(inode);
		if (!dentry)
			dentry = d_find_any_alias(inode);
		if (!dentry) {
			pr_warn("cannot handle this inode, "
				"please report to aufs-users ML\n");
			err = -ENOENT;
			goto out;
		}
	}

	ssz = au_sxattr(dentry, inode, &arg);
	/* forget even it if succeeds since the branch might set differently */
	forget_cached_acl(inode, type);
	dput(dentry);
	err = ssz;
	if (ssz >= 0)
		err = 0;

out:
	return err;
}
