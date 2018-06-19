/* Block Driver over DDK */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/fs.h> // ioctl cmds are also here
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/genhd.h> // For basic block driver framework
#include <linux/blkdev.h> // For at least, struct block_device_operations
#include <linux/hdreg.h> // For struct hd_geometry
#include <linux/workqueue.h> // For workqueue related functionalities
#include <linux/errno.h>
#include <asm/atomic.h> // For atomic_t, ...

#include "ddk_storage.h"

#define DDK_FIRST_MINOR 0
#define DDK_MINOR_CNT 16

/* 
 * The internal structure representation of our Device
 */
static struct ddk_device
{
	u_int major;
	/* Size is the size of the device (in sectors) */
	unsigned int size;
	/* For access protection of the request queue */
	spinlock_t lock;
	/* And the Request Queue */
	struct request_queue *queue;
	/* This is kernel's representation of an individual disk device */
	struct gendisk *disk;
	/* Data structures used in request processing work */
	atomic_t under_process;
	struct request *current_req;
	struct work_struct work;
} ddk_dev;

static int ddk_open(struct block_device *bdev, fmode_t mode)
{
	unsigned unit = iminor(bdev->bd_inode);

	printk(KERN_INFO "ddkb: Device is opened\n");
	printk(KERN_INFO "ddkb: Inode number is %d\n", unit);

	if (unit > DDK_MINOR_CNT)
		return -ENODEV;
	return 0;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0))
static int ddk_close(struct gendisk *disk, fmode_t mode)
{
	printk(KERN_INFO "ddkb: Device is closed\n");
	return 0;
}
#else
static void ddk_close(struct gendisk *disk, fmode_t mode)
{
	printk(KERN_INFO "ddkb: Device is closed\n");
}
#endif

static void ddk_geo_fill(struct hd_geometry *geo)
{
	geo->heads = 1;
	geo->cylinders = 1;
	geo->sectors = 1;
	geo->start = 0;
}

static int ddk_ioctl(struct block_device *bdev, fmode_t mode, unsigned cmd, unsigned long arg)
{
	long lval;
	u64 dval;
	size_t sval;
	int val;
	struct hd_geometry geo;

	switch (cmd)
	{
		case BLKGETSIZE:
			lval = (long)ddk_dev.size;
			if (copy_to_user((long *)arg, &lval, sizeof(long)))
			{
				return -EFAULT;
			}
			break;
		case BLKGETSIZE64:
			dval = (u64)ddk_dev.size * DDK_SECTOR_SIZE;
			if (copy_to_user((u64 *)arg, &dval, sizeof(u64)))
			{
				return -EFAULT;
			}
			break;
		case BLKSSZGET:
			sval = (size_t)DDK_SECTOR_SIZE;
			if (copy_to_user((size_t *)arg, &sval, sizeof(size_t)))
			{
				return -EFAULT;
			}
			break;
		case BLKBSZGET:
			val = DDK_SECTOR_SIZE;
			if (copy_to_user((int *)arg, &val, sizeof(int)))
			{
				return -EFAULT;
			}
			break;
		case HDIO_GETGEO:
			ddk_geo_fill(&geo);
			if (copy_to_user((struct hd_geometry *)arg, &geo, sizeof(struct hd_geometry)))
			{
				return -EFAULT;
			}
			break;
		default:
			return -EINVAL;
			break;
	}
	return 0;
}
#ifdef TODO
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38))
static int ddk_media_changed(struct gendisk *disk)
{
	return -EPERM;
}
#else
static unsigned int ddk_check_events(struct gendisk *disk, unsigned int clearing)
{
	return 0;
}
#endif

static int ddk_revalidate_disk(struct gendisk *disk)
{
	return -EPERM;
}
#endif

static int ddk_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	ddk_geo_fill(geo);
	return 0;
}

/* 
 * Actual Data transfer function scheduled as a work on the system's global
 * work_queue. This function is written in an re-entrant fashion, so that
 * its multiple invocation could actually be processing multiple requests
 * simultaneously. And hence, we reset the under_process flag after making
 * a local copy of the current_req pointer
 */
static void ddk_transfer(struct work_struct *w)
{
	struct request *req = ddk_dev.current_req;
	int dir = rq_data_dir(req);
	sector_t start_sector = blk_rq_pos(req);
	unsigned int sector_cnt = blk_rq_sectors(req);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0))
