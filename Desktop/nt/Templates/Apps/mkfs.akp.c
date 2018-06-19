#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include "akpfs.h"

#define ADD_ONE_FILE 1

#define SECTOR_SIZE 512
#define PAGE_SIZE 4096
#define AVG_FILE_SIZE PAGE_SIZE

void print_stat(struct stat *stat)
{
	printf("ID of device containing file: 0x%X\n", stat->st_dev); /* dev_t */
	printf("inode number: %d\n", stat->st_ino); /* ino_t */
	printf("protection: 0%o\n", stat->st_mode); /* mode_t */
	printf("number of hard links: %d\n", stat->st_nlink); /* nlink_t */
	printf("user ID of owner: %d\n", stat->st_uid); /* uid_t */
	printf("group ID of owner: %d\n", stat->st_gid); /* gid_t */
	printf("device ID (if special file): 0x%X\n", stat->st_rdev); /* dev_t */
	printf("total size, in bytes: %d\n", stat->st_size); /* off_t */
	printf("blocksize for file system I/O: %d\n", stat->st_blksize); /* blksize_t */
	printf("number of 512B blocks allocated: %d\n", stat->st_blocks); /* blkcnt_t */
	printf("time of last access: %d\n", stat->st_atime); /* time_t */
	printf("time of last modification: %d\n", stat->st_mtime); /* time_t */
	printf("time of last status change: %d\n", stat->st_ctime); /* time_t */
}

int get_dev_size(int fd)
{
	int dev_size;

	dev_size = lseek(fd, 0, SEEK_END);
	//printf("Device Size: %d bytes\n", dev_size);
	return dev_size;
}

int compute_blk_size(int dev_size)
{
#if 0
	int block_size, block_cnt, inodes_per_block;
	int avg_file_cnt, avg_meta_block_cnt;
	float prev_ratio;

	if (dev_size % SECTOR_SIZE)
	{
		fprintf(stderr,
			"Device is not sector-based. Extra bytes would be unused.\n");
	}
	block_size = SECTOR_SIZE;
	block_cnt = dev_size / block_size;
	prev_ratio = 1;
	while (1)
	{
		inodes_per_block = block_size / sizeof(struct akpfs_inode);
		avg_file_cnt = block_cnt / ((AVG_FILE_SIZE + block_size - 1) / block_size);
		avg_meta_block_cnt = (avg_file_cnt + inodes_per_block - 1) / inodes_per_block;
		if (1.0 * avg_meta_block_cnt / block_cnt > prev_ratio)
		{
			block_size /= 2;
			block_cnt *= 2;
			break;
		}
		prev_ratio = 1.0 * avg_meta_block_cnt / block_cnt;
		/*
		printf("BS: %d; BC: %d; FC: %d; MBC: %d; Ratio: %.2f%\n",
			block_size, block_cnt, avg_file_cnt, avg_meta_block_cnt,
			100.0 * prev_ratio);
		 */
		if (block_cnt % 2 != 0)
		{
			break;
		}
		block_size *= 2;
		block_cnt /= 2;
	}
	return block_size;
#else // Currently, driver logic is hard coded, assuming block device blk size <= file system blk size (PAGE_SIZE)
	return PAGE_SIZE;
#endif
}

void compute_akpfs_sb(int dev_size, int block_size, struct akpfs_sb *sb)
{
	int block_cnt;
	int avg_file_cnt, avg_meta_block_cnt;

	block_cnt = dev_size / block_size;

	sb->fs_type = AKPFS_SUPER_MAGIC;
	sb->block_size = block_size;
	sb->block_cnt = block_cnt;
	sb->inodes_per_block = block_size / sizeof(struct akpfs_inode);
	sb->inodes_block_start = 1;
	avg_file_cnt = block_cnt / ((AVG_FILE_SIZE + block_size - 1) / block_size);
	sb->inodes_block_cnt =
		(avg_file_cnt + sb->inodes_per_block - 1) / sb->inodes_per_block;
	sb->free_bits_per_block = block_size * 8;
	sb->free_bits_block_start = sb->inodes_block_start + sb->inodes_block_cnt;
	sb->free_bits_block_cnt =
		(block_cnt + sb->free_bits_per_block - 1) / sb->free_bits_per_block;
	sb->data_block_start = sb->free_bits_block_start + sb->free_bits_block_cnt;
	sb->data_block_cnt = block_cnt - sb->data_block_start;
	sb->root_inode = 1;
}

void compute_akpfs_empty_inode(struct akpfs_inode *inode)
{
	inode->file_name[0] = 0;
}

void compute_akpfs_empty_dir_inode(char *dir_name, struct akpfs_inode *inode)
{
	int i;

	strncpy(inode->file_name, dir_name, AKPFS_MAX_FILE_NAME_SIZE);
	inode->file_size = akpfs_dir;
	for (i = 0; i < AKPFS_MAX_ENTRIES_PER_DIR; i++)
	{
		inode->u.dir_entries[i] = 0;
	}
}

