#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/pci.h>
#include <linux/errno.h>

#include <asm/io.h>

//#define ENABLE_FILE_OPS

#ifdef ENABLE_FILE_OPS
#include <linux/fs.h>
#include <linux/cdev.h>
#include <asm/uaccess.h>
#endif

#define EXPT_VENDOR_ID PCI_VENDOR_ID_REALTEK
#define EXPT_PRODUCT_ID 0x8136 /* Fast ethernet card on PCs */

#define	MAC_ADDR_REG_START 0

static struct dev_priv
{
	void __iomem *reg_base;
#ifdef ENABLE_FILE_OPS
	dev_t dev;
	struct cdev c_dev;
	struct class *cl;
#endif
} _pvt;

#ifdef ENABLE_FILE_OPS
static int expt_open(struct inode *i, struct file *f)
{
	printk(KERN_INFO "Expt: In open\n");
	f->private_data = &_pvt;
	return 0;
}
static int expt_close(struct inode *i, struct file *f)
{
	printk(KERN_INFO "Expt: In close\n");
	return 0;
}

static ssize_t expt_read(struct file *f, char __user *buf, size_t len, loff_t *off)
{
	struct dev_priv *dpv = f->private_data;
	int i, to_read;
	uint8_t addr;

	printk(KERN_INFO "Expt: In read - Buf: %p; Len: %d; Off: %Ld\nData:\n", buf, len, *off);

	if (*off >= 6)
		return 0;

	to_read = min(len, 6 - (size_t)*off);

	for (i = 0; i < to_read; i++)
	{
		addr = ioread8(dpv->reg_base + MAC_ADDR_REG_START + *off + i);
		if (copy_to_user(buf + i, &addr, 1))
		{
			return -EFAULT;
		}
	}
	*off += to_read;

	return to_read;
}

static struct file_operations fops =
{
	.open = expt_open,
	.release = expt_close,
	.read = expt_read,
};

static int char_register_dev(struct dev_priv *dpv)
{
	int ret;
	struct device *dev_ret;

	if ((ret = alloc_chrdev_region(&dpv->dev, 0, 1, "expt_pci")) < 0)
	{
		return ret;
	}
	printk(KERN_INFO "(Major, Minor): (%d, %d)\n", MAJOR(dpv->dev), MINOR(dpv->dev));

	cdev_init(&dpv->c_dev, &fops);
	if ((ret = cdev_add(&dpv->c_dev, dpv->dev, 1)) < 0)
	{
		unregister_chrdev_region(dpv->dev, 1);
		return ret;
	}

	if (IS_ERR(dpv->cl = class_create(THIS_MODULE, "pci")))
	{
		cdev_del(&dpv->c_dev);
		unregister_chrdev_region(dpv->dev, 1);
		return PTR_ERR(dpv->cl);
	}
	if (IS_ERR(dev_ret = device_create(dpv->cl, NULL, dpv->dev, NULL, "expt%d", 0)))
	{
		class_destroy(dpv->cl);
		cdev_del(&dpv->c_dev);
		unregister_chrdev_region(dpv->dev, 1);
		return PTR_ERR(dev_ret);
	}
	return 0;
}

static void char_deregister_dev(struct dev_priv *dpv)
{
	device_destroy(dpv->cl, dpv->dev);
	class_destroy(dpv->cl);
	cdev_del(&dpv->c_dev);
	unregister_chrdev_region(dpv->dev, 1);
}
#endif

static void display_pci_config_space(struct pci_dev *dev)
{
	int i;
	uint8_t b;
	uint16_t w;
   	uint32_t dw;

	for (i = 0; i < 6; i++)
	{
		printk(KERN_INFO "Bar %d: 0x%016llu-%016llu : %08lX : %s %s\n", i,
			(unsigned long long)(pci_resource_start(dev, i)),
			(unsigned long long)(pci_resource_end(dev, i)),
			pci_resource_flags(dev, i),
			pci_resource_flags(dev, i) & PCI_BASE_ADDRESS_SPACE_IO ? " IO" : "MEM",
			pci_resource_flags(dev, i) & PCI_BASE_ADDRESS_MEM_PREFETCH ? "PREFETCH" : "NON-PREFETCH");
	}

	printk(KERN_INFO "PCI Configuration Space:\n");
	pci_read_config_word(dev, 0, &w); printk("%04X:", w);
	pci_read_config_word(dev, 2, &w); printk("%04X:", w);
	pci_read_config_word(dev, 4, &w); printk("%04X:", w);
	pci_read_config_word(dev, 6, &w); printk("%04X:", w);
	pci_read_config_dword(dev, 8, &dw);
	printk("%02X:", dw & 0xFF); printk("%06X:", dw >> 8);
	pci_read_config_byte(dev, 12, &b); printk("%02X:", b);
	pci_read_config_byte(dev, 13, &b); printk("%02X:", b);
	pci_read_config_byte(dev, 14, &b); printk("%02X:", b);
	pci_read_config_byte(dev, 15, &b); printk("%02X\n", b);

	pci_read_config_dword(dev, 16, &dw); printk("%08X:", dw);
	pci_read_config_dword(dev, 20, &dw); printk("%08X:", dw);
	pci_read_config_dword(dev, 24, &dw); printk("%08X:", dw);
	pci_read_config_dword(dev, 28, &dw); printk("%08X\n", dw);

	pci_read_config_dword(dev, 32, &dw); printk("%08X:", dw);
	pci_read_config_dword(dev, 36, &dw); printk("%08X:", dw);
	pci_read_config_dword(dev, 40, &dw); printk("%08X:", dw);
	pci_read_config_word(dev, 44, &w); printk("%04X:", w);
	pci_read_config_word(dev, 46, &w); printk("%04X\n", w);

	pci_read_config_dword(dev, 48, &dw); printk("%08X:", dw);
	pci_read_config_dword(dev, 52, &dw); printk("%08X:", dw);
	pci_read_config_dword(dev, 56, &dw); printk("%08X:", dw);
	pci_read_config_byte(dev, 60, &b); printk("%02X:", b);
	pci_read_config_byte(dev, 61, &b); printk("%02X:", b);
	pci_read_config_byte(dev, 62, &b); printk("%02X:", b);
	pci_read_config_byte(dev, 63, &b); printk("%02X\n", b);
}

