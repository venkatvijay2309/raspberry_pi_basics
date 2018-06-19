#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#include <linux/serial_reg.h>

#define FIRST_MINOR 0
#define MINOR_CNT 1

#define SERIAL_PORT_BASE 0x3F8

static int my_open(struct inode *i, struct file *f)
{
	return 0;
}

static int my_close(struct inode *i, struct file *f)
{
	return 0;
}

static void led_on(void)
{
	u8 data, clock;

	/* Pulling the Tx line low */
	data = inb(SERIAL_PORT_BASE + UART_LCR);
	data |= UART_LCR_SBC;
	outb(data, SERIAL_PORT_BASE + UART_LCR);

	/* Sending the -ve edge (high->low) clock over DTR */
	clock = inb(SERIAL_PORT_BASE + UART_MCR);
	clock |= UART_MCR_DTR;
	outb(clock, SERIAL_PORT_BASE + UART_MCR);
	msleep(10);
	clock = inb(SERIAL_PORT_BASE + UART_MCR);
	clock &= ~UART_MCR_DTR;
	outb(clock, SERIAL_PORT_BASE + UART_MCR);
	msleep(10);
}

static void led_off(void)
{
	u8 data, clock;

	/* Defaulting the Tx line high */
	data = inb(SERIAL_PORT_BASE + UART_LCR);
	data &= ~UART_LCR_SBC;
	outb(data, SERIAL_PORT_BASE + UART_LCR);

	/* Sending the -ve edge (high->low) clock over DTR */
	clock = inb(SERIAL_PORT_BASE + UART_MCR);
	clock |= UART_MCR_DTR;
	outb(clock, SERIAL_PORT_BASE + UART_MCR);
	msleep(10);
	clock = inb(SERIAL_PORT_BASE + UART_MCR);
	clock &= ~UART_MCR_DTR;
	outb(clock, SERIAL_PORT_BASE + UART_MCR);
	msleep(10);
}

static ssize_t my_write(struct file *f, const char __user *buf, size_t cnt, loff_t *off)
{
	int i, j;
	unsigned char val;

	for (i = 0; i < cnt; i++)
	{
		if (copy_from_user(&val, buf + i, 1))
		{
			return -EFAULT;
		}	
		for (j = 0; j < 8; j++)
		{
			if (val & (0x80 >> j))
			{
				led_on();
			}
			else
			{
				led_off();
			}
		}
		msleep(1000);
	}

	return cnt;
}

static dev_t dev;
static struct file_operations my_fops =
{
	.owner = THIS_MODULE,
	.open = my_open,
	.release = my_close,
	.write = my_write
};
static struct cdev c_dev;
static struct class *cl;

static int __init led_board_init(void)
{
	int ret;
	struct device *dev_ret;

	if ((ret = alloc_chrdev_region(&dev, FIRST_MINOR, MINOR_CNT, "led_board")) < 0)
	{
		return ret;
	}

	cdev_init(&c_dev, &my_fops);
	if ((ret = cdev_add(&c_dev, dev, MINOR_CNT)) < 0)
	{
		unregister_chrdev_region(dev, MINOR_CNT);
		return ret;
	}

	if (IS_ERR(cl = class_create(THIS_MODULE, "led")))
	{
		cdev_del(&c_dev);
		unregister_chrdev_region(dev, MINOR_CNT);
		return PTR_ERR(cl);
	}

	if (IS_ERR(dev_ret = device_create(cl, NULL, dev, NULL, "led%d", FIRST_MINOR)))
	{
		class_destroy(cl);
		cdev_del(&c_dev);
		unregister_chrdev_region(dev, MINOR_CNT);
		return PTR_ERR(dev_ret);
	}

	return 0;
}

static void __exit led_board_exit(void)
{
	device_destroy(cl, dev);
	class_destroy(cl);
	cdev_del(&c_dev);
	unregister_chrdev_region(dev, MINOR_CNT);
}

module_init(led_board_init);
module_exit(led_board_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Anil Kumar Pugalia <email@sarika-pugs.com>");
MODULE_DESCRIPTION("LED Array Driver for DDK v1.1");
