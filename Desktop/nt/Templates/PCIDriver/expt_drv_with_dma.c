#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/pci.h>
#include <linux/errno.h>
#include <linux/interrupt.h>

#include <asm/io.h>
#include <asm/atomic.h>

#include <linux/mii.h>
#include <linux/delay.h>

#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <asm/uaccess.h>

//#define PKT_FLOW_CFG

#define EXPT_VENDOR_ID PCI_VENDOR_ID_REALTEK
#define EXPT_PRODUCT_ID 0x8136 /* Fast ethernet card on PCs */

#define	MAC_ADDR_REG_START 0
#define TX_DESC_ADDR_LOW_REG 0x20
#define TX_DESC_ADDR_HIGH_REG 0x24
#define CHIP_CMD_REG 0x37
#define TX_POLL_REG 0x38
#define	INTR_MASK_REG 0x3C
#define	INTR_STATUS_REG 0x3E
#define TX_CONFIG_REG 0x40
#define RX_CONFIG_REG 0x44
#define	CFG9346_REG 0x50
#define	PHY_ACCESS_REG 0x60
#define	PHY_STATUS_REG 0x6C
#define RX_MAX_SIZE_REG 0xDA
#define RX_DESC_ADDR_LOW_REG 0xE4
#define RX_DESC_ADDR_HIGH_REG 0xE8
#define MTPS_REG 0xEC

/* INTR_STATUS_REG bits */
#define INTR_TX_DESC_UNAVAIL (1 << 7)
//#define INTR_RX_FIFO_OVER (1<< 6)
#define INTR_LINK_CHG_BIT (1 << 5)
#define INTR_RX_DESC_UNAVAIL (1 << 4)
#define INTR_TX_ERR (1 << 3)
#define INTR_TX_OK_BIT (1 << 2)
#define INTR_RX_ERR (1 << 1)
#define INTR_RX_OK_BIT (1 << 0)

#define	INTR_MASK (INTR_LINK_CHG_BIT | INTR_TX_OK_BIT | INTR_RX_OK_BIT)
#define	INTR_ERR_MASK (INTR_TX_DESC_UNAVAIL | INTR_RX_DESC_UNAVAIL | INTR_TX_ERR | INTR_RX_ERR)

/* CHIP_CMD cmds */
#define CMD_RESET (1 << 4)
#define CMD_RX_ENB (1 << 3)
#define CMD_TX_ENB (1 << 2)

/* TX_POLL_REG (Transmit Priority Polling) bits */
#define HPQ_BIT (1 << 7)
#define NPQ_BIT (1 << 6)

/* CFG9346_REG cmds */
#define CFG9346_LOCK 0x00
#define CFG9346_UNLOCK 0xC0

/* PHY_STATUS_REG bits */
#define PHY_LINK_OK_BIT (1 << 1)

#define PKT_SIZE 256
#define TX_PKT_SIZE PKT_SIZE
#define RX_PKT_SIZE PKT_SIZE

typedef struct _Desc
{
	uint32_t opts1;
	uint32_t opts2;
	uint64_t addr;
} Desc;

enum DescStatusBits {
	DescOwn		= (1 << 31), /* Descriptor is owned by NIC */
	RingEnd		= (1 << 30), /* End of descriptor ring */
	FirstFrag	= (1 << 29), /* First segment of a packet */
	LastFrag	= (1 << 28), /* Final segment of a packet */
};

static struct dev_priv
{
	void __iomem *reg_base;
	dev_t dev;
	struct cdev c_dev;
	struct class *cl;
	Desc *desc_tx, *desc_rx;
	dma_addr_t phy_desc_tx, phy_desc_rx;
	uint8_t *pkt_tx, *pkt_rx;
	dma_addr_t phy_pkt_tx, phy_pkt_rx;
	int rx_pkt_size;
	atomic_t pkt_tx_busy, pkt_rx_avail;
} _pvt;

