#include <linux/module.h>
#include <linux/device.h>
#include <linux/err.h>

static struct class *cool_cl;
static struct class *char_cl;
static struct class *video_cl;
static struct class *usb_cl;
EXPORT_SYMBOL(cool_cl);
EXPORT_SYMBOL(char_cl);
EXPORT_SYMBOL(video_cl);
EXPORT_SYMBOL(usb_cl);

static int __init glob_cl_init(void)
{
	if (IS_ERR(cool_cl = class_create(THIS_MODULE, "cool")))
	{
		return PTR_ERR(cool_cl);
	}
	if (IS_ERR(char_cl = class_create(THIS_MODULE, "char")))
	{
		class_destroy(cool_cl);
		return PTR_ERR(char_cl);
	}
	if (IS_ERR(video_cl = class_create(THIS_MODULE, "video")))
	{
		class_destroy(char_cl);
		class_destroy(cool_cl);
		return PTR_ERR(video_cl);
	}
	if (IS_ERR(usb_cl = class_create(THIS_MODULE, "usb")))
	{
		class_destroy(video_cl);
		class_destroy(char_cl);
		class_destroy(cool_cl);
		return PTR_ERR(usb_cl);
	}
	return 0;
}

static void __exit glob_cl_exit(void)
{
	class_destroy(usb_cl);
	class_destroy(video_cl);
	class_destroy(char_cl);
	class_destroy(cool_cl);
}

module_init(glob_cl_init);
module_exit(glob_cl_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Anil Kumar Pugalia <email@sarika-pugs.com>");
MODULE_DESCRIPTION("Global Device Class Creation Driver");
