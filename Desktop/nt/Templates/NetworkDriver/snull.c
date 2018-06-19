/*
 * snull.c -- the Simple Network Utility
 *
 * Copyright (C) 2001 Alessandro Rubini and Jonathan Corbet
 * Copyright (C) 2001 O'Reilly & Associates
 *
 * The source code in this file can be freely used, adapted,
 * and redistributed in source or binary form, so long as an
 * acknowledgment appears in derived source files. The citation
 * should list that the code comes from the book "Linux Device
 * Drivers" by Alessandro Rubini and Jonathan Corbet, published
 * by O'Reilly & Associates. No warranty is attached;
 * we cannot take responsibility for errors or fitness for use.
 *
 * $Id: snull.c,v 1.2 2010/05/19 21:08:39 baker Exp baker $
 */

/* I have made an attempt to bring this code up to kernel 2.6.25,
 * but am not certain it works, and am less convinced it is still
 * a good example of how to write a network device driver.
 * It seems that circa kernel 2.6.24 the NAPI interfaces were
 * changed. Responsibility for managing the quote was move
 * from individual drivers' poll functions to shared code.
 * There also seems to be some new NAPI registration stuff.
 * See http://lwn.net/Articles/244640/ for more info.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/version.h>

#include <linux/sched.h>
#include <linux/kernel.h> /* printk() */
#include <linux/slab.h> /* kmalloc() */
#include <linux/errno.h> /* error codes */
#include <linux/types.h> /* size_t */
#include <linux/interrupt.h> /* mark_bh */
#include <linux/spinlock.h> /* spinlock_t */

#include <linux/in.h>
#include <linux/netdevice.h> /* struct device, and other headers */
#include <linux/etherdevice.h> /* eth_type_trans */
#include <linux/ip.h> /* struct iphdr */
#include <linux/tcp.h> /* struct tcphdr */
#include <linux/skbuff.h>

#include "snull.h"

#include <linux/in6.h>
#include <asm/checksum.h>

MODULE_AUTHOR("Alessandro Rubini, Jonathan Corbet");
MODULE_LICENSE("Dual BSD/GPL");

static int timeout = SNULL_TIMEOUT;

static int lockup = 0;
module_param(lockup, int, 0);

static int drop = 0;
module_param(drop, int, 0);

/*
 * Do we run in NAPI mode?
 */
static int use_napi = 0;
module_param(use_napi, int, 0);

/*
 * A structure representing an in-flight packet.
 */
struct snull_packet {
	struct snull_packet *next;
	struct net_device *dev;
	int datalen;
	u8 data[ETH_DATA_LEN];
};

static int pool_size = 8;
module_param(pool_size, int, 0);

/*
 * This structure is private to each device. It is used to pass
 * packets in and out, so there is place for a packet
 */

struct snull_priv {
	struct net_device_stats stats;
	int status;
	struct snull_packet *ppool;
	struct snull_packet *rx_queue; /* List of incoming packets */
	int rx_int_enabled;
	int tx_packetlen;
	u8 *tx_packetdata;
	struct sk_buff *skb;
	spinlock_t lock;
	struct net_device *dev;
	struct napi_struct napi;
	/* Consider creating new struct for snull device, and putting
	 * the struct net_dev in here.
	 */
};

/*
 * The devices
 */
static struct net_device *snull_devs[2];

/*
 * Set up a device's packet pool.
 */
static void snull_setup_pool(struct net_device *dev)
{
	struct snull_priv *priv = netdev_priv(dev);
	int i;
	struct snull_packet *pkt;

	priv->ppool = NULL;
	for (i = 0; i < pool_size; i++) {
		pkt = kmalloc (sizeof (struct snull_packet), GFP_KERNEL);
		if (pkt == NULL) {
			printk (KERN_NOTICE "Ran out of memory allocating packet pool\n");
			return;
		}
		pkt->dev = dev;
		pkt->next = priv->ppool;
		priv->ppool = pkt;
	}
}

static void snull_teardown_pool(struct net_device *dev)
{
	struct snull_priv *priv = netdev_priv(dev);
	struct snull_packet *pkt;

	while ((pkt = priv->ppool)) {
		priv->ppool = pkt->next;
		kfree (pkt);
		/* FIXME - in-flight packets ? */
	}
}

