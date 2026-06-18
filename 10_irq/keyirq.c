/***************************************************************
Copyright (C) 2026 lhb.
文件名		: keyirq.c
描述		: RK3588 GPIO按键中断实验，支持最多4个按键
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
#include <linux/of_irq.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <asm/uaccess.h>

#define KEY_CNT			1
#define KEY_NAME		"key"
#define KEY_NUM			3
#define KEY_DEBOUNCE_MS		15

enum key_status {
	KEY_PRESS = 0,
	KEY_RELEASE,
	KEY_KEEP,
};

struct key_event {
	int id;
	int status;
};

struct key_data {
	int id;
	int gpio;
	int irq_num;
	int active_low;
	int last_pressed;
	const char *label;
	struct timer_list timer;
	struct key_dev *parent;
};

struct key_dev {
	dev_t devid;
	struct cdev cdev;
	struct class *class;
	struct device *device;
	struct device_node *nd;
	struct key_data keys[KEY_NUM];
	int key_num;
	struct key_event event;
	int has_event;
	spinlock_t spinlock;
	wait_queue_head_t waitq;
};

static struct key_dev key;

static irqreturn_t key_interrupt(int irq, void *dev_id)
{
	struct key_data *data = dev_id;

	mod_timer(&data->timer, jiffies + msecs_to_jiffies(KEY_DEBOUNCE_MS));
	return IRQ_HANDLED;
}

static void key_timer_function(struct timer_list *arg)
{
	struct key_data *data = from_timer(data, arg, timer);
	struct key_dev *dev = data->parent;
	unsigned long flags;
	int value;
	int pressed;
	int status;

	value = gpio_get_value(data->gpio);
	pressed = data->active_low ? !value : value;

	if (pressed && !data->last_pressed)
		status = KEY_PRESS;
	else if (!pressed && data->last_pressed)
		status = KEY_RELEASE;
	else
		status = KEY_KEEP;

	data->last_pressed = pressed;

	if (status == KEY_KEEP)
		return;

	spin_lock_irqsave(&dev->spinlock, flags);
	dev->event.id = data->id;
	dev->event.status = status;
	dev->has_event = 1;
	spin_unlock_irqrestore(&dev->spinlock, flags);

	wake_up_interruptible(&dev->waitq);
}

static void key_free_all(struct key_dev *dev)
{
	int i;

	for (i = 0; i < dev->key_num; i++) {
		del_timer_sync(&dev->keys[i].timer);

		if (dev->keys[i].irq_num > 0)
			free_irq(dev->keys[i].irq_num, &dev->keys[i]);

		if (gpio_is_valid(dev->keys[i].gpio))
			gpio_free(dev->keys[i].gpio);
	}

	dev->key_num = 0;
}

static int key_parse_one(struct device_node *child, int index)
{
	int ret;
	unsigned long irq_flags;
	enum of_gpio_flags gpio_flags;
	struct key_data *data = &key.keys[index];

	data->id = index;
	data->parent = &key;
	data->gpio = of_get_named_gpio_flags(child, "gpios", 0, &gpio_flags);
	if (!gpio_is_valid(data->gpio)) {
		printk("keyirq: can't get key%d gpios\r\n", index);
		return -EINVAL;
	}

	ret = of_property_read_string(child, "label", &data->label);
	if (ret < 0)
		data->label = child->name;

	data->active_low = !!(gpio_flags & OF_GPIO_ACTIVE_LOW);

	ret = gpio_request(data->gpio, data->label);
	if (ret) {
		printk("keyirq: request %s gpio failed\r\n", data->label);
		return ret;
	}

	ret = gpio_direction_input(data->gpio);
	if (ret) {
		printk("keyirq: set %s input failed\r\n", data->label);
		goto free_gpio;
	}

	data->irq_num = irq_of_parse_and_map(child, 0);
	if (!data->irq_num)
		data->irq_num = gpio_to_irq(data->gpio);
	if (data->irq_num < 0) {
		ret = data->irq_num;
		printk("keyirq: get %s irq failed\r\n", data->label);
		goto free_gpio;
	}

	irq_flags = irq_get_trigger_type(data->irq_num);
	if (irq_flags == IRQF_TRIGGER_NONE)
		irq_flags = IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING;

	data->last_pressed = data->active_low ?
			     !gpio_get_value(data->gpio) :
			     gpio_get_value(data->gpio);
	timer_setup(&data->timer, key_timer_function, 0);

	ret = request_irq(data->irq_num, key_interrupt, irq_flags,
			  data->label, data);
	if (ret) {
		printk("keyirq: request %s irq failed\r\n", data->label);
		goto free_gpio;
	}

	printk("keyirq: %s gpio=%d irq=%d active_low=%d\r\n",
	       data->label, data->gpio, data->irq_num, data->active_low);
	return 0;

free_gpio:
	gpio_free(data->gpio);
	return ret;
}

static int key_parse_dt(void)
{
	int ret;
	int i = 0;
	struct device_node *child;

	key.nd = of_find_node_by_path("/lhb-keys");
	if (key.nd == NULL) {
		printk("keyirq: lhb-keys node not found!\r\n");
		return -EINVAL;
	}

	if (!of_device_is_available(key.nd)) {
		printk("keyirq: lhb-keys node is disabled!\r\n");
		return -EINVAL;
	}

	if (!of_device_is_compatible(key.nd, "lhb,gpio-keys")) {
		printk("keyirq: compatible match failed\r\n");
		return -EINVAL;
	}

	for_each_available_child_of_node(key.nd, child) {
		if (i >= KEY_NUM)
			break;

		ret = key_parse_one(child, i);
		if (ret) {
			of_node_put(child);
			key_free_all(&key);
			return ret;
		}

		i++;
		key.key_num = i;
	}

	if (!key.key_num)
		return -EINVAL;

	return 0;
}

static int key_open(struct inode *inode, struct file *filp)
{
	unsigned long flags;

	filp->private_data = &key;

	/* Drop stale events generated before this app opened /dev/key. */
	spin_lock_irqsave(&key.spinlock, flags);
	key.event.id = -1;
	key.event.status = KEY_KEEP;
	key.has_event = 0;
	spin_unlock_irqrestore(&key.spinlock, flags);

	return 0;
}

