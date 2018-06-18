#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <asm/uaccess.h>

static dev_t first; // Global variable for the first device number
static struct cdev c_dev; // Global variable for the character device structure
static struct class *cl; // Global variable for the device class

static char c;

/*static int my_open(struct inode *i, struct file *f)
{
    printk(KERN_INFO "Driver: open()\n");
    return 0;
}
static int my_close(struct inode *i, struct file *f)
{
    printk(KERN_INFO "Driver: close()\n");
    return 0;
}*/
//static ssize_t my_read(struct file *f, char __user *buf, size_t len, loff_t *off)
//{
//    printk(KERN_INFO "Driver: read()\n");
//    buf[0] = c;
//    return 1;
//}
//static ssize_t my_read(struct file *f, char __user *buf, size_t len, loff_t *off)
//{
//    printk(KERN_INFO "Driver: read()\n");
//    if (copy_to_user(buf, &c, 1) != 0)
//        return -EFAULT;
//    else
//        return 1;
//}
/*static ssize_t my_read(struct file *f, char __user *buf, size_t len, loff_t *off)
{
    printk(KERN_INFO "Driver: read()\n");
    if (*off == 0)
    {
        if (copy_to_user(buf, &c, 1) != 0)
            return -EFAULT;
        else
        {
            (*off)++;
            return 1;
        }
    }
    else
        return 0;
}*/

/*static ssize_t my_write(struct file *f, const char __user *buf, size_t len, loff_t *off)
{
    printk(KERN_INFO "Driver: write()\n");
    if (copy_from_user(&c, buf+len-1, 1) != 0)
        return -EFAULT;
    else
        return len;
}
*/
//static ssize_t my_write(struct file *f, const char __user *buf, size_t len, loff_t *off)
//{
//    printk(KERN_INFO "Driver: write()\n");
//    c = buf[len – 1];
//    return len;
//}
static struct file_operations pugs_fops =
{
    .owner = THIS_MODULE,
  //  .open = my_open,
  //  .release = my_close,
   // .read = my_read,
    //.write = my_write
};

static int __init ofcd_init(void) /* Constructor */
{
    int ret;
    struct device *dev_ret;

    printk(KERN_INFO "device_1 : registed");
    if ((ret = alloc_chrdev_region(&first, 0, 1, "Vijay")) < 0)
    {
        return ret;
    }
    if (IS_ERR(cl = class_create(THIS_MODULE, "chardrv_vijay")))
    {
        unregister_chrdev_region(first, 1);
        return PTR_ERR(cl);
    }
    if (IS_ERR(dev_ret = device_create(cl, NULL, first, NULL, "vijay_null")))
    {
        class_destroy(cl);
        unregister_chrdev_region(first, 1);
        return PTR_ERR(dev_ret);
    }

    cdev_init(&c_dev, &pugs_fops);
    if ((ret = cdev_add(&c_dev, first, 1)) < 0)
    {
        device_destroy(cl, first);
        class_destroy(cl);
        unregister_chrdev_region(first, 1);
        return ret;
    }
    return 0;
}

static void __exit ofcd_exit(void) /* Destructor */
{
    cdev_del(&c_dev);
    device_destroy(cl, first);
    class_destroy(cl);
    unregister_chrdev_region(first, 1);
    printk(KERN_INFO "device1 : unregistred");
}

module_init(ofcd_init);
module_exit(ofcd_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Anil Kumar Pugalia <email@sarika-pugs.com>");
MODULE_DESCRIPTION("Our First Character Driver");