/*
 * Buffer/pool management.
 */
static struct snull_packet *snull_get_tx_buffer(struct net_device *dev)
{
	struct snull_priv *priv = netdev_priv(dev);
	unsigned long flags;
	struct snull_packet *pkt;

	spin_lock_irqsave(&priv->lock, flags);
	pkt = priv->ppool;
	priv->ppool = pkt->next;
	if (priv->ppool == NULL) {
		printk (KERN_INFO "Pool empty\n");
		netif_stop_queue(dev);
	}
	spin_unlock_irqrestore(&priv->lock, flags);
	return pkt;
}

static void snull_release_buffer(struct snull_packet *pkt)
{
	unsigned long flags;
	struct snull_priv *priv = netdev_priv(pkt->dev);
	
	spin_lock_irqsave(&priv->lock, flags);
	pkt->next = priv->ppool;
	priv->ppool = pkt;
	spin_unlock_irqrestore(&priv->lock, flags);
	if (netif_queue_stopped(pkt->dev) && pkt->next == NULL)
		netif_wake_queue(pkt->dev);
}

static void snull_enqueue_buf(struct net_device *dev, struct snull_packet *pkt)
{
	unsigned long flags;
	struct snull_priv *priv = netdev_priv(dev);

	spin_lock_irqsave(&priv->lock, flags);
	pkt->next = priv->rx_queue; /* FIXME - misorders packets */
	priv->rx_queue = pkt;
	spin_unlock_irqrestore(&priv->lock, flags);
}

static struct snull_packet *snull_dequeue_buf(struct net_device *dev)
{
	struct snull_priv *priv = netdev_priv(dev);
	struct snull_packet *pkt;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	pkt = priv->rx_queue;
	if (pkt != NULL)
		priv->rx_queue = pkt->next;
	spin_unlock_irqrestore(&priv->lock, flags);
	return pkt;
}

/*
 * Enable and disable receive interrupts.
 */
static void snull_rx_ints(struct net_device *dev, int enable)
{
	struct snull_priv *priv = netdev_priv(dev);
	priv->rx_int_enabled = enable;
}

/*
 * Open and close
 */

static int snull_open(struct net_device *dev)
{
	/* request_region(), request_irq(), .... (like fops->open) */

	/*
	 * Assign the hardware address of the board: use "\0SNULx", where
	 * x is 0 or 1. The first byte is '\0' to avoid being a multicast
	 * address (the first byte of multicast addrs is odd).
	 */
	memcpy(dev->dev_addr, "\0SNUL0", ETH_ALEN);
	if (dev == snull_devs[1])
		dev->dev_addr[ETH_ALEN-1]++; /* \0SNUL1 */
	netif_start_queue(dev);
	return 0;
}

static int snull_release(struct net_device *dev)
{
	/* release ports, irq and such -- like fops->close */

	netif_stop_queue(dev); /* can't transmit any more */
	return 0;
}

/*
 * Receive a packet: retrieve, encapsulate and pass over to upper levels
 */
static void snull_rx(struct net_device *dev, struct snull_packet *pkt)
{
	static int i = 0;
	struct sk_buff *skb;
	struct snull_priv *priv = netdev_priv(dev);

	if (drop && (++i % drop == 0)) // Simulate packet dropping
	{
		i = 0;
		if (printk_ratelimit())
			printk(KERN_NOTICE "snull rx: too fast packet arrival - packet dropped\n");
		priv->stats.rx_dropped++;
		return;
	}
	/*
	 * The packet has been retrieved from the transmission
	 * medium. Build an skb around it, so upper layers can handle it
	 */
	skb = dev_alloc_skb(pkt->datalen + 2);
	if (!skb) {
		if (printk_ratelimit())
			printk(KERN_NOTICE "snull rx: low on mem - packet dropped\n");
		priv->stats.rx_dropped++;
		return;
	}
	skb_reserve(skb, 2); /* align IP on 16B boundary */
	memcpy(skb_put(skb, pkt->datalen), pkt->data, pkt->datalen);

	/* Write metadata, and then pass to the receive level */
	skb->dev = dev;
	skb->protocol = eth_type_trans(skb, dev);
	skb->ip_summed = CHECKSUM_UNNECESSARY; /* don't check it */
	priv->stats.rx_packets++;
	priv->stats.rx_bytes += pkt->datalen;
	netif_rx(skb);
}