static ssize_t key_read(struct file *filp, char __user *buf,
			size_t cnt, loff_t *offt)
{
	struct key_dev *dev = filp->private_data;
	struct key_event event;
	unsigned long flags;
	int ret;

	if (cnt < sizeof(event))
		return -EINVAL;

	ret = wait_event_interruptible(dev->waitq, dev->has_event);
	if (ret)
		return ret;

	spin_lock_irqsave(&dev->spinlock, flags);
	event = dev->event;
	dev->has_event = 0;
	spin_unlock_irqrestore(&dev->spinlock, flags);

	if (copy_to_user(buf, &event, sizeof(event)))
		return -EFAULT;

	return sizeof(event);
}

static int key_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static const struct file_operations key_fops = {
	.owner = THIS_MODULE,
	.open = key_open,
	.read = key_read,
	.release = key_release,
};

static int __init mykey_init(void)
{
	int ret;

	spin_lock_init(&key.spinlock);
	init_waitqueue_head(&key.waitq);
	key.event.id = -1;
	key.event.status = KEY_KEEP;
	key.has_event = 0;

	ret = key_parse_dt();
	if (ret)
		return ret;

	ret = alloc_chrdev_region(&key.devid, 0, KEY_CNT, KEY_NAME);
	if (ret < 0) {
		pr_err("%s Couldn't alloc_chrdev_region, ret=%d\r\n",
		       KEY_NAME, ret);
		goto free_keys;
	}

	key.cdev.owner = THIS_MODULE;
	cdev_init(&key.cdev, &key_fops);

	ret = cdev_add(&key.cdev, key.devid, KEY_CNT);
	if (ret < 0)
		goto del_unregister;

	key.class = class_create(THIS_MODULE, KEY_NAME);
	if (IS_ERR(key.class)) {
		ret = PTR_ERR(key.class);
		goto del_cdev;
	}

	key.device = device_create(key.class, NULL, key.devid, NULL, KEY_NAME);
	if (IS_ERR(key.device)) {
		ret = PTR_ERR(key.device);
		goto destroy_class;
	}

	return 0;

destroy_class:
	class_destroy(key.class);
del_cdev:
	cdev_del(&key.cdev);
del_unregister:
	unregister_chrdev_region(key.devid, KEY_CNT);
free_keys:
	key_free_all(&key);
	return ret;
}

static void __exit mykey_exit(void)
{
	device_destroy(key.class, key.devid);
	class_destroy(key.class);
	cdev_del(&key.cdev);
	unregister_chrdev_region(key.devid, KEY_CNT);
	key_free_all(&key);
}

module_init(mykey_init);
module_exit(mykey_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("lhb");
MODULE_INFO(intree, "Y");