void compute_akpfs_empty_file_inode(char *file_name, struct akpfs_inode *inode)
{
	int i;

	strncpy(inode->file_name, file_name, AKPFS_MAX_FILE_NAME_SIZE);
	inode->file_size = 0;
	for (i = 0; i < AKPFS_MAX_ENTRIES_PER_DIR; i++)
	{
		inode->u.file_blocks[i] = 0;
	}
}

int make_akpfs(int fd, int dev_size, int block_size)
{
	struct akpfs_sb sb;
	struct akpfs_inode empty_inode, root_inode;
#ifdef ADD_ONE_FILE
	struct akpfs_inode file_inode;
 	// file_data length (excluding '\0') is assumed to be less than block size
	char file_data[] = "Good Morning Universe";
#endif
	unsigned int i, bc /* block count */, ec /* entry count */;
	unsigned int free_blocks_start;
	unsigned char bitmask;

	compute_akpfs_sb(dev_size, block_size, &sb);
	compute_akpfs_empty_inode(&empty_inode);
	compute_akpfs_empty_dir_inode("/", &root_inode);
#ifdef ADD_ONE_FILE
	root_inode.u.dir_entries[0] = sb.root_inode + 1;
	compute_akpfs_empty_file_inode("moksha.txt", &file_inode);
	file_inode.file_size = strlen(file_data);
	file_inode.u.file_blocks[0] = sb.data_block_start;
#endif
	/* Write the Super Block */
	lseek(fd, 0 * block_size, SEEK_SET);
	if (write(fd, &sb, sizeof(sb)) == -1) return -1;
	/* Clear the Inode Blocks */
	for (i = 0, bc = sb.inodes_block_start; i < sb.inodes_block_cnt; i++, bc++)
	{
		lseek(fd, bc * block_size, SEEK_SET);
		for (ec = 0; ec < sb.inodes_per_block; ec++)
		{
			if (write(fd, &empty_inode, sizeof(empty_inode)) == -1) return -2;
		}
	}
	/* Write the Root Inode */
	lseek(fd,
		(sb.inodes_block_start + (sb.root_inode / sb.inodes_per_block)) *
			block_size +
		(sb.root_inode % sb.inodes_per_block) * sizeof(struct akpfs_inode),
		SEEK_SET);
	if (write(fd, &root_inode, sizeof(root_inode)) == -1) return -3;
#ifdef ADD_ONE_FILE
	/* Write the Inode for "moksha.txt" */
	if (write(fd, &file_inode, sizeof(file_inode)) == -1) return -3;
	/* Write the Data Block for "moksha.txt" */
	lseek(fd, sb.data_block_start * block_size, SEEK_SET);
	if (write(fd, &file_data, file_inode.file_size) == -1) return -3;
	free_blocks_start = sb.data_block_start + 1;
#else
	free_blocks_start = sb.data_block_start;
#endif
	/* Initialize the Free Bits for Blocks */
	lseek(fd, sb.free_bits_block_start * block_size, SEEK_SET);
	bitmask = 0xFF;
	for (i = 0; i < (free_blocks_start / 8); i++)
	{
		if (write(fd, &bitmask, sizeof(bitmask)) == -1) return -4;
	}
	if (free_blocks_start % 8)
	{
		bitmask >>= (8 - (free_blocks_start % 8));
		if (write(fd, &bitmask, sizeof(bitmask)) == -1) return -5;
		i++;
	}
	bitmask = 0x00;
	for ( ; i < (sb.block_cnt / 8); i++)
	{
		if (write(fd, &bitmask, sizeof(bitmask)) == -1) return -6;
	}
	if (sb.block_cnt % 8)
	{
		bitmask >>= (8 - (sb.block_cnt % 8));
		if (write(fd, &bitmask, sizeof(bitmask)) == -1) return -7;
		i++;
	}
	return 0;
}

int main(int argc, char *argv[])
{
	struct stat dev_stat;
	int fd;
	int dev_size, block_size;

	if (argc != 2)
	{
		fprintf(stderr, "Usage: %s <block device>\n", argv[0]);
		return 1;
	}
	if (stat(argv[1], &dev_stat) == -1)
	{
		perror(argv[0]);
		return 2;
	}
	//print_stat(&dev_stat);
	if ((fd = open(argv[1], O_RDWR)) == -1)
	{
		perror(argv[0]);
		return 3;
	}
	dev_size = get_dev_size(fd);
	block_size = compute_blk_size(dev_size);
	if (make_akpfs(fd, dev_size, block_size) < 0)
	{
		close(fd);
		return 4;
	}
	printf("akp fs created on device %s [%d bytes = %d blocks] (1 block = %d bytes)\n",
		argv[1], dev_size, dev_size / block_size, block_size);
	close(fd);
	return 0;
}
