#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/pci.h>
#include <linux/errno.h>
#include <linux/interrupt.h>

#include <asm/io.h>

//#define ENABLE_PHY

#ifdef ENABLE_PHY
#include <linux/mii.h>
#include <linux/delay.h>
#endif

#include <linux/fs.h>
#include <linux/cdev.h>
#include <asm/uaccess.h>

#define EXPT_VENDOR_ID PCI_VENDOR_ID_REALTEK
#define EXPT_PRODUCT_ID 0x8136 /* Fast ethernet card on PCs */

#define	MAC_ADDR_REG_START 0
#define	INTR_MASK_REG 0x3C
#define	INTR_STATUS_REG 0x3E
#define	PHY_ACCESS_REG 0x60
#define	PHY_STATUS_REG 0x6C

#define INTR_LINK_CHG_BIT (1 << 5)
#define PHY_LINK_OK_BIT (1 << 1)

static struct dev_priv
{
	void __iomem *reg_base;
	dev_t dev;
	struct cdev c_dev;
	struct class *cl;
} _pvt;

static inline void hw_enable_intr(struct pci_dev *dev)
{
	struct dev_priv *dpv = pci_get_drvdata(dev);

	iowrite16(INTR_LINK_CHG_BIT, dpv->reg_base + INTR_MASK_REG);
}

static inline void hw_disable_intr(struct pci_dev *dev)
{
	struct dev_priv *dpv = pci_get_drvdata(dev);

	iowrite16(0, dpv->reg_base + INTR_MASK_REG);
}

#ifdef ENABLE_PHY
static inline void phy_reg_write(void __iomem *reg_base, uint8_t reg_addr, uint16_t reg_value)
{
	int i;

	iowrite32((1 << 31) | (reg_addr << 16) | (reg_value << 0), reg_base + PHY_ACCESS_REG);
	for (i = 0; i < 10; i++)
	{
		udelay(100);
		if (!(ioread32(reg_base + PHY_ACCESS_REG) & (1 << 31))) // Write completed
			break;
	}
}

static inline uint16_t phy_reg_read(void __iomem *reg_base, uint8_t reg_addr)
{
	int i;

	iowrite32((0 << 31) | (reg_addr << 16), reg_base + PHY_ACCESS_REG);
	for (i = 0; i < 10; i++)
	{
		udelay(100);
		if ((ioread32(reg_base + PHY_ACCESS_REG) & (1 << 31))) // Read completed
			break;
	}
	return (ioread32(reg_base + PHY_ACCESS_REG) & 0xFFFF);
}

static inline void phy_init(struct pci_dev *dev)
{
	struct dev_priv *dpv = pci_get_drvdata(dev);

	phy_reg_write(dpv->reg_base, MII_BMCR, BMCR_ANENABLE);
}

static inline void phy_shut(struct pci_dev *dev)
{
	struct dev_priv *dpv = pci_get_drvdata(dev);

	phy_reg_write(dpv->reg_base, MII_BMCR, BMCR_PDOWN);
}
#endif

static inline void hw_init(struct pci_dev *dev)
{
#ifdef ENABLE_PHY
	phy_init(dev);
#endif
	hw_enable_intr(dev);
}

static inline void hw_shut(struct pci_dev *dev)
{
	hw_disable_intr(dev);
#ifdef ENABLE_PHY
	phy_shut(dev);
#endif
}

static irqreturn_t expt_pci_intr_handler(int irq, void *dev)
{
	static int link_ok = -1;

	struct dev_priv *dpv = pci_get_drvdata(dev);
	int cur_state;

	if (!(ioread16(dpv->reg_base + INTR_STATUS_REG) & INTR_LINK_CHG_BIT)) // Not our interrupt
	{
		printk(KERN_ERR "Expt Intr: Not our interrupt (Should not happen)\n");
		return IRQ_NONE;
	}

	iowrite16(INTR_LINK_CHG_BIT, dpv->reg_base + INTR_STATUS_REG); // Clear it off

	cur_state = ((ioread8(dpv->reg_base + PHY_STATUS_REG) & PHY_LINK_OK_BIT) ? 1 : 0);
	if (link_ok != -1) // Not first time
	{
		if (link_ok == cur_state)
		{
			printk(KERN_WARNING "Expt: Possibly missed a link change interrupt\n");
		}
	}
	link_ok = cur_state;

	if (link_ok)
	{
		printk(KERN_INFO "Expt Intr: link up\n");
	}
	else
	{
		printk(KERN_INFO "Expt Intr: link down\n");
	}

	return IRQ_HANDLED;
}

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

	printk(KERN_INFO "Expt: In read - Buf: %p; Len: %zd; Off: %Ld\nData:\n", buf, len, *off);

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

	retval = pci_enable_msi(dev);
	if (retval)
	{
		printk(KERN_INFO "Unable to enable MSI for this PCI device\n"); // Still okay to continue
	}
	else
	{
		printk(KERN_INFO "PCI device enabled w/ MSI\n");
		printk(KERN_INFO "IRQ w/ MSI: %u\n", dev->irq);
	}

	retval = request_irq(dev->irq, expt_pci_intr_handler,
							(pci_dev_msi_enabled(dev) ? 0 : IRQF_SHARED), "expt_pci", dev);
	if (retval)
	{
		printk(KERN_ERR "Unable to register interrupt handler for this PCI device\n");
		if (pci_dev_msi_enabled(dev)) pci_disable_msi(dev);
		iounmap(dpv->reg_base);
		pci_release_regions(dev);
		pci_disable_device(dev);
		return retval;
	}
	else
	{
		printk(KERN_INFO "PCI device interrupt handler registered\n");
	}

	pci_set_drvdata(dev, dpv);
	hw_init(dev); // Needs the drvdata

	retval = char_register_dev(dpv);
	if (retval)
	{
		/* Something prevented us from registering this driver */
		printk(KERN_ERR "Unable to register the character vertical\n");
		hw_shut(dev); // Needs the drvdata
		pci_set_drvdata(dev, NULL);
		free_irq(dev->irq, dev);
		if (pci_dev_msi_enabled(dev)) pci_disable_msi(dev);
		iounmap(dpv->reg_base);
		pci_release_regions(dev);
		pci_disable_device(dev);
		return retval;
	}
	else
	{
		printk(KERN_INFO "Character vertical registered\n");
	}

	printk(KERN_INFO "PCI device registered\n");

	return 0;
}

static void expt_remove(struct pci_dev *dev)
{
	struct dev_priv *dpv = pci_get_drvdata(dev);

	char_deregister_dev(dpv);
	printk(KERN_INFO "Character vertical deregistered\n");

	hw_shut(dev); // Needs the drvdata
	pci_set_drvdata(dev, NULL);

	free_irq(dev->irq, dev);
	printk(KERN_INFO "PCI device interrupt handler unregistered\n");

	if (pci_dev_msi_enabled(dev)) pci_disable_msi(dev);
	printk(KERN_INFO "PCI device MSI disabled\n");

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
