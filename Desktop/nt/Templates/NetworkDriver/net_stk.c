#include <linux/module.h>
#include <linux/version.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/in.h>
#include <linux/skbuff.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/workqueue.h>
#include <linux/timer.h>

#include <asm/io.h>

#include "r8101.h"
#include "phy.h"
#include "mac.h"
#include "net_stk.h"

#define RTL_CHIP_NAME "RTL8101E" // "RTL8105E"

#define assert(expr) \
		if(!(expr)) {					\
			printk("Assertion failed! %s,%s,%s,line=%d\n",	\
			#expr,__FILE__,__FUNCTION__,__LINE__);		\
		}

#define R8101_NAPI_WEIGHT	64

#define R8101_TX_RING_BYTES	(NUM_TX_DESC * sizeof(struct Desc))
#define R8101_RX_RING_BYTES	(NUM_RX_DESC * sizeof(struct Desc))

#define RTL8101_TX_TIMEOUT		(6*HZ)
#define RTL8101_LINK_TIMEOUT	(1*HZ)

#define TX_BUFFS_AVAIL(pvt) \
	(pvt->dirty_tx + NUM_TX_DESC - pvt->cur_tx - 1)

/*
 * Due to the hardware design of RTL8101E, the low 32 bit address of receive
 * buffer must be 8-byte alignment
 */
#define RTK_RX_ALIGN 8
#define RX_BUF_SIZE 0x05F3	/* Rx Buffer size */

/*** Buffer Management Logic Start ***/

static void rtl8101_unmap_tx_skb(struct pci_dev *pdev,
					 RingInfo *tx_skb,
					 struct Desc *desc)
{
	unsigned int len = tx_skb->len;

	pci_unmap_single(pdev, le64_to_cpu(desc->addr), len, PCI_DMA_TODEVICE);
	desc->opts1 = 0x00;
	desc->opts2 = 0x00;
	desc->addr = 0x00;
	tx_skb->len = 0;
}

static void rtl8101_tx_clear(DrvPvt *pvt)
{
	unsigned int i;
	struct net_device *dev = pvt->dev;

	for (i = pvt->dirty_tx; i < pvt->dirty_tx + NUM_TX_DESC; i++) {
		unsigned int entry = i % NUM_TX_DESC;
		RingInfo *tx_skb = pvt->tx_skb + entry;
		unsigned int len = tx_skb->len;

		if (len) {
			struct sk_buff *skb = tx_skb->skb;

			rtl8101_unmap_tx_skb(pvt->pci_dev, tx_skb,
								 pvt->TxDescArray + entry);
			if (skb) {
				dev_kfree_skb(skb);
				tx_skb->skb = NULL;
			}
			dev->stats.tx_dropped++;
		}
	}
	pvt->cur_tx = pvt->dirty_tx = 0;
}

inline void rtl8101_make_unusable_by_asic(struct Desc *desc)
{
	desc->addr = 0x0badbadbadbadbadull;
	desc->opts1 &= ~cpu_to_le32(DescOwn | 0x3fffc000);
}

static inline void rtl8101_mark_to_asic(struct Desc *desc,
					 uint32_t rx_buf_sz)
{
	uint32_t eor = le32_to_cpu(desc->opts1) & RingEnd;

	desc->opts1 = cpu_to_le32(DescOwn | eor | rx_buf_sz);
}

static inline void rtl8101_map_to_asic(struct Desc *desc,
					dma_addr_t mapping,
					uint32_t rx_buf_sz)
{
	desc->addr = cpu_to_le64(mapping);
	smp_wmb();
	rtl8101_mark_to_asic(desc, rx_buf_sz);
}

static int rtl8101_alloc_rx_skb(struct pci_dev *pdev,
					 struct sk_buff **sk_buff,
					 struct Desc *desc,
					 int rx_buf_sz)
{
	struct sk_buff *skb;
	dma_addr_t mapping;

	skb = dev_alloc_skb(rx_buf_sz + RTK_RX_ALIGN);
	if (!skb)
		return -ENOMEM;

	skb_reserve(skb, RTK_RX_ALIGN);
	*sk_buff = skb;

	mapping = pci_map_single(pdev, skb->data, rx_buf_sz,
							 PCI_DMA_FROMDEVICE);

	rtl8101_map_to_asic(desc, mapping, rx_buf_sz);

	return 0;
}

static void rtl8101_free_rx_skb(DrvPvt *pvt,
					struct sk_buff **sk_buff,
					struct Desc *desc)
{
	struct pci_dev *pdev = pvt->pci_dev;

	pci_unmap_single(pdev, le64_to_cpu(desc->addr), pvt->rx_buf_sz,
					 PCI_DMA_FROMDEVICE);
	dev_kfree_skb(*sk_buff);
	*sk_buff = NULL;
	rtl8101_make_unusable_by_asic(desc);
}

static uint32_t rtl8101_rx_fill(DrvPvt *pvt, uint32_t start, uint32_t end)
{
	uint32_t cur;

	for (cur = start; end - cur > 0; cur++) {
		int ret, i = cur % NUM_RX_DESC;

		if (pvt->rx_skbuff[i])
			continue;

		ret = rtl8101_alloc_rx_skb(pvt->pci_dev, pvt->rx_skbuff + i,
									pvt->RxDescArray + i, pvt->rx_buf_sz);
		if (ret < 0)
			break;
	}
	return cur - start;
}

static void rtl8101_rx_clear(DrvPvt *pvt)
{
	int i;

	for (i = 0; i < NUM_RX_DESC; i++) {
		if (pvt->rx_skbuff[i]) {
			rtl8101_free_rx_skb(pvt, pvt->rx_skbuff + i,
								pvt->RxDescArray + i);
		}
	}
}

static void rtl8101_init_ring_indexes(DrvPvt *pvt)
{
	pvt->dirty_tx = 0;
	pvt->dirty_rx = 0;
	pvt->cur_tx = 0;
	pvt->cur_rx = 0;
}

static int rtl8101_init_buffers(DrvPvt *pvt)
{
	struct pci_dev *pdev = pvt->pci_dev;

	/*
	 * Rx and Tx desscriptors needs 256 bytes alignment.
	 * pci_alloc_consistent provides more.
	 */
	pvt->TxDescArray = pci_alloc_consistent(pdev, R8101_TX_RING_BYTES,
											&pvt->TxPhyAddr);
	if (!pvt->TxDescArray)
		return -ENOMEM;

	pvt->RxDescArray = pci_alloc_consistent(pdev, R8101_RX_RING_BYTES,
											&pvt->RxPhyAddr);
	if (!pvt->RxDescArray)
	{
		pci_free_consistent(pdev, R8101_TX_RING_BYTES, pvt->TxDescArray,
							pvt->TxPhyAddr);
		pvt->TxDescArray = NULL;
		return -ENOMEM;
	}

	rtl8101_init_ring_indexes(pvt);

	memset(pvt->tx_skb, 0x0, NUM_TX_DESC * sizeof(RingInfo));
	memset(pvt->rx_skbuff, 0x0, NUM_RX_DESC * sizeof(struct sk_buff *));

	memset(pvt->TxDescArray, 0x0, NUM_TX_DESC * sizeof(struct Desc));
	pvt->TxDescArray[NUM_TX_DESC - 1].opts1 = cpu_to_le32(RingEnd);
	memset(pvt->RxDescArray, 0x0, NUM_RX_DESC * sizeof(struct Desc));
	pvt->RxDescArray[NUM_RX_DESC - 1].opts1 = cpu_to_le32(RingEnd);
	if (rtl8101_rx_fill(pvt, 0, NUM_RX_DESC) != NUM_RX_DESC)
	{
		rtl8101_rx_clear(pvt);
		pci_free_consistent(pdev, R8101_RX_RING_BYTES, pvt->RxDescArray,
							pvt->RxPhyAddr);
		pvt->RxDescArray = NULL;
		pci_free_consistent(pdev, R8101_TX_RING_BYTES, pvt->TxDescArray,
							pvt->TxPhyAddr);
		pvt->TxDescArray = NULL;
		return -ENOMEM;
	}

	return 0;
}

static void rtl8101_shut_buffers(DrvPvt *pvt)
{
	struct pci_dev *pdev = pvt->pci_dev;

	pci_free_consistent(pdev, R8101_RX_RING_BYTES, pvt->RxDescArray,
						pvt->RxPhyAddr);
	pvt->RxDescArray = NULL;
	pci_free_consistent(pdev, R8101_TX_RING_BYTES, pvt->TxDescArray,
						pvt->TxPhyAddr);
	pvt->TxDescArray = NULL;
}

/*** Buffer Management Logic End ***/

/*** Link Status Polling Logic Start ***/

static void rtl8101_check_link_status(struct net_device *dev)
{
	DrvPvt *pvt = netdev_priv(dev);
	void __iomem *reg_base = pvt->reg_base;
	int link_status_on;

	link_status_on = phy_link_status_on(reg_base);

	if (netif_carrier_ok(dev) != link_status_on) {

		if (link_status_on) {
			rtl8101_hw_init(dev);
			netif_carrier_on(dev);
			iprintk("%s: link up\n", dev->name);
		} else {
			iprintk("%s: link down\n", dev->name);
			netif_carrier_off(dev);
			netif_stop_queue(dev);
			rtl8101_hw_reset(reg_base);
			rtl8101_tx_clear(pvt);
			rtl8101_init_ring_indexes(pvt);
		}
	}
}

static void rtl8101_link_timer(unsigned long __opaque)
{
	struct net_device *dev = (struct net_device *)__opaque;
	DrvPvt *pvt = netdev_priv(dev);
	struct timer_list *timer = &pvt->link_timer;
	unsigned long flags;

	spin_lock_irqsave(&pvt->lock, flags);
	rtl8101_check_link_status(dev);
	spin_unlock_irqrestore(&pvt->lock, flags);

	mod_timer(timer, jiffies + RTL8101_LINK_TIMEOUT);
}

static inline void rtl8101_request_link_timer(struct net_device *dev)
{
	DrvPvt *pvt = netdev_priv(dev);
	struct timer_list *timer = &pvt->link_timer;

	init_timer(timer);
	timer->expires = jiffies + RTL8101_LINK_TIMEOUT;
	timer->data = (unsigned long)(dev);
	timer->function = rtl8101_link_timer;
	add_timer(timer);
}

static inline void rtl8101_delete_link_timer(struct net_device *dev)
{
	DrvPvt *pvt = netdev_priv(dev);
	struct timer_list *timer = &pvt->link_timer;

	del_timer_sync(timer);
}

/*** Link Status Polling Logic End ***/

/*** Interrupt Handlers Start ***/

static int rtl8101_rx_interrupt(struct net_device *dev,
					 DrvPvt *pvt,
					 void __iomem *reg_base, uint32_t budget)
{
	unsigned int cur_rx, rx_left;
	unsigned int delta, count = 0;

	assert(dev != NULL);
	assert(pvt != NULL);
	assert(reg_base != NULL);

	cur_rx = pvt->cur_rx;
	rx_left = NUM_RX_DESC + pvt->dirty_rx - cur_rx;
	rx_left = min(rx_left, (uint32_t) budget);

	if ((pvt->RxDescArray == NULL) || (pvt->rx_skbuff == NULL))
		goto rx_out;

	for (; rx_left > 0; rx_left--, cur_rx++) {
		unsigned int entry = cur_rx % NUM_RX_DESC;
		struct Desc *desc = pvt->RxDescArray + entry;
		uint32_t status;

		smp_rmb();
		status = le32_to_cpu(desc->opts1);

		if (status & DescOwn)
			break;
		if (unlikely(status & RxRES)) {
			iprintk("%s: Rx ERROR. status = %08x\n", dev->name, status);
			dev->stats.rx_errors++;

			if (status & (RxRWT | RxRUNT))
				dev->stats.rx_length_errors++;
			if (status & RxCRC)
				dev->stats.rx_crc_errors++;
			rtl8101_mark_to_asic(desc, pvt->rx_buf_sz);
		} else {
			struct sk_buff *skb = pvt->rx_skbuff[entry];
			int pkt_size = (status & 0x00003FFF) - 4;

			pci_unmap_single(pvt->pci_dev,
							 le64_to_cpu(desc->addr), pvt->rx_buf_sz,
							 PCI_DMA_FROMDEVICE);
			pvt->rx_skbuff[entry] = NULL;
			skb->dev = dev;
			skb_put(skb, pkt_size);
			skb->protocol = eth_type_trans(skb, dev);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,29)
			netif_receive_skb(skb);
#else
			napi_gro_receive(&pvt->napi, skb);
#endif

			dev->last_rx = jiffies;
			dev->stats.rx_bytes += pkt_size;
			dev->stats.rx_packets++;
		}
	}

	count = cur_rx - pvt->cur_rx;
	pvt->cur_rx = cur_rx;

	delta = rtl8101_rx_fill(pvt, pvt->dirty_rx, pvt->cur_rx);
	if (!delta && count)
		iprintk("%s: no Rx buffer allocated\n", dev->name);
	pvt->dirty_rx += delta;

	/*
	 * FIXME: until there is periodic timer to try and refill the ring,
	 * a temporary shortage may definitely kill the Rx process.
	 * - disable the asic to try and avoid an overflow and kick it again
	 *   after refill ?
	 * - how do others driver handle this condition (Uh oh...).
	 */
	if ((pvt->dirty_rx + NUM_RX_DESC == pvt->cur_rx))
		egprintk("%s: Rx buffers exhausted\n", dev->name);

rx_out:

	return count;
}