/*
 * The poll implementation.
 */
static int snull_poll(struct napi_struct *napi, int budget)
{
	static int i = 0;
	int npackets = 0;
	struct sk_buff *skb;
	struct snull_priv *priv = container_of(napi, struct snull_priv, napi);
	struct net_device *dev = priv->dev;
	struct snull_packet *pkt;

	while (npackets < budget && priv->rx_queue) {
		pkt = snull_dequeue_buf(dev);
		if (drop && (++i % drop == 0)) // Simulate packet dropping
		{
			i = 0;
			if (printk_ratelimit())
				printk(KERN_NOTICE "snull: too fast packet arrival - packet dropped\n");
			priv->stats.rx_dropped++;
			snull_release_buffer(pkt);
			continue;
		}
		skb = dev_alloc_skb(pkt->datalen + 2);
		if (!skb) {
			if (printk_ratelimit())
				printk(KERN_NOTICE "snull: low on mem - packet dropped\n");
			priv->stats.rx_dropped++;
			snull_release_buffer(pkt);
			continue;
		}
		skb_reserve(skb, 2); /* align IP on 16B boundary */
		memcpy(skb_put(skb, pkt->datalen), pkt->data, pkt->datalen);
		skb->dev = dev;
		skb->protocol = eth_type_trans(skb, dev);
		skb->ip_summed = CHECKSUM_UNNECESSARY; /* don't check it */
		netif_receive_skb(skb);
		/* Maintain stats */
		npackets++;
		priv->stats.rx_packets++;
		priv->stats.rx_bytes += pkt->datalen;
		snull_release_buffer(pkt);
	}
	/* If we processed all packets, we're done; tell the kernel and re-enable ints */
	if (npackets < budget) {
		napi_complete(napi);
		snull_rx_ints(dev, 1);
	}
	return npackets;
}
	
/*
 * The typical interrupt entry point
 */
static void snull_interrupt(int irq, void *dev_id)
{
	int statusword;
	struct snull_priv *priv;
	struct snull_packet *pkt = NULL;
	struct net_device *dev = (struct net_device *)dev_id;

	/* paranoid */
	if (!dev)
		return;

	/* Lock the device */
	priv = netdev_priv(dev);
	spin_lock(&priv->lock);

	/* retrieve statusword: real netdevices use I/O instructions */
	statusword = priv->status;
	priv->status = 0;
	if (statusword & SNULL_RX_INTR) {
		if (use_napi)
		{
			snull_rx_ints(dev, 0); /* Disable further interrupts */
			napi_schedule(&priv->napi);
		}
		else
		{
			/* send it to snull_rx for handling */
			pkt = priv->rx_queue;
			if (pkt) {
				priv->rx_queue = pkt->next;
				snull_rx(dev, pkt);
			}
		}
	}
	if (statusword & SNULL_TX_INTR) {
		/* a transmission is over: free the skb */
		priv->stats.tx_packets++;
		priv->stats.tx_bytes += priv->tx_packetlen;
		dev_kfree_skb(priv->skb);
	}

	/* Unlock the device and we are done */
	spin_unlock(&priv->lock);
	if (pkt) snull_release_buffer(pkt); /* Do this outside the lock! */
	return;
}

/*
 * Transmit a packet (low level interface)
 */
