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

/* Offsets for the registers detailed in the documentation */
#define LED_REG_OFF        0x0000 // leds
#define SW_REG_OFF         0x0040 // switches
#define KEY_DATA_OFF       0x0050 // data for keys
#define KEY_MASK_OFF       0x0058 // mask for interrupts
#define KEY_EDGE_OFF       0x005C // capture of interrupts

/* Disable the interrupt */
#define INT_DISABLE	 0x00
#define INT_ENABLE	 0x07 // 0b0111 for keys 0, 1, 2

/* Keys masks */
#define KEY0 0x01
#define KEY1 0x02
#define KEY2 0x04

/**
 * @brief private structure for data shared accross functions accessible through plateform device
 *
 * mem_ptr pointer over memory mapped via ioremap
 * irq_num irq number that is handled
 * dev pointer towards the device (useful for dev_info())
 */
struct priv {
	void __iomem *mem_ptr;
	int irq_num;
	struct device *dev;
};

/**
 * @brief Read a register from the REDS-adder device.
 *
 * @param priv pointer to driver's private data
 * @param reg_offset offset (in bytes) of the desired register
 *
 * @return value read from the specified register.
 */
static int ra_read(struct priv const *const priv, int const reg_offset) {

	// stacktrace when condition is true
	WARN_ON(priv == NULL);
	WARN_ON(priv->mem_ptr == NULL);

	// write inside the register (divided by 4 because int)
	return ioread32((int *)priv->mem_ptr + (reg_offset / 4));
}

/**
 * @brief Write an integer value to a register of the REDS-adder device.
 *
 * @param priv pointer to driver's private data
 * @param reg_offset offset (in bytes) of the desired register
 * @param value value that has to be written
 */
static void ra_write(struct priv const *const priv, int const reg_offset, int const value) {

	// stacktrace when condition is true
	WARN_ON(priv == NULL);
	WARN_ON(priv->mem_ptr == NULL);

	// write inside the register (divided by 4 because int)
	iowrite32(value, (int *)priv->mem_ptr + (reg_offset / 4));
}

/**
 * @brief Handles IRQs for switch_copy
 *
 * @param irq received
 * @param data pointer over the struct priv
 *
 * @return irqreturn_t to inform how the interrupt was handled
 */
static irqreturn_t irq_handler(int irq, void *data) {

	int edge;									// read edge capture to see which interrupts activated
	struct priv *priv = (struct priv *)data;	// cast private data back for access to private data

	dev_info(priv->dev, "IRQ %d received\n", irq);

	// read interrupt
	edge = ra_read(priv, KEY_EDGE_OFF);
	if (!edge)
		return IRQ_NONE;

	// clear interrupt
	ra_write(priv, KEY_EDGE_OFF, edge);

	dev_info(priv->dev, "Edge value is %d\n", edge);

	// handle depending on key pressed
	if (edge & KEY0) {
		int sw = ra_read(priv, SW_REG_OFF);
		ra_write(priv, LED_REG_OFF, sw);
	} else if (edge & KEY1) {
		int leds = ra_read(priv, LED_REG_OFF);
		ra_write(priv, LED_REG_OFF, leds >> 1);
	} else if (edge & KEY2) {
		int leds = ra_read(priv, LED_REG_OFF);
		ra_write(priv, LED_REG_OFF, leds << 1);
	}

	// inform kernel everything went well
	return IRQ_HANDLED;
}

/**
 * @brief Initializes the driver
 *
 * @param pdev plateform device
 *
 * @return 0 if it went well, an error code otherwise
 */
static int switch_copy_probe(struct platform_device *pdev) {

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

	// retrieve the address of the register's region from the DT
	mem_info = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (unlikely(!mem_info)) {
		dev_err(&pdev->dev, "Failed to get memory resource from device tree !\n");
		rc = -EINVAL;
		goto return_fail;
	}

	dev_info(&pdev->dev, "Successfully retrieved the IOMEM address from the DT\n");

	// remap physical addresse retrieved into a virtual one
	priv->mem_ptr = devm_ioremap_resource(priv->dev, mem_info);
	if (IS_ERR(priv->mem_ptr)) {
		dev_err(&pdev->dev, "Failed to map memory!\n");
		rc = PTR_ERR(priv->mem_ptr);
		goto return_fail;
	}

	dev_info(&pdev->dev, "IOMEM address remapped\n");

	// retrieve IRQ number from DT
	priv->irq_num = platform_get_irq(pdev, 0);
	if (priv->irq_num < 0) {
		dev_err(&pdev->dev,
			"Failed to get interrupt resource from device tree!\n");
		rc = -EINVAL;
		goto return_fail;
	}

	// register ISR function for the IRQ
	rc = devm_request_irq(
		&pdev->dev,					/* Our device */
		priv->irq_num,				/* IRQ number */
		&irq_handler,				/* ISR */
		IRQF_SHARED,             	/* Flags */
		"switch_copy_irq_handler",	/* Name in /proc/interrupts */
		(void *)priv				/* Access to private data in IRQ handler */
	);
	if (rc != 0) {
		dev_err(&pdev->dev,
			"switch_copy_irq_handler: cannot register IRQ, error code: %d\n",
			rc);
		goto return_fail;
	}

	dev_info(&pdev->dev, "Successfully registered the interrupt handler\n");

	dev_info(&pdev->dev, "Setting up initial values for the registers...\n");

	ra_write(priv, KEY_EDGE_OFF, 0xF); // clear pending key interrupts
	ra_write(priv, KEY_MASK_OFF, 0x7); // enable KEY0, KEY1, KEY2 interrupts

	dev_info(&pdev->dev, "switch_copy ready !\n");

	return 0;

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
static int switch_copy_remove(struct platform_device *pdev) {

	// retrieve private data from plateform device structure
	struct priv *priv = platform_get_drvdata(pdev);

	dev_info(&pdev->dev, "Turning leds off, clearing and disabling interrupts...\n");

	// clear everything
	ra_write(priv, LED_REG_OFF, 0x0);
	ra_write(priv, KEY_EDGE_OFF, 0xF);
	ra_write(priv, KEY_MASK_OFF, INT_DISABLE);

	dev_info(&pdev->dev, "Driver removed\n");

	return 0;
}

static const struct of_device_id switch_copy_driver_id[] = {
	{ .compatible = "drv2026" },
	{ /* END */ },
};

MODULE_DEVICE_TABLE(of, switch_copy_driver_id);

static struct platform_driver switch_copy_driver = {
	.driver = {
		.name = "drv-lab4",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(switch_copy_driver_id),
	},
	.probe = switch_copy_probe,
	.remove = switch_copy_remove,
};

module_platform_driver(switch_copy_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("REDS");
MODULE_DESCRIPTION("Introduction to the interrupt and platform drivers");
