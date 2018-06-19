#ifndef DDK_LED_H
#define DDK_LED_H

#include <linux/ioctl.h>

#define DDK_LED_GET _IOR('u', 1, int *)
#define DDK_LED_SET _IOW('u', 2, int)

#endif
