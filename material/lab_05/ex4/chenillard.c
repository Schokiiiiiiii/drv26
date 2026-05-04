/****************************************************************************
 * @file    chenillard.c
 * @author  Fabien Léger
 * @date    03.05.2026
 * @version 1.0
 * @brief   Linux driver for the de1-soc to make up and
 *          down movements with the LEDs and offer a
 *          sysfs interface to modify values
 *
 *          insert driver -> insmod chenillard.ko
 *          remove driver -> rmmod chenillard
 *          device        -> /dev/chaser0
 *          class         -> /sys/devices/platform/soc/ff200000.drv2026/
 ****************************************************************************/

// device / driver / etc
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/kfifo.h>
#include <linux/kthread.h>
#include <linux/cdev.h>
// io
#include <linux/io.h>
#include <linux/ioport.h>
// timer
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/completion.h>
// concurency
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/atomic.h>

// offsets for the registers detailed in the documentation
#define LED_REG_OFF        0x0000

#define NB_LEDS 10
#define UP   0
#define DOWN 1

#define DEV_NAME "chaser"

/*********************************/
/*         Private data          */
/*********************************/

/**
 * @brief private structure for data shared accross functions
 */
struct priv {
	// device
	void __iomem *mem_ptr;
	struct device *dev;

	// character device
	dev_t dev_num;
	struct class *dev_class;
	struct cdev cdev;
	struct device *dev_file;

	// thread
	struct task_struct *thread;
	wait_queue_head_t worker_wait;
	struct kfifo jobs;
	uint8_t current_job;
	uint8_t current_step;
	uint32_t period_ms;
	atomic_t nb_sequence_done;

	// timer
	struct timer_list timer;
	struct completion sequence_done;
	bool sequence_running;

	// concurrency
	struct mutex jobs_lock;
	spinlock_t anim_lock;
};

/*********************************/
/*         File operations       */
/*********************************/

static int chaser_file_open(struct inode *inode, struct file *filp);
static ssize_t chaser_file_write(struct file *filp, const char __user *buf, size_t count, loff_t *ppos);

static const struct file_operations chaser_fops = {
	.owner = THIS_MODULE,
	.open = chaser_file_open,
	.write = chaser_file_write,
	.llseek = default_llseek, // Use default to enable seeking to 0
};

/*********************************/
/*         Sysfs infos           */
/*********************************/

/**
 * @brief Shows the current period between each led animation in ms
 */
static ssize_t period_show(struct device *dev,
                           struct device_attribute *attr,
                           char *buf)
{
	int rc;
	struct priv *priv = dev_get_drvdata(dev);
	unsigned long flags;

	spin_lock_irqsave(&priv->anim_lock, flags);
	rc = sysfs_emit(buf, "%d\n", priv->period_ms);
	spin_unlock_irqrestore(&priv->anim_lock, flags);

	return rc;
}

/**
 * @brief Stores the period between each led animation in ms
 */
static ssize_t period_store(struct device *dev,
                            struct device_attribute *attr,
                            const char *buf,
                            size_t count)
{
	int rc;
	struct priv *priv = dev_get_drvdata(dev);
	unsigned long flags;
	unsigned int val;

	rc = kstrtouint(buf, 0, &val);
	if (rc || val == 0)
		return -EINVAL;

	spin_lock_irqsave(&priv->anim_lock, flags);
	priv->period_ms = val;
	spin_unlock_irqrestore(&priv->anim_lock, flags);

	return count;
}

/**
 * @brief Shows which led is currently on or -1 if no sequence is running
 */
static ssize_t led_on_show(struct device *dev,
                           struct device_attribute *attr,
                           char *buf)
{
	int rc;
	struct priv *priv = dev_get_drvdata(dev);
	int led;
	unsigned long flags;

	spin_lock_irqsave(&priv->anim_lock, flags);
	led = (priv->sequence_running ?
			(priv->current_job == UP ? priv->current_step - 1 : NB_LEDS - priv->current_step): -1);
	spin_unlock_irqrestore(&priv->anim_lock, flags);

	rc = sysfs_emit(buf, "%d\n", led);

	return rc;
}

