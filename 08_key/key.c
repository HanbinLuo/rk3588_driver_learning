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
//#include <asm/mach/map.h>
#include <asm/uaccess.h>
#include <asm/io.h>
/***************************************************************
Copyright © ALIENTEK Co., Ltd. 1998-2029. All rights reserved.
文件名		: key.c
作者	  	: 正点原子Linux团队
版本	   	: V1.0
描述	   	: Linux按键输入驱动实验
其他	   	: 无
论坛 	   	: www.openedv.com
日志	   	: 初版V1.0 2022/12/23 正点原子Linux团队创建
***************************************************************/
#define KEY_CNT			1		/* 设备号个数 	*/
#define KEY_NAME		"key"	/* 名字 		*/
#define KEY_NUM			4		/* 最大按键数量	*/

/* 定义按键值 */
#define KEY0VALUE		0XF0	/* 按键值 		*/
#define KEY1VALUE		0XF1
#define KEY2VALUE		0XF2
#define KEY3VALUE		0XF3
#define INVAKEY			0X00	/* 无效的按键值  */

struct key_gpio_desc {
	int gpio;
	int active_low;
	const char *label;
	unsigned int value;
};

/* key设备结构体 */
struct key_dev{
	dev_t devid;			/* 设备号 	 */
	struct cdev cdev;		/* cdev 	*/
	struct class *class;	/* 类 		*/
	struct device *device;	/* 设备 	 */
	int major;				/* 主设备号	  */
	int minor;				/* 次设备号   */
	struct device_node	*nd; /* 设备节点 */
	struct key_gpio_desc keys[KEY_NUM];
	int key_num;
	atomic_t keyvalue;		/* 按键值 		*/	
};

static struct key_dev keydev;		/* key设备 */

/*
 * @description	: 初始化按键IO，open函数打开驱动的时候
 * 				  初始化按键所使用的GPIO引脚。
 * @param 		: 无
 * @return 		: 无
 */
static int keyio_init(void)
{
	int ret;
	int i = 0;
	const char *str;
	struct device_node *child;
	enum of_gpio_flags flags;
	unsigned int key_values[KEY_NUM] = {
		KEY0VALUE, KEY1VALUE, KEY2VALUE, KEY3VALUE
	};
	
	/* 1、获取设备节点：lhb-keys */
	keydev.nd = of_find_node_by_path("/lhb-keys");
	if(keydev.nd == NULL) {
		printk("lhb-keys node not find!\r\n");
		return -EINVAL;
	}

	/* 2.读取status属性 */
	ret = of_property_read_string(keydev.nd, "status", &str);
	if(ret < 0) 
	    return -EINVAL;

	if (strcmp(str, "okay"))
        return -EINVAL;
    
	/* 3、获取compatible属性值并进行匹配 */
	ret = of_property_read_string(keydev.nd, "compatible", &str);
	if(ret < 0) {
		printk("keydev: Failed to get compatible property\n");
		return -EINVAL;
	}

	if (strcmp(str, "lhb,gpio-keys")) {
        printk("keydev: Compatible match failed\n");
        return -EINVAL;
    }

	/* 4、遍历gpio-keys子节点，获取设备树中的gpios属性 */
	for_each_available_child_of_node(keydev.nd, child) {
		if (i >= KEY_NUM)
			break;

		keydev.keys[i].gpio = of_get_named_gpio_flags(child, "gpios", 0, &flags);
		if (keydev.keys[i].gpio < 0) {
			printk("can't get key%d gpios\r\n", i);
			continue;
		}

		ret = of_property_read_string(child, "label", &keydev.keys[i].label);
		if (ret < 0)
			keydev.keys[i].label = child->name;

		keydev.keys[i].active_low = !!(flags & OF_GPIO_ACTIVE_LOW);
		keydev.keys[i].value = key_values[i];
		printk("%s gpio num = %d, active_low = %d\r\n",
		       keydev.keys[i].label, keydev.keys[i].gpio,
		       keydev.keys[i].active_low);

		/* 5.向gpio子系统申请使用GPIO */
		ret = gpio_request(keydev.keys[i].gpio, keydev.keys[i].label);
		if (ret) {
			printk(KERN_ERR "keydev: Failed to request %s gpio\n",
			       keydev.keys[i].label);
			goto free_gpio;
		}

		/* 6、设置GPIO输入模式 */
		ret = gpio_direction_input(keydev.keys[i].gpio);
		if(ret < 0) {
			printk("can't set %s gpio!\r\n", keydev.keys[i].label);
			gpio_free(keydev.keys[i].gpio);
			goto free_gpio;
		}

		i++;
	}

	keydev.key_num = i;
	if (keydev.key_num == 0)
		return -EINVAL;

	return 0;

free_gpio:
	while (--i >= 0)
		gpio_free(keydev.keys[i].gpio);
	return ret;
}

/*
 * @description		: 打开设备
 * @param - inode 	: 传递给驱动的inode
 * @param - filp 	: 设备文件，file结构体有个叫做private_data的成员变量
 * 					  一般在open的时候将private_data指向设备结构体。
 * @return 			: 0 成功;其他 失败
 */
static int key_open(struct inode *inode, struct file *filp)
{
	int ret = 0;
	filp->private_data = &keydev; 	/* 设置私有数据 */

	ret = keyio_init();				/* 初始化按键IO */
	if (ret < 0) {
		return ret;
	}

	return 0;
}