static int setup_buffers(struct pci_dev *dev)
{
	struct dev_priv *dpv = pci_get_drvdata(dev);

	dpv->desc_tx = pci_alloc_consistent(dev, sizeof(Desc), &dpv->phy_desc_tx);
	if (!dpv->desc_tx)
		return -ENOMEM;
	dpv->desc_rx = pci_alloc_consistent(dev, sizeof(Desc), &dpv->phy_desc_rx);
	if (!dpv->desc_rx)
	{
		pci_free_consistent(dev, sizeof(Desc), dpv->desc_tx, dpv->phy_desc_tx);
		return -ENOMEM;
	}

	if ((dpv->pkt_tx = kmalloc(TX_PKT_SIZE, GFP_KERNEL | GFP_DMA)) == NULL)
	{
		pci_free_consistent(dev, sizeof(Desc), dpv->desc_rx, dpv->phy_desc_rx);
		pci_free_consistent(dev, sizeof(Desc), dpv->desc_tx, dpv->phy_desc_tx);
	}
	dpv->phy_pkt_tx = pci_map_single(dev, dpv->pkt_tx, TX_PKT_SIZE, PCI_DMA_TODEVICE);
	if ((dpv->pkt_rx = kmalloc(RX_PKT_SIZE, GFP_KERNEL | GFP_DMA)) == NULL)
	{
		pci_unmap_single(dev, dpv->phy_pkt_tx, TX_PKT_SIZE, PCI_DMA_TODEVICE);
		kfree(dpv->pkt_tx);
		pci_free_consistent(dev, sizeof(Desc), dpv->desc_rx, dpv->phy_desc_rx);
		pci_free_consistent(dev, sizeof(Desc), dpv->desc_tx, dpv->phy_desc_tx);
	}
	dpv->phy_pkt_rx = pci_map_single(dev, dpv->pkt_rx, RX_PKT_SIZE, PCI_DMA_FROMDEVICE);

	dpv->desc_tx->opts1 = cpu_to_le32(RingEnd | FirstFrag | LastFrag);
	dpv->desc_tx->opts2 = 0;
	dpv->desc_tx->addr = cpu_to_le64(dpv->phy_pkt_tx);
	dpv->desc_rx->opts1 = cpu_to_le32(DescOwn | RingEnd | RX_PKT_SIZE);
	dpv->desc_rx->opts2 = 0;
	dpv->desc_rx->addr = cpu_to_le64(dpv->phy_pkt_rx);

	iowrite8(CFG9346_UNLOCK, dpv->reg_base + CFG9346_REG);
	iowrite32(dpv->phy_desc_tx & DMA_BIT_MASK(32), dpv->reg_base + TX_DESC_ADDR_LOW_REG);
	iowrite32((uint64_t)(dpv->phy_desc_tx) >> 32, dpv->reg_base + TX_DESC_ADDR_HIGH_REG);
	iowrite32(dpv->phy_desc_rx & DMA_BIT_MASK(32), dpv->reg_base + RX_DESC_ADDR_LOW_REG);
	iowrite32((uint64_t)(dpv->phy_desc_rx) >> 32, dpv->reg_base + RX_DESC_ADDR_HIGH_REG);
	iowrite8(CFG9346_LOCK, dpv->reg_base + CFG9346_REG);
	
	atomic_set(&dpv->pkt_tx_busy, 0);
	atomic_set(&dpv->pkt_rx_avail, 0);

	return 0;
}

static void cleanup_buffers(struct pci_dev *dev)
{
	struct dev_priv *dpv = pci_get_drvdata(dev);
	//int i;

#if 0 // Instead do a chip reset
	iowrite8(CFG9346_UNLOCK, dpv->reg_base + CFG9346_REG);
	iowrite32(0, dpv->reg_base + TX_DESC_ADDR_LOW_REG);
	iowrite32(0, dpv->reg_base + TX_DESC_ADDR_HIGH_REG);
	iowrite32(0, dpv->reg_base + RX_DESC_ADDR_LOW_REG);
	iowrite32(0, dpv->reg_base + RX_DESC_ADDR_HIGH_REG);
	iowrite8(CFG9346_LOCK, dpv->reg_base + CFG9346_REG);
	
	dpv->desc_rx->opts1 = 0;
	dpv->desc_rx->opts2 = 0;
	dpv->desc_rx->addr = 0;
	dpv->desc_tx->opts1 = 0;
	dpv->desc_tx->opts2 = 0;
	dpv->desc_tx->addr = 0;
#else
	/* Not needed as already done in hw_shut()
	iowrite8(CMD_RESET, dpv->reg_base + CHIP_CMD_REG);
	for (i = 0; i < 100; i--)
	{
		udelay(100);
		if ((ioread8(dpv->reg_base + CHIP_CMD_REG) & CMD_RESET) == 0)
			break;
	}
	*/
#endif

	pci_unmap_single(dev, dpv->phy_pkt_rx, RX_PKT_SIZE, PCI_DMA_FROMDEVICE);
	kfree(dpv->pkt_rx);
	pci_unmap_single(dev, dpv->phy_pkt_tx, TX_PKT_SIZE, PCI_DMA_TODEVICE);
	kfree(dpv->pkt_tx);

	pci_free_consistent(dev, sizeof(Desc), dpv->desc_rx, dpv->phy_desc_rx);
	pci_free_consistent(dev, sizeof(Desc), dpv->desc_tx, dpv->phy_desc_tx);
}

