// SPDX-License-Identifier: GPL-2.0
/*
 * Parrot file
 */

#include <asm-generic/errno-base.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/device.h>

#include <linux/string.h>

#define MAJOR_NUM 98
#define MAJMIN MKDEV(MAJOR_NUM, 0)
#define DEVICE_NAME "parrot"

static struct cdev cdev;
static struct class *cl;

// parrot data
#define BASE_LENGTH	   8
#define MAX_LENGTH	1024
static struct {
	struct device *clsdev;
	ssize_t length;
	uint8_t *data;
} parrot_data;

/**
 * @brief Read back previously written data in the internal buffer.
 *
 * @param filp pointer to the file descriptor in use
 * @param buf destination buffer in user space
 * @param count maximum number of data to read
 * @param ppos current position in file from which data will be read
 *              will be updated to new location
 *
 * @return Actual number of bytes read from internal buffer,
 *         or a negative error code
 */
static ssize_t parrot_read(struct file *filp, char __user *buf, size_t count,
			   loff_t *ppos) {

	// check arguments validity
	if (count <= 0 || *ppos < 0 || *ppos >= parrot_data.length || buf == NULL)
		return -EINVAL;

	// check we don't read over length
	if (*ppos + count > parrot_data.length)
		return -EINVAL;

	// copy to user
	if (copy_to_user(buf, parrot_data.data + *ppos, count))
		return -EFAULT;

	// update ppos
	*ppos += count;

	return count;
}

/**
 * @brief Write data to the internal buffer
 *
 * @param filp pointer to the file descriptor in use
 * @param buf source buffer in user space
 * @param count number of data to write in the buffer
 * @param ppos current position in file to which data will be written
 *              will be updated to new location
 *
 * @return Actual number of bytes writen to internal buffer,
 *         or a negative error code
 */
static ssize_t parrot_write(struct file *filp, const char __user *buf,
			    size_t count, loff_t *ppos) {

	// check arguments validity
	if (count <= 0 || *ppos < 0 || *ppos >= parrot_data.length || buf == NULL)
		return -EINVAL;

	// check we don't go over MAX_LENGTH
	if (*ppos + count > MAX_LENGTH)
		return -EINVAL;

	// realloc if necessary
	if (*ppos + count > parrot_data.length) {
		parrot_data.data = devm_krealloc(parrot_data.clsdev, (void *)parrot_data.data, *ppos + count, GFP_KERNEL);
		if (parrot_data.data == NULL)
			return -ENOMEM;
	}

	// copy from user space
	if (copy_from_user(parrot_data.data + *ppos, buf, count))
		return -EFAULT;

	// update ppos
	*ppos += count;

	return count;
}

/**
 * @brief uevent callback to set the permission on the device file
 *
 * @param dev pointer to the device
 * @param env ueven environnement corresponding to the device
 */
static int parrot_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	// Set the permissions of the device file
	add_uevent_var(env, "DEVMODE=%#o", 0666);
	return 0;
}

static const struct file_operations parrot_fops = {
	.owner = THIS_MODULE,
	.read = parrot_read,
	.write = parrot_write,
	.llseek = default_llseek, // Use default to enable seeking to 0
};

static int __init parrot_init(void) {

	struct device *clsdev;
	int err;

	// Register the device
	err = register_chrdev_region(MAJMIN, 1, DEVICE_NAME);
	if (err != 0) {
		pr_err("Parrot: Registering char device failed (%d)\n", err);
		return err;
	}

	cl = class_create(THIS_MODULE, DEVICE_NAME);
	if (IS_ERR(cl)) {
		err = PTR_ERR(cl);
		pr_err("Parrot: Error creating class (%d)\n", err);
		goto err_class_create;
	}
	cl->dev_uevent = parrot_uevent;

	clsdev = device_create(cl, NULL, MAJMIN, NULL, DEVICE_NAME);
	if (IS_ERR(clsdev)) {
		err = PTR_ERR(clsdev);
		pr_err("Parrot: Error creating device (%d)\n", err);
		goto err_device_create;
	}

	// store device
	parrot_data.clsdev = clsdev;

	cdev_init(&cdev, &parrot_fops);
	err = cdev_add(&cdev, MAJMIN, 1);
	if (err < 0) {
		pr_err("Parrot: Adding char device failed (%d)\n", err);
		goto err_cdev_add;
	}

	// create buffer with BASE_LENGTH bytes
	parrot_data.data = devm_kmalloc(clsdev, BASE_LENGTH * sizeof(uint8_t), GFP_KERNEL);
	if (parrot_data.data == NULL)
		return -EBUSY;
	parrot_data.length = BASE_LENGTH;

	pr_info("Parrot ready!\n");

	return 0;

err_cdev_add:
	device_destroy(cl, MAJMIN);
err_device_create:
	class_destroy(cl);
err_class_create:
	unregister_chrdev_region(MAJMIN, 1);

	return err;

}

static void __exit parrot_exit(void) {

	// Unregister the device
	cdev_del(&cdev);
	device_destroy(cl, MAJMIN);
	class_destroy(cl);
	unregister_chrdev_region(MAJMIN, 1);

	pr_info("Parrot done!\n");
}

MODULE_AUTHOR("REDS");
MODULE_LICENSE("GPL");

module_init(parrot_init);
module_exit(parrot_exit);
