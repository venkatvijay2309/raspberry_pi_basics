//#define ENABLE_USB_DEV 1

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/usb.h>
#ifndef ENABLE_USB_DEV
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h> // For <linux/err.h> for IS_ERR, PTR_ERR
#endif

#include "ddk.h"

#ifndef ENABLE_USB_DEV
#define FIRST_MINOR 0
#define MINOR_CNT 1

static dev_t dev;
static struct cdev c_dev;
static struct class *cl;
#endif
static struct usb_class_driver ucd;

static int ddk_open(struct inode *i, struct file *f)
{
	return 0;
}
static int ddk_close(struct inode *i, struct file *f)
{
	return 0;
}

static struct file_operations ddk_fops =
{
	.owner = THIS_MODULE,
	.open = ddk_open,
	.release = ddk_close,
};

#ifndef ENABLE_USB_DEV
static int char_register_dev(struct usb_interface *interface, struct usb_class_driver *ucd)
{
	int ret;
	struct device *dev_ret;

	if ((ret = alloc_chrdev_region(&dev, FIRST_MINOR, MINOR_CNT, "ddk_vert")) < 0)
	{
		return ret;
	}
	printk(KERN_INFO "Major, Minor: %d, %d\n", MAJOR(dev), MINOR(dev));

	cdev_init(&c_dev, ucd->fops);
	if ((ret = cdev_add(&c_dev, dev, MINOR_CNT)) < 0)
	{
		unregister_chrdev_region(dev, MINOR_CNT);
		return ret;
	}

	if (IS_ERR(cl = class_create(THIS_MODULE, "ddk")))
	{
		cdev_del(&c_dev);
		unregister_chrdev_region(dev, MINOR_CNT);
		return PTR_ERR(cl);
	}
	if (IS_ERR(dev_ret = device_create(cl, NULL, dev, NULL, ucd->name, FIRST_MINOR)))
	{
		class_destroy(cl);
		cdev_del(&c_dev);
		unregister_chrdev_region(dev, MINOR_CNT);
		return PTR_ERR(dev_ret);
	}

	interface->minor = MINOR(dev);

	return 0;
}

static void char_deregister_dev(struct usb_interface *interface, struct usb_class_driver *ucd)
{
	interface->minor = -1;
	device_destroy(cl, dev);
	class_destroy(cl);
	cdev_del(&c_dev);
	unregister_chrdev_region(dev, MINOR_CNT);
}
#endif

static int ddk_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	int retval;

	printk(KERN_INFO "DDK USB i/f %d now probed: (%04X:%04X)\n",
		interface->cur_altsetting->desc.bInterfaceNumber,
		id->idVendor, id->idProduct);

	if (interface->cur_altsetting->desc.bInterfaceNumber == 0)
	{
		ucd.name = "ddk%d";
		ucd.fops = &ddk_fops;
#ifndef ENABLE_USB_DEV
		retval = char_register_dev(interface, &ucd);
#else
		retval = usb_register_dev(interface, &ucd);
#endif
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
	}

	return 0;
}

static void ddk_disconnect(struct usb_interface *interface)
{
	if (interface->cur_altsetting->desc.bInterfaceNumber == 0)
	{
		printk(KERN_INFO "Releasing Minor: %d\n", interface->minor);

#ifndef ENABLE_USB_DEV
		/* Give back our minor */
		char_deregister_dev(interface, &ucd);
#else
		usb_deregister_dev(interface, &ucd);
#endif
	}

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
	.name = "ddk_vert",
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
MODULE_DESCRIPTION("USB w/ Vertical Driver for LDDK");
