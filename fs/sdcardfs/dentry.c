/*
 * Copyright (c) 1998-2011 Erez Zadok
 * Copyright (c) 2009	   Shrikar Archak
 * Copyright (c) 2003-2011 Stony Brook University
 * Copyright (c) 2003-2011 The Research Foundation of SUNY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "sdcardfs.h"
#include "linux/ctype.h"

/*
 * returns: -ERRNO if error (returned to user)
 *          0: tell VFS to invalidate dentry
 *          1: dentry is valid
 */
static int sdcardfs_d_revalidate(struct dentry *dentry, unsigned int flags)
{
	struct path parent_lower_path, lower_path;
	struct dentry *parent_dentry = NULL;
	struct dentry *parent_lower_dentry = NULL;
	struct dentry *lower_cur_parent_dentry = NULL;
	struct dentry *lower_dentry = NULL;
	int err = 1;

	if (flags & LOOKUP_RCU)
		return -ECHILD;

	spin_lock(&dentry->d_lock);
	if (IS_ROOT(dentry)) {
		spin_unlock(&dentry->d_lock);
		return 1;
	}
	spin_unlock(&dentry->d_lock);

	parent_dentry = dget_parent(dentry);
	sdcardfs_get_lower_path(parent_dentry, &parent_lower_path);
	sdcardfs_get_lower_path(dentry, &lower_path);
	parent_lower_dentry = parent_lower_path.dentry;
	lower_dentry = lower_path.dentry;
	lower_cur_parent_dentry = dget_parent(lower_dentry);

	if ((lower_dentry->d_flags & DCACHE_OP_REVALIDATE)) {
		err = lower_dentry->d_op->d_revalidate(lower_dentry, flags);
		if (err == 0) {
			d_drop(dentry);
			goto out;
		}
	}
	spin_lock(&lower_dentry->d_lock);
	if (d_unhashed(lower_dentry)) {
		spin_unlock(&lower_dentry->d_lock);
		d_drop(dentry);
		err = 0;
		goto out;
	}
	spin_unlock(&lower_dentry->d_lock);

	if (parent_lower_dentry != lower_cur_parent_dentry) {
		d_drop(dentry);
		err = 0;
		goto out;
	}

	if (dentry < lower_dentry) {
		spin_lock(&dentry->d_lock);
		spin_lock(&lower_dentry->d_lock);
	} else {
		spin_lock(&lower_dentry->d_lock);
		spin_lock(&dentry->d_lock);
	}

	if (!qstr_case_eq(&dentry->d_name, &lower_dentry->d_name)) {
		__d_drop(dentry);
		err = 0;
	}

	if (dentry < lower_dentry) {
		spin_unlock(&lower_dentry->d_lock);
		spin_unlock(&dentry->d_lock);
	} else {
		spin_unlock(&dentry->d_lock);
		spin_unlock(&lower_dentry->d_lock);
	}
	
	if (!err)
		goto out;

	/* If our top's inode is gone, we may be out of date */
	inode = igrab(dentry->d_inode);
	if (inode) {
		data = top_data_get(SDCARDFS_I(inode));
		if (!data || data->abandoned) {
			d_drop(dentry);
			err = 0;
		}
		if (data)
			data_put(data);
		iput(inode);
	}
out:
	dput(parent_dentry);
	dput(lower_cur_parent_dentry);
	sdcardfs_put_lower_path(parent_dentry, &parent_lower_path);
	sdcardfs_put_lower_path(dentry, &lower_path);
	return err;
}

static void sdcardfs_d_release(struct dentry *dentry)
{
	/* release and reset the lower paths */
	sdcardfs_put_reset_lower_path(dentry);
	free_dentry_private_data(dentry);
	return;
}

#ifdef CONFIG_SDCARD_FS_CI_SEARCH
static int sdcardfs_hash_ci(const struct dentry *dentry, 
				const struct inode *inode, struct qstr *qstr)
{
	/* 
	 * This function is copy of vfat_hashi.
	 * FIXME Should we support national language?
	 *       Refer to vfat_hashi()
	 * struct nls_table *t = MSDOS_SB(dentry->d_sb)->nls_io;
	 */
	const unsigned char *name;
	unsigned int len;
	unsigned long hash;

	name = qstr->name;
	//len = vfat_striptail_len(qstr);
	len = qstr->len; 

	hash = init_name_hash();
	while (len--)
		//hash = partial_name_hash(nls_tolower(t, *name++), hash);
		hash = partial_name_hash(tolower(*name++), hash);
	qstr->hash = end_name_hash(hash);

	return 0;
}

/*
 * Case insensitive compare of two vfat names.
 */
static int sdcardfs_cmp_ci(const struct dentry *parent, 
		const struct inode *pinode,
		const struct dentry *dentry, const struct inode *inode,
		unsigned int len, const char *str, const struct qstr *name)
{
	/* This function is copy of vfat_cmpi */
	// FIXME Should we support national language? 
	//struct nls_table *t = MSDOS_SB(parent->d_sb)->nls_io;
	//unsigned int alen, blen;

	/* A filename cannot end in '.' or we treat it like it has none */
	/*
	alen = vfat_striptail_len(name);
	blen = __vfat_striptail_len(len, str);
	if (alen == blen) {
		if (nls_strnicmp(t, name->name, str, alen) == 0)
			return 0;
	}
	*/
	if (name->len == len) {
		if (strncasecmp(name->name, str, len) == 0)
			return 0; 
	}
	return 1;
}

const struct dentry_operations sdcardfs_dops = {
	.d_revalidate	= sdcardfs_d_revalidate,
	.d_release	= sdcardfs_d_release,
	.d_hash 	= sdcardfs_hash_ci, 
	.d_compare	= sdcardfs_cmp_ci,
};

#else /* CONFIG_SDCARD_FS_CI_SEARCH */
const struct dentry_operations sdcardfs_dops = {
	.d_revalidate	= sdcardfs_d_revalidate,
	.d_release	= sdcardfs_d_release,
};

#endif /* CONFIG_SDCARD_FS_CI_SEARCH */
