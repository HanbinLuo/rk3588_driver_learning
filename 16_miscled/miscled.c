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
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
//#include <asm/mach/map.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#define MISCLED_NAME        "miscled"   	/* 名字   		*/
#define MISCLED_MINOR       144         	/* 子设备号 		*/
#define LEDOFF              	0           	/* 关LED 		*/
#define LEDON               	1           	/* 开LED 		*/

/* miscled设备结构体 */
struct miscled_dev{
    dev_t devid;            	/* 设备号     		*/
    struct cdev cdev;       	/* cdev     		*/
    struct class *class;    	/* 类      			*/
    struct device *device;  	/* 设备    			*/
    int led_gpio;           	/* led所使用的GPIO编号        */
};

struct miscled_dev miscled;     /* led设备 */

/*
* @description  	: beep相关初始化操作
* @param – pdev 	: platform_device指针，也就是platform设备指针
 * @return        	: 成功返回0，失败返回负数
 */
static int led_gpio_init(struct device_node *nd)
{
    int ret;
    
    /* 从设备树中获取GPIO */
    miscled.led_gpio = of_get_named_gpio(nd, "miscled-gpio", 0);
    if(!gpio_is_valid(miscled.led_gpio)) {
        printk("miscled:Failed to get led-gpio\n");
        return -EINVAL;
    }
    
    /* 申请使用GPIO */
    ret = gpio_request(miscled.led_gpio, "led");
    if(ret) {
        printk("led: Failed to request miscled-gpio\n");
        return ret;
    }
    
    /* 将GPIO设置为输出模式并设置GPIO初始化电平状态 */
    gpio_direction_output(miscled.led_gpio, 0);
    
    return 0;
}

/*
 * @description  	: 打开设备
 * @param – inode	: 传递给驱动的inode
 * @param - filp 	: 设备文件，file结构体有个叫做private_data的成员变量
 *                    一般在open的时候将private_data指向设备结构体。
 * @return        	: 0 成功;其他 失败
 */
static int miscled_open(struct inode *inode, struct file *filp)
{
    return 0;
}

/*
 * @description  	: 向设备写数据 
 * @param - filp 	: 设备文件，表示打开的文件描述符
 * @param - buf  	: 要写给设备写入的数据
 * @param - cnt  	: 要写入的数据长度
 * @param – offt	: 相对于文件首地址的偏移
 * @return        	: 写入的字节数，如果为负值，表示写入失败
*/
static ssize_t miscled_write(struct file *filp, const char __user *buf, size_t cnt, loff_t *offt)
{
    int retvalue;
    unsigned char databuf[1];
    unsigned char ledstat;

    retvalue = copy_from_user(databuf, buf, cnt);
    if(retvalue < 0) {
        printk("kernel write failed!\r\n");
        return -EFAULT;
    }

    ledstat = databuf[0];       /* 获取状态值 */
    if(ledstat == LEDON) {  
        gpio_set_value(miscled.led_gpio, 1);    /* 打开LED */
    } else if(ledstat == LEDOFF) {
        gpio_set_value(miscled.led_gpio, 0);    /* 关闭LED */
    }
    return 0;
}

/* 设备操作函数 */
static struct file_operations miscled_fops = {
    .owner = THIS_MODULE,
    .open = miscled_open,
    .write = miscled_write,
};

/* MISC设备结构体 */
static struct miscdevice led_miscdev = {
    .minor = MISCLED_MINOR,
    .name = MISCLED_NAME,
    .fops = &miscled_fops,
};

 /*
  * @description 	: flatform驱动的probe函数，当驱动与
  *                    设备匹配以后此函数就会执行
  * @param – dev	: platform设备
  * @return      	: 0，成功;其他负值,失败
  */
static int miscled_probe(struct platform_device *pdev)
{
    int ret = 0;

    printk("led driver and device was matched!\r\n");

    /* 初始化BEEP */
    ret = led_gpio_init(pdev->dev.of_node);
    if(ret < 0)
        return ret;
        
    /* 一般情况下会注册对应的字符设备，但是这里我们使用MISC设备
     * 所以我们不需要自己注册字符设备驱动，只需要注册misc设备驱动即可
     */
    ret = misc_register(&led_miscdev);
    if(ret < 0){
        printk("misc device register failed!\r\n");
        goto free_gpio;
    }

    return 0;
    
free_gpio:
    gpio_free(miscled.led_gpio);
    return -EINVAL;
}

/*
 * @description 	: platform驱动的remove函数
 * @param - dev  	: platform设备
 * @return       	: 0，成功;其他负值,失败
 */
static int miscled_remove(struct platform_device *dev)
{
    /* 注销设备的时候关闭LED灯 */
    gpio_set_value(miscled.led_gpio, 1);
    
    /* 释放LED */
    gpio_free(miscled.led_gpio);

    /* 注销misc设备 */
    misc_deregister(&led_miscdev);
    return 0;
}

/* 匹配列表 */
static const struct of_device_id led_of_match[] = {
    { .compatible = "lhb,miscled" },
    { /* Sentinel */ }
};

/* platform驱动结构体 */
static struct platform_driver led_driver = {
     .driver     = {
         .name   = "lhb,miscled",   	/* 驱动名字，用于和设备匹配 */
         .of_match_table = led_of_match, 	/* 设备树匹配表          */
     },
     .probe      = miscled_probe,
     .remove     = miscled_remove,
};

/*
 * @description 	: 驱动出口函数
 * @param       	: 无
 * @return      	: 无
 */
static int __init miscled_init(void)
{
    return platform_driver_register(&led_driver);
}

/*
 * @description 	: 驱动出口函数
 * @param       	: 无
 * @return      	: 无
 */
static void __exit miscled_exit(void)
{
    platform_driver_unregister(&led_driver);
}

module_init(miscled_init);
module_exit(miscled_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("ALIENTEK");
MODULE_INFO(intree, "Y");
