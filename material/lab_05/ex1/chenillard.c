#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/of.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/kfifo.h>
#include <linux/wait.h>
#include <linux/types.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/cdev.h>

/* Offsets for the registers detailed in the documentation */
#define LED_REG_OFF        0x0000 // leds

#define NB_LEDS 10
#define UP   0
#define DOWN 1

#define DEV_NAME "chaser"

/**
 * @brief private structure for data shared accross functions accessible through plateform device
 *
 * mem_ptr pointer over memory mapped via ioremap
 * irq_num irq number that is handled
 * dev pointer towards the device (useful for dev_info())
 */
struct priv {
	// platform device
	void __iomem *mem_ptr;
	struct device *dev;

	// character device
	dev_t dev_num;
	struct class *dev_class;
	struct cdev cdev;
	struct device *dev_file;

	// shared data
	struct task_struct *thread;
	wait_queue_head_t worker_wait;
	struct kfifo jobs;
};

static int chaser_file_open(struct inode *inode, struct file *filp);
static ssize_t chaser_file_write(struct file *filp, const char __user *buf, size_t count, loff_t *ppos);

static const struct file_operations chaser_fops = {
	.owner = THIS_MODULE,
	.open = chaser_file_open,
	.write = chaser_file_write,
	.llseek = default_llseek, // Use default to enable seeking to 0
};

// group sysfs attributes in a single sysfs group
static struct attribute *chaser_attrs[] = {
	NULL,
};

// put sysfs attributes in a group
static const struct attribute_group chaser_attribute_group = {

	.name = "chaser_sysfs",
	.attrs = chaser_attrs,
};

/**
 * @brief Write an integer value to a register of the REDS-adder device.
 *
 * @param priv pointer to driver's private data
 * @param reg_offset offset (in bytes) of the desired register
 * @param value value that has to be written
 */
static void chaser_write(struct priv const *const priv, int const reg_offset, int const value) {

	// stacktrace when condition is true
	WARN_ON(priv == NULL);
	WARN_ON(priv->mem_ptr == NULL);

	// DO NOT REMOVE CASTING ELSE IT EXPLODES
	iowrite32(value, (int *)priv->mem_ptr + (reg_offset / 4));
}

static ssize_t chaser_file_open(struct inode *inode, struct file *filp) {
	struct priv *priv;

	// Register private data back in filp
	priv = container_of(inode->i_cdev, struct priv, cdev);
	filp->private_data = priv;

	dev_info(priv->dev, "Received an open call\n");

	return nonseekable_open(inode, filp);
}

/**
 * @brief Creates a new job for the worker depending on the command received
 */
static int chaser_file_write(struct file *filp, const char __user *buf, size_t count, loff_t *ppos) {

	struct priv *priv;
	char tmp[5];
	size_t len;

	// check pointers
	if (filp == NULL || buf == NULL) {
		return -EINVAL;
	}

	priv = (struct priv *)filp->private_data;
	if (priv == NULL) {
		return -EINVAL;
	}

	// check count
	if (count == 0) {
		return 0;
	}

	dev_info(priv->dev, "Received a job from write\n");

	// check we can add a job
	if (kfifo_is_full(&priv->jobs)) {
		dev_info(priv->dev, "No space for more jobs\n");
		return -ENOMEM;
	}

	// with echo '\n' is counted (echo "UP" > /dev/chaser0)
	// "DOWN\n" (count = 5) => 4 characters, add [4] as \0
	// "UP\n"   (count = 3) => 2 characters, add [2] as \0
	len = (count > 3 ? 4 : min(count, (size_t)2));

	// copy from user space
	if (copy_from_user(tmp, buf, len))
		return -EFAULT;
	tmp[len] = '\0';

	dev_info(priv->dev, "buf:%s and count:%u", tmp, count);

	if (strcmp(tmp, "up") == 0) {
		kfifo_put(&priv->jobs, UP);
	} else if (strcmp(tmp, "down") == 0) {
		kfifo_put(&priv->jobs, DOWN);
	} else {
		dev_info(priv->dev, "Received an invalid command, thrown in the bin\n");
		return -EINVAL;
	}

	// wake up worker
	wake_up_interruptible(&priv->worker_wait);

	// update ppos
	*ppos += len;

	return count;
}

