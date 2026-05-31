/**
 * @file        adxl345.c
 * @version     1.0
 * @date        31.05.2026
 * @author      Fabien Léger
 * @brief       Driver for interacting with the adxl345 accelerometer
 *              on the DE1-SoC
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/types.h>
#include <linux/pm.h>
#include <linux/interrupt.h>
// CHAR DEVICE
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
// CONCURRENCY
#include <linux/completion.h>
#include <linux/atomic.h>
#include <linux/mutex.h>

// device
#define ADXL345_REG_DEVID               0x00
#define ADXL345_DEVID_VALUE             0xE5

// tapping
#define ADXL345_REG_THRESH_TAP          0x1D // threshold when tap gets detected
#define ADXL345_VALUE_THRESH_TAP        0x30 // 62.5mg/LSB
#define ADXL345_REG_DUR                 0x21 // max duration of the tap
#define ADXL345_VALUE_DUR               0x18 // 625us/LSB
#define ADXL345_REG_LATENT              0x22 // waiting time until time window for second tap
#define ADXL345_VALUE_LATENT            0x50 // 1.25ms/LSB
#define ADXL345_REG_WINDOW              0x23 // window to have a second tap
#define ADXL345_VALUE_WINDOW            0xA0 // 1.25ms/Lsb
#define ADXL345_REG_TAP_AXES            0x2A
#define ADXL345_TAP_AXES_X              0x04
#define ADXL345_TAP_AXES_Y              0x02
#define ADXL345_TAP_AXES_Z              0x01
#define ADXL345_REG_ACT_TAP_STATUS      0x2B
#define ADXL345_TAP_STATUS_X            0x04
#define ADXL345_TAP_STATUS_Y            0x02
#define ADXL345_TAP_STATUS_Z            0x01

// power/interrupts
#define ADXL345_REG_POWER_CTL           0x2D
#define ADXL345_POWER_MEASURE_MASK      BIT(3)
#define ADXL345_REG_INT_ENABLE          0x2E
#define ADXL345_INT_ENABLE_OFF          0x00
#define ADXL345_INT_ENABLE_SINGLE       BIT(6)
#define ADXL345_INT_ENABLE_DOUBLE       BIT(5)
#define ADXL345_INT_ENABLE_BOTH         0x60
#define ADXL345_REG_INT_SOURCE          0x30
#define ADXL345_SINGLE_TAP_INT_SOURCE   0x40
#define ADXL345_DOUBLE_TAP_INT_SOURCE   0x20

// data
#define ADXL345_REG_DATA_FORMAT         0x31
#define ADXL345_RANGE_4G                0x01
#define ADXL345_REG_DATAX0              0x32
#define ADXL345_REG_DATAY0              0x34
#define ADXL345_REG_DATAZ0              0x36

#define DEV_NAME "adxl345"

enum Axis{Z, Y, X};
enum Mode{OFF, SINGLE, DOUBLE, BOTH};

struct priv
{
        // i2c client (register, dev are inside)
        struct i2c_client *client;

        // character device
        dev_t dev_num;
        struct cdev cdev;
        struct class *dev_class;
        struct device *dev_file;

        int irq;

        // sysfs data
        enum Axis tap_axis;
        enum Mode tap_mode;
        struct completion tap_happened;
        atomic_t last_tap_single;
        atomic_t tap_count;
        atomic_t wait_busy;
        struct mutex state_lock;
};

static const char AXIS[3] = {'z', 'y', 'x'};
static const u8 AXIS_EN[3] = {
        [Z] = ADXL345_TAP_AXES_Z,
        [Y] = ADXL345_TAP_AXES_Y,
        [X] = ADXL345_TAP_AXES_X,
};

static const char* const MODE[4] = {"off", "single", "double", "both"};
static const u8 MODE_EN[4] = {
        ADXL345_INT_ENABLE_OFF,
        ADXL345_INT_ENABLE_SINGLE,
        ADXL345_INT_ENABLE_DOUBLE,
        ADXL345_INT_ENABLE_BOTH
};

/*********************/
/*       SYSFS       */
/*********************/

/**
 * @brief Shows the current axis for tapping detection
 */
static ssize_t tap_axis_show(struct device *dev,
                             struct device_attribute *attr,
                             char *buf)
{
        int rc;
        struct priv *priv = dev_get_drvdata(dev);
        enum Axis axis;

        mutex_lock(&priv->state_lock);
        axis = priv->tap_axis;
        mutex_unlock(&priv->state_lock);

        rc = sysfs_emit(buf, "%c\n", AXIS[axis]);

        return rc;
}

