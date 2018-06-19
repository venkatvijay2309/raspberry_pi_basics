#ifndef NET_STK_H
#define NET_STK_H

#ifdef __KERNEL__

#include <linux/kernel.h> /* printk() */
#include <linux/netdevice.h> /* struct net_device */
#include <linux/spinlock.h> /* spinlock_t */
#include <linux/pci.h> /* struct pci_dev */
#include <linux/skbuff.h> /* struct sk_buff */
#include <linux/workqueue.h> /* struct delayed_work */

#include <asm/io.h> /* __iomem */

#define dprintk(fmt, args...)	do { printk(KERN_DEBUG "pn: " fmt, ## args); } while (0)
#define iprintk(fmt, args...)	do { printk(KERN_INFO "pn: " fmt, ## args); } while (0)
#define nprintk(fmt, args...)	do { printk(KERN_NOTICE "pn: " fmt, ## args); } while (0)
#define wprintk(fmt, args...)	do { printk(KERN_WARNING "pn: " fmt, ## args); } while (0)
#define eprintk(fmt, args...)	do { printk(KERN_ERR "pn: " fmt, ## args); } while (0)
#define cprintk(fmt, args...)	do { printk(KERN_CRIT "pn: " fmt, ## args); } while (0)
#define egprintk(fmt, args...)	do { printk(KERN_EMERG "pn: " fmt, ## args); } while (0)

#define NUM_TX_DESC	1024 /* Number of Tx descriptor registers */
#define NUM_RX_DESC	1024 /* Number of Rx descriptor registers */

struct Desc
{
	uint32_t opts1;
	uint32_t opts2;
	uint64_t addr;
};

typedef struct _RingInfo
{
	struct sk_buff *skb;
	uint32_t len;
	uint8_t __pad[sizeof(void *) - sizeof(uint32_t)];
} RingInfo;

typedef struct _DrvPvt
{
	struct pci_dev *pci_dev; /* Pointer to PCI device data */
	void __iomem *reg_base;	/* Memory map to bus address */

	spinlock_t lock; /* Spin lock for this */
	struct net_device *dev; /* Pointer to net device data */
	struct napi_struct napi; /* The napi structure */
	struct net_device_stats stats; /* Statistics of net device */

	uint32_t cur_rx; /* RxDescArray index of the Rx desc of the next to be processed (by sw) pkt */
	uint32_t cur_tx; /* TxDescArray index of the next available Tx desc for putting Tx pkt (by sw) */
	uint32_t dirty_rx; /* RxDescArray index of the next to last available Rx desc for putting Rx pkt (by hw) */
	uint32_t dirty_tx; /* TxDescArray index of the Tx desc of the next to be transmitted (by hw) pkt */
	struct Desc *RxDescArray; /* 256-aligned Rx descriptor ring */
	struct Desc *TxDescArray; /* 256-aligned Tx descriptor ring */
	dma_addr_t RxPhyAddr; /* Rx descriptor ring's physical address */
	dma_addr_t TxPhyAddr; /* Tx descriptor ring's physical address */
	struct sk_buff *rx_skbuff[NUM_RX_DESC]; /* Rx data buffers */
	RingInfo tx_skb[NUM_TX_DESC]; /* Tx data buffers */
	unsigned rx_buf_sz;

	struct timer_list link_timer; // For handling link status changes

	struct delayed_work task; // For handling timeouts
} DrvPvt;

int net_register_dev(struct pci_dev *pdev);
void net_deregister_dev(struct pci_dev *pdev);

#endif

#endif
