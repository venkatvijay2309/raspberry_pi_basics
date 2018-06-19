/* AKP File System Module's Inode Operations */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/errno.h>

#include "akpfs.h"

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,6,0))
static struct dentry *akpfs_inode_lookup(struct inode *parent_inode, struct dentry *dentry, struct nameidata *nameidata)
#else
static struct dentry *akpfs_inode_lookup(struct inode *parent_inode, struct dentry *dentry, unsigned int flags)
#endif
{
	struct super_block *sb = parent_inode->i_sb;
	struct buffer_head *bh, *bh2;
	struct akpfs_inode *akpfs_inode, *akpfs_inode2;
	int i;
	struct inode *file_inode;

	printk(KERN_INFO "akp_fs: akpfs_inode_lookup\n");

	if (!(akpfs_inode = akpfs_get_inode(sb, parent_inode->i_ino, &bh)))
	{
		printk (KERN_ERR "akp_fs: unable to read parent inode\n");
		return ERR_PTR(-EIO);
	}
	for (i = 0; i < AKPFS_MAX_ENTRIES_PER_DIR; i++)
	{
		if (akpfs_inode->u.dir_entries[i])
		{
			if (!(akpfs_inode2 =
						akpfs_get_inode(sb, akpfs_inode->u.dir_entries[i], &bh2)))
			{
				printk (KERN_ERR "akp_fs: unable to read inode\n");
				brelse(bh);
				return ERR_PTR(-EIO);
			}
			if (strncmp(dentry->d_name.name, akpfs_inode2->file_name,
						AKPFS_MAX_FILE_NAME_SIZE))
			{
				brelse(bh2);
			}
			else
			{
				break;
			}
		}
	}
	if (i == AKPFS_MAX_ENTRIES_PER_DIR)
	{
		brelse(bh);
		return ERR_PTR(-ENOENT);
	}

	printk(KERN_INFO "akp_fs: Getting an inode\n");
	file_inode = iget_locked(sb, akpfs_inode->u.dir_entries[i]);
	if (!file_inode)
	{
		brelse(bh2);
		brelse(bh);
		return ERR_PTR(-EACCES);
	}
	if (file_inode->i_state & I_NEW)
	{
		printk(KERN_INFO "akp_fs: Got new inode, let's fill in\n");
		file_inode->i_size = akpfs_inode2->file_size;
		file_inode->i_mode = akpfs_get_inode_flags(akpfs_inode2);
		file_inode->i_mapping->a_ops = &akpfs_aops;
		file_inode->i_fop = &akpfs_fops;
		unlock_new_inode(file_inode);
	}
	else
	{
		printk(KERN_INFO "akp_fs: Got inode from inode cache\n");
	}
	d_add(dentry, file_inode);
	brelse(bh2);
	brelse(bh);

	return NULL;
}

struct inode_operations akpfs_iops =
{
	lookup: akpfs_inode_lookup
};

MODULE_LICENSE("GPL");
