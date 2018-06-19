#ifndef DDK_H
#define DDK_H

#ifdef __KERNEL__
#include <linux/usb.h>

#define DDK_VENDOR_ID 0x16c0
#define DDK_PRODUCT_ID 0x05dc

#define CUSTOM_RQ_SET_LED_STATUS    1
#define CUSTOM_RQ_GET_LED_STATUS    2
#define CUSTOM_RQ_SET_MEM_RD_OFFSET 3
#define CUSTOM_RQ_GET_MEM_RD_OFFSET 4
#define CUSTOM_RQ_SET_MEM_WR_OFFSET 5
#define CUSTOM_RQ_GET_MEM_WR_OFFSET 6
#define CUSTOM_RQ_GET_MEM_SIZE      7
#define CUSTOM_RQ_SET_MEM_TYPE      8
#define CUSTOM_RQ_GET_MEM_TYPE      9
#define CUSTOM_RQ_SET_REGISTER     10
#define CUSTOM_RQ_GET_REGISTER     11

#define MAX_ENDPOINTS 4

#define MEM_EP_IN (USB_DIR_IN | 0x01)
#define MEM_EP_OUT 0x01
#define SER_EP_IN (USB_DIR_IN | 0x02)
#define SER_EP_OUT 0x02

#define ACQUIRE_SINGLE_INTERFACE 1

#endif

#endif