static void rtl8101_tx_interrupt(struct net_device *dev,
					 DrvPvt *pvt,
					 void __iomem *reg_base)
{
	unsigned int dirty_tx, tx_left;

	assert(dev != NULL);
	assert(pvt != NULL);
	assert(reg_base != NULL);

	dirty_tx = pvt->dirty_tx;
	smp_rmb();
	tx_left = pvt->cur_tx - dirty_tx;

	while (tx_left > 0) {
		unsigned int entry = dirty_tx % NUM_TX_DESC;
		RingInfo *tx_skb = pvt->tx_skb + entry;
		uint32_t len = tx_skb->len;
		uint32_t status;

		smp_rmb();
		status = le32_to_cpu(pvt->TxDescArray[entry].opts1);
		if (status & DescOwn)
			break;

		dev->stats.tx_bytes += len;
		dev->stats.tx_packets++;

		rtl8101_unmap_tx_skb(pvt->pci_dev, tx_skb, pvt->TxDescArray + entry);

		if (status & LastFrag) {
			dev_kfree_skb_irq(tx_skb->skb);
			tx_skb->skb = NULL;
		}
		dirty_tx++;
		tx_left--;
	}

	if (pvt->dirty_tx != dirty_tx) {
		pvt->dirty_tx = dirty_tx;
		smp_wmb();
		if (netif_queue_stopped(dev) &&
			(TX_BUFFS_AVAIL(pvt) >= MAX_SKB_FRAGS)) {
			netif_wake_queue(dev);
		}
		smp_wmb();
		if (pvt->cur_tx != dirty_tx)
			rtl8101_notify_for_tx(reg_base);
	}
}

