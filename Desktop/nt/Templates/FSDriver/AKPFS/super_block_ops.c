/* AKP File System Module's Super Block Operations */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/writeback.h>
#include <linux/errno.h>

#include "akpfs.h"

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,34))
static int akpfs_super_write_inode(struct inode *inode, int do_sync)
{
#else
static int akpfs_super_write_inode(struct inode *inode, struct writeback_control *wbc)
{
	int do_sync = (wbc->sync_mode == WB_SYNC_ALL);
#endif
	struct super_block *sb = inode->i_sb;
	struct buffer_head *bh;
	struct akpfs_inode *akpfs_inode;
	struct akpfs_sb *akpfs_sb = ((struct akpfs_fs_info *)(sb->s_fs_info))->akpfs_sb;
	int i;

	printk(KERN_INFO "akp_fs: akpfs_super_write_inode (i_ino = %ld) = %lld bytes\n",
		inode->i_ino, i_size_read(inode));

	if (!(akpfs_inode = akpfs_get_inode(sb, inode->i_ino, &bh)))
	{
		printk (KERN_ERR "akp_fs: unable to read inode\n");
		return -EIO;
	}
	if ((inode->i_mode & S_IFREG) && (akpfs_inode->file_size >= 0))
   	{
		akpfs_inode->file_size = i_size_read(inode);
		for (i = (akpfs_inode->file_size + akpfs_sb->block_size - 1) /
					akpfs_sb->block_size; i < AKPFS_MAX_BLOCKS_PER_FILE; i++)
		{
			if (akpfs_inode->u.file_blocks[i])
			{
				akpfs_put_free_block(sb, akpfs_inode->u.file_blocks[i]);
				// Even if the put fails, let's mark it free
				akpfs_inode->u.file_blocks[i] = 0;
			}
		}
		mark_buffer_dirty(bh);
		if (do_sync)
		{
			sync_dirty_buffer(bh);
			if (buffer_req(bh) && !buffer_uptodate(bh))
			{
				printk ("akpfs: IO error syncing inode [%s:%08lx]\n",
						sb->s_id, inode->i_ino);
				brelse(bh);
				return -EIO;
			}
		}

	}
	brelse(bh);

	return 0;
}

struct super_operations akpfs_sops =
{
	statfs: simple_statfs, /* handler from libfs */
	write_inode: akpfs_super_write_inode
};

MODULE_LICENSE("GPL");
