#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/usb.h>
#include <linux/mutex.h>
#include <linux/errno.h>
#include <linux/slab.h>

#include <asm/uaccess.h>

#include "ddk.h"

#define MIN(x, y) (((x) < (y)) ? (x) : (y))

struct ddk_device
{
	struct mutex m;
	int in_ep, out_ep;
	int in_buf_size, out_buf_size;
	unsigned char *in_buf, *out_buf;
	int in_buf_left_over;
	struct usb_device *device;
	struct usb_class_driver ucd;
};

static struct usb_driver ddk_driver;

static int ddk_open(struct inode *i, struct file *f)
{
	struct usb_interface *interface;
	struct ddk_device *dev;

	interface = usb_find_interface(&ddk_driver, iminor(i));
	if (!interface)
	{
		printk(KERN_ERR "%s - error, can't find device for minor %d\n",
				__func__, iminor(i));
		return -ENODEV;
	}

	if (!(dev = usb_get_intfdata(interface)))
	{
		return -ENODEV;
	}

	if (!mutex_trylock(&dev->m))
	{
		return -EBUSY;
	}

	dev->in_buf_left_over = 0;
	f->private_data = (void *)(dev);
	return 0;
}
static int ddk_close(struct inode *i, struct file *f)
{
	struct ddk_device *dev = (struct ddk_device *)(f->private_data);

	mutex_unlock(&dev->m);
	return 0;
}
static ssize_t ddk_write(struct file *f, const char __user *buf, size_t cnt, loff_t *off)
{
	struct ddk_device *dev = (struct ddk_device *)(f->private_data);
	int write_size, wrote_size, wrote_cnt;
	int retval;

	wrote_cnt = 0;
	while (wrote_cnt < cnt)
	{
		write_size = MIN(dev->out_buf_size, cnt - wrote_cnt /* Remaining */);
		/* Using buf may cause sync issues */
		if (copy_from_user(dev->out_buf, buf + wrote_cnt, write_size))
		{
			return -EFAULT;
		}
		/* Send the data out of the int endpoint */
		retval = usb_interrupt_msg(dev->device, usb_sndintpipe(dev->device, dev->out_ep),
			dev->out_buf, write_size, &wrote_size, 0);
		if (retval)
		{
			printk(KERN_ERR "Interrupt message returned %d\n", retval);
			return retval;
		}
		wrote_cnt += wrote_size;
		printk(KERN_INFO "Wrote %d bytes\n", wrote_size);
	}

	return wrote_cnt;
}
static ssize_t ddk_read(struct file *f, char __user *buf, size_t cnt, loff_t *off)
{
	struct ddk_device *dev = (struct ddk_device *)(f->private_data);
	int read_size, read_cnt;
	int retval;
	int i;

	printk(KERN_INFO "Read request for %zd bytes\n", cnt);
	/* Check for left over data */
	if (dev->in_buf_left_over)
	{
		read_size = dev->in_buf_left_over;
		dev->in_buf_left_over = 0;
	}
	else
	{
		/* Read the data in from the int endpoint */
		/* Using buf may cause sync issues - need mutex protection */
		retval = usb_interrupt_msg(dev->device, usb_rcvintpipe(dev->device, dev->in_ep),
			dev->in_buf, dev->in_buf_size, &read_size, 10);
		if (retval)
		{
			printk(KERN_ERR "Interrupt message returned %d\n", retval);
			if (retval != -ETIMEDOUT)
			{
				return retval;
			}
			else /* Not really an error but no data available */
			{
				read_size = 0;
			}
		}
	}
	if (read_size <= cnt)
	{
		read_cnt = read_size;
	}
	else
	{
		read_cnt = cnt;
	}
	if (copy_to_user(buf, dev->in_buf, read_cnt))
	{
		dev->in_buf_left_over = read_size;
		return -EFAULT;
	}
	for (i = cnt; i < read_size; i++)
	{
		dev->in_buf[i - cnt] = dev->in_buf[i];
	}
	if (cnt < read_size)
	{
		dev->in_buf_left_over = read_size - cnt;
	}
	else
	{
		dev->in_buf_left_over = 0;
	}
	printk(KERN_INFO "Actually read %d bytes (Sent to user: %d)\n", read_size, read_cnt);

	return read_cnt;
}

