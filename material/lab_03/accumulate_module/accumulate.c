#include <linux/module.h>	/* Needed by all modules */
#include <linux/kernel.h>	/* Needed for KERN_INFO */
#include <linux/init.h>	/* Needed for the macros */
#include <linux/fs.h>		/* Needed for file_operations */
#include <linux/slab.h>	/* Needed for kmalloc */
#include <linux/uaccess.h>	/* copy_(to|from)_user */

#include <linux/string.h>

#include "accumulate.h"

#define MAJOR_NUM		97
#define DEVICE_NAME		"accumulate"

#define MAX_NB_VALUE		256

static uint64_t accumulate_value;
static int operation = OP_ADD;

static ssize_t accumulate_read(struct file *filp, char __user *buf,
			       size_t count, loff_t *ppos)
{
	char buffer[21] = { 0 }; /* UINT64_MAX has 20 digits */
	size_t to_copy = count;
	int nb_char;

	if (buf == NULL || count == 0) {
		return 0;
	}

	nb_char = snprintf(buffer, sizeof(buffer), "%lld", accumulate_value);

	if (*ppos >= nb_char) {
		return 0;
	}

	to_copy = nb_char > count ? count : nb_char;

	copy_to_user(buf, buffer, to_copy);

	*ppos = nb_char;

	return nb_char;
}

static ssize_t accumulate_write(struct file *filp, const char __user *buf,
				size_t count, loff_t *ppos)
{
	char buffer[21] = { 0 }; /* UINT64_MAX has 20 digits */
	uint64_t value;

	if (count == 0) {
		return 0;
	} else if (count > sizeof(buffer)) {
		return -EINVAL;
	}

	*ppos = 0;

	copy_from_user(buffer, buf, count);
	kstrtoull(buffer, 10, &value);

	switch (operation) {
	case OP_ADD:
		accumulate_value += value;
		break;

	case OP_MULTIPLY:
		accumulate_value *= value;
		break;

	default:
		break;
	}

	return count;
}

static long accumulate_ioctl(struct file *filep, unsigned int cmd, unsigned long arg)
{
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

static const struct file_operations accumulate_fops = {
	.owner          = THIS_MODULE,
	.read           = accumulate_read,
	.write          = accumulate_write,
	.unlocked_ioctl = accumulate_ioctl,
};

static int __init accumulate_init(void)
{
	register_chrdev(MAJOR_NUM, DEVICE_NAME, &accumulate_fops);

	accumulate_value = 0;

	pr_info("Acumulate ready!\n");
	pr_info("ioctl ACCUMULATE_CMD_RESET: %lu\n", (unsigned long)ACCUMULATE_CMD_RESET);
	pr_info("ioctl ACCUMULATE_CMD_CHANGE_OP: %lu\n", (unsigned long)ACCUMULATE_CMD_CHANGE_OP);

	return 0;
}

static void __exit accumulate_exit(void)
{
	unregister_chrdev(MAJOR_NUM, DEVICE_NAME);

	pr_info("Acumulate done!\n");
}

MODULE_AUTHOR("REDS");
MODULE_LICENSE("GPL");

module_init(accumulate_init);
module_exit(accumulate_exit);
