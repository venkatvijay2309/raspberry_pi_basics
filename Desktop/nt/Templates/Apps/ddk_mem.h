#ifndef DDK_MEM_H
#define DDK_MEM_H

#include <linux/ioctl.h>

#define DDK_RD_OFF_GET _IOR('u', 3, int *)
#define DDK_RD_OFF_SET _IOW('u', 4, int)
#define DDK_WR_OFF_GET _IOR('u', 5, int *)
#define DDK_WR_OFF_SET _IOW('u', 6, int)
#define DDK_MEM_SIZE_GET _IOW('u', 7, int)

#endif
