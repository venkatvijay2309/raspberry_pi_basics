#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/usb.h>
#include <linux/mutex.h>
#include <linux/errno.h>

#include "ddk_storage.h"
#include "ddk_block.h"

enum
{
	e_read,
	e_write
};

static struct usb_device *device;
/*
 * The following mutex does two protections:
 * + Makes the above device pointer NULL (in disconnect), only when no one is accessing it
 * + All the following DDK operations using USB transactions are guaranteed to be atomic. Thus,
 * 	enabling the callers of these functions to be free from synchronization issues
 */
static DEFINE_MUTEX(m);

static int _set_mem(struct usb_device *dev) // w/o mutex
{
	/* Control OUT */
	return usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
			CUSTOM_RQ_SET_MEM_TYPE, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			flash, 0, NULL, 0, USB_CTRL_GET_TIMEOUT);
}

static int set_mem(struct usb_device *dev)
{
	int retval;

	mutex_lock(&m);
	if (!dev)
	{
		mutex_unlock(&m);
		return -ENODEV;
	}
	retval = _set_mem(dev);
	mutex_unlock(&m);
	return retval;
}

static int _get_size(struct usb_device *dev) // w/o mutex
{
	int retval;
	short val;

	/* Control IN */
	retval = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
			CUSTOM_RQ_GET_MEM_SIZE, USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			0, 0, &val, sizeof(val), USB_CTRL_GET_TIMEOUT);
	if (retval == 2)
	{
		return val;
	}
	else
	{
		return DDK_SECTOR_SIZE; // Just one sector
	}
}

static int get_size(struct usb_device *dev)
{
	int retval;

	mutex_lock(&m);
	if (!dev)
	{
		mutex_unlock(&m);
		return -ENODEV;
	}
	retval = _get_size(dev);
	mutex_unlock(&m);
	return retval;
}
#if 0
static int _get_off(struct usb_device *dev, int dir) // w/o mutex
{
	int retval;
	short val;

	/* Control IN */
	retval = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
			(dir == e_read) ? CUSTOM_RQ_GET_MEM_RD_OFFSET : CUSTOM_RQ_GET_MEM_WR_OFFSET,
			USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			0, 0, &val, sizeof(val), USB_CTRL_GET_TIMEOUT);

	if (retval == 2)
	{
		return val;
	}
	else
	{
		return (retval < 0) ? retval : -EINVAL;
	}
}
static int get_off(struct usb_device *dev, int dir)
{
	int retval;

	mutex_lock(&m);
	if (!dev)
	{
		mutex_unlock(&m);
		return -ENODEV;
	}
	retval = _get_off(dev, dir);
	mutex_unlock(&m);
	return retval;
}
#endif
static int _set_off(struct usb_device *dev, int dir, int offset) // w/o mutex
{
	/* Control OUT */
	return usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
			(dir == e_read) ? CUSTOM_RQ_SET_MEM_RD_OFFSET : CUSTOM_RQ_SET_MEM_WR_OFFSET,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			offset, 0, NULL, 0, USB_CTRL_SET_TIMEOUT);
}
#if 0
static int set_off(struct usb_device *dev, int dir, int offset)
{
	int retval;

	mutex_lock(&m);
	if (!dev)
	{
		mutex_unlock(&m);
		return -ENODEV;
	}
	retval = _set_off(dev, dir, offset);
	mutex_unlock(&m);
	return retval;
}
#endif

