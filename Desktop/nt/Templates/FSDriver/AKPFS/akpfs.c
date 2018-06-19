/* AKP File System Module's Init File */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/errno.h>

#include "akpfs.h"

struct akpfs_inode *akpfs_get_inode(struct super_block *sb, int ino,
	struct buffer_head **bhp)
/*
 * Caller of this function is expected to do brelse(*bhp), after done using the
 * return pointer
 */
{
	struct akpfs_sb *akpfs_sb = ((struct akpfs_fs_info *)(sb->s_fs_info))->akpfs_sb;

	if (!(*bhp = sb_bread(sb,
				akpfs_sb->inodes_block_start + (ino / akpfs_sb->inodes_per_block))))
	{
		printk (KERN_ERR "akp_fs: unable to read super block\n");
		return NULL;
	}

	return ((struct akpfs_inode *)((*bhp)->b_data)) +
		(ino % akpfs_sb->inodes_per_block);
}

umode_t akpfs_get_file_type(struct akpfs_inode *ino)
{
	return
		(ino->file_size >= 0) ? S_IFREG :
			((ino->file_size == akpfs_dir) ? S_IFDIR :
				((ino->file_size == akpfs_link) ? S_IFLNK : 0));
}

umode_t akpfs_get_inode_flags(struct akpfs_inode *ino)
{
	if (ino->file_size >= 0) /* Regular File */
	{
		return S_IFREG | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
	}
	else if (ino->file_size == akpfs_dir) /* Directory */
	{
		return S_IFDIR | S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
	}
	else if (ino->file_size == akpfs_link)
	{
		return S_IFLNK | S_IRWXU | S_IRWXG | S_IRWXO;
	}
	else
	{
		return 0;
	}
}

/*
 * TODO: Race condition between multiple invocations of the following 2
 * functions, not handled yet. It can be done by protecting the complete code of
 * both the functions using a common mutex
 */
unsigned int akpfs_get_free_block(struct super_block *sb)
{
	struct akpfs_sb *akpfs_sb = ((struct akpfs_fs_info *)(sb->s_fs_info))->akpfs_sb;
	unsigned int bi, bc /* block index & count */, ec = 0 /* entry (bitmask) count */;
	unsigned int first_free_blk, i;
	struct buffer_head *bh = NULL;
	unsigned char *bitmask = NULL;

	for (bi = 0, bc = akpfs_sb->free_bits_block_start;
								bi < akpfs_sb->free_bits_block_cnt; bi++, bc++)
	{
		if (!(bh = sb_bread(sb, bc)))
		{
			printk (KERN_ERR "akp_fs: unable to read free bits block\n");
			return -EIO;
		}
		bitmask = (unsigned char *)(bh->b_data);
		for (ec = 0; ec < akpfs_sb->free_bits_per_block / 8; ec++)
		{
			if (bitmask[ec] != 0xFF)
			{
				break;
			}
		}
		if (ec < akpfs_sb->free_bits_per_block / 8)
		{
			break;
		}
		brelse(bh);
	}
	if (bi >= akpfs_sb->free_bits_block_cnt)
	{
		return 0; // No free block
	}
	first_free_blk = bi * akpfs_sb->free_bits_per_block + ec * 8;
	for (i = 0; i < 8; i++, first_free_blk++)
	{
		if (!(bitmask[ec] & (1 << i)))
		{
			break;
		}
	}
	if ((i >= 8) /* Race condition case */ ||
		(first_free_blk >= akpfs_sb->block_cnt))
	{
		brelse(bh);
		return 0; // No free block
	}
	else
	{
		bitmask[ec] |= (1 << i);
		mark_buffer_dirty(bh);
		printk(KERN_INFO "akp_fs: Alloted free block #%d\n", first_free_blk);
		brelse(bh);
		return first_free_blk;
	}
}
int akpfs_put_free_block(struct super_block *sb, int block_no)
{
	struct akpfs_sb *akpfs_sb = ((struct akpfs_fs_info *)(sb->s_fs_info))->akpfs_sb;
	struct buffer_head *bh;
	unsigned char *bitmask;
	int bitpos;

	if (block_no >= akpfs_sb->block_cnt)
	{
		return -EINVAL;
	}
	if (!(bh = sb_bread(sb, akpfs_sb->free_bits_block_start +
								block_no / akpfs_sb->free_bits_per_block)))
	{
		printk (KERN_ERR "akp_fs: unable to read free bits block\n");
		return -EIO;
	}
	bitmask = (unsigned char *)(bh->b_data);
	bitmask += (block_no % akpfs_sb->free_bits_per_block) / 8;
	bitpos = (block_no % akpfs_sb->free_bits_per_block) % 8;
	if (!((*bitmask) & (1 << bitpos)))
	{
		printk (KERN_WARNING "akp_fs: Block #%d already freed\n", block_no);
	}
	else
	{
		(*bitmask) &= ~(1 << bitpos);
		mark_buffer_dirty(bh);
		printk(KERN_INFO "akp_fs: Freed alloted block #%d\n", block_no);
	}
	brelse(bh);
	return 0;
}

static int __init akpfs_init(void)
{
	int err;

	err = register_filesystem(&akpfs);
	return err;
}

static void __exit akpfs_exit(void)
{
	unregister_filesystem(&akpfs);
}

module_init(akpfs_init);
module_exit(akpfs_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Anil Kumar Pugalia <email@sarika-pugs.com>");
MODULE_DESCRIPTION("File System Module for AKP File System");