static int rtl8101_poll(struct napi_struct *napi_ptr, int budget)
{
	DrvPvt *pvt = container_of(napi_ptr, DrvPvt, napi);
	void __iomem *reg_base = pvt->reg_base;
	struct net_device *dev = pvt->dev;
	unsigned int work_done;
	unsigned long flags;

	work_done = rtl8101_rx_interrupt(dev, pvt, reg_base, (uint32_t) budget);
	spin_lock_irqsave(&pvt->lock, flags);
	rtl8101_tx_interrupt(dev, pvt, reg_base);
	spin_unlock_irqrestore(&pvt->lock, flags);

	if (work_done < budget) {
		napi_complete(napi_ptr);
		/*
		 * The barrier is not strictly required but the behavior of the irq
		 * handler could be less predictable without it
		 */
		smp_wmb();
		rtl8101_enable_interrupts(reg_base, IntrEnMask);
	}

	return work_done;
}

/* The interrupt handler does all of the Rx thread work and cleans up after the Tx thread. */
static irqreturn_t rtl8101_interrupt(int irq, void *dev_instance)
{
	struct net_device *dev = (struct net_device *)dev_instance;
	DrvPvt *pvt = netdev_priv(dev);
	void __iomem *reg_base = pvt->reg_base;
	int status;

	status = rtl8101_get_intr_status(reg_base);

	/* hotplug/major error/no more work/shared irq */
	if ((status == 0xFFFF) || !status)
		return IRQ_NONE;

	if (!(status & IntrEnMask))
		return IRQ_NONE;

	if (unlikely(!netif_running(dev)))
		return IRQ_NONE;

	rtl8101_disable_n_clear_interrupts(reg_base);

	napi_schedule(&pvt->napi);

	return IRQ_HANDLED;
}