static inline void hw_enable_intr(struct dev_priv *dpv, uint16_t mask)
{
	iowrite16(ioread16(dpv->reg_base + INTR_MASK_REG) | mask, dpv->reg_base + INTR_MASK_REG);
}

static inline void hw_disable_intr(struct dev_priv *dpv, uint16_t mask)
{
	iowrite16(ioread16(dpv->reg_base + INTR_MASK_REG) & ~mask, dpv->reg_base + INTR_MASK_REG);
}

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

static inline void hw_init(struct pci_dev *dev)
{
#ifdef PKT_FLOW_CFG
	enum TxConfigBits {
		/* Interframe Gap Time. Only TxIFG96 doesn't violate IEEE 802.3 */
		TxIFGShift	= 24,
		TxIFG84		= (0 << TxIFGShift), /* 8.4us / 840ns (10 / 100Mbps) */
		TxIFG88		= (1 << TxIFGShift), /* 8.8us / 880ns (10 / 100Mbps) */
		TxIFG92		= (2 << TxIFGShift), /* 9.2us / 920ns (10 / 100Mbps) */
		TxIFG96		= (3 << TxIFGShift), /* 9.6us / 960ns (10 / 100Mbps) */

		TxLoopBack	= (1 << 18) | (1 << 17), /* Enable loopback test mode */
		TxCRC		= (1 << 16),	/* DISABLE Tx pkt CRC append */

		/* Max DMA burst */
		TxDMAShift	= 8, /* DMA burst value (0-6) is shifted this many bits */
		TxDMAUnlimited = (6 << TxDMAShift),
	};

	enum RxConfigBits {
		/* rx fifo threshold */
		RxFIFOShift	= 13,
		RxFIFONone	= (7 << RxFIFOShift),

		/* Max DMA burst */
		RxDMAShift	= 8, /* DMA burst value (0-6) is shifted this many bits */
		RxDMAUnlimited = (6 << RxDMAShift),

		AcceptErr	= 0x20,
		AcceptRunt	= 0x10,
		AcceptBroadcast	= 0x08,
		AcceptMulticast	= 0x04,
		AcceptMyPhys	= 0x02,
		AcceptAllPhys	= 0x01,
	};
#endif

	struct dev_priv *dpv = pci_get_drvdata(dev);

	phy_init(dev);

#ifdef PKT_FLOW_CFG
	iowrite8(CFG9346_UNLOCK, dpv->reg_base + CFG9346_REG);

	/* Maximum threshold for starting tx, even if packet not complete */
	iowrite8(0x3F, dpv->reg_base + MTPS_REG);
	/* Set DMA burst size and Interframe Gap Time */
	iowrite32(TxDMAUnlimited, dpv->reg_base + TX_CONFIG_REG);
	//iowrite32(TxDMAUnlimited | TxLoopBack | TxCRC, dpv->reg_base + TX_CONFIG_REG);
	/*
	 * Enabling loopback by adding TxLoopBack will get back the same packet.
 	 *	+ Along with, it ignores the link status & hence LED remains always on.
 	 *	+ Also, the link change interrupts wouldn't come.
	 * Disabling CRC appending by adding TxCRC along with loopback will generate RxError.
	 */

	iowrite32(RxDMAUnlimited, dpv->reg_base + RX_CONFIG_REG);
	iowrite16(RX_PKT_SIZE, dpv->reg_base + RX_MAX_SIZE_REG);
	/* Set Rx packet filter */
	iowrite32(RxFIFONone | RxDMAUnlimited | AcceptBroadcast | AcceptMulticast | AcceptMyPhys, dpv->reg_base + RX_CONFIG_REG);
	//iowrite32(RxFIFONone | RxDMAUnlimited | AcceptErr | AcceptRunt | AcceptBroadcast | AcceptMulticast | AcceptMyPhys, dpv->reg_base + RX_CONFIG_REG);
	/*
	 * Enabling accepting errors by adding AcceptErr &/or AcceptRunt will silently drop the error packets
	 */

	iowrite8(CFG9346_LOCK, dpv->reg_base + CFG9346_REG);
#endif

	iowrite8(CMD_TX_ENB | CMD_RX_ENB, dpv->reg_base + CHIP_CMD_REG);

	hw_enable_intr(dpv, INTR_MASK | INTR_ERR_MASK);
}