/**
 * @brief Stores the axis for tapping detection
 */
static ssize_t tap_axis_store(struct device *dev,
                              struct device_attribute *attr,
                              const char *buf,
                              size_t count)
{
        int rc;
        struct priv *priv = dev_get_drvdata(dev);
        enum Axis val;

        // compare which string we received to retrieve axis
        if (sysfs_streq(buf, "x"))
                val = X;
        else if (sysfs_streq(buf, "y"))
                val = Y;
        else if (sysfs_streq(buf, "z"))
                val = Z;
        else
                return -EINVAL;

        // modify register on adxl345
        rc = i2c_smbus_write_byte_data(priv->client, ADXL345_REG_TAP_AXES, AXIS_EN[val]);
        if (rc < 0)
                return rc;

        // change value stored inside private data
        mutex_lock(&priv->state_lock);
        priv->tap_axis = val;
        mutex_unlock(&priv->state_lock);

        return count;
}

/**
 * @brief Shows the current mode for tapping detection
 */
static ssize_t tap_mode_show(struct device *dev,
                             struct device_attribute *attr,
                             char *buf)
{
        int rc;
        struct priv *priv = dev_get_drvdata(dev);
        enum Mode mode;

        mutex_lock(&priv->state_lock);
        mode = priv->tap_mode;
        mutex_unlock(&priv->state_lock);

        rc = sysfs_emit(buf, "%s\n", MODE[mode]);

        return rc;
}

/**
 * @brief Stores the mode for tapping detection
 */
static ssize_t tap_mode_store(struct device *dev,
                              struct device_attribute *attr,
                              const char *buf,
                              size_t count)
{
        int rc;
        struct priv *priv = dev_get_drvdata(dev);
        enum Mode mode;

        // compare which string we received to retrieve mode
        if (sysfs_streq(buf, "off")) {
                mode = OFF;
        } else if (sysfs_streq(buf, "single")) {
                mode = SINGLE;
        } else if (sysfs_streq(buf, "double")) {
                mode = DOUBLE;
        } else if (sysfs_streq(buf, "both")) {
                mode = BOTH;
        } else {
                return -EINVAL;
        }

        rc = i2c_smbus_write_byte_data(priv->client, ADXL345_REG_INT_ENABLE, MODE_EN[mode]);
        if (rc < 0)
                return rc;

        // change value stored inside private data
        mutex_lock(&priv->state_lock);
        priv->tap_mode = mode;
        mutex_unlock(&priv->state_lock);

        return count;
}

/**
 * @brief Blocks until it gets a tapping even which it'll retrieve the result
 */
static ssize_t tap_wait_show(struct device *dev,
                             struct device_attribute *attr,
                             char *buf)
{
        int rc;
        struct priv *priv = dev_get_drvdata(dev);

        // only a single thread can enter tap_wait_show
        if (atomic_cmpxchg(&priv->wait_busy, 0, 1))
                return sysfs_emit(buf, "busy\n");

        // launch completion again and wait till a tap appears
        reinit_completion(&priv->tap_happened);
        rc = wait_for_completion_interruptible(&priv->tap_happened);
        if (rc) {
                atomic_set(&priv->wait_busy, 0);
                return rc;
        }

        rc = sysfs_emit(buf, "%s\n", atomic_read(&priv->last_tap_single) ? "single" : "double");

        atomic_set(&priv->wait_busy, 0);

        return rc;
}

/**
 * @brief Blocks until it gets a tapping even which it'll retrieve the result
 */
static ssize_t tap_count_show(struct device *dev,
                             struct device_attribute *attr,
                             char *buf)
{
        int rc;
        struct priv *priv = dev_get_drvdata(dev);

        rc = sysfs_emit(buf, "%d\n", atomic_read(&priv->tap_count));

        return rc;
}

static DEVICE_ATTR_RW(tap_axis);
static DEVICE_ATTR_RW(tap_mode);
static DEVICE_ATTR_RO(tap_wait);
static DEVICE_ATTR_RO(tap_count);

// group sysfs attributes in a single sysfs group
static struct attribute *adxl345_attrs[] = {
        &dev_attr_tap_axis.attr,
        &dev_attr_tap_mode.attr,
        &dev_attr_tap_wait.attr,
        &dev_attr_tap_count.attr,
        NULL,
};

// put sysfs attributes in a group
static const struct attribute_group adxl345_attribute_group = {
        //.name = "adxl345_sysfs",
        .attrs = adxl345_attrs,
};

/*********************/
/*    FILE OPS       */
/*********************/