static int expt_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	struct dev_priv *dpv = &_pvt; // Should be converted to kmalloc for multi-card support
	int retval;

	retval = pci_enable_device(dev);
	if (retval)
	{
		printk(KERN_ERR "Unable to enable this PCI device\n");
		return retval;
	}
	else
	{
		printk(KERN_INFO "PCI device enabled\n");
	}

	display_pci_config_space(dev);

	retval = pci_request_regions(dev, "expt_pci");
	if (retval)
	{
		printk(KERN_ERR "Unable to acquire regions of this PCI device\n");
		pci_disable_device(dev);
		return retval;
	}
	else
	{
		printk(KERN_INFO "PCI device regions acquired\n");
	}

	if ((dpv->reg_base = ioremap(pci_resource_start(dev, 2), pci_resource_len(dev, 2))) == NULL)
	{
		printk(KERN_ERR "Unable to map registers of this PCI device\n");
		pci_release_regions(dev);
		pci_disable_device(dev);
		return -ENODEV;
	}
	printk(KERN_INFO "Register Base: %p\n", dpv->reg_base);

	printk(KERN_INFO "IRQ: %u\n", dev->irq);

	pci_set_drvdata(dev, dpv);

#ifdef ENABLE_FILE_OPS
	retval = char_register_dev(dpv);
	if (retval)
	{
		/* Something prevented us from registering this driver */
		printk(KERN_ERR "Unable to register the character vertical\n");
		pci_set_drvdata(dev, NULL);
		iounmap(dpv->reg_base);
		pci_release_regions(dev);
		pci_disable_device(dev);
		return retval;
	}
	else
	{
		printk(KERN_INFO "Character vertical registered\n");
	}
#endif

	printk(KERN_INFO "PCI device registered\n");

	return 0;
}

static void expt_remove(struct pci_dev *dev)
{
	struct dev_priv *dpv = pci_get_drvdata(dev);

	pci_set_drvdata(dev, NULL);

#ifdef ENABLE_FILE_OPS
	char_deregister_dev(dpv);
	printk(KERN_INFO "Character vertical deregistered\n");
#endif

	iounmap(dpv->reg_base);
	printk(KERN_INFO "PCI device memory unmapped\n");

	pci_release_regions(dev);
	printk(KERN_INFO "PCI device regions released\n");

	pci_disable_device(dev);
	printk(KERN_INFO "PCI device disabled\n");

	printk(KERN_INFO "PCI device unregistered\n");
}

/* Table of devices that work with this driver */
static struct pci_device_id expt_table[] =
{
	{
		PCI_DEVICE(EXPT_VENDOR_ID, EXPT_PRODUCT_ID)
	},
	{} /* Terminating entry */
};
MODULE_DEVICE_TABLE (pci, expt_table);

static struct pci_driver pci_drv =
{
	.name = "expt_pci",
	.probe = expt_probe,
	.remove = expt_remove,
	.id_table = expt_table,
};

static int __init expt_pci_init(void)
{
	int result;

	/* Register this driver with the PCI subsystem */
	if ((result = pci_register_driver(&pci_drv)))
	{
		printk(KERN_ERR "pci_register_driver failed. Error number %d\n", result);
	}
	printk(KERN_INFO "Expt PCI driver registered\n");
	return result;
}

static void __exit expt_pci_exit(void)
{
	/* Deregister this driver with the PCI subsystem */
	pci_unregister_driver(&pci_drv);
	printk(KERN_INFO "Expt PCI driver unregistered\n");
}

module_init(expt_pci_init);
module_exit(expt_pci_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Anil Kumar Pugalia <email@sarika-pugs.com>");
MODULE_DESCRIPTION("Experimental PCI Device Driver");