/*** Interrupt Handlers End ***/

/*** Network Stack Layer Operations Start ***/

static void rtl8101_reset_task(struct work_struct *work)
{
	DrvPvt *pvt =
		container_of(work, DrvPvt, task.work);
	struct net_device *dev = pvt->dev;
	void __iomem *reg_base = pvt->reg_base;
	unsigned long flags;

	if (!netif_running(dev))
		return;

	synchronize_irq(dev->irq);
	/* Wait for any pending NAPI task to complete */
	napi_disable(&pvt->napi);
	rtl8101_disable_n_clear_interrupts(reg_base);
	napi_enable(&pvt->napi);
	rtl8101_rx_interrupt(dev, pvt, pvt->reg_base, ~(uint32_t)0);
	spin_lock_irqsave(&pvt->lock, flags);
	rtl8101_tx_clear(pvt);
	if (pvt->dirty_rx == pvt->cur_rx) {
		rtl8101_init_ring_indexes(pvt);
		rtl8101_hw_init(dev);
		netif_wake_queue(dev);
		spin_unlock_irqrestore(&pvt->lock, flags);
	} else {
		spin_unlock_irqrestore(&pvt->lock, flags);
		if (net_ratelimit()) {
			egprintk("%s: Rx buffers shortage\n", dev->name);
		}
		schedule_delayed_work(&pvt->task, 4);
	}
}