static inline void hw_shut(struct pci_dev *dev)
{
	struct dev_priv *dpv = pci_get_drvdata(dev);
	int i;

	hw_disable_intr(dpv, INTR_MASK | INTR_ERR_MASK);

	iowrite8(CMD_RESET, dpv->reg_base + CHIP_CMD_REG); // To disable Tx & Rx
	for (i = 0; i < 100; i--)
	{
		udelay(100);
		if ((ioread8(dpv->reg_base + CHIP_CMD_REG) & CMD_RESET) == 0)
			break;
	}

#ifdef PKT_FLOW_CFG
	iowrite8(CFG9346_UNLOCK, dpv->reg_base + CFG9346_REG);

	iowrite32(0, dpv->reg_base + TX_CONFIG_REG);
	iowrite32(0, dpv->reg_base + RX_CONFIG_REG);

	iowrite8(CFG9346_LOCK, dpv->reg_base + CFG9346_REG);
#endif

	phy_shut(dev);
}

static irqreturn_t expt_pci_intr_handler(int irq, void *dev)
{
	static int link_ok = -1;

	struct dev_priv *dpv = pci_get_drvdata(dev);
	uint16_t intr_status;
	int cur_state;

	intr_status = ioread16(dpv->reg_base + INTR_STATUS_REG);
	if (!(intr_status & (INTR_MASK | INTR_ERR_MASK))) // Not our interrupt
	{
		printk(KERN_ERR "Expt Intr: Not our interrupt (Should not happen)\n");
		return IRQ_NONE;
	}

	if (intr_status & INTR_LINK_CHG_BIT)
	{
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
	}
	if (intr_status & INTR_TX_OK_BIT)
	{
		iowrite16(INTR_TX_OK_BIT, dpv->reg_base + INTR_STATUS_REG); // Clear it off
		atomic_dec(&dpv->pkt_tx_busy); // No longer busy
		printk(KERN_INFO "Expt Intr: packet transmitted\n");
	}
	if (intr_status & INTR_RX_OK_BIT)
	{
		iowrite16(INTR_RX_OK_BIT, dpv->reg_base + INTR_STATUS_REG); // Clear it off
		dpv->rx_pkt_size = le32_to_cpu(dpv->desc_rx->opts1) & ((1 << 14) - 1);
		atomic_inc(&dpv->pkt_rx_avail); // Now pkt is available
		printk(KERN_INFO "Expt Intr: packet received\n");
	}
	if (intr_status & INTR_ERR_MASK)
	{
		if (intr_status & INTR_TX_DESC_UNAVAIL)
			printk(KERN_INFO "Expt Intr: tx desc unavailable\n");
		if (intr_status & INTR_RX_DESC_UNAVAIL)
		{
			hw_disable_intr(dpv, INTR_RX_DESC_UNAVAIL);
			// Interrupt shall be enabled in read once desc is available
			printk(KERN_INFO "Expt Intr: rx desc unavailable\n");
		}
		if (intr_status & INTR_TX_ERR)
			printk(KERN_INFO "Expt Intr: tx error\n");
		if (intr_status & INTR_RX_ERR)
			printk(KERN_INFO "Expt Intr: rx error\n");
		iowrite16(INTR_ERR_MASK, dpv->reg_base + INTR_STATUS_REG); // Clear it off
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
	int to_read;

	printk(KERN_INFO "Expt: In read\n");
	if (!atomic_read(&dpv->pkt_rx_avail))
		return -EAGAIN;

	if (*off >= dpv->rx_pkt_size)
	{
		atomic_set(&dpv->pkt_rx_avail, 0);
		*off = 0;
		/* Make the buffer available to hardware */
		dpv->desc_rx->opts1 = cpu_to_le32(le32_to_cpu(dpv->desc_rx->opts1) | DescOwn | RX_PKT_SIZE);
		/* And so enable the rx desc unavailable intr */
		hw_enable_intr(dpv, INTR_RX_DESC_UNAVAIL);
		return 0;
	}

	to_read = min(len, dpv->rx_pkt_size - (size_t)*off);

	if (copy_to_user(buf, dpv->pkt_rx + *off, to_read))
	{
		return -EFAULT;
	}
	*off += to_read;

	return to_read;
}

static ssize_t expt_write(struct file *f, const char __user *buf, size_t len, loff_t *off)
{
	struct dev_priv *dpv = f->private_data;
	int to_write;

	printk(KERN_INFO "Expt: In write\n");
	atomic_inc(&dpv->pkt_tx_busy);
	if (atomic_read(&dpv->pkt_tx_busy) != 1)
	{
		atomic_dec(&dpv->pkt_tx_busy);
		return -EBUSY;
	}

	to_write = min(len, (size_t)TX_PKT_SIZE);

	if (copy_from_user(dpv->pkt_tx, buf, to_write))
	{
		return -EFAULT;
	}

	/* Make the buffer available to hardware and trigger transmit */
	dpv->desc_tx->opts1 = cpu_to_le32(le32_to_cpu(dpv->desc_tx->opts1) | DescOwn | to_write);
	iowrite8(NPQ_BIT, dpv->reg_base + TX_POLL_REG);

	return len;
}

static struct file_operations fops =
{
	.open = expt_open,
	.release = expt_close,
	.read = expt_read,
	.write = expt_write,
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

	//pci_set_master(dev); // Enable the PCI device to become the bus master for initiating DMA

	if ((dpv->reg_base = ioremap(pci_resource_start(dev, 2), pci_resource_len(dev, 2))) == NULL)
	{
		printk(KERN_ERR "Unable to map registers of this PCI device\n");
		pci_release_regions(dev);
		pci_disable_device(dev);
		return -ENODEV;
	}
	printk(KERN_INFO "Register Base: %p\n", dpv->reg_base);

	pci_set_drvdata(dev, dpv);

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

	retval = setup_buffers(dev); // Needs the drvdata
	if (retval)
	{
		printk(KERN_ERR "Unable to setup buffers for this PCI device\n");
		if (pci_dev_msi_enabled(dev)) pci_disable_msi(dev);
		pci_set_drvdata(dev, NULL);
		iounmap(dpv->reg_base);
		pci_release_regions(dev);
		pci_disable_device(dev);
		return retval;
	}
	else
	{
		printk(KERN_INFO "PCI device buffers setup\n");
	}

	retval = request_irq(dev->irq, expt_pci_intr_handler,
							(pci_dev_msi_enabled(dev) ? 0 : IRQF_SHARED), "expt_pci", dev);
	if (retval)
	{
		printk(KERN_ERR "Unable to register interrupt handler for this PCI device\n");
		cleanup_buffers(dev);
		if (pci_dev_msi_enabled(dev)) pci_disable_msi(dev);
		pci_set_drvdata(dev, NULL);
		iounmap(dpv->reg_base);
		pci_release_regions(dev);
		pci_disable_device(dev);
		return retval;
	}
	else
	{
		printk(KERN_INFO "PCI device interrupt handler registered\n");
	}

	hw_init(dev); // Needs the drvdata

	retval = char_register_dev(dpv);
	if (retval)
	{
		/* Something prevented us from registering this driver */
		printk(KERN_ERR "Unable to register the character vertical\n");
		hw_shut(dev); // Needs the drvdata
		free_irq(dev->irq, dev);
		cleanup_buffers(dev);
		if (pci_dev_msi_enabled(dev)) pci_disable_msi(dev);
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

	printk(KERN_INFO "PCI device registered\n");

	return 0;
}

static void expt_remove(struct pci_dev *dev)
{
	struct dev_priv *dpv = pci_get_drvdata(dev);

	char_deregister_dev(dpv);
	printk(KERN_INFO "Character vertical deregistered\n");

	hw_shut(dev); // Needs the drvdata

	free_irq(dev->irq, dev);
	printk(KERN_INFO "PCI device interrupt handler unregistered\n");

	cleanup_buffers(dev); // Needs the drvdata

	if (pci_dev_msi_enabled(dev)) pci_disable_msi(dev);
	printk(KERN_INFO "PCI device MSI disabled\n");

	pci_set_drvdata(dev, NULL);

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
