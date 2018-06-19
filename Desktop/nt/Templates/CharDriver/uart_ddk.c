#include <linux/module.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <asm/io.h>

#define SERIAL_PORT_BASE 0x3F8

#define LCR 3
#define MCR 4

#define SBC_MASK (1 << 6)
#define DTR_MASK (1 << 0)

#define CLOCK DTR_MASK

//#define HACK_BYPASS // Uncomment, to hack the driver to load

static int __init uart_ddk_init(void) // Glow LED
{
#ifndef HACK_BYPASS
	struct resource *r;
#endif
	u8 data, clock;

#ifndef HACK_BYPASS
	if ((r = request_region((resource_size_t)SERIAL_PORT_BASE, (resource_size_t)8, "ddk")) == NULL)
	{
		return -EBUSY;
	}
#endif
	/* Pulling the Tx line low */
	data = inb(SERIAL_PORT_BASE + LCR);
	data |= SBC_MASK;
	outb(data, SERIAL_PORT_BASE + LCR);

	/* Sending the -ve edge (high->low) clock over DTR */
	clock = inb(SERIAL_PORT_BASE + MCR);
	clock |= CLOCK;
	outb(clock, SERIAL_PORT_BASE + MCR);
	msleep(10);
	clock = inb(SERIAL_PORT_BASE + MCR);
	clock &= ~CLOCK;
	outb(clock, SERIAL_PORT_BASE + MCR);
	msleep(10);

	return 0;
}

static void __exit uart_ddk_exit(void) // Off LED
{
	u8 data, clock;

	/* Defaulting the Tx line high */
	data = inb(SERIAL_PORT_BASE + LCR);
	data &= ~SBC_MASK;
	outb(data, SERIAL_PORT_BASE + LCR);

	/* Sending the -ve edge (high->low) clock over DTR */
	clock = inb(SERIAL_PORT_BASE + MCR);
	clock |= CLOCK;
	outb(clock, SERIAL_PORT_BASE + MCR);
	msleep(10);
	clock = inb(SERIAL_PORT_BASE + MCR);
	clock &= ~CLOCK;
	outb(clock, SERIAL_PORT_BASE + MCR);
	msleep(10);
#ifndef HACK_BYPASS
	release_region((resource_size_t)SERIAL_PORT_BASE, (resource_size_t)8);
#endif
}

module_init(uart_ddk_init);
module_exit(uart_ddk_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Anil Kumar Pugalia <email@sarika-pugs.com>");
MODULE_DESCRIPTION("UART Device Driver for DDK v1.1");
