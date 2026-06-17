#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/gpio.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>
#include <linux/semaphore.h>
#include <linux/timer.h>
//#include <asm/mach/map.h>
#include <asm/uaccess.h>
#include <asm/io.h>
/***************************************************************
Copyright © ALIENTEK Co., Ltd. 1998-2029. All rights reserved.
文件名		: timer.c
作者	  	: 正点原子Linux团队
版本	   	: V1.0
描述	   	: Linux内核定时器实验
其他	   	: 无
论坛 	   	: www.openedv.com
日志	   	: 初版V1.0 2022/12/24 正点原子Linux团队创建
***************************************************************/
#define TIMER_CNT		1		/* 设备号个数 	*/
#define TIMER_NAME		"timer"	/* 名字 		*/
#define CLOSE_CMD 		(_IO(0XEF, 0x1))	/* 关闭定时器 */
#define OPEN_CMD		(_IO(0XEF, 0x2))	/* 打开定时器 */
#define SETPERIOD_CMD	(_IO(0XEF, 0x3))	/* 设置定时器周期命令 */
#define LEDON 			1		/* 开灯 */
#define LEDOFF 			0		/* 关灯 */
#define LED_CNT			4		/* LED数量 */

/* timer设备结构体 */
struct timer_dev{
	dev_t devid;			/* 设备号 	 */
	struct cdev cdev;		/* cdev 	*/
	struct class *class;	/* 类 		*/
	struct device *device;	/* 设备 	 */
	int major;				/* 主设备号	  */
	int minor;				/* 次设备号   */
	struct device_node	*nd; /* 设备节点 */
	int led_gpio[LED_CNT];	/* LED所使用的GPIO编号 */
	int led_count;			/* 成功申请的LED数量 */
	int timeperiod; 		/* 定时周期,单位为ms */
	struct timer_list timer;/* 定义一个定时器*/
	spinlock_t lock;		/* 定义自旋锁 */
};

struct timer_dev timerdev;	/* timer设备 */

/*
 * @description	: 初始化LED灯IO，open函数打开驱动的时候
 * 				  初始化LED灯所使用的GPIO引脚。
 * @param 		: 无
 * @return 		: 无
 */
static int led_init(void)
{
	int i = 0;
	int ret;
	struct device_node *child;
	char name[16];

	if (timerdev.led_count)
		return 0;

	/* 1、获取设备节点：compatible = "lhb,gpio-leds" */
	timerdev.nd = of_find_compatible_node(NULL, NULL, "lhb,gpio-leds");
	if (timerdev.nd == NULL) {
		printk("timerdev: gpio-leds node not found!\r\n");
		return -EINVAL;
	}

	if (!of_device_is_available(timerdev.nd)) {
		printk("timerdev: gpio-leds node is disabled!\r\n");
		return -EINVAL;
	}

	/* 2、遍历 led0~led3 子节点，读取每个子节点的 gpios 属性 */
	for_each_available_child_of_node(timerdev.nd, child) {
		if (i >= LED_CNT)
			break;

		timerdev.led_gpio[i] = of_get_named_gpio(child, "gpios", 0);
		if (!gpio_is_valid(timerdev.led_gpio[i])) {
			printk("timerdev: can't get led%d gpio\r\n", i);
			ret = -EINVAL;
			goto free_gpio;
		}

		snprintf(name, sizeof(name), "timer-led%d", i);
		ret = gpio_request(timerdev.led_gpio[i], name);
		if (ret) {
			printk("timerdev: request led%d gpio failed, ret=%d\r\n",
			       i, ret);
			goto free_gpio;
		}

		ret = gpio_direction_output(timerdev.led_gpio[i], LEDOFF);
		if (ret) {
			printk("timerdev: set led%d gpio output failed\r\n", i);
			gpio_free(timerdev.led_gpio[i]);
			goto free_gpio;
		}

		printk("timerdev: led%d gpio num = %d\r\n",
		       i, timerdev.led_gpio[i]);
		i++;
	}

	timerdev.led_count = i;
	if (timerdev.led_count != LED_CNT) {
		printk("timerdev: need %d leds, only found %d\r\n",
		       LED_CNT, timerdev.led_count);
		ret = -EINVAL;
		goto free_gpio;
	}

	return 0;

free_gpio:
	while (i--)
		gpio_free(timerdev.led_gpio[i]);
	timerdev.led_count = 0;
	return ret;
}

