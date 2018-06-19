#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/mii.h>

#include "net_stk.h"
#include "r8101.h"

enum PhyBits {
	/* PHY access */
	PHYAR_Flag = 0x80000000,
	PHYAR_Write = 0x80000000,
	PHYAR_Read = 0x00000000,
	PHYAR_Reg_Mask = 0x1f,
	PHYAR_Reg_Shift = 16,
	PHYAR_Data_Mask = 0xffff,

	/* PHY status */
	TxFlowCtrl = 0x40,
	RxFlowCtrl = 0x20,
	_100bps = 0x08,
	_10bps = 0x04,
	LinkStatus = 0x02,
	FullDup = 0x01
};

static void phy_reg_write(void *__iomem reg_base, uint32_t reg_addr, uint32_t value)
{
	int i;

	REG_W32(PHYAR_Write |
			((reg_addr & PHYAR_Reg_Mask) << PHYAR_Reg_Shift) |
			(value & PHYAR_Data_Mask), PHYAR);

	for (i = 0; i < 10; i++)
	{
		udelay(100);
		// Check if the RTL8101 has completed writing to the specified MII register
		if (!(REG_R32(PHYAR) & PHYAR_Flag))
			break;
	}
}

/*
static uint32_t phy_reg_read(void *__iomem reg_base, uint32_t reg_addr)
{
	int i;
	uint32_t value = 0;

	REG_W32(PHYAR_Read |
			((reg_addr & PHYAR_Reg_Mask) << PHYAR_Reg_Shift), PHYAR);

	for (i = 0; i < 10; i++)
	{
		udelay(100);
		// Check if the RTL8101 has completed retrieving data from the specified MII register
		if (REG_R32(PHYAR) & PHYAR_Flag)
		{
			value = REG_R32(PHYAR) & PHYAR_Data_Mask;
			break;
		}
	}

	return value;
}
*/

void phy_power_up(void __iomem *reg_base)
{
	phy_reg_write(reg_base, MII_BMCR, BMCR_ANENABLE);
}

void phy_power_down(void __iomem *reg_base)
{
	phy_reg_write(reg_base, MII_BMCR, BMCR_PDOWN);
}

bool phy_link_status_on(void __iomem *reg_base)
{
	return (REG_R8(PHYstatus) & LinkStatus) ? 1 : 0;
}