static int rtl8101_open(struct net_device *dev)
{
	DrvPvt *pvt = netdev_priv(dev);
	void __iomem *reg_base = pvt->reg_base;
	struct pci_dev *pdev = pvt->pci_dev;
	int retval;

	pvt->rx_buf_sz = RX_BUF_SIZE;

	retval = rtl8101_init_buffers(pvt);
	if (retval < 0)
		return retval;

	INIT_DELAYED_WORK(&pvt->task, rtl8101_reset_task);

	napi_enable(&pvt->napi);
	rtl8101_hw_reset(reg_base);
	phy_power_up(reg_base);
	rtl8101_hw_init(dev);

	retval = request_irq(dev->irq, rtl8101_interrupt,
				(pci_dev_msi_enabled(pdev)) ? 0 : IRQF_SHARED, dev->name, dev);

	if (retval < 0)
	{
		rtl8101_shut_buffers(pvt);
		return retval;
	}

	rtl8101_request_link_timer(dev);

	return 0;
}

static int rtl8101_close(struct net_device *dev)
{
	DrvPvt *pvt = netdev_priv(dev);
	unsigned long flags;
	void __iomem *reg_base = pvt->reg_base;
	struct pci_dev *pdev = pvt->pci_dev;

	if (pvt->TxDescArray!=NULL && pvt->RxDescArray!=NULL) {
		rtl8101_delete_link_timer(dev);
		napi_disable(&pvt->napi);
		netif_stop_queue(dev);
		/* Give a racing hard_start_xmit a few cycles to complete. */
		synchronize_sched();  /* FIXME: should this be synchronize_irq()? */
		netif_carrier_off(dev);
		spin_lock_irqsave(&pvt->lock, flags);
		rtl8101_hw_reset(reg_base);
		spin_unlock_irqrestore(&pvt->lock, flags);
		synchronize_irq(dev->irq);
		spin_lock_irqsave(&pvt->lock, flags);
		rtl8101_tx_clear(pvt);
		spin_unlock_irqrestore(&pvt->lock, flags);
		rtl8101_rx_clear(pvt);
		phy_power_down(reg_base);
		free_irq(dev->irq, dev);
		pci_free_consistent(pdev, R8101_RX_RING_BYTES, pvt->RxDescArray, pvt->RxPhyAddr);
		pci_free_consistent(pdev, R8101_TX_RING_BYTES, pvt->TxDescArray, pvt->TxPhyAddr);
		pvt->TxDescArray = NULL;
		pvt->RxDescArray = NULL;
	}

	return 0;
}

static int rtl8101_start_xmit(struct sk_buff *skb,
					struct net_device *dev)
{
	DrvPvt *pvt = netdev_priv(dev);
	unsigned int entry = pvt->cur_tx % NUM_TX_DESC;
	struct Desc *txd = pvt->TxDescArray + entry;
	void __iomem *reg_base = pvt->reg_base;
	dma_addr_t mapping;
	uint32_t len;
	uint32_t opts1;
	uint32_t opts2;
	int ret = NETDEV_TX_OK;
	unsigned long flags;

	spin_lock_irqsave(&pvt->lock, flags);

	if (unlikely(TX_BUFFS_AVAIL(pvt) < skb_shinfo(skb)->nr_frags)) {
		eprintk("%s: BUG! Tx Ring full when queue awake!\n", dev->name);
		goto err_stop;
	}

	if (unlikely(le32_to_cpu(txd->opts1) & DescOwn))
		goto err_stop;

	opts1 = DescOwn;
	opts2 = 0;

	len = skb->len;
	opts1 |= FirstFrag | LastFrag;
	pvt->tx_skb[entry].skb = skb;

