#ifndef AKPFS_H
#define AKPFS_H

#define AKPFS_SUPER_MAGIC 0xABCD
#define AKPFS_MAX_FILE_NAME_SIZE 16
#define AKPFS_MAX_BLOCKS_PER_FILE (3 + 24)
#define AKPFS_MAX_ENTRIES_PER_DIR AKPFS_MAX_BLOCKS_PER_FILE
#define AKPFS_MAX_ACTUAL_FILE_PATH_SIZE (AKPFS_MAX_BLOCKS_PER_FILE * sizeof(int))

enum file_type
{
	akpfs_dir = -1,
	akpfs_link = -2
};

struct akpfs_sb
{
	unsigned short fs_type;
	unsigned short block_size;
	unsigned int block_cnt;
	unsigned int inodes_per_block;
	unsigned int inodes_block_start;
	unsigned int inodes_block_cnt;
	unsigned int free_bits_per_block;
	unsigned int free_bits_block_start;
	unsigned int free_bits_block_cnt;
	unsigned int data_block_start;
	unsigned int data_block_cnt;
	unsigned int root_inode;
};
struct akpfs_inode
{
	char file_name[AKPFS_MAX_FILE_NAME_SIZE];
	int file_size; // -1: directory; -2: link
	union
	{
		unsigned int file_blocks[AKPFS_MAX_BLOCKS_PER_FILE];
		unsigned int dir_entries[AKPFS_MAX_ENTRIES_PER_DIR];
		char actual_file_path[AKPFS_MAX_ACTUAL_FILE_PATH_SIZE];
	} u;
};

#ifdef __KERNEL__
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/types.h>

struct akpfs_fs_info
{
	struct akpfs_sb *akpfs_sb;
	struct buffer_head *bh;
};

extern struct file_system_type akpfs;
extern struct super_operations akpfs_sops;
extern struct inode_operations akpfs_iops;
extern struct address_space_operations akpfs_aops;
extern struct file_operations akpfs_fops;

/*
 * Caller of the following function is expected to do brelse(*bhp), after done using
 * the return pointer
 */
extern struct akpfs_inode *akpfs_get_inode(struct super_block *sb, int ino,
	struct buffer_head **bhp);
extern umode_t akpfs_get_file_type(struct akpfs_inode *i);
extern umode_t akpfs_get_inode_flags(struct akpfs_inode *i);
extern unsigned int akpfs_get_free_block(struct super_block *sb);
int akpfs_put_free_block(struct super_block *sb, int block_no);
#endif
#endif
