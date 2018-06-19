/* AKP File System Module's File System Operations */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/slab.h>
#include <linux/errno.h>

#include "akpfs.h"

static int akpfs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct buffer_head *bh, *bh2;
	struct akpfs_fs_info *akpfs_fs_info;
	struct akpfs_sb *akpfs_sb;
	struct inode *root_inode;
	struct akpfs_inode *akpfs_root_inode;

	printk(KERN_INFO "akp_fs: akpfs_fill_super\n");

	if (!(akpfs_fs_info = (struct akpfs_fs_info *)
							(kzalloc(sizeof(struct akpfs_fs_info), GFP_KERNEL))))
	/* If all goes fine, this akpfs_fs_info will be kfree in akpfs_kill_sb */
	{
		printk (KERN_ERR "akp_fs: unable to allocate memory for fs info\n");
		return -ENOMEM;
	}
	if (!(bh = sb_bread(sb, 0 /* First block */)))
	/* If all goes fine, this bh will be brelse in akpfs_kill_sb */
	{
		printk (KERN_ERR "akp_fs: unable to read super block\n");
		kfree(akpfs_fs_info);
		return -EIO;
	}
	akpfs_fs_info->bh = bh;
	akpfs_sb = (struct akpfs_sb *)(bh->b_data);
	akpfs_fs_info->akpfs_sb = akpfs_sb;
	if (akpfs_sb->fs_type != AKPFS_SUPER_MAGIC)
	{
		printk (KERN_ERR "akp_fs: unable to find akp fs\n");
		brelse(bh);
		kfree(akpfs_fs_info);
		return -EINVAL;
	}
	sb->s_fs_info = akpfs_fs_info;
	if (sb_min_blocksize(sb, akpfs_sb->block_size) != akpfs_sb->block_size)
	{
		printk (KERN_ERR "akp_fs: unable to set the block device block size to the file system block size\n");
		brelse(bh);
		kfree(akpfs_fs_info);
		return -EINVAL;
	}
	sb->s_magic = akpfs_sb->fs_type;
	sb->s_op = &akpfs_sops; // super block operations
	sb->s_type = &akpfs; // file system type
	printk(KERN_INFO "akp_fs: BS: %ld bytes = (1 << %d); Magic: 0x%lX; Root Inode: %d\n",
		sb->s_blocksize, sb->s_blocksize_bits, sb->s_magic, akpfs_sb->root_inode);

	root_inode = iget_locked(sb, akpfs_sb->root_inode); // allocate an inode
	if (!root_inode)
	{
		printk(KERN_ERR "akp_fs: unable to get root inode\n");
		sb->s_fs_info = NULL;
		brelse(bh);
		kfree(akpfs_fs_info);
		return -EACCES;
	}
	if (root_inode->i_state & I_NEW)
	{
		if (!(akpfs_root_inode = akpfs_get_inode(sb, akpfs_sb->root_inode, &bh2)))
		{
			printk(KERN_ERR "akp_fs: unable to read root inode\n");
			iget_failed(root_inode);
			sb->s_fs_info = NULL;
			brelse(bh);
			kfree(akpfs_fs_info);
			return -EIO;
		}
		if (strncmp(akpfs_root_inode->file_name, "/", AKPFS_MAX_FILE_NAME_SIZE))
		{
			printk(KERN_ERR "akp_fs: root is %s\n", akpfs_root_inode->file_name);
			brelse(bh2);
			iget_failed(root_inode);
			sb->s_fs_info = NULL;
			brelse(bh);
			kfree(akpfs_fs_info);
			return -EINVAL;
		}
		printk(KERN_INFO "akp_fs: Got new root inode, let's fill in\n");
		root_inode->i_mode = akpfs_get_inode_flags(akpfs_root_inode);
		root_inode->i_op = &akpfs_iops; // set the inode ops
		root_inode->i_mapping->a_ops = &akpfs_aops;
		root_inode->i_fop = &akpfs_fops;
		brelse(bh2);
		unlock_new_inode(root_inode);
	}
	else
	{
		printk(KERN_INFO "akp_fs: Got root inode from inode cache\n");
	}
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,4,0))
	sb->s_root = d_alloc_root(root_inode);
#else
	sb->s_root = d_make_root(root_inode);
#endif
	if (!sb->s_root)
	{
		printk(KERN_ERR "akp_fs: unable to attach root inode\n");
		iget_failed(root_inode);
		sb->s_fs_info = NULL;
		brelse(bh);
		kfree(akpfs_fs_info);
		return -ENOMEM;
	}

	return 0;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38))
static int akpfs_get_sb(struct file_system_type *fs_type, int flags, const char *devname, void *data, struct vfsmount *vm)
{
	printk(KERN_INFO "akp_fs: akpfs_get_sb\n");
	printk(KERN_INFO "akp_fs: devname = %s\n", devname);

	 /* akpfs_fill_super this will be called to fill the super block */
	return get_sb_bdev(fs_type, flags, devname, data, &akpfs_fill_super, vm);
}
#else
static struct dentry *akpfs_mount(struct file_system_type *fs_type, int flags, const char *devname, void *data)
{
	printk(KERN_INFO "akp_fs: akpfs_mount\n");
	printk(KERN_INFO "akp_fs: devname = %s\n", devname);

	 /* akpfs_fill_super this will be called to fill the super block */
	return mount_bdev(fs_type, flags, devname, data, &akpfs_fill_super);
}
#endif

static void akpfs_kill_sb(struct super_block *sb)
{
	struct akpfs_fs_info *akpfs_fs_info = (struct akpfs_fs_info *)(sb->s_fs_info);

	printk(KERN_INFO "akp_fs: akpfs_kill_sb\n");

	if (akpfs_fs_info)
	{
		if (akpfs_fs_info->bh)
		{
			brelse(akpfs_fs_info->bh);
		}
		kfree(akpfs_fs_info);
	}
	kill_block_super(sb);
}

struct file_system_type akpfs =
{
	name: "akp",
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38))
	get_sb:  akpfs_get_sb,
#else
	mount:  akpfs_mount,
#endif
	kill_sb: akpfs_kill_sb,
	owner: THIS_MODULE
};

MODULE_LICENSE("GPL");