static void snull_hw_tx(char *buf, int len, struct net_device *dev)
{
	/*
	 * This function deals with hw details. This interface loops
	 * back the packet to the other snull interface (if any).
	 * In other words, this function implements the snull behaviour,
	 * while all other procedures are rather device-independent
	 */
	struct iphdr *ih;
	struct net_device *dest;
	struct snull_priv *priv;
	u32 *saddr, *daddr;
	struct snull_packet *tx_buffer;

	/* I am paranoid. Ain't I? */
	if (len < sizeof(struct ethhdr) + sizeof(struct iphdr)) {
		printk(KERN_WARNING "snull: Hmm... packet too short (%i octets)\n",
				len);
		return;
	}

	if (0) { /* enable this conditional to look at the data */
		int i;
		PDEBUG("len is %i\n" KERN_DEBUG "data:", len);
		for (i=14 ; i<len; i++)
			printk(" %02x", buf[i]&0xff);
		printk("\n");
	}
	/*
	 * Ethhdr is 14 bytes, but the kernel arranges for iphdr
	 * to be aligned (i.e., ethhdr is unaligned)
	 */
	ih = (struct iphdr *)(buf+sizeof(struct ethhdr));
	saddr = &ih->saddr;
	daddr = &ih->daddr;

	((u8 *)saddr)[2] ^= 1; /* change the third octet (class C) */
	((u8 *)daddr)[2] ^= 1;

	ih->check = 0;	 /* and rebuild the checksum (ip needs it) */
	ih->check = ip_fast_csum((unsigned char *)ih,ih->ihl);

	if (dev == snull_devs[0])
		PDEBUGG("%08x:%05i --> %08x:%05i\n",
				ntohl(ih->saddr),ntohs(((struct tcphdr *)(ih+1))->source),
				ntohl(ih->daddr),ntohs(((struct tcphdr *)(ih+1))->dest));
	else
		PDEBUGG("%08x:%05i <-- %08x:%05i\n",
				ntohl(ih->daddr),ntohs(((struct tcphdr *)(ih+1))->dest),
				ntohl(ih->saddr),ntohs(((struct tcphdr *)(ih+1))->source));

	/*
	 * Ok, now the packet is ready for transmission: first simulate a
	 * receive interrupt on the twin device, then a
	 * transmission-done on the transmitting device
	 */
	dest = snull_devs[dev == snull_devs[0] ? 1 : 0];
	priv = netdev_priv(dest);
	tx_buffer = snull_get_tx_buffer(dev);
	tx_buffer->datalen = len;
	memcpy(tx_buffer->data, buf, len);
	snull_enqueue_buf(dest, tx_buffer);
	if (priv->rx_int_enabled) {
		priv->status |= SNULL_RX_INTR;
		snull_interrupt(0, dest);
	}

	priv = netdev_priv(dev);
	priv->tx_packetlen = len;
	priv->tx_packetdata = buf;
	priv->status |= SNULL_TX_INTR;
	snull_interrupt(0, dev);
}

/*
 * Deal with a transmit timeout.
 */
static void snull_tx_timeout (struct net_device *dev)
{
	struct snull_priv *priv = netdev_priv(dev);

	PDEBUG("Transmit timeout at %ld, latency %ld\n", jiffies,
			jiffies - netdev_get_tx_queue(dev, 0)->trans_start);
	/* Simulate a transmission interrupt to get things moving */
	priv->status = SNULL_TX_INTR;
	snull_interrupt(0, dev);
	priv->stats.tx_errors++;
	netif_wake_queue(dev);
	return;
}

/*
 * Transmit a packet (called by the kernel)
 */
static int snull_tx(struct sk_buff *skb, struct net_device *dev)
{
	static int i = 0;
	struct snull_priv *priv = netdev_priv(dev);
	
	netdev_get_tx_queue(dev, 0)->trans_start = jiffies; /* save the timestamp */

	/* Remember the skb, so we can free it at interrupt time */
	priv->skb = skb;

	if (lockup && (++i % lockup == 0)) // Simulate lockup
	{
		i = 0;
		snull_tx_timeout(dev);
	}
	else
	{
		/* actual deliver of data is device-specific, and not shown here */
		snull_hw_tx(skb->data, skb->len, dev);
	}

	return 0; /* Our simple device can not fail */
}

/*
 * Return statistics to the caller
 */
static struct net_device_stats *snull_stats(struct net_device *dev)
{
	struct snull_priv *priv = netdev_priv(dev);
	return &priv->stats;
}

static int snull_header(struct sk_buff *skb, struct net_device *dev,
		unsigned short type, const void *daddr, const void *saddr,
		unsigned int len)
{
	struct ethhdr *eth = (struct ethhdr *)skb_push(skb,ETH_HLEN);

	eth->h_proto = htons(type);
	memcpy(eth->h_source, saddr ? saddr : dev->dev_addr, dev->addr_len);
	memcpy(eth->h_dest, daddr ? daddr : dev->dev_addr, dev->addr_len);
	eth->h_dest[ETH_ALEN-1] ^= 0x01; /* dest is us xor 1 */
	return (dev->hard_header_len);
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4,0,0))
/*
 * This function is called to fill up an eth header, since arp is not
 * available on the interface
 */
