#ifndef MAC_H

#define MAC_H

#include <linux/io.h>
#include <linux/netdevice.h>

void rtl8101_enable_interrupts(void __iomem *reg_base, uint16_t intr_mask);
void rtl8101_disable_n_clear_interrupts(void __iomem *reg_base);
uint16_t rtl8101_get_intr_status(void __iomem *reg_base);
void rtl8101_hw_reset(void __iomem *reg_base);
void rtl8101_get_mac_address(struct net_device *dev);
void rtl8101_hw_init(struct net_device *dev);
void rtl8101_notify_for_tx(void __iomem *reg_base);

#endif
