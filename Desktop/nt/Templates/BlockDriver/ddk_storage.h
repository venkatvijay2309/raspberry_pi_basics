#ifndef DDK_STORAGE_H
#define DDK_STORAGE_H

#ifdef __KERNEL__
#include <linux/usb.h>

#define DDK_VENDOR_ID 0x16c0
#define DDK_PRODUCT_ID 0x05dc

#define CUSTOM_RQ_SET_MEM_RD_OFFSET 3
#define CUSTOM_RQ_GET_MEM_RD_OFFSET 4
#define CUSTOM_RQ_SET_MEM_WR_OFFSET 5
#define CUSTOM_RQ_GET_MEM_WR_OFFSET 6
#define CUSTOM_RQ_GET_MEM_SIZE      7
#define CUSTOM_RQ_SET_MEM_TYPE      8
#define CUSTOM_RQ_GET_MEM_TYPE      9

#define MEM_EP_IN (USB_DIR_IN | 0x01)
#define MEM_EP_OUT 0x01

#define MAX_PKT_SIZE 8

#define DDK_SECTOR_SIZE 512

enum
{
	eeprom,
	flash,
	total_mem_type
};

extern int ddk_storage_init(void);
extern void ddk_storage_cleanup(void);
extern int ddk_storage_write(sector_t sector_off, u8 *buffer, unsigned int sectors);
extern int ddk_storage_read(sector_t sector_off, u8 *buffer, unsigned int sectors);
#endif

#endif
