#ifndef GLOBAL_CLASSES_H
#define GLOBAL_CLASSES_H

#ifdef __KERNEL__
#include <linux/device.h>

extern struct class *cool_cl;
extern struct class *char_cl;
extern struct class *video_cl;
extern struct class *usb_cl;
#endif

#endif