#define BV_PAGE(bv) ((bv)->bv_page)
#define BV_OFFSET(bv) ((bv)->bv_offset)
#define BV_LEN(bv) ((bv)->bv_len)
	struct bio_vec *bv;
#else
#define BV_PAGE(bv) ((bv).bv_page)
#define BV_OFFSET(bv) ((bv).bv_offset)
#define BV_LEN(bv) ((bv).bv_len)
	struct bio_vec bv;
#endif
	struct req_iterator iter;

	sector_t sector_offset;
	unsigned int sectors;
	u8 *buffer;

	int ret = 0, ret2;

	atomic_set(&ddk_dev.under_process, 0);

	//printk(KERN_DEBUG "ddkb: Dir:%d; Sec:%lld; Cnt:%d\n", dir, start_sector, sector_cnt);

	sector_offset = 0;
	rq_for_each_segment(bv, req, iter)
	{
		buffer = page_address(BV_PAGE(bv)) + BV_OFFSET(bv);
		if (BV_LEN(bv) % DDK_SECTOR_SIZE != 0)
		{
			printk(KERN_ERR "ddkb: Should never happen: "
				"bio size (%d) is not a multiple of DDK_SECTOR_SIZE (%d).\n"
				"This may lead to data truncation.\n",
				BV_LEN(bv), DDK_SECTOR_SIZE);
			ret = -EIO;
		}
		sectors = BV_LEN(bv) / DDK_SECTOR_SIZE;
		printk(KERN_DEBUG "ddkb: Sector Offset: %llu; Buffer: %p; Length: %d sectors\n",
			(unsigned long long)(sector_offset), buffer, sectors);
		if (dir == WRITE) /* Write to the device */
		{
			ret2 = ddk_storage_write(start_sector + sector_offset, buffer, sectors);
		}
		else /* Read from the device */
		{
			ret2 = ddk_storage_read(start_sector + sector_offset, buffer, sectors);
		}

		if (ret2 < 0)
		{
			ret = ret2;
		}
		sector_offset += sectors;
	}
	if (sector_offset != sector_cnt)
	{
		printk(KERN_ERR "ddkb: bio info doesn't match with the request info");
		ret = -EIO;
	}

	blk_end_request_all(req, ret); // Servicing the request done
}

static int ddk_initiate_transfer(struct request *req)
{
	int ret;

	atomic_set(&ddk_dev.under_process, 1);
	ddk_dev.current_req = req;
	if ((ret = schedule_work(&ddk_dev.work)) < 0)
	{
		atomic_set(&ddk_dev.under_process, 0);
		return ret;
	}
	return 0;
}

/*
 * Represents a block I/O request for us to process.
 * Caller of this would have taken the spin_lock on the queue.
 * So, use all the no spin_lock'ing functions
 */
static void ddk_request(struct request_queue *q)
{
	struct request *req;
	int ret;

	if (atomic_read(&ddk_dev.under_process)) // Already under process, come again later
	{
		return;
	}
	/*
	 * The above 'test' & then 'set' in ddk_initiate_transfer need not be atomic,
	 * as this func is non-reentrant bcoz of request queue's spin lock
	 */

	/* Gets the current request from the dispatch queue */
	while ((req = blk_fetch_request(q)) != NULL)
	{
#if 0
		/*
		 * This function tells us whether we are looking at a filesystem request
		 * - one that moves block of data
		 */
		if (!blk_fs_request(req))
		{
			printk(KERN_NOTICE "ddkb: Skip non-fs request\n");
			/* We pass 0 to indicate that we successfully completed the request */
			__blk_end_request_all(req, 0);
			continue;
		}
#endif
		/*
		 * The following call initiates the transfer & returns asynchronously,
		 * as we can't yield here, as request functions doesn't execute in any
		 * process context - in fact they may get executed in interrupt context
		 */
		ret = ddk_initiate_transfer(req);
		if (ret < 0) // Transfer not possible: Finish off this request & continue
		{
			__blk_end_request_all(req, ret);
			//__blk_end_request(req, ret, blk_rq_bytes(req)); // Partial request served
		}
		else
		/*
		 * Request under processing - come again later. Also, the completing of
		 * the request using *blk_end_request* will be done once the request is
		 * actually processed
		 */
		{
			return;
		}
	}
}