/**
 * @brief Makes LEDs go brrrrrr when woken up
 */
static int chaser_worker(void *data) {

	struct priv *priv = (struct priv *)data; // cast private data back for access to private data
	uint8_t job;

	dev_info(priv->dev, "Working thread here sleeping until work o7");

	while (!kthread_should_stop()) {

		wait_event_interruptible(priv->worker_wait, !kfifo_is_empty(&priv->jobs) || kthread_should_stop());
		if (kthread_should_stop()) break;

		if (!kfifo_get(&priv->jobs, &job)) {
			dev_info(priv->dev, "Tried to get a job from empty fifo\n");
			continue;
		}

		if (job == (uint8_t) UP) {
			dev_info(priv->dev, "Processing up signal...\n");
		} else if (job == (uint8_t) DOWN) {
			dev_info(priv->dev, "Processing down signal...\n");
		} else {
			dev_info(priv->dev, "Tried to process an unknown signal\n");
			continue;
		}

		for (uint8_t i = 0; i < NB_LEDS && !kthread_should_stop(); ++i) {
			chaser_write(priv, LED_REG_OFF, job == UP ? 1 << i : 1 << (NB_LEDS - 1 - i));
			msleep_interruptible(1000);
		}
		chaser_write(priv, LED_REG_OFF, 0);
	}

	// inform everything went well
	return 0;
}

/**
 * @brief Initializes the driver
 *
 * @param pdev plateform device
 *
 * @return 0 if it went well, an error code otherwise
 */
