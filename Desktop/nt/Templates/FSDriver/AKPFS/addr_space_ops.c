/* AKP File System Module's Address Operations */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/mpage.h>
#include <linux/errno.h>

#include "akpfs.h"

static int akpfs_get_block(struct inode *inode, sector_t iblock, struct buffer_head *bh_result, int create)
{
	struct super_block *sb = inode->i_sb;
	struct buffer_head *bh;
	struct akpfs_inode *akpfs_inode;
	sector_t phys;

	printk(KERN_INFO "akp_fs: akpfs_get_block called for I: %ld, B: %llu, C: %d\n",
			inode->i_ino, (unsigned long long)(iblock), create);

	if (iblock >= AKPFS_MAX_BLOCKS_PER_FILE)
	{
		return -ENOSPC;
	}

	if (!(akpfs_inode = akpfs_get_inode(sb, inode->i_ino, &bh)))
	{
		printk (KERN_ERR "akp_fs: unable to read inode to get blocks\n");
		return -EIO;
	}
	if (!akpfs_inode->u.file_blocks[iblock])
	{
		if (!create)
		{
			brelse(bh);
			return -EIO;
		}
		else
		{
			if (!(akpfs_inode->u.file_blocks[iblock] = akpfs_get_free_block(sb)))
			{
				brelse(bh);
				return -ENOSPC;
			}
			mark_buffer_dirty(bh);
		}
	}
	phys = akpfs_inode->u.file_blocks[iblock];
	brelse(bh);

	map_bh(bh_result, sb, phys);

	return 0;
}

static int akpfs_readpage(struct file *file, struct page *page)
{
	return mpage_readpage(page, akpfs_get_block);
}

static int akpfs_writepage(struct page *page, struct writeback_control *wbc)
{
	return block_write_full_page(page, akpfs_get_block, wbc);
}

static int akpfs_write_begin(struct file *file, struct address_space *mapping,
	loff_t pos, unsigned len, unsigned flags, struct page **pagep, void **fsdata)
{
	*pagep = NULL;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,36))
	return block_write_begin(file, mapping, pos, len, flags, pagep, fsdata,
		akpfs_get_block);
#else
	return block_write_begin(mapping, pos, len, flags, pagep, akpfs_get_block);
#endif
}

struct address_space_operations akpfs_aops =
{
	.readpage = akpfs_readpage,
	.writepage = akpfs_writepage,
	.write_begin = akpfs_write_begin,
	.write_end = generic_write_end,
};

MODULE_LICENSE("GPL");
