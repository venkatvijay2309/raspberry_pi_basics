#ifndef DDK_IO_H
#define DDK_IO_H

#include <linux/ioctl.h>

#define DDK_REG_GET _IOR('u', 1, Register *)
#define DDK_REG_SET _IOW('u', 2, Register *)

typedef enum
{
	rsvd,
	dira,
	dirb,
	dirc,
	dird,
	porta,
	portb,
	portc,
	portd,
	total_reg_id
} RegId;
typedef struct
{
	RegId id;
	unsigned char val;
} Register;

#endif
