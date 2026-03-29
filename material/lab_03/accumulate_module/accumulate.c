#include <linux/module.h>	/* Needed by all modules */
#include <linux/kernel.h>	/* Needed for KERN_INFO */
#include <linux/init.h>	    /* Needed for the macros */
#include <linux/fs.h>		/* Needed for file_operations */
#include <linux/slab.h>	    /* Needed for kmalloc */
#include <linux/uaccess.h>	/* copy_(to|from)_user */
#include <linux/cdev.h>     /* device register */
#include <linux/device.h>   /* device register */

#include <linux/string.h>

#include "accumulate.h"

#include <asm-generic/errno-base.h>

// #define MAJOR_NUM		97				// major number for device
#define DEVICE_NAME		"accumulate"	// name for device
#define MAX_DEV			1

#define MAX_NB_VALUE		256

static uint64_t accumulate_value;
static int operation = OP_ADD;
static int dev_major;
static dev_t dev_base;

static ssize_t accumulate_read(struct file *filp, char __user *buf,
							   size_t count, loff_t *ppos) {

	// read value
	uint64_t value = accumulate_value;

	// EOF
	if (*ppos != 0)
		return 0;

	// check we get 8 bytes
	if (count < sizeof(uint64_t))
		return -EINVAL;

	// copy to userspace
	if (copy_to_user(buf, &value, sizeof(uint64_t)))
		return -EFAULT;

	// register that we read
	*ppos = sizeof(uint64_t);

	return sizeof(uint64_t);
}

static ssize_t accumulate_write(struct file *filp, const char __user *buf,
								size_t count, loff_t *ppos) {

	// variable to store
	uint64_t value;

	// check we're getting 8 bytes
	if (count != sizeof(uint64_t))
		return -EINVAL;

	// put the reader position back to the beginning
	*ppos = 0;

	// copy from userspace to our variable
	if (copy_from_user(&value, buf, sizeof(uint64_t)))
		return -EFAULT;

	// do the operation
	switch (operation) {
	case OP_ADD:
		accumulate_value += value;
		break;

	case OP_MULTIPLY:
		accumulate_value *= value;
		break;

	default:
		return -EINVAL;
	}

	return sizeof(uint64_t);
}

static long accumulate_ioctl(struct file *filep, unsigned int cmd, unsigned long arg) {

	// switch over either reset or change of operation
	switch (cmd) {
	case ACCUMULATE_CMD_RESET:
		accumulate_value = 0;
		break;

	case ACCUMULATE_CMD_CHANGE_OP:
		if (arg != OP_ADD && arg != OP_MULTIPLY) {
			return -EINVAL;
		}
		operation = arg;
		break;

	default:
		return -EINVAL;
	}
	return 0;
}

// device data holder, this structure may be extended to hold additional data
struct accumulate_device_data {
	struct cdev cdev;
};

// sysfs class structure
static struct class *accumulate_class = NULL;

// array of mychar_device_data for
static struct accumulate_device_data accumulate_data[MAX_DEV];

/**
 * @brief operations allowed with this driver
 */
static const struct file_operations accumulate_fops = {
	.owner          = THIS_MODULE,
	.read           = accumulate_read,
	.write          = accumulate_write,
	.unlocked_ioctl = accumulate_ioctl,
};

/**
 * @brief initialize the accumulate driver
 * @return 0 if everything went well
 */
static int __init accumulate_init(void) {
	int err, i;

	// allocate chardev region and assign Major number
	err = alloc_chrdev_region(&dev_base, 0, MAX_DEV, DEVICE_NAME);
	if (err < 0)
		return err;

	dev_major = MAJOR(dev_base);

	// create sysfs class
	accumulate_class = class_create(THIS_MODULE, DEVICE_NAME);
	if (IS_ERR(accumulate_class)) {
		err = PTR_ERR(accumulate_class);
		unregister_chrdev_region(dev_base, MAX_DEV);
		return err;
	}

	// Create necessary number of the devices
	for (i = 0; i < MAX_DEV; ++i)
	{
		// calculate dev number
		dev_t devno = MKDEV(dev_major, MINOR(dev_base) + i);

		// init new device
		cdev_init(&accumulate_data[i].cdev, &accumulate_fops);
		accumulate_data[i].cdev.owner = THIS_MODULE;

		// add device to the system where "i" is a Minor number of the new device
		err = cdev_add(&accumulate_data[i].cdev, devno, 1);
		if (err < 0)
			goto err_cleanup;

		// create device node /dev/mychardev-x where "x" is "i", equal to the Minor number
		if (IS_ERR(device_create(accumulate_class, NULL, devno, NULL, "accumulate-%d", i))) {
			cdev_del(&accumulate_data[i].cdev);
			err = -ENOMEM;
			goto err_cleanup;
		}
	}

	// set current value to 0
	accumulate_value = 0;

	// print info messages for user
	pr_info("Accumulate ready!\n");
	pr_info("ioctl ACCUMULATE_CMD_RESET: %lu\n", (unsigned long)ACCUMULATE_CMD_RESET);
	pr_info("ioctl ACCUMULATE_CMD_CHANGE_OP: %lu\n", (unsigned long)ACCUMULATE_CMD_CHANGE_OP);

	return 0;

err_cleanup:
	while (--i >= 0) {
		dev_t devno = MKDEV(dev_major, MINOR(dev_base) + i);
		device_destroy(accumulate_class, devno);
		cdev_del(&accumulate_data[i].cdev);
	}
	class_destroy(accumulate_class);
	unregister_chrdev_region(dev_base, MAX_DEV);
	return err;
}

/**
 * @brief unregisters the accumulate driver
 */
static void __exit accumulate_exit(void) {

	int i;

	for (i = 0; i < MAX_DEV; ++i) {
		dev_t devno = MKDEV(dev_major, MINOR(dev_base) + i);
		device_destroy(accumulate_class, devno);
		cdev_del(&accumulate_data[i].cdev);
	}

	class_destroy(accumulate_class);
	unregister_chrdev_region(dev_base, MAX_DEV);

	// print an info emssage
	pr_info("Accumulate done!\n");
}

MODULE_AUTHOR("REDS");
MODULE_LICENSE("GPL");

module_init(accumulate_init);
module_exit(accumulate_exit);