static struct file_operations fops =
{
	.owner = THIS_MODULE,
	.open = ddk_open,
	.release = ddk_close,
	.write = ddk_write,
	.read = ddk_read,
};

static int ddk_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	struct ddk_device *ddk_dev;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	int i;
	int retval;

	iface_desc = interface->cur_altsetting;
	printk(KERN_INFO "DDK USB i/f %d now probed: (%04X:%04X)\n",
			iface_desc->desc.bInterfaceNumber,
			id->idVendor, id->idProduct);

#ifdef ACQUIRE_SINGLE_INTERFACE
	if (iface_desc->desc.bInterfaceNumber == 1)
	{
#endif
		ddk_dev = (struct ddk_device *)(kzalloc(sizeof(struct ddk_device), GFP_KERNEL));
		if (!ddk_dev)
		{
			printk(KERN_ERR "Not able to get memory for data structure of this device.\n");
			retval = -ENOMEM;
			goto probe_err;
		}
		mutex_init(&ddk_dev->m);

		/* Set up the endpoint information related to serial */
		for (i = 0; i < iface_desc->desc.bNumEndpoints; i++)
		{
			endpoint = &iface_desc->endpoint[i].desc;

			if (endpoint->bEndpointAddress == SER_EP_IN)
			{
				ddk_dev->in_ep = endpoint->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK;
				ddk_dev->in_buf_size = endpoint->wMaxPacketSize;
				ddk_dev->in_buf = kmalloc(endpoint->wMaxPacketSize, GFP_KERNEL);
				if (!ddk_dev->in_buf)
				{
					printk(KERN_ERR "Not able to get memory for in ep buffer of this device.\n");
					retval = -ENOMEM;
					goto probe_err;
				}
				ddk_dev->in_buf_left_over = 0;
			}
			if (endpoint->bEndpointAddress == SER_EP_OUT)
			{
				ddk_dev->out_ep = endpoint->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK;
				ddk_dev->out_buf_size = endpoint->wMaxPacketSize;
				ddk_dev->out_buf = kmalloc(endpoint->wMaxPacketSize, GFP_KERNEL);
				if (!ddk_dev->out_buf)
				{
					printk(KERN_ERR "Not able to get memory for out ep buffer of this device.\n");
					retval = -ENOMEM;
					goto probe_err;
				}
			}
		}

		ddk_dev->device = interface_to_usbdev(interface);

		ddk_dev->ucd.name = "ddk_u2s%d";
		ddk_dev->ucd.fops = &fops;
		retval = usb_register_dev(interface, &ddk_dev->ucd);
		if (retval)
		{
			/* Something prevented us from registering this driver */
			printk(KERN_ERR "Not able to get a minor for this device.\n");
			goto probe_err;
		}
		else
		{
			printk(KERN_INFO "Minor obtained: %d\n", interface->minor);
		}

		usb_set_intfdata(interface, ddk_dev);

		return 0;
#ifdef ACQUIRE_SINGLE_INTERFACE
	}
	else
	{
		return -EINVAL;
	}
#endif

probe_err:
	if (ddk_dev->out_buf)
	{
		kfree(ddk_dev->out_buf);
	}
	if (ddk_dev->in_buf)
	{
		kfree(ddk_dev->in_buf);
	}
	if (ddk_dev)
	{
		kfree(ddk_dev);
	}

	return retval;
}

static void ddk_disconnect(struct usb_interface *interface)
{
	struct ddk_device *ddk_dev;

	printk(KERN_INFO "Releasing Minor: %d\n", interface->minor);

	ddk_dev = (struct ddk_device *)(usb_get_intfdata(interface));
	usb_set_intfdata(interface, NULL);

	/* Give back our minor */
	usb_deregister_dev(interface, &ddk_dev->ucd);

	/* Prevent ddk_open() from racing ddk_disconnect() */
	mutex_lock(&ddk_dev->m);
	kfree(ddk_dev->out_buf);
	kfree(ddk_dev->in_buf);
	mutex_unlock(&ddk_dev->m);

	kfree(ddk_dev);

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
	.name = "ddk_u2s",
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
MODULE_DESCRIPTION("USB 2 Serial Device Driver for LDDK");