/*
 * @description		: 从设备读取数据 
 * @param - filp 	: 要打开的设备文件(文件描述符)
 * @param - buf 	: 返回给用户空间的数据缓冲区
 * @param - cnt 	: 要读取的数据长度
 * @param - offt 	: 相对于文件首地址的偏移
 * @return 			: 读取的字节数，如果为负值，表示读取失败
 */
static ssize_t key_read(struct file *filp, char __user *buf, size_t cnt, loff_t *offt)
{
	int ret = 0;
	int i;
	int value;
	int pressed;
	struct key_dev *dev = filp->private_data;

	atomic_set(&dev->keyvalue, INVAKEY);		/* 无效的按键值 */

	for (i = 0; i < dev->key_num; i++) {
		value = gpio_get_value(dev->keys[i].gpio);
		pressed = dev->keys[i].active_low ? !value : value;
		if (pressed) {
			do {
				value = gpio_get_value(dev->keys[i].gpio);
				pressed = dev->keys[i].active_low ? !value : value;
			} while (pressed);		/* 等待按键释放 */
			atomic_set(&dev->keyvalue, dev->keys[i].value);
			break;
		}
	}

	value = atomic_read(&dev->keyvalue);
	ret = copy_to_user(buf, &value, sizeof(value));
	return ret;
}

/*
 * @description		: 向设备写数据 
 * @param - filp 	: 设备文件，表示打开的文件描述符
 * @param - buf 	: 要写给设备写入的数据
 * @param - cnt 	: 要写入的数据长度
 * @param - offt 	: 相对于文件首地址的偏移
 * @return 			: 写入的字节数，如果为负值，表示写入失败
 */
static ssize_t key_write(struct file *filp, const char __user *buf, size_t cnt, loff_t *offt)
{
	return 0;
}

/*
 * @description		: 关闭/释放设备
 * @param - filp 	: 要关闭的设备文件(文件描述符)
 * @return 			: 0 成功;其他 失败
 */
static int key_release(struct inode *inode, struct file *filp)
{
	int i;
	struct key_dev *dev = filp->private_data;

	for (i = 0; i < dev->key_num; i++)
		gpio_free(dev->keys[i].gpio);
	
	return 0;
}

/* 设备操作函数 */
static struct file_operations key_fops = {
	.owner = THIS_MODULE,
	.open = key_open,
	.read = key_read,
	.write = key_write,
	.release = 	key_release,
};

/*
 * @description	: 驱动入口函数
 * @param 		: 无
 * @return 		: 无
 */
static int __init mykey_init(void)
{
	int ret;
	/* 1、初始化原子变量    */
	keydev.keyvalue= (atomic_t)ATOMIC_INIT(0);

	/* 2、原子变量初始值为INVAKEY */
	atomic_set(&keydev.keyvalue, INVAKEY);

	/* 注册字符设备驱动 */
	/* 1、创建设备号 */
	if (keydev.major) {		/*  定义了设备号 */
		keydev.devid = MKDEV(keydev.major, 0);
		ret = register_chrdev_region(keydev.devid, KEY_CNT, KEY_NAME);
		if(ret < 0) {
			pr_err("cannot register %s char driver [ret=%d]\n", KEY_NAME, KEY_CNT);
			return -EIO;
		}
	} else {						/* 没有定义设备号 */
		ret = alloc_chrdev_region(&keydev.devid, 0, KEY_CNT, KEY_NAME);	/* 申请设备号 */
		if(ret < 0) {
			pr_err("%s Couldn't alloc_chrdev_region, ret=%d\r\n", KEY_NAME, ret);
			return -EIO;
		}
		keydev.major = MAJOR(keydev.devid);	/* 获取分配号的主设备号 */
		keydev.minor = MINOR(keydev.devid);	/* 获取分配号的次设备号 */
	}
	printk("keydev major=%d,minor=%d\r\n",keydev.major, keydev.minor);	
	
	/* 2、初始化cdev */
	keydev.cdev.owner = THIS_MODULE;
	cdev_init(&keydev.cdev, &key_fops);
	
	/* 3、添加一个cdev */
	cdev_add(&keydev.cdev, keydev.devid, KEY_CNT);
	if(ret < 0)
		goto del_unregister;
		
	/* 4、创建类 */
	keydev.class = class_create(THIS_MODULE, KEY_NAME);
	if (IS_ERR(keydev.class)) {
		goto del_cdev;
	}

	/* 5、创建设备 */
	keydev.device = device_create(keydev.class, NULL, keydev.devid, NULL, KEY_NAME);
	if (IS_ERR(keydev.device)) {
		goto destroy_class;
	}
	return 0;

destroy_class:
	device_destroy(keydev.class, keydev.devid);
del_cdev:
	cdev_del(&keydev.cdev);
del_unregister:
	unregister_chrdev_region(keydev.devid, KEY_CNT);
	return -EIO;
}

/*
 * @description	: 驱动出口函数
 * @param 		: 无
 * @return 		: 无
 */
static void __exit mykey_exit(void)
{
	/* 注销字符设备驱动 */
	cdev_del(&keydev.cdev);/*  删除cdev */
	unregister_chrdev_region(keydev.devid, KEY_CNT); /* 注销设备号 */

	device_destroy(keydev.class, keydev.devid);
	class_destroy(keydev.class);
}

module_init(mykey_init);
module_exit(mykey_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("ALIENTEK");
MODULE_INFO(intree, "Y");