static int adxl345_open(struct inode *inode, struct file *filp)
{
        struct priv *priv;

        // Register private data back in filp
        priv = container_of(inode->i_cdev, struct priv, cdev);
        filp->private_data = priv;

        return nonseekable_open(inode, filp);
}

static int format_axis(char *buf, size_t size, const char *axis, int mg)
{
        char sign = mg < 0 ? '-' : '+';
        int val = abs(mg);

        return scnprintf(buf, size, "%s = %c%d.%03d",
                         axis, sign, val / 1000, val % 1000);
}

static ssize_t adxl345_read(struct file *filp, char __user *buf, size_t count, loff_t *offset)
{
        struct priv *priv;
        u8 data[6];
        s16 x_raw, y_raw, z_raw;
        int x_mg, y_mg, z_mg;
        char kbuf[128];
        int len = 0;
        int rc;

        // retrieve private data from file
        priv = filp->private_data;
        if (!priv)
                return -EINVAL;

        // offset is not allowed because not a real file
        if (*offset > 0)
                return 0;

        // read data from register
        rc = i2c_smbus_read_i2c_block_data(priv->client, ADXL345_REG_DATAX0, 6, (u8 *)data);
        if (rc < 0)
                return rc;
        if (rc != 6)
                return -EIO;

        // put data back in order ([1] is high and [0] is low)
        x_raw = (s16)((data[1] << 8) | data[0]);
        y_raw = (s16)((data[3] << 8) | data[2]);
        z_raw = (s16)((data[5] << 8) | data[4]);

        // take mg to get a better approximation (divide by 128 = 1g)
        x_mg = x_raw * 1000 / 128;
        y_mg = y_raw * 1000 / 128;
        z_mg = z_raw * 1000 / 128;

        // format output
        len += format_axis(kbuf + len, sizeof(kbuf) - len, "X", x_mg);
        len += scnprintf(kbuf + len, sizeof(kbuf) - len, "; ");
        len += format_axis(kbuf + len, sizeof(kbuf) - len, "Y", y_mg);
        len += scnprintf(kbuf + len, sizeof(kbuf) - len, "; ");
        len += format_axis(kbuf + len, sizeof(kbuf) - len, "Z", z_mg);
        len += scnprintf(kbuf + len, sizeof(kbuf) - len, "\n");

        // check len
        if (count < len)
                return -EINVAL;

        // copy to user
        if (copy_to_user(buf, kbuf, len))
                return -EFAULT;

        // update offset
        *offset += len;

        // return number of bytes copied
        return len;
}

static const struct file_operations adxl345_fops =
{
        .owner  = THIS_MODULE,
        .open   = adxl345_open,
        .read   = adxl345_read,
        .llseek = no_llseek, // cannot seek since it's single read
};

/*********************/
/*       IRQ         */
/*********************/

static irqreturn_t adxl345_irq_threaded(int irq, void *dev_id)
{
        struct priv *priv = dev_id;
        int int_source;
        int tap_status;
        enum Axis axis;

        // read status of tapping to retrieve which axis detected the tap
        tap_status = i2c_smbus_read_byte_data(priv->client, ADXL345_REG_ACT_TAP_STATUS);
        if (tap_status < 0)
                return IRQ_NONE;

        // read int source to know if it's a single or double tap (clears while reading)
        int_source = i2c_smbus_read_byte_data(priv->client, ADXL345_REG_INT_SOURCE);
        if (int_source < 0)
                return IRQ_NONE;

        // print to show which tap was detected
        if (int_source & ADXL345_DOUBLE_TAP_INT_SOURCE) {
                atomic_set(&priv->last_tap_single, 0);
                dev_info(&priv->client->dev, "Double tap detected\n");
        } else if (int_source & ADXL345_SINGLE_TAP_INT_SOURCE) {
                atomic_set(&priv->last_tap_single, 1);
                dev_info(&priv->client->dev, "Single tap detected\n");
        } else {
                dev_info(&priv->client->dev, "Received an IRQ without tap\n");
                return IRQ_NONE;
        }

        // retrieve tap axis
        mutex_lock(&priv->state_lock);
        axis = priv->tap_axis;
        mutex_unlock(&priv->state_lock);

        // print to show which axis detected the tap
        if ((tap_status & ADXL345_TAP_STATUS_X) && axis == X)
                dev_info(&priv->client->dev, "Tap on X axis\n");
        else if ((tap_status & ADXL345_TAP_STATUS_Y) && axis == Y)
                dev_info(&priv->client->dev, "Tap on Y axis\n");
        else if ((tap_status & ADXL345_TAP_STATUS_Z) && axis == Z)
                dev_info(&priv->client->dev, "Tap on Z axis\n");
        else
                dev_info(&priv->client->dev, "Tap on unknown axis\n");

        // increase number of taps detected
        atomic_inc(&priv->tap_count);

        // wake up possible reader
        complete(&priv->tap_happened);

        return IRQ_HANDLED;
}