	opts1 |= len | (RingEnd * !((entry + 1) % NUM_TX_DESC));
	mapping = pci_map_single(pvt->pci_dev, skb->data, len, PCI_DMA_TODEVICE);
	pvt->tx_skb[entry].len = len;
	txd->addr = cpu_to_le64(mapping);
	txd->opts2 = cpu_to_le32(opts2);
	txd->opts1 = cpu_to_le32(opts1 & ~DescOwn);
	smp_wmb();
	txd->opts1 = cpu_to_le32(opts1);

	netdev_get_tx_queue(dev, 0)->trans_start = jiffies;

	pvt->cur_tx++;

	smp_wmb();

	rtl8101_notify_for_tx(reg_base);

	if (TX_BUFFS_AVAIL(pvt) < MAX_SKB_FRAGS) {
		netif_stop_queue(dev);
		smp_rmb();
		if (TX_BUFFS_AVAIL(pvt) >= MAX_SKB_FRAGS)
			netif_wake_queue(dev);
	}

	spin_unlock_irqrestore(&pvt->lock, flags);

out:
	return ret;

err_stop:
	netif_stop_queue(dev);
	ret = NETDEV_TX_BUSY;
	dev->stats.tx_dropped++;

	spin_unlock_irqrestore(&pvt->lock, flags);
	goto out;
}

static void rtl8101_tx_timeout(struct net_device *dev)
{
	DrvPvt *pvt = netdev_priv(dev);
	void __iomem *reg_base = pvt->reg_base;
	unsigned long flags;

	iprintk("Transmit timed out\n");

	spin_lock_irqsave(&pvt->lock, flags);
	rtl8101_hw_reset(reg_base);
	spin_unlock_irqrestore(&pvt->lock, flags);

	/* Let's wait a bit while any (async) irq lands on */
	schedule_delayed_work(&pvt->task, 4);
}

static struct net_device_stats *rtl8101_get_stats(struct net_device *dev)
{
	return &dev->stats;
}

/*** Network Stack Layer Operations End ***/

static const struct net_device_ops rtl8101_netdev_ops = {
	.ndo_open				= rtl8101_open,
	.ndo_stop				= rtl8101_close,
	.ndo_start_xmit			= rtl8101_start_xmit,
	.ndo_tx_timeout			= rtl8101_tx_timeout,
	.ndo_get_stats			= rtl8101_get_stats,
};

int net_register_dev(struct pci_dev *pdev)
{
	void __iomem *reg_base = pci_get_drvdata(pdev);
	struct net_device *dev = NULL;
	DrvPvt *pvt;
	int ret;

	iprintk("Network driver pn loaded\n");

	/* dev zeroed in alloc_etherdev */
	dev = alloc_etherdev(sizeof (*pvt));
	if (dev == NULL) {
		eprintk("unable to alloc new ethernet\n");
		return -ENOMEM;
	}
	SET_NETDEV_DEV(dev, &pdev->dev);
	pvt = netdev_priv(dev);
	pvt->dev = dev;
	pvt->pci_dev = pdev;
	pvt->reg_base = reg_base;

	dev->netdev_ops = &rtl8101_netdev_ops;

	dev->watchdog_timeo = RTL8101_TX_TIMEOUT;
	dev->base_addr = (unsigned long) reg_base;
	dev->irq = pdev->irq;

	netif_napi_add(dev, &pvt->napi, rtl8101_poll, R8101_NAPI_WEIGHT);

	spin_lock_init(&pvt->lock);

	rtl8101_hw_reset(reg_base);
	rtl8101_get_mac_address(dev);

	iprintk("%s: %s, %2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x",
				dev->name,
				RTL_CHIP_NAME,
				dev->dev_addr[0], dev->dev_addr[1],
				dev->dev_addr[2], dev->dev_addr[3],
				dev->dev_addr[4], dev->dev_addr[5]);

	if ((ret = register_netdev(dev))) {
		free_netdev(dev);
		return ret;
	}
	netif_carrier_off(dev);
	pci_set_drvdata(pdev, dev); // Replacing reg_base by our pointer. Shall restore on deregister

	return 0;
}

void net_deregister_dev(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	DrvPvt *pvt = netdev_priv(dev);

	pci_set_drvdata(pdev, pvt->reg_base); // Restore the reg_base
	flush_scheduled_work();
	unregister_netdev(dev);
	free_netdev(dev);
}
