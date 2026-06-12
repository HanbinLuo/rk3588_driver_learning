#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>

#define LED_CNT 4
#define LED_NAME "led"
#define LEDOFF 0
#define LEDON 1

struct led_data {
	struct cdev cdev;
	struct device *device;
	int gpio;
	bool gpio_requested;
	bool cdev_added;
	char name[8];
};

struct led_dev {
	dev_t devid;
	struct class *class;
	int major;
	int count;
	struct led_data leds[LED_CNT];
};

static struct led_dev leddev;

static int led_open(struct inode *inode, struct file *filp)
{
	struct led_data *led;

	led = container_of(inode->i_cdev, struct led_data, cdev);
	filp->private_data = led;

	return 0;
}

static ssize_t led_write(struct file *filp, const char __user *buf,
			 size_t cnt, loff_t *offt)
{
	unsigned char ledstat;
	struct led_data *led = filp->private_data;

	if (cnt < 1)
		return -EINVAL;

	if (copy_from_user(&ledstat, buf, 1))
		return -EFAULT;

	switch (ledstat) {
	case LEDON:
		gpio_set_value(led->gpio, 1);
		break;
	case LEDOFF:
		gpio_set_value(led->gpio, 0);
		break;
	default:
		return -EINVAL;
	}

	return 1;
}

static const struct file_operations led_fops = {
	.owner = THIS_MODULE,
	.open = led_open,
	.write = led_write,
};

static void led_destroy_devices(struct led_dev *dev)
{
	int i;

	for (i = 0; i < dev->count; i++) {
		if (dev->leds[i].gpio_requested)
			gpio_set_value(dev->leds[i].gpio, 0);

		if (dev->leds[i].device)
			device_destroy(dev->class, MKDEV(dev->major, i));

		if (dev->leds[i].cdev_added)
			cdev_del(&dev->leds[i].cdev);

		if (dev->leds[i].gpio_requested)
			gpio_free(dev->leds[i].gpio);
	}

	dev->count = 0;
}

static int led_create_one(struct platform_device *pdev,
			  struct device_node *child, int index)
{
	int ret;
	struct led_data *led = &leddev.leds[index];

	snprintf(led->name, sizeof(led->name), "led%d", index);

	led->gpio = of_get_named_gpio(child, "gpios", 0);
	if (!gpio_is_valid(led->gpio)) {
		dev_err(&pdev->dev, "can't get %s gpio\n", led->name);
		return -EINVAL;
	}

	ret = gpio_request(led->gpio, led->name);
	if (ret) {
		dev_err(&pdev->dev, "request %s gpio failed\n", led->name);
		return ret;
	}
	led->gpio_requested = true;

	ret = gpio_direction_output(led->gpio, 0);
	if (ret) {
		dev_err(&pdev->dev, "set %s gpio output failed\n", led->name);
		return ret;
	}

	cdev_init(&led->cdev, &led_fops);
	led->cdev.owner = THIS_MODULE;

	ret = cdev_add(&led->cdev, MKDEV(leddev.major, index), 1);
	if (ret)
		return ret;
	led->cdev_added = true;

	led->device = device_create(leddev.class, NULL,
				    MKDEV(leddev.major, index),
				    NULL, led->name);
	if (IS_ERR(led->device)) {
		ret = PTR_ERR(led->device);
		led->device = NULL;
		return ret;
	}

	dev_info(&pdev->dev, "/dev/%s gpio=%d\n", led->name, led->gpio);

	return 0;
}

static int led_probe(struct platform_device *pdev)
{
	int i = 0;
	int ret;
	struct device_node *child;

	ret = alloc_chrdev_region(&leddev.devid, 0, LED_CNT, LED_NAME);
	if (ret < 0)
		return ret;

	leddev.major = MAJOR(leddev.devid);

	leddev.class = class_create(THIS_MODULE, LED_NAME);
	if (IS_ERR(leddev.class)) {
		ret = PTR_ERR(leddev.class);
		leddev.class = NULL;
		goto unregister_chrdev;
	}

	for_each_available_child_of_node(pdev->dev.of_node, child) {
		if (i >= LED_CNT)
			break;

		ret = led_create_one(pdev, child, i);
		if (ret) {
			of_node_put(child);
			goto destroy_devices;
		}

		leddev.count++;
		i++;
	}

	if (leddev.count != LED_CNT) {
		dev_err(&pdev->dev, "need %d leds, only found %d\n",
			LED_CNT, leddev.count);
		ret = -EINVAL;
		goto destroy_devices;
	}

	platform_set_drvdata(pdev, &leddev);

	return 0;

destroy_devices:
	led_destroy_devices(&leddev);
	class_destroy(leddev.class);
	leddev.class = NULL;
unregister_chrdev:
	unregister_chrdev_region(leddev.devid, LED_CNT);
	leddev.devid = 0;
	return ret;
}

static int led_remove(struct platform_device *pdev)
{
	struct led_dev *dev = platform_get_drvdata(pdev);

	led_destroy_devices(dev);
	class_destroy(dev->class);
	unregister_chrdev_region(dev->devid, LED_CNT);

	return 0;
}

static const struct of_device_id led_of_match[] = {
	{ .compatible = "lhb,gpio-leds" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, led_of_match);

static struct platform_driver led_driver = {
	.probe = led_probe,
	.remove = led_remove,
	.driver = {
		.name = "lhb-gpio-leds",
		.of_match_table = led_of_match,
	},
};

module_platform_driver(led_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("lhb");
MODULE_DESCRIPTION("GPIO LEDs char driver");
MODULE_INFO(intree, "Y");
