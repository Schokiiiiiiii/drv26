#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/types.h>
#include <linux/pm.h>
// CHAR DEVICE
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#define ADXL345_REG_DEVID        0x00
#define ADXL345_REG_POWER_CTL    0x2D
#define ADXL345_REG_DATA_FORMAT  0x31
#define ADXL345_REG_DATAX0       0x32
#define ADXL345_REG_DATAY0       0x34
#define ADXL345_REG_DATAZ0       0x36

#define ADXL345_DEVID_VALUE      0b11100101 // 0xE5

#define ADXL345_POWER_MEASURE_MASK 0b00001000

#define ADXL345_RANGE_4G         0x01

#define DEV_NAME "adxl345"

struct priv
{
        // i2c client (register, dev are inside)
        struct i2c_client *client;

        // character device
        dev_t dev_num;
        struct cdev cdev;
        struct class *dev_class;
        struct device *dev_file;
};

/*********************/
/*       SYSFS       */
/*********************/

// group sysfs attributes in a single sysfs group
static struct attribute *adxl345_attrs[] = {
        NULL,
};

// put sysfs attributes in a group
static const struct attribute_group adxl345_attribute_group = {

        .name = "adxl345_sysfs",
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

// TODO handle read to format output through scnprintf then copy with copy_to_user
static ssize_t adxl345_read(struct file *filp, char __user *buf, size_t count, loff_t *offset)
{
        struct priv *priv;
        uint16_t data[3];
        int rc;

        // retrieve private data from file
        priv = filp->private_data;

        // read data from register
        rc = i2c_smbus_read_i2c_block_data(priv->client, ADXL345_REG_DATAX0, 6, (uint8_t *)data);
        if (rc < 0)
        {
                return rc;
        }
        if (rc != 6)
        {
                return -EIO;
        }

        // copy to user
        rc = copy_to_user(buf, data, 6);
        if (rc)
        {
                return -EFAULT;
        }

        *offset += count;

        // return count for success
        return count;
}

static const struct file_operations adxl345_fops =
{
        .owner  = THIS_MODULE,
        .open   = adxl345_open,
        .read   = adxl345_read,
        .llseek = default_llseek, // Use default to enable seeking to 0
};

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
        if (devid < 0)
        {
                rc = devid;
                goto power_ctl_off;
        }

        // check if it's the correct one
        if (devid != ADXL345_DEVID_VALUE)
        {
                rc = -ENODEV;
                goto power_ctl_off;
        }

        dev_info(&client->dev, "DEVID is correct\n");

        // measure range +/-4g (other bytes = 0)
        rc = i2c_smbus_write_byte_data(client, ADXL345_REG_DATA_FORMAT, ADXL345_RANGE_4G);
        if (rc < 0)
        {
                goto power_ctl_off;
        }

        dev_info(&client->dev, "Fixed measure range to +/-4g\n");

        // retrieve power ctl
        power_ctl = i2c_smbus_read_byte_data(client, ADXL345_REG_POWER_CTL);
        if (power_ctl < 0)
        {
                rc = power_ctl;
                goto power_ctl_off;
        }

        // change mode to measure
        power_ctl |= ADXL345_POWER_MEASURE_MASK;
        rc = i2c_smbus_write_byte_data(client, ADXL345_REG_POWER_CTL, power_ctl);
        if (rc < 0)
        {
                goto power_ctl_off;
        }

        dev_info(&client->dev, "Fixed power ctl measure to 1\n");

        // allocate private data
        priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
        if (priv == NULL)
        {
                goto power_ctl_off;
        }

        // initialize private data members
        priv->client = client;

        // store data in client device for future access
        i2c_set_clientdata(client, (void *)priv);

        // create sysfs group entry
        rc = sysfs_create_group(&client->dev.kobj, &adxl345_attribute_group);
        if (rc) {
                dev_err(&client->dev, "Failed to create a sysfs group\n");
                goto power_ctl_off;
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
                                           "adxl345-%d",
                                           0);							/* Device file's name */
        if (IS_ERR(priv->dev_file)) {
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

        // free data created
        device_destroy(priv->dev_class, priv->dev_num);
        cdev_del(&priv->cdev);
        class_destroy(priv->dev_class);
        unregister_chrdev_region(priv->dev_num, 1);
        sysfs_remove_group(&client->dev.kobj, &adxl345_attribute_group);

        // power ctl off
        rc = i2c_smbus_write_byte_data(client, ADXL345_REG_POWER_CTL, 0x00);
        if (rc < 0)
                dev_warn(&client->dev, "Failed to put device in standby\n");
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