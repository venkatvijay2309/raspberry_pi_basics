#include <linux/module.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/device.h>

#include "global_classes.h"

#define COOL_MAJOR 1000
#define COOL_MINOR 0
#define COOL_CNT 1
#define COOL_DEV MKDEV(COOL_MAJOR, COOL_MINOR)

static int __init use_cl_init(void)
{
	int ret;
	struct device *dev_ret;

	if ((ret = register_chrdev_region(COOL_DEV, COOL_CNT, "cool")) < 0)
	{
		return ret;
	}
	if (IS_ERR(dev_ret = device_create(cool_cl, NULL, COOL_DEV, NULL, "cool/use_class")))
	{
		unregister_chrdev_region(COOL_DEV, COOL_CNT);
		return PTR_ERR(dev_ret);
	}
	return 0;
}

static void __exit use_cl_exit(void)
{
	device_destroy(cool_cl, COOL_DEV);
	unregister_chrdev_region(COOL_DEV, COOL_CNT);
}

module_init(use_cl_init);
module_exit(use_cl_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Anil Kumar Pugalia <email@sarika-pugs.com>");
MODULE_DESCRIPTION("Global Device Class Usage Driver");
