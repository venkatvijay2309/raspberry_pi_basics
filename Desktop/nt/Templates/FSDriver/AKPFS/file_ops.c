/* AKP File System Module's File Operations */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/errno.h>

#include "akpfs.h"

static int akpfs_file_open(struct inode *inode, struct file *file)
{
	return generic_file_open(inode, file);
}

static int akpfs_file_release(struct inode *inode, struct file *file)
{
	return 0;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4,1,0))
static ssize_t akpfs_file_read(struct file *file, char __user *buf, size_t len, loff_t *off)
{
	printk(KERN_INFO "akp_fs: akpfs_file_read max_readahead = %d\n", file->f_ra.ra_pages);

	file->f_ra.ra_pages = 0; /* No read-ahead */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,16,0))
	return do_sync_read(file, buf, len, off);
#else
	return new_sync_read(file, buf, len, off);
#endif
}

static ssize_t akpfs_file_write(struct file *file, const char __user *buf, size_t len, loff_t *off)
{
	printk(KERN_INFO "akp_fs: akpfs_file_write\n");

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,16,0))
	return do_sync_write(file, buf, len, off);
#else
	return new_sync_write(file, buf, len, off);
#endif
}
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,11,0))
static int akpfs_file_readdir(struct file *file, void *dirent, filldir_t filldir)
{
	struct dentry *de = file->f_dentry;
	struct super_block *sb = de->d_inode->i_sb;
	struct buffer_head *bh, *bh2;
	struct akpfs_inode *akpfs_inode, *akpfs_inode2;
	int i, pos, retval;

	printk(KERN_INFO "akp_fs: akpfs_file_readdir\n");

	if (file->f_pos >= (2 + AKPFS_MAX_ENTRIES_PER_DIR))
	{
		return 0;
	}
	if (file->f_pos == 0)
	{
		retval = filldir(dirent, ".", 1, file->f_pos, de->d_inode->i_ino, DT_DIR);
		if (retval)
		{
			return retval;
		}
		file->f_pos++;
	}
	if (file->f_pos == 1)
	{
		retval = filldir(dirent, "..", 2, file->f_pos, de->d_parent->d_inode->i_ino, DT_DIR);
		if (retval)
		{
			return retval;
		}
		file->f_pos++;
	}
	pos = 1; /* For . & .. */
	if (!(akpfs_inode = akpfs_get_inode(sb, de->d_inode->i_ino, &bh)))
	{
		printk (KERN_ERR "akp_fs: unable to read parent inode\n");
		return -EIO;
	}
	for (i = 0; i < AKPFS_MAX_ENTRIES_PER_DIR; i++)
	{
		if (akpfs_inode->u.dir_entries[i])
		{
			pos++;
			if (!(akpfs_inode2 =
						akpfs_get_inode(sb, akpfs_inode->u.dir_entries[i], &bh2)))
			{
				printk (KERN_ERR "akp_fs: unable to read inode\n");
				brelse(bh);
				return -EIO;
			}
			if (pos == file->f_pos)
			{
				retval = filldir(dirent, akpfs_inode2->file_name,
						strnlen(akpfs_inode2->file_name, AKPFS_MAX_FILE_NAME_SIZE),
						file->f_pos, akpfs_inode->u.dir_entries[i],
						akpfs_get_file_type(akpfs_inode2));
				if (retval)
				{
					brelse(bh2);
					brelse(bh);
					return retval;
				}
				file->f_pos++;
			}
			brelse(bh2);
		}
	}
	brelse(bh);
	return 0;
}
#else
static int akpfs_file_iterate(struct file *file, struct dir_context *ctx)
{
	struct super_block *sb = file_inode(file)->i_sb;
	struct buffer_head *bh, *bh2;
	struct akpfs_inode *akpfs_inode, *akpfs_inode2;
	int i, pos;

	printk(KERN_INFO "akp_fs: akpfs_file_iterate\n");

	if (ctx->pos >= (2 + AKPFS_MAX_ENTRIES_PER_DIR))
	{
		return 0;
	}
	if (!dir_emit_dots(file, ctx))
	{
		return -ENOSPC;
	}
	pos = 1; /* For . & .. */
	if (!(akpfs_inode = akpfs_get_inode(sb, file_inode(file)->i_ino, &bh)))
	{
		printk (KERN_ERR "akp_fs: unable to read parent inode\n");
		return -EIO;
	}
	for (i = 0; i < AKPFS_MAX_ENTRIES_PER_DIR; i++)
	{
		if (akpfs_inode->u.dir_entries[i])
		{
			pos++;
			if (!(akpfs_inode2 =
						akpfs_get_inode(sb, akpfs_inode->u.dir_entries[i], &bh2)))
			{
				printk (KERN_ERR "akp_fs: unable to read inode\n");
				brelse(bh);
				return -EIO;
			}
			if (pos == ctx->pos)
			{
				if (!dir_emit(ctx, akpfs_inode2->file_name,
						strnlen(akpfs_inode2->file_name, AKPFS_MAX_FILE_NAME_SIZE),
						akpfs_inode->u.dir_entries[i],
						akpfs_get_file_type(akpfs_inode2)))
				{
					brelse(bh2);
					brelse(bh);
					return -ENOSPC;
				}
				ctx->pos++;
			}
			brelse(bh2);
		}
	}
	brelse(bh);
	return 0;
}
#endif

struct file_operations akpfs_fops =
{
	open: akpfs_file_open,
	release: akpfs_file_release,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4,1,0))
	read: akpfs_file_read,
	write: akpfs_file_write,
#endif
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,16,0))
	aio_read: generic_file_aio_read,
	aio_write: generic_file_aio_write,
#else
	read_iter: generic_file_read_iter,
	write_iter: generic_file_write_iter,
#endif
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,11,0))
	readdir: akpfs_file_readdir,
#else
	iterate: akpfs_file_iterate,
#endif
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,35))
	fsync: simple_sync_file
#else
	fsync: noop_fsync
#endif
};

MODULE_LICENSE("GPL");
