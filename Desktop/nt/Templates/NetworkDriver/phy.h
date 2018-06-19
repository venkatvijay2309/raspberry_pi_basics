#ifndef PHY_H

#define PHY_H

#include <linux/io.h>

void phy_power_up(void __iomem *reg_base);
void phy_power_down(void __iomem *reg_base);
bool phy_link_status_on(void __iomem *reg_base);

#endif