/**
 * @brief Shows the number of sequence done since the start of the module
 */
static ssize_t nb_sequence_done_show(struct device *dev,
                                     struct device_attribute *attr,
                                     char *buf)
{
	int rc;
	struct priv *priv = dev_get_drvdata(dev);

	rc = sysfs_emit(buf, "%d\n", atomic_read(&priv->nb_sequence_done));

	return rc;
}

/**
 * @brief Shows the number of sequence still waiting in the fifo
 */
static ssize_t nb_sequence_waiting_show(struct device *dev,
                                        struct device_attribute *attr,
                                        char *buf)
{
	int rc;
	struct priv *priv = dev_get_drvdata(dev);

	mutex_lock(&priv->jobs_lock);
	rc = sysfs_emit(buf, "%d\n", kfifo_len(&priv->jobs) / sizeof(uint8_t));
	mutex_unlock(&priv->jobs_lock);

	return rc;
}

/**
 * @brief Shows the list of sequence still waiting in the fifo
 */
static ssize_t sequence_waiting_show(struct device *dev,
                                     struct device_attribute *attr,
                                     char *buf)
{
	int rc = 0;
	struct priv *priv = dev_get_drvdata(dev);
	uint8_t waiting_jobs[16];
	size_t len;

	mutex_lock(&priv->jobs_lock);
	len = kfifo_out_peek(&priv->jobs, &waiting_jobs, 16);
	mutex_unlock(&priv->jobs_lock);

	for (size_t i = 0; i < len ; ++i) {
		rc += sysfs_emit_at(buf, rc, "%s\n", (waiting_jobs[i] == UP ? "up" : "down"));
	}

	return rc;
}

static DEVICE_ATTR_RW(period);
static DEVICE_ATTR_RO(led_on);
static DEVICE_ATTR_RO(nb_sequence_done);
static DEVICE_ATTR_RO(nb_sequence_waiting);
static DEVICE_ATTR_RO(sequence_waiting);

// group sysfs attributes in a single sysfs group
static struct attribute *chaser_attrs[] = {
	&dev_attr_period.attr,
	&dev_attr_led_on.attr,
	&dev_attr_nb_sequence_done.attr,
	&dev_attr_nb_sequence_waiting.attr,
	&dev_attr_sequence_waiting.attr,
	NULL,
};

// put sysfs attributes in a group
static const struct attribute_group chaser_attribute_group = {

	.name = "chaser_sysfs",
	.attrs = chaser_attrs,
};

/*********************************/
/*         Driver functions      */
/*********************************/

/**
 * @brief Write an integer value to a register of the REDS-adder device.
 *
 * @param priv pointer to driver's private data
 * @param reg_offset offset (in bytes) of the desired register
 * @param value value that has to be written
 */
static void chaser_write(struct priv const *const priv, int const reg_offset, int const value)
{
	// stacktrace when condition is true
	WARN_ON(priv == NULL);
	WARN_ON(priv->mem_ptr == NULL);

	// DO NOT REMOVE CASTING ELSE IT EXPLODES
	iowrite32(value, (int *)priv->mem_ptr + (reg_offset / 4));
}

/**
 * @brief Opens the driver virtual file and retrieve priv structure
 */
static int chaser_file_open(struct inode *inode, struct file *filp)
{
	struct priv *priv;

	// Register private data back in filp
	priv = container_of(inode->i_cdev, struct priv, cdev);
	filp->private_data = priv;

	return nonseekable_open(inode, filp);
}

/**
 * @brief Creates a new job for the worker depending on the command received
 */