static int snull_rebuild_header(struct sk_buff *skb)
{
	struct ethhdr *eth = (struct ethhdr *) skb->data;
	struct net_device *dev = skb->dev;

	memcpy(eth->h_source, dev->dev_addr, dev->addr_len);
	memcpy(eth->h_dest, dev->dev_addr, dev->addr_len);
	eth->h_dest[ETH_ALEN-1] ^= 0x01; /* dest is us xor 1 */
	return 0;
}
#endif

static const struct header_ops snull_header_ops = {
	.create = snull_header,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4,0,0))
	.rebuild = snull_rebuild_header,
#endif
	.cache = NULL, /* disable caching */
};

static const struct net_device_ops snull_netdev_ops = {
	.ndo_open = snull_open,
	.ndo_stop = snull_release,
	.ndo_start_xmit = snull_tx,
	.ndo_get_stats = snull_stats,
	.ndo_tx_timeout = snull_tx_timeout,
};

/*
 * The init function (sometimes called probe).
 * It is invoked by register_netdev()
 */
static void snull_probe(struct net_device *dev)
{
	struct snull_priv *priv;
#if 0
	/*
	 * Make the usual checks: check_region(), probe irq, ... -ENODEV
	 * should be returned if no device found. No resource should be
	 * grabbed: this is done on open().
	 */
#endif

	/*
	 * Then, assign other fields in dev, using ether_setup() and some
	 * hand assignments
	 */
	ether_setup(dev); /* assign some of the fields */

	dev->netdev_ops = &snull_netdev_ops;
	dev->header_ops = &snull_header_ops;
	dev->watchdog_timeo = timeout;
	/* keep the default flags, just add NOARP */
	dev->flags |= IFF_NOARP;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,3,0))
	dev->features |= NETIF_F_NO_CSUM;
#else
	dev->features |= NETIF_F_UFO | NETIF_F_HW_CSUM | NETIF_F_RXCSUM;
#endif

	/*
	 * Then, initialize the priv field. This encloses the statistics
	 * and a few private fields.
	 */
	priv = netdev_priv(dev);
	memset(priv, 0, sizeof(struct snull_priv));
	priv->dev = dev;
	netif_napi_add(dev, &priv->napi, snull_poll, 2);
	/* The last parameter above is the NAPI "weight". */
	spin_lock_init(&priv->lock);
	snull_rx_ints(dev, 1); /* enable receive interrupts */
	snull_setup_pool(dev);
}

/*
 * Finally, the module stuff
 */
static void snull_exit(void)
{
	int i;

	for (i = 0; i < 2; i++) {
		if (snull_devs[i]) {
			unregister_netdev(snull_devs[i]);
			snull_teardown_pool(snull_devs[i]);
			free_netdev(snull_devs[i]);
		}
	}
	return;
}

static int snull_init(void)
{
	int result, i, ret = -ENOMEM;

	/* Allocate the devices */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,17,0))
	snull_devs[0] = alloc_netdev(sizeof(struct snull_priv), "sn%d", snull_probe);
	snull_devs[1] = alloc_netdev(sizeof(struct snull_priv), "sn%d", snull_probe);
#else
	snull_devs[0] = alloc_netdev(sizeof(struct snull_priv), "sn%d", NET_NAME_UNKNOWN, snull_probe);
	snull_devs[1] = alloc_netdev(sizeof(struct snull_priv), "sn%d", NET_NAME_UNKNOWN, snull_probe);
#endif
	if (snull_devs[0] == NULL || snull_devs[1] == NULL)
		goto out;

	ret = -ENODEV;
	for (i = 0; i < 2; i++)
		if ((result = register_netdev(snull_devs[i])))
			printk(KERN_WARNING "snull: error %i registering device \"%s\"\n",
					result, snull_devs[i]->name);
		else
			ret = 0;
out:
	if (ret)
		snull_exit();
	return ret;
}

module_init(snull_init);
module_exit(snull_exit);
