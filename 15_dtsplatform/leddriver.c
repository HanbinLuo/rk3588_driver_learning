/***************************************************************
Copyright (C) 2026 lhb.
文件名		: leddriver.c
描述		: RK3588设备树platform LED驱动，支持4个GPIO LED
***************************************************************/
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/gpio.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <asm/uaccess.h>

#define LEDDEV_CNT		1
#define LEDDEV_NAME		"dtsplatled"
#define LED_NUM			4
#define LEDOFF			0
#define LEDON			1

struct led_data {
	int id;
	int gpio;
	int active_low;
	const char *label;
};

struct leddev_dev {
	dev_t devid;
	struct cdev cdev;
	struct class *class;
	struct device *device;
	struct device_node *node;
	struct led_data leds[LED_NUM];
	int led_num;
};

static struct leddev_dev leddev;

static void led_set_value(struct led_data *led, int state)
{
	int value = state == LEDON ? 1 : 0;

	if (led->active_low)
		value = !value;

	gpio_set_value(led->gpio, value);
}

static void led_free_all(struct leddev_dev *dev)
{
	int i;

	for (i = 0; i < dev->led_num; i++) {
		if (gpio_is_valid(dev->leds[i].gpio)) {
			led_set_value(&dev->leds[i], LEDOFF);
			gpio_free(dev->leds[i].gpio);
		}
	}

	dev->led_num = 0;
}

static int led_parse_one(struct device_node *child, int index)
{
	int ret;
	enum of_gpio_flags gpio_flags;
	struct led_data *data = &leddev.leds[index];

	data->id = index;
	data->gpio = of_get_named_gpio_flags(child, "gpios", 0, &gpio_flags);
	if (!gpio_is_valid(data->gpio)) {
		printk("dtsplatled: can't get led%d gpios\r\n", index);
		return -EINVAL;
	}

	ret = of_property_read_string(child, "label", &data->label);
	if (ret < 0)
		data->label = child->name;

	data->active_low = !!(gpio_flags & OF_GPIO_ACTIVE_LOW);

	ret = gpio_request(data->gpio, data->label);
	if (ret) {
		printk("dtsplatled: request %s gpio failed\r\n", data->label);
		return ret;
	}

	ret = gpio_direction_output(data->gpio, data->active_low ? 1 : 0);
	if (ret) {
		printk("dtsplatled: set %s output failed\r\n", data->label);
		gpio_free(data->gpio);
		return ret;
	}

	printk("dtsplatled: %s id=%d gpio=%d active_low=%d\r\n",
	       data->label, data->id, data->gpio, data->active_low);
	return 0;
}

static int led_parse_dt(struct device_node *node)
{
	int ret;
	int i = 0;
	struct device_node *child;

	leddev.node = node;

	for_each_available_child_of_node(node, child) {
		if (i >= LED_NUM)
			break;

		ret = led_parse_one(child, i);
		if (ret) {
			of_node_put(child);
			led_free_all(&leddev);
			return ret;
		}

		i++;
		leddev.led_num = i;
	}

	if (!leddev.led_num)
		return -EINVAL;

	return 0;
}

static int led_open(struct inode *inode, struct file *filp)
{
	filp->private_data = &leddev;
	return 0;
}

static ssize_t led_write(struct file *filp, const char __user *buf,
			 size_t cnt, loff_t *offt)
{
	unsigned char databuf[2];
	unsigned char led_id;
	unsigned char led_state;
	struct leddev_dev *dev = filp->private_data;

	if (cnt < sizeof(databuf))
		return -EINVAL;

	if (copy_from_user(databuf, buf, sizeof(databuf)))
		return -EFAULT;

	led_id = databuf[0];
	led_state = databuf[1];

	if (led_id >= dev->led_num)
		return -EINVAL;

	if (led_state != LEDON && led_state != LEDOFF)
		return -EINVAL;

	led_set_value(&dev->leds[led_id], led_state);
	return sizeof(databuf);
}

static const struct file_operations led_fops = {
	.owner = THIS_MODULE,
	.open = led_open,
	.write = led_write,
};

static int led_probe(struct platform_device *pdev)
{
	int ret;

	printk("dtsplatled: platform driver and device matched\r\n");

	ret = led_parse_dt(pdev->dev.of_node);
	if (ret)
		return ret;

	ret = alloc_chrdev_region(&leddev.devid, 0, LEDDEV_CNT, LEDDEV_NAME);
	if (ret < 0)
		goto free_leds;

	leddev.cdev.owner = THIS_MODULE;
	cdev_init(&leddev.cdev, &led_fops);

	ret = cdev_add(&leddev.cdev, leddev.devid, LEDDEV_CNT);
	if (ret < 0)
		goto del_unregister;

	leddev.class = class_create(THIS_MODULE, LEDDEV_NAME);
	if (IS_ERR(leddev.class)) {
		ret = PTR_ERR(leddev.class);
		goto del_cdev;
	}

	leddev.device = device_create(leddev.class, NULL, leddev.devid,
				      NULL, LEDDEV_NAME);
	if (IS_ERR(leddev.device)) {
		ret = PTR_ERR(leddev.device);
		goto destroy_class;
	}

	return 0;

destroy_class:
	class_destroy(leddev.class);
del_cdev:
	cdev_del(&leddev.cdev);
del_unregister:
	unregister_chrdev_region(leddev.devid, LEDDEV_CNT);
free_leds:
	led_free_all(&leddev);
	return ret;
}

static int led_remove(struct platform_device *pdev)
{
	device_destroy(leddev.class, leddev.devid);
	class_destroy(leddev.class);
	cdev_del(&leddev.cdev);
	unregister_chrdev_region(leddev.devid, LEDDEV_CNT);
	led_free_all(&leddev);
	return 0;
}

static const struct of_device_id led_of_match[] = {
	{ .compatible = "lhb,gpio-leds" },
	{ /* Sentinel */ }
};

MODULE_DEVICE_TABLE(of, led_of_match);

static struct platform_driver led_driver = {
	.driver = {
		.name = "rk3588-gpio-leds",
		.of_match_table = led_of_match,
	},
	.probe = led_probe,
	.remove = led_remove,
};

module_platform_driver(led_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("lhb");
MODULE_INFO(intree, "Y");