static ssize_t chaser_file_write(struct file *filp, const char __user *buf, size_t count, loff_t *ppos)
{
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

	mutex_lock(&priv->jobs_lock);

	dev_info(priv->dev, "Received a job from write\n");

	// check we can add a job
	if (kfifo_is_full(&priv->jobs)) {
		dev_info(priv->dev, "No space for more jobs\n");
		mutex_unlock(&priv->jobs_lock);
		return -ENOMEM;
	}

	// with echo '\n' is counted (echo "UP" > /dev/chaser0)
	// "DOWN\n" (count = 5) => 4 characters, add [4] as \0
	// "UP\n"   (count = 3) => 2 characters, add [2] as \0
	len = (count > 3 ? 4 : min(count, (size_t)2));

	// copy from user space
	if (copy_from_user(tmp, buf, len)) {
		mutex_unlock(&priv->jobs_lock);
		return -EFAULT;
	}
	tmp[len] = '\0';

	if (strcmp(tmp, "up") == 0) {
		kfifo_put(&priv->jobs, UP);
	} else if (strcmp(tmp, "down") == 0) {
		kfifo_put(&priv->jobs, DOWN);
	} else {
		dev_info(priv->dev, "Received an invalid command, thrown in the bin\n");
		mutex_unlock(&priv->jobs_lock);
		return -EINVAL;
	}

	mutex_unlock(&priv->jobs_lock);

	// wake up worker
	wake_up_interruptible(&priv->worker_wait);

	// update ppos
	*ppos += len;

	return count;
}

/**
 * @brief Callback function for when the period goes off to play the new LED animation
 */
static void chaser_timer_callback(struct timer_list *t)
{
	struct priv *priv = container_of(t, struct priv, timer);
	unsigned long flags;
	uint32_t led_value;
	uint32_t period_ms;

	spin_lock_irqsave(&priv->anim_lock, flags);

	if (priv->current_step >= NB_LEDS) {
		priv->sequence_running = false;
		chaser_write(priv, LED_REG_OFF, 0);
		spin_unlock_irqrestore(&priv->anim_lock, flags);
		complete(&priv->sequence_done);
		return;
	}

	if (priv->current_job == UP)
		led_value = 1 << priv->current_step;
	else
		led_value = 1 << (NB_LEDS - 1 - priv->current_step);

	chaser_write(priv, LED_REG_OFF, led_value);
	++priv->current_step;

	period_ms = priv->period_ms;

	spin_unlock_irqrestore(&priv->anim_lock, flags);

	mod_timer(&priv->timer, jiffies + msecs_to_jiffies(period_ms));
}

/**
 * @brief Thread waiting to be woken up by jobs and play animation through callback function
 */
static int chaser_worker(void *data)
{
	int rc;
	struct priv *priv = (struct priv *)data; // cast private data back for access to private data
	uint8_t job;
	unsigned long flags;

	dev_info(priv->dev, "Working thread here sleeping until work o7");

	while (!kthread_should_stop()) {

		// wait until job appears
		wait_event_interruptible(priv->worker_wait, !kfifo_is_empty(&priv->jobs) || kthread_should_stop());
		if (kthread_should_stop()) break;

		mutex_lock(&priv->jobs_lock);
		rc = kfifo_get(&priv->jobs, &job);
		mutex_unlock(&priv->jobs_lock);
		if (!rc) {
			dev_info(priv->dev, "Tried to get a job from empty fifo\n");
			continue;
		}

		// reinit
		reinit_completion(&priv->sequence_done);

		spin_lock_irqsave(&priv->anim_lock, flags);
		priv->current_job = job;
		priv->current_step = 0;
		priv->sequence_running = true;
		spin_unlock_irqrestore(&priv->anim_lock, flags);

		mod_timer(&priv->timer, jiffies);

		if (!wait_for_completion_interruptible(&priv->sequence_done))
			atomic_inc(&priv->nb_sequence_done);
	}

	// inform everything went well
	return 0;
}

/*********************************/
/*         Driver probe          */
/*********************************/

/**
 * @brief Initializes the driver
 *
 * @param pdev plateform device
 *
 * @return 0 if it went well, an error code otherwise
 */