/*********************/
/*    MODULE         */
/*********************/
static int adxl345_i2c_probe(struct i2c_client *client, const struct i2c_device_id *device_id)
{
        struct priv *priv;
        int devid;
        int power_ctl;
        int rc;

        // read devid
        devid = i2c_smbus_read_byte_data(client, ADXL345_REG_DEVID);
        if (devid < 0) {
                rc = devid;
                goto power_ctl_off;
        }

        // check if it's the correct one
        if (devid != ADXL345_DEVID_VALUE) {
                rc = -ENODEV;
                goto power_ctl_off;
        }

        dev_info(&client->dev, "DEVID is correct\n");

        // measure range +/-4g (other bytes = 0)
        rc = i2c_smbus_write_byte_data(client, ADXL345_REG_DATA_FORMAT, ADXL345_RANGE_4G);
        if (rc < 0) {
                goto power_ctl_off;
        }

        // retrieve power ctl
        power_ctl = i2c_smbus_read_byte_data(client, ADXL345_REG_POWER_CTL);
        if (power_ctl < 0) {
                rc = power_ctl;
                goto power_ctl_off;
        }

        // change mode to measure
        power_ctl |= ADXL345_POWER_MEASURE_MASK;
        rc = i2c_smbus_write_byte_data(client, ADXL345_REG_POWER_CTL, power_ctl);
        if (rc < 0) {
                goto power_ctl_off;
        }

        // write threshold for tapping detection
        rc = i2c_smbus_write_byte_data(client, ADXL345_REG_THRESH_TAP, ADXL345_VALUE_THRESH_TAP);
        if (rc < 0) {
                goto power_ctl_off;
        }

        // write duration for tapping
        rc = i2c_smbus_write_byte_data(client, ADXL345_REG_DUR, ADXL345_VALUE_DUR);
        if (rc < 0) {
                goto power_ctl_off;
        }

        // write latency for tapping
        rc = i2c_smbus_write_byte_data(client, ADXL345_REG_LATENT, ADXL345_VALUE_LATENT);
        if (rc < 0) {
                goto power_ctl_off;
        }

        // write window for tapping
        rc = i2c_smbus_write_byte_data(client, ADXL345_REG_WINDOW, ADXL345_VALUE_WINDOW);
        if (rc < 0) {
                goto power_ctl_off;
        }

        dev_info(&client->dev, "Fixed registers inside adxl345\n");

        // allocate private data
        priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
        if (priv == NULL) {
                rc = -ENOMEM;
                goto power_ctl_off;
        }

        // initialize private data members
        priv->client = client;
        init_completion(&priv->tap_happened);
        mutex_init(&priv->state_lock);
        atomic_set(&priv->tap_count, 0);
        atomic_set(&priv->wait_busy, 0);

        // store data in client device for future access
        i2c_set_clientdata(client, (void *)priv);

        // retrieve irq number
        priv->irq = client->irq;
        if (priv->irq <= 0) {
                rc = -EINVAL;
                goto power_ctl_off;
        }

        // register isr
        rc = devm_request_threaded_irq(&client->dev,
                                       priv->irq,
                                       NULL,
                                       adxl345_irq_threaded,
                                       IRQF_ONESHOT,
                                       "adxl345_irq_handler",
                                       priv);
        if (rc != 0) {
                goto power_ctl_off;
        }

        // enable z axis by default
        rc = i2c_smbus_write_byte_data(client, ADXL345_REG_TAP_AXES, ADXL345_TAP_AXES_Z);
        if (rc < 0) {
                goto power_ctl_off;
        }

        // have interrupts off by default
        rc = i2c_smbus_write_byte_data(client, ADXL345_REG_INT_ENABLE, ADXL345_INT_ENABLE_OFF);
        if (rc < 0) {
                goto power_ctl_off;
        }

        // create sysfs group entry
        rc = sysfs_create_group(&client->dev.kobj, &adxl345_attribute_group);
        if (rc) {
                dev_err(&client->dev, "Failed to create a sysfs group\n");
                goto interrupts_off;
        }

        // get major and minor from kernel
        rc = alloc_chrdev_region(
                &priv->dev_num, /* Will contain the assigned numbers */
                0,		/* First minor we request */
                1,		/* Number of minors we want */
                DEV_NAME);	/* Name of the device */
        if (rc != 0) {
                dev_err(&client->dev, "Cannot get a major/minor number pair\n");
                goto destroy_sysfs_group;
        }

        // create a class in /sys/class
        priv->dev_class = class_create(THIS_MODULE, DEV_NAME);
        if (IS_ERR(priv->dev_class)) {
                dev_err(&client->dev, "Failed to allocate device's class\n");
                rc = PTR_ERR(priv->dev_class);
                goto free_chrdev;
        }

        // initialize cdev and register file operations
        cdev_init(&priv->cdev, &adxl345_fops);
        priv->cdev.owner = THIS_MODULE;

        // add character device
        rc = cdev_add(&priv->cdev,	/* This is our handle to the cdev */
                      priv->dev_num,	/* The cdev will have this major/minor */
                      1);		/* Number of minors to be added */
        if (rc != 0) {
                dev_err(&client->dev, "Failed to add cdev\n");
                goto free_cdev;
        }

        // create device file in /dev and register it in sysfs
        priv->dev_file = device_create(priv->dev_class, /* Device's class */
                                           &client->dev,					/* Parent device */
                                           priv->dev_num,				/* Major/minor numbers */
                                           priv,						/* Pointer to private data */
                                           DEV_NAME
                                           /*"adxl345-%d",
                                           0*/);							/* Device file's name */
        if (IS_ERR(priv->dev_file)) {
                rc = PTR_ERR(priv->dev_file);
                dev_err(&client->dev, "Failed to create device file\n");
                goto delete_cdev;
        }

        dev_info(&client->dev, "Finished probe successfully\n");

        return 0;

delete_cdev:
        cdev_del(&priv->cdev);
free_cdev:
        class_destroy(priv->dev_class);
free_chrdev:
        unregister_chrdev_region(priv->dev_num, 1);
destroy_sysfs_group:
        sysfs_remove_group(&client->dev.kobj, &adxl345_attribute_group);
interrupts_off:
        i2c_smbus_write_byte_data(client, ADXL345_REG_INT_ENABLE, 0x00);
        i2c_smbus_read_byte_data(client, ADXL345_REG_INT_SOURCE); // clear pending interrupts
power_ctl_off:
        i2c_smbus_write_byte_data(client, ADXL345_REG_POWER_CTL, 0x00);

        return rc;
}