static void led_set_all(struct timer_dev *dev, int value)
{
	int i;

	for (i = 0; i < dev->led_count; i++)
		gpio_set_value(dev->led_gpio[i], value);
}

static void led_free_all(struct timer_dev *dev)
{
	int i;

	led_set_all(dev, LEDOFF);

	for (i = 0; i < dev->led_count; i++)
		gpio_free(dev->led_gpio[i]);

	dev->led_count = 0;
}

/*
 * @description		: 打开设备
 * @param - inode 	: 传递给驱动的inode
 * @param - filp 	: 设备文件，file结构体有个叫做private_data的成员变量
 * 					  一般在open的时候将private_data指向设备结构体。
 * @return 			: 0 成功;其他 失败
 */
static int timer_open(struct inode *inode, struct file *filp)
{
	int ret = 0;
	filp->private_data = &timerdev;	/* 设置私有数据 */

	timerdev.timeperiod = 1000;		/* 默认周期为1s */
	ret = led_init();				/* 初始化LED IO */
	if (ret < 0) {
		return ret;
	}

	return 0;
}

/*
 * @description		: ioctl函数，
 * @param - filp 	: 要打开的设备文件(文件描述符)
 * @param - cmd 	: 应用程序发送过来的命令
 * @param - arg 	: 参数
 * @return 			: 0 成功;其他 失败
 */
static long timer_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct timer_dev *dev =  (struct timer_dev *)filp->private_data;
	int timerperiod;
	unsigned long flags;
	
	switch (cmd) {
		case CLOSE_CMD:		/* 关闭定时器 */
			del_timer_sync(&dev->timer);
			break;
		case OPEN_CMD:		/* 打开定时器 */
			spin_lock_irqsave(&dev->lock, flags);
			timerperiod = dev->timeperiod;
			spin_unlock_irqrestore(&dev->lock, flags);
			mod_timer(&dev->timer, jiffies + msecs_to_jiffies(timerperiod));
			break;
		case SETPERIOD_CMD: /* 设置定时器周期 */
			spin_lock_irqsave(&dev->lock, flags);
			dev->timeperiod = arg;
			spin_unlock_irqrestore(&dev->lock, flags);
			mod_timer(&dev->timer, jiffies + msecs_to_jiffies(arg));
			break;
		default:
			break;
	}
	return 0;
}

/*
 * @description		: 关闭/释放设备
 * @param - filp 	: 要关闭的设备文件(文件描述符)
 * @return 			: 0 成功;其他 失败
 */
static int led_release(struct inode *inode, struct file *filp)
{
	struct timer_dev *dev = filp->private_data;
	del_timer_sync(&dev->timer);		/* 关闭定时器 			*/
	led_free_all(dev);					/* APP结束的时候关闭并释放LED */

	return 0;
}

/* 设备操作函数 */
static struct file_operations timer_fops = {
	.owner = THIS_MODULE,
	.open = timer_open,
	.unlocked_ioctl = timer_unlocked_ioctl,
	.release = 	led_release,
};