static int chaser_probe(struct platform_device *pdev)
{
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

	// fix default period
	priv->period_ms = 100;

	// store device in our private data
	priv->dev = &pdev->dev;

	// initialize different variables
	init_waitqueue_head(&priv->worker_wait);
	mutex_init(&priv->jobs_lock);
	spin_lock_init(&priv->anim_lock);
	atomic_set(&priv->nb_sequence_done, 0);

	// init kfifo to store jobs
	rc = kfifo_alloc(&priv->jobs, 16 * sizeof(uint8_t), GFP_KERNEL);
	if (rc) {
		dev_err(&pdev->dev, "Failed to allocate memory resource for kfifo handling jobs\n");
		rc = -ENOMEM;
		goto return_fail;
	}

	// init timer to play sequence
	timer_setup(&priv->timer, chaser_timer_callback, 0);
	init_completion(&priv->sequence_done);
	priv->sequence_running = false;
	priv->current_step = 0;

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

	// create thread
	priv->thread = kthread_run(chaser_worker, priv, "chaser_worker");
	if (IS_ERR(priv->thread)) {
		pr_err("Unable to create thread 1\n");
		rc = PTR_ERR(priv->thread);
		goto destroy_sysfs_group;
	}

	dev_info(&pdev->dev, "Launched worker\n");

	// get major and minor from kernel
	rc = alloc_chrdev_region(
		&priv->dev_num, /* Will contain the assigned numbers */
		0,				/* First minor we request */
		1,				/* Number of minors we want */
		DEV_NAME);		/* Name of the device */
	if (rc != 0) {
		dev_err(&pdev->dev, "Cannot get a major/minor number pair\n");
		goto free_thread;
	}

	// create a class in /sys/class
	priv->dev_class = class_create(THIS_MODULE, DEV_NAME);
	if (IS_ERR(priv->dev_class)) {
		dev_err(&pdev->dev, "Failed to allocate device's class\n");
		rc = PTR_ERR(priv->dev_class);
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
	complete(&priv->sequence_done);
	wake_up_interruptible(&priv->worker_wait);
	kthread_stop(priv->thread);
destroy_sysfs_group:
	sysfs_remove_group(&pdev->dev.kobj, &chaser_attribute_group);
free_kfifo:
	del_timer_sync(&priv->timer);
	kfifo_free(&priv->jobs);
return_fail:
	return rc;
}

/*********************************/
/*         Driver remove         */
/*********************************/

/**
 * @brief clears all data and removes the driver
 *
 * @param pdev platform device
 *
 * @return 0 if it went well
 */
static int chaser_remove(struct platform_device *pdev)
{
	// retrieve private data from plateform device structure
	struct priv *priv = platform_get_drvdata(pdev);

	dev_info(&pdev->dev, "Turning system off...\n");

	// destroy device
	device_destroy(priv->dev_class, priv->dev_num);

	// delete cdev
	cdev_del(&priv->cdev);

	// destroy class
	class_destroy(priv->dev_class);

	// unregister character device
	unregister_chrdev_region(priv->dev_num, 1);

	// free thread
	if (priv->thread) {
		complete(&priv->sequence_done);
		wake_up_interruptible(&priv->worker_wait);
		kthread_stop(priv->thread);
	}

	// delete timer
	del_timer_sync(&priv->timer);

	// remove sysfs group
	sysfs_remove_group(&pdev->dev.kobj, &chaser_attribute_group);

	// free kfifo
	kfifo_free(&priv->jobs);

	// clear everything
	chaser_write(priv, LED_REG_OFF, 0x0);

	dev_info(&pdev->dev, "Driver removed\n");

	return 0;
}

/*********************************/
/*         Platform driver       */
/*********************************/

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

/*********************************/
/*         Module infos          */
/*********************************/

MODULE_LICENSE("GPL");
MODULE_AUTHOR("REDS");
MODULE_DESCRIPTION("Introduction kfifo and kthread");