static void adxl345_i2c_remove(struct i2c_client *client)
{
        struct priv *priv;
        int rc;

        // retrieve private structure
        priv = i2c_get_clientdata(client);

        // disable interrupts
        rc = i2c_smbus_write_byte_data(client, ADXL345_REG_INT_ENABLE, 0x00);
        if (rc < 0)
                dev_warn(&client->dev, "Failed to disable interrupts\n");

        // clear pending interrupts
        i2c_smbus_read_byte_data(client, ADXL345_REG_INT_SOURCE);

        // free data created
        sysfs_remove_group(&client->dev.kobj, &adxl345_attribute_group);
        device_destroy(priv->dev_class, priv->dev_num);
        cdev_del(&priv->cdev);
        class_destroy(priv->dev_class);
        unregister_chrdev_region(priv->dev_num, 1);

        // power ctl off
        rc = i2c_smbus_write_byte_data(client, ADXL345_REG_POWER_CTL, 0x00);
        if (rc < 0)
                dev_warn(&client->dev, "Failed to put device in standby\n");

        // last message
        dev_info(&client->dev, "Finished remove successfully\n");
}

/*********************/
/*    EXTRAS         */
/*********************/
static const struct i2c_device_id adxl345_id[] = {
        { "adxl345", 0 },
        { }
};

MODULE_DEVICE_TABLE(i2c, adxl345_id);

static const struct of_device_id adxl345_of_id[] = {
        { .compatible = "adi,adxl345", },
        { }
};

MODULE_DEVICE_TABLE(of, adxl345_of_id);

static struct i2c_driver adxl345_driver = {
        .driver = {
                .name = "adxl345",
                .of_match_table = adxl345_of_id,
        },
        .probe    = adxl345_i2c_probe,
        .remove   = adxl345_i2c_remove,
        .id_table = adxl345_id,
};

module_i2c_driver(adxl345_driver);

MODULE_AUTHOR("Fabien Léger <fabien@leger-dev.swiss>");
MODULE_DESCRIPTION("ADXL345 3-Axis Digital Accelerometer I2C driver");
MODULE_LICENSE("GPL");