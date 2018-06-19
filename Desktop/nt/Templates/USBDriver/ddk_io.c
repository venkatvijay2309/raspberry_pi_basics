#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/usb.h>
#include <linux/errno.h>

#include <asm/uaccess.h>

#include "ddk.h"
#include "ddk_io.h"

struct ddk_device
{
	struct usb_device *device;
	struct usb_class_driver ucd;
};

static struct ddk_device ddk_dev; // Need to be persistent

static int ddk_open(struct inode *i, struct file *f)
{
	return 0;
}
static int ddk_close(struct inode *i, struct file *f)
{
	return 0;
}
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,35))
static int ddk_ioctl(struct inode *i, struct file *f, unsigned int cmd, unsigned long arg)
#else
static long ddk_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
#endif
{
	Register r;
	int val, ind;
	char buf;
	int retval;

	if (copy_from_user(&r, (Register *)arg, sizeof(Register)))
	{
		return -EFAULT;
	}
	ind = r.id;
	switch (cmd)
	{
		case DDK_REG_GET:
			/* Control IN */
			retval = usb_control_msg(ddk_dev.device, usb_rcvctrlpipe(ddk_dev.device, 0),
						CUSTOM_RQ_GET_REGISTER, USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
						0, ind, &buf, sizeof(buf), 0);
			if (retval < 0)
			{
				printk(KERN_ERR "Control message returned %d\n", retval);
				return retval;
			}
			r.val = buf;
			if (copy_to_user((Register *)arg, &r, sizeof(Register)))
			{
				return -EFAULT;
			}
			break;
		case DDK_REG_SET:
			/* Control OUT */
			val = r.val;
			retval = usb_control_msg(ddk_dev.device, usb_sndctrlpipe(ddk_dev.device, 0),
						CUSTOM_RQ_SET_REGISTER, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
						val, ind, NULL, 0, 0);
			if (retval < 0)
			{
				printk(KERN_ERR "Control message returned %d\n", retval);
				return retval;
			}
			break;
		default:
			return -EINVAL;
			break;
	}
	return 0;
}

static struct file_operations fops =
{
	.owner = THIS_MODULE,
	.open = ddk_open,
	.release = ddk_close,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,35))
	.ioctl = ddk_ioctl,
#else
	.unlocked_ioctl = ddk_ioctl,
#endif
};

static int ddk_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	int retval;

	printk(KERN_INFO "DDK USB i/f %d now probed: (%04X:%04X)\n",
			interface->cur_altsetting->desc.bInterfaceNumber,
			id->idVendor, id->idProduct);

#ifdef ACQUIRE_SINGLE_INTERFACE
	if (interface->cur_altsetting->desc.bNumEndpoints == 0)
	{
#endif
		ddk_dev.device = interface_to_usbdev(interface);

		ddk_dev.ucd.name = "ddk_io%d";
		ddk_dev.ucd.fops = &fops;
		retval = usb_register_dev(interface, &ddk_dev.ucd);
		if (retval)
		{
			/* Something prevented us from registering this driver */
			printk(KERN_ERR "Not able to get a minor for this device.\n");
			return retval;
		}
		else
		{
			printk(KERN_INFO "Minor obtained: %d\n", interface->minor);
		}

		return 0;
#ifdef ACQUIRE_SINGLE_INTERFACE
	}
	else
	{
		return -EINVAL;
	}
#endif
}

static void ddk_disconnect(struct usb_interface *interface)
{
	printk(KERN_INFO "Releasing Minor: %d\n", interface->minor);

	/* Give back our minor */
	usb_deregister_dev(interface, &ddk_dev.ucd);

	printk(KERN_INFO "DDK USB i/f %d now disconnected\n",
			interface->cur_altsetting->desc.bInterfaceNumber);
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
	.name = "ddk_io",
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
	printk(KERN_INFO "DDK usb_registered\n");
	return result;
}

static void __exit ddk_exit(void)
{
	/* Deregister this driver with the USB subsystem */
	usb_deregister(&ddk_driver);
	printk(KERN_INFO "DDK usb_deregistered\n");
}

module_init(ddk_init);
module_exit(ddk_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Anil Kumar Pugalia <email@sarika-pugs.com>");
MODULE_DESCRIPTION("USB I/O Device Driver for DDK v2.1");