int ddk_storage_init(void)
{
	int retval;
	
	if ((retval = set_mem(device)) < 0) // Setting memory type to flash
	{
		return retval;
	}
	else
	{
		if ((retval = get_size(device)) < 0)
			return retval;
		else
			return retval / DDK_SECTOR_SIZE;
	}
}
void ddk_storage_cleanup(void)
{
}
int ddk_storage_write(sector_t sector_off, u8 *buffer, unsigned int sectors)
{
	int offset = sector_off * DDK_SECTOR_SIZE;
	int cnt = sectors * DDK_SECTOR_SIZE;
	int wrote_cnt, wrote_size;
	int retval;

	mutex_lock(&m);
	//printk(KERN_INFO "ddkb: Wr:S:C:O - %Ld:%d:%d\n", sector_off, sectors, _get_off(device, e_write));
	if (!device)
	{
		mutex_unlock(&m);
		return -ENODEV;
	}
	if ((retval = _set_off(device, e_write, offset)) < 0)
	{
		mutex_unlock(&m);
		printk(KERN_ERR "ddkb: Set Off Error: %d\n", retval);
		return retval;
	}
	wrote_cnt = 0;
	while (wrote_cnt < cnt)
	{
		/* Send the data out the int endpoint */
		retval = usb_interrupt_msg(device, usb_sndintpipe(device, MEM_EP_OUT),
			buffer + wrote_cnt, MAX_PKT_SIZE, &wrote_size, 0);
		if (retval)
		{
			mutex_unlock(&m);
			printk(KERN_ERR "ddkb: Interrupt message returned %d\n", retval);
			return retval;
		}
		//printk(KERN_INFO "ddkb: Wrote %d bytes\n", wrote_size);
		if (wrote_size == 0)
		{
			break;
		}
		wrote_cnt += wrote_size;
	}
	printk(KERN_INFO "ddkb: Wrote %d bytes\n", wrote_cnt);
	msleep(100); // TODO: Details in ../USBDriver/ddk_mem.c
	mutex_unlock(&m);

	return wrote_cnt;
}
int ddk_storage_read(sector_t sector_off, u8 *buffer, unsigned int sectors)
{
	int offset = sector_off * DDK_SECTOR_SIZE;
	int cnt = sectors * DDK_SECTOR_SIZE;
	int read_cnt, read_size;
	int retval;

	mutex_lock(&m);
	//printk(KERN_INFO "ddkb: Rd:S:C:O - %Ld:%d:%d\n", sector_off, sectors, _get_off(device, e_read));
	if (!device)
	{
		mutex_unlock(&m);
		return -ENODEV;
	}
	if ((retval = _set_off(device, e_read, offset)) < 0)
	{
		mutex_unlock(&m);
		printk(KERN_ERR "ddkb: Set Off Error: %d\n", retval);
		return retval;
	}
	read_cnt = 0;
	while (read_cnt < cnt)
	{
		/* Read the data in the int endpoint */
		retval = usb_interrupt_msg(device, usb_rcvintpipe(device, MEM_EP_IN),
			buffer + read_cnt, MAX_PKT_SIZE, &read_size, 0);
		if (retval)
		{
			mutex_unlock(&m);
			printk(KERN_ERR "ddkb: Interrupt message returned %d\n", retval);
			return retval;
		}
		//printk(KERN_INFO "ddkb: Read %d bytes\n", read_size);
		if (read_size == 0)
		{
			break;
		}
		read_cnt += read_size;
	}
	printk(KERN_INFO "ddkb: Read %d bytes\n", read_cnt);
	mutex_unlock(&m);

	return read_cnt;
}

static int ddk_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	if (interface->cur_altsetting->desc.bInterfaceNumber == 0)
	{
		device = interface_to_usbdev(interface);
		return block_register_dev();
	}
	else
	{
		return -EINVAL;
	}
}

static void ddk_disconnect(struct usb_interface *interface)
{
	block_deregister_dev();
	mutex_lock(&m); // Wait till any of the above calls are not over
	device = NULL;
	mutex_unlock(&m);
}

/* Table of devices that work with this driver */
static struct usb_device_id ddk_table[] =
{
	{
		USB_DEVICE(DDK_VENDOR_ID, DDK_PRODUCT_ID)
	},
	{} /* Terminating entry */
};
MODULE_DEVICE_TABLE (usb, ddk_table);

static struct usb_driver ddk_driver =
{
	.name = "ddk_block",
	.probe = ddk_probe,
	.disconnect = ddk_disconnect,
	.id_table = ddk_table,
};

static int __init ddk_init(void)
{
	int result;

	/* Register this driver with the USB subsystem */
	if ((result = usb_register(&ddk_driver)))
	{
		printk(KERN_ERR "usb_register failed. Error number %d\n", result);
	}
	printk(KERN_INFO "ddkb: DDK usb_registered\n");
	return result;
}

static void __exit ddk_exit(void)
{
	/* Deregister this driver with the USB subsystem */
	usb_deregister(&ddk_driver);
	printk(KERN_INFO "ddkb: DDK usb_deregistered\n");
}

module_init(ddk_init);
module_exit(ddk_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Anil Kumar Pugalia <email@sarika-pugs.com>");
MODULE_DESCRIPTION("USB Block Device Driver for DDK v1.1");
