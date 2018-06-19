#include <linux/io.h>
#include <linux/netdevice.h>
#include <linux/interrupt.h>
#include <linux/delay.h>

#include "net_stk.h"
#include "r8101.h"

void rtl8101_enable_interrupts(void __iomem *reg_base, uint16_t intr_mask)
{
	REG_W16(intr_mask, IntrMask);
}

void rtl8101_disable_n_clear_interrupts(void __iomem *reg_base)
{
	REG_W16(0x0000, IntrMask);
	REG_W16(REG_R16(IntrStatus), IntrStatus);
}

uint16_t rtl8101_get_intr_status(void __iomem *reg_base)
{
	return REG_R16(IntrStatus);
}

void rtl8101_get_mac_address(struct net_device *dev)
{
	DrvPvt *pvt = netdev_priv(dev);
	void __iomem *reg_base = pvt->reg_base;
	int i;

	for (i = 0; i < dev->addr_len; i++)
	{
		dev->dev_addr[i] = REG_R8(MAC0 + i);
		dev->perm_addr[i] = dev->dev_addr[i];
	}
}

static void rtl8101_nic_reset(void __iomem *reg_base)
{
	int i;

	REG_W32(RxDMAUnlimited, RxConfig);

	mdelay(10);

	/* Soft reset the chip. */
	REG_W8(CmdReset, ChipCmd);

	/* Check that the chip has finished the reset. */
	for (i = 100; i > 0; i--)
	{
		udelay(100);
		if ((REG_R8(ChipCmd) & CmdReset) == 0)
			break;
	}
}

void rtl8101_hw_reset(void __iomem *reg_base)
{
	rtl8101_disable_n_clear_interrupts(reg_base);
	rtl8101_nic_reset(reg_base);
}

static void rtl8101_desc_addr_fill(DrvPvt *pvt)
{
	void __iomem *reg_base = pvt->reg_base;

	if (!pvt->TxPhyAddr || !pvt->RxPhyAddr)
		return;

	REG_W32(((uint64_t) pvt->TxPhyAddr & DMA_BIT_MASK(32)), TxDescStartAddrLow);
	REG_W32(((uint64_t) pvt->TxPhyAddr >> 32), TxDescStartAddrHigh);
	REG_W32(((uint64_t) pvt->RxPhyAddr & DMA_BIT_MASK(32)), RxDescAddrLow);
	REG_W32(((uint64_t) pvt->RxPhyAddr >> 32), RxDescAddrHigh);
}

static void rtl8101_set_rx_mode(void __iomem *reg_base)
{
	int rx_mode;

	rx_mode = REG_R32(RxConfig) & RxConfigMask;
	rx_mode |= RxFIFONone | RxDMAUnlimited;
	rx_mode |= AcceptBroadcast | AcceptMulticast | AcceptMyPhys;

	REG_W32(rx_mode, RxConfig);
	REG_W32(~0, MAR0 + 0);
	REG_W32(~0, MAR0 + 4);
}

void rtl8101_hw_init(struct net_device *dev)
{
	DrvPvt *pvt = netdev_priv(dev);
	void __iomem *reg_base = pvt->reg_base;

	netif_stop_queue(dev);

	rtl8101_hw_reset(reg_base);

	REG_W8(Cfg9346_Unlock, Cfg9346);

	rtl8101_desc_addr_fill(pvt);

	REG_W8(0x3F, MTPS); // Maximum threshold for starting tx, even if packet not complete

	/* Set DMA burst size and Interframe Gap Time */
	REG_W32(TxDMAUnlimited | TxIFG96, TxConfig);

	REG_W32(RxDMAUnlimited, RxConfig);

	REG_W16(pvt->rx_buf_sz, RxMaxSize);

	/* Set Rx packet filter */
	rtl8101_set_rx_mode(reg_base);

	REG_W8(Cfg9346_Lock, Cfg9346);

	REG_W8(CmdTxEnb | CmdRxEnb, ChipCmd);

	/* Enable all known interrupts by setting the interrupt mask. */
	rtl8101_enable_interrupts(reg_base, IntrEnMask);

	netif_start_queue(dev);

	udelay(10);
}

void rtl8101_notify_for_tx(void __iomem *reg_base)
{
	REG_W8(NPQ, TxPoll);
}