/* 定时器回调函数 */
void timer_function(struct timer_list *arg)
{
	/* 	from_timer是个宏，可以根据结构体的成员地址，获取到这个结构体的首地址。
		第一个参数表示结构体，第二个参数表示第一个参数里的一个成员，第三个参数表示第二个参数的类型,得到第一个参数的首地址。
	*/
	struct timer_dev *dev = from_timer(dev, arg, timer);
	static int sta = 1;
	int timerperiod;
	unsigned long flags;

	sta = !sta;		/* 每次都取反，实现LED灯反转 */
	led_set_all(dev, sta);
	printk("timer_function: LED state changed to %d\n", sta);
	
	/* 重启定时器 */
	spin_lock_irqsave(&dev->lock, flags);
	timerperiod = dev->timeperiod;
	spin_unlock_irqrestore(&dev->lock, flags);
	mod_timer(&dev->timer, jiffies + msecs_to_jiffies(timerperiod)); 
 }

/*
 * @description	: 驱动入口函数
 * @param 		: 无
 * @return 		: 无
 */
static int __init timer_init(void)
{
	int ret;
	
	/* 初始化自旋锁 */
	spin_lock_init(&timerdev.lock);

	/* 注册字符设备驱动 */
	/* 1、创建设备号 */
	if (timerdev.major) {		/*  定义了设备号 */
		timerdev.devid = MKDEV(timerdev.major, 0);
		ret = register_chrdev_region(timerdev.devid, TIMER_CNT, TIMER_NAME);
		if(ret < 0) {
			pr_err("cannot register %s char driver [ret=%d]\n", TIMER_NAME, TIMER_CNT);
			return -EIO;
		}
	} else {						/* 没有定义设备号 */
		ret = alloc_chrdev_region(&timerdev.devid, 0, TIMER_CNT, TIMER_NAME);	/* 申请设备号 */
		if(ret < 0) {
			pr_err("%s Couldn't alloc_chrdev_region, ret=%d\r\n", TIMER_NAME, ret);
			return -EIO;
		}
		timerdev.major = MAJOR(timerdev.devid);	/* 获取分配号的主设备号 */
		timerdev.minor = MINOR(timerdev.devid);	/* 获取分配号的次设备号 */
	}
	printk("timerdev major=%d,minor=%d\r\n",timerdev.major, timerdev.minor);	
	
	/* 2、初始化cdev */
	timerdev.cdev.owner = THIS_MODULE;
	cdev_init(&timerdev.cdev, &timer_fops);
	
	/* 3、添加一个cdev */
	cdev_add(&timerdev.cdev, timerdev.devid, TIMER_CNT);
	if(ret < 0)
		goto del_unregister;
		
	/* 4、创建类 */
	timerdev.class = class_create(THIS_MODULE, TIMER_NAME);
	if (IS_ERR(timerdev.class)) {
		goto del_cdev;
	}

	/* 5、创建设备 */
	timerdev.device = device_create(timerdev.class, NULL, timerdev.devid, NULL, TIMER_NAME);
	if (IS_ERR(timerdev.device)) {
		goto destroy_class;
	}
	
	/* 6、初始化timer，设置定时器处理函数,还未设置周期，所有不会激活定时器 */
	timer_setup(&timerdev.timer, timer_function, 0);
	
	return 0;

destroy_class:
	device_destroy(timerdev.class, timerdev.devid);
del_cdev:
	cdev_del(&timerdev.cdev);
del_unregister:
	unregister_chrdev_region(timerdev.devid, TIMER_CNT);
	return -EIO;
}

/*
 * @description	: 驱动出口函数
 * @param 		: 无
 * @return 		: 无
 */
static void __exit timer_exit(void)
{
	del_timer_sync(&timerdev.timer);		/* 删除timer */
#if 0
	del_timer(&timerdev.tiemr);
#endif

	/* 注销字符设备驱动 */
	cdev_del(&timerdev.cdev);/*  删除cdev */
	unregister_chrdev_region(timerdev.devid, TIMER_CNT); /* 注销设备号 */

	device_destroy(timerdev.class, timerdev.devid);
	class_destroy(timerdev.class);
}

module_init(timer_init);
module_exit(timer_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("ALIENTEK");
MODULE_INFO(intree, "Y");
