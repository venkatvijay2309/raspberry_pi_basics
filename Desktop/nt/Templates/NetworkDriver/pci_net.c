#include <linux/module.h>
#include <linux/errno.h>
#include <linux/pci.h>

#include "net_stk.h"

#define EXPT_VENDOR_ID PCI_VENDOR_ID_REALTEK
#define EXPT_PRODUCT_ID 0x8136

static int pn_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	int retval;
	void __iomem *reg_base;

	retval = pci_enable_device(dev);
	if (retval)
	{
		eprintk("PCI device not enabled\n");
		return retval;
	}
	else
	{
		iprintk("PCI device enabled\n");
	}

	retval = pci_set_mwi(dev); // Possibly some performance optimization
	if (retval)
	{
		eprintk("PCI device memory write invalidate set failed\n");
		pci_disable_device(dev);
		return retval;
	}
	else
	{
		iprintk("PCI device memory write invalidate set\n");
	}

	retval = pci_request_regions(dev, "pn");
	if (retval)
	{
		eprintk("PCI device regions not acquired\n");
		pci_clear_mwi(dev);
		pci_disable_device(dev);
		return retval;
	}
	else
	{
		iprintk("PCI device regions acquired\n");
	}

	pci_set_master(dev);

	if ((reg_base = ioremap(pci_resource_start(dev, 2), pci_resource_len(dev, 2))) == NULL)
	{
		eprintk("PCI device registers not mapped\n");
		pci_release_regions(dev);
		pci_clear_mwi(dev);
		pci_disable_device(dev);
		return -ENODEV;
	}
	iprintk("Register Base: 0x%p (%llu bytes)\n",
		reg_base, (unsigned long long)(pci_resource_len(dev, 2)));

	retval = pci_set_dma_mask(dev, DMA_BIT_MASK(32));
	if (retval)
	{
		eprintk("PCI device doesn't support DMA\n");
		iounmap(reg_base);
		pci_release_regions(dev);
		pci_clear_mwi(dev);
		pci_disable_device(dev);
		return retval;
	}
	else
	{
		iprintk("PCI device DMA enabled\n");
	}

	retval = pci_enable_msi(dev);
	if (retval)
	{
		iprintk("PCI device doesn't support MSI\n"); // That's okay
	}
	else
	{
		iprintk("PCI device enabled w/ MSI\n");
	}

	iprintk("PCI device IRQ: %u\n", dev->irq);

	pci_set_drvdata(dev, reg_base); // Used by net_register_dev()

	retval = net_register_dev(dev);
	if (retval)
	{
		pci_set_drvdata(dev, NULL);
		if (pci_dev_msi_enabled(dev))
		{
			pci_disable_msi(dev);
		}
		iounmap(reg_base);
		pci_release_regions(dev);
		pci_clear_mwi(dev);
		pci_disable_device(dev);
		eprintk("Not able to register this device.");
		return retval;
	}
	else
	{
		iprintk("PCI device registered\n");
	}

	return 0;
}

static void pn_remove(struct pci_dev *dev)
{
	void __iomem *reg_base;

	net_deregister_dev(dev);
	iprintk("PCI net device deregistered\n");

	reg_base = pci_get_drvdata(dev);
	pci_set_drvdata(dev, NULL);

	if (pci_dev_msi_enabled(dev))
	{
		pci_disable_msi(dev);
		iprintk("PCI device MSI disabled\n");
	}

	iounmap(reg_base);
	iprintk("PCI device memory unmapped\n");

	pci_release_regions(dev);
	iprintk("PCI device regions released\n");

	pci_clear_mwi(dev);
	iprintk("PCI device memory write invalidate cleared\n");

	pci_disable_device(dev);
	iprintk("PCI device disabled\n");
}

/* Table of devices that work with this driver */
static struct pci_device_id pn_table[] =
{
	{
		PCI_DEVICE(EXPT_VENDOR_ID, EXPT_PRODUCT_ID)
	},
	{} /* Terminating entry */
};
MODULE_DEVICE_TABLE(pci, pn_table);

static struct pci_driver pci_drv =
{
	.name = "pn",
	.probe = pn_probe,
	.remove = pn_remove,
	.id_table = pn_table,
};

static int __init pn_drv_init(void)
{
	int result;

	/* Register this driver with the PCI subsystem */
	if ((result = pci_register_driver(&pci_drv)))
	{
		eprintk("pci_register_driver failed. Error number %d", result);
	}
	iprintk("PCI driver registered\n");
	return result;
}

static void __exit pn_drv_exit(void)
{
	/* Deregister this driver with the PCI subsystem */
	pci_unregister_driver(&pci_drv);
	iprintk("PCI driver unregistered\n");
}

module_init(pn_drv_init);
module_exit(pn_drv_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Anil Kumar Pugalia <anil@sysplay.in>");
MODULE_DESCRIPTION("PCI Network Device Driver");