static int chaser_probe(struct platform_device *pdev) {

	struct priv *priv;			// private data for the driver
	struct resource *mem_info;	// register's region address
	int rc;						// return code

	// allocate private data
	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (unlikely(!priv)) {
		rc = -ENOMEM;
		goto return_fail;
	}

	dev_info(&pdev->dev, "Memory for the private data allocated\n");

	// store data in plateform device for future access
	platform_set_drvdata(pdev, priv);

	// store device in our private data
	priv->dev = &pdev->dev;

	// init wait queue for blocking worker
	init_waitqueue_head(&priv->worker_wait);

	// init kfifo to store jobs
	rc = kfifo_alloc(&priv->jobs, 16 * sizeof(uint8_t), GFP_KERNEL);
	if (rc) {
		dev_err(&pdev->dev, "Failed to allocate memory resource for kfifo handling jobs\n");
		rc = -ENOMEM;
		goto return_fail;
	}

	dev_info(&pdev->dev, "Main data for the private data initialized\n");

	// retrieve the address of the register's region from the DT
	mem_info = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (unlikely(!mem_info)) {
		dev_err(&pdev->dev, "Failed to get memory resource from device tree\n");
		rc = -EINVAL;
		goto free_kfifo;
	}

	dev_info(&pdev->dev, "Successfully retrieved the IOMEM address from the DT\n");

	// remap physical addresse retrieved into a virtual one
	priv->mem_ptr = devm_ioremap_resource(priv->dev, mem_info);
	if (IS_ERR(priv->mem_ptr)) {
		dev_err(&pdev->dev, "Failed to map memory!\n");
		rc = PTR_ERR(priv->mem_ptr);
		goto free_kfifo;
	}

	dev_info(&pdev->dev, "IOMEM address remapped\n");

	// create sysfs group entry
	rc = sysfs_create_group(&pdev->dev.kobj, &chaser_attribute_group);
	if (rc) {
		dev_err(&pdev->dev, "Failed to create a sysfs group\n");
		goto free_kfifo;
	}

	dev_info(&pdev->dev, "Created sysfs group sucessfully\n");

	// clear leds
	chaser_write(priv, LED_REG_OFF, 0x0);

	dev_info(&pdev->dev, "Set all registers to default values\n");

	priv->thread = kthread_run(chaser_worker, priv, "chaser_worker");
	if (IS_ERR(priv->thread)) {
		pr_err("Unable to create thread 1\n");
		rc = PTR_ERR(priv->thread);
		goto destroy_sysfs_group;
	}

	dev_info(&pdev->dev, "Launched worker\n");

	// get major and minor from kerneldestroy_sysfs_group
	rc = alloc_chrdev_region(
		&priv->dev_num, /* Will contain the assigned numbers */
		0,				/* First minor we request */
		1,				/* Number of minors we want */
		DEV_NAME);		/* Name of the device */
	if (rc != 0) {
		dev_err(&pdev->dev, "Cannot get a major/minor number pair\n");
		goto destroy_sysfs_group;
	}

	// create a class in /sys/class
	priv->dev_class = class_create(THIS_MODULE, DEV_NAME);
	if (IS_ERR(priv->dev_class)) {
		dev_err(&pdev->dev, "Failed to allocate device's class\n");
		goto free_chrdev;
	}

	// initialize cdev and register file operations
	cdev_init(&priv->cdev, &chaser_fops);
	priv->cdev.owner = THIS_MODULE;

	// add character device
	rc = cdev_add(&priv->cdev,		/* This is our handle to the cdev */
			      priv->dev_num,	/* The cdev will have this major/minor */
			      1);				/* Number of minors to be added */
	if (rc != 0) {
		dev_err(&pdev->dev, "Failed to add cdev\n");
		goto free_cdev;
	}

	// create device file in /dev and register it in sysfs
	priv->dev_file = device_create(priv->dev_class, /* Device's class */
					   priv->dev,					/* Parent device */
					   priv->dev_num,				/* Major/minor numbers */
					   priv,						/* Pointer to private data */
					   "chaser%d",
					   0);							/* Device file's name */
	if (IS_ERR(priv->dev_file)) {
		dev_err(&pdev->dev, "Failed to create device file\n");
		goto delete_cdev;
	}

	dev_info(&pdev->dev, "Chaser ready !\n");

	return 0;

delete_cdev:
	cdev_del(&priv->cdev);
free_cdev:
	class_destroy(priv->dev_class);
free_chrdev:
	unregister_chrdev_region(priv->dev_num, 1);
free_thread:
	kthread_stop(priv->thread);
destroy_sysfs_group:
	sysfs_remove_group(&pdev->dev.kobj, &chaser_attribute_group);
free_kfifo:
	kfifo_free(&priv->jobs);
return_fail:
	return rc;
}

/**
 * @brief clears all data and removes the driver
 *
 * @param pdev platform device
 *
 * @return 0 if it went well
 */
static int chaser_remove(struct platform_device *pdev) {

	// retrieve private data from plateform device structure
	struct priv *priv = platform_get_drvdata(pdev);

	dev_info(&pdev->dev, "Turning system off...\n");

	// delete cdev
	cdev_del(&priv->cdev);

	// destroy class
	class_destroy(priv->dev_class);

	// unregister character device
	unregister_chrdev_region(priv->dev_num, 1);

	// free thread
	if (priv->thread) {
		kthread_stop(priv->thread);
		wake_up_interruptible(&priv->worker_wait);
	}

	// remove sysfs group
	sysfs_remove_group(&pdev->dev.kobj, &chaser_attribute_group);

	// free kfifo
	kfifo_free(&priv->jobs);

	// destry device
	device_destroy(priv->dev_class, priv->dev_num);

	// clear everything
	chaser_write(priv, LED_REG_OFF, 0x0);

	dev_info(&pdev->dev, "Driver removed\n");

	return 0;
}

static const struct of_device_id chaser_driver_id[] = {
	{ .compatible = "drv2026" },
	{ /* END */ },
};

MODULE_DEVICE_TABLE(of, chaser_driver_id);

static struct platform_driver chaser_driver = {
	.probe = chaser_probe,
	.remove = chaser_remove,
	.driver = {
		.name = "drv-lab5",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(chaser_driver_id),
	},
};

module_platform_driver(chaser_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("REDS");
MODULE_DESCRIPTION("Introduction kfifo and kthread");
