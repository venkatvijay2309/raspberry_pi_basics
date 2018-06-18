#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>


static dev_t first;                 // global variable for the device number

static int __init device_init(void)    // constructor
{
	printk(KERN_INFO "DEVICE IS REGISTRED");
	if (alloc_chrdev_region(&first, 0, 3, "vijay")<0)
	{
		return -1;
	}
	printk(KERN_INFO "<Major,Minor>:<%d,%d>\n",MAJOR(first),MINOR(first));
	return 0;
}
static void __exit device_exit(void) // Destructor
{
	unregister_chrdev_region(first,3);
	printk(KERN_INFO "Device is exit");
}
module_init(device_init);
module_exit(device_exit);