/* 
 * These are the file operations that performed on the ddk block device
 */
static struct block_device_operations ddk_fops =
{
	.owner = THIS_MODULE,
	.open = ddk_open,
	.release = ddk_close,
	.ioctl = ddk_ioctl,
#ifdef TODO
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38))
	.media_changed = ddk_media_changed,
#else
	.check_events = ddk_check_events,
#endif
	.revalidate_disk = ddk_revalidate_disk,
#endif
	.getgeo = ddk_getgeo
};
	
/* 
 * This is the registration and initialization section of the ddk block device
 * driver
 */
int block_register_dev(void)
{
	int ret;

	/*
	 * Set up the request processing work related data structures as the first
	 * thing, as the request processing would get triggered internally by call
	 * of some of the function(s) below, esp. add_disk
	 */
	atomic_set(&ddk_dev.under_process, 0);
	ddk_dev.current_req = NULL;
	INIT_WORK(&ddk_dev.work, ddk_transfer);

	/* Set up our DDK Storage */
	if ((ret = ddk_storage_init()) < 0)
	{
		return ret;
	}
	ddk_dev.size = ret;

	/* Get Registered */
	ddk_dev.major = register_blkdev(0, "ddk");
	if (ddk_dev.major <= 0)
	{
		printk(KERN_ERR "ddkb: Unable to get Major Number\n");
		ddk_storage_cleanup();
		return -EBUSY;
	}

	/* Get a request queue (here queue is created) */
	spin_lock_init(&ddk_dev.lock);
	ddk_dev.queue = blk_init_queue(ddk_request, &ddk_dev.lock);
	if (ddk_dev.queue == NULL)
	{
		printk(KERN_ERR "ddkb: blk_init_queue failure\n");
		unregister_blkdev(ddk_dev.major, "ddk");
		ddk_storage_cleanup();
		return -ENOMEM;
	}
	
	/*
	 * Allocating the gendisk structure,
	 * with support for minor count of partitions
	 */
	ddk_dev.disk = alloc_disk(DDK_MINOR_CNT);
	if (!ddk_dev.disk)
	{
		printk(KERN_ERR "ddkb: alloc_disk failure\n");
		blk_cleanup_queue(ddk_dev.queue);
		unregister_blkdev(ddk_dev.major, "ddk");
		ddk_storage_cleanup();
		return -ENOMEM;
	}

 	/* Setting the major number */
	ddk_dev.disk->major = ddk_dev.major;
  	/* Setting the first minor number */
	ddk_dev.disk->first_minor = DDK_FIRST_MINOR;
 	/* Setting the block device operations */
	ddk_dev.disk->fops = &ddk_fops;
 	/* driver-specific own internal data */
	ddk_dev.disk->private_data = &ddk_dev;
	ddk_dev.disk->queue = ddk_dev.queue;
	/*
	 * You do not want partition information to show up in 
	 * cat /proc/partitions set this flags
	 */
	//ddk_dev.disk->flags = GENHD_FL_SUPPRESS_PARTITION_INFO;
	sprintf(ddk_dev.disk->disk_name, "ddk");
	
	/*
	 * This function sets the capacity of the device in its gendisk structure.
	 * Prototype of this function is in linux/genhd.h.
	 * ddk_dev.disk->part0.nr_sects = size;
	 */
	set_capacity(ddk_dev.disk, ddk_dev.size);

	/* 
	 * Adding the disk to the system. Now the disk is "live"
	 */
	add_disk(ddk_dev.disk);
	printk(KERN_INFO "ddkb: DDK Block driver initialised (%d sectors; %d bytes)\n",
		ddk_dev.size, ddk_dev.size * DDK_SECTOR_SIZE);

	return 0;
}
/*
 * This is the unregistration and uninitialization section of the ddk block
 * device driver
 */
void block_deregister_dev(void)
{
	cancel_work_sync(&ddk_dev.work); // In case, any work is already scheduled
	del_gendisk(ddk_dev.disk);
	put_disk(ddk_dev.disk);
	blk_cleanup_queue(ddk_dev.queue);
	unregister_blkdev(ddk_dev.major, "ddk");
	ddk_storage_cleanup();
}
