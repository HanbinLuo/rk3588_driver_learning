/***************************************************************
Copyright (C) 2026 lhb.
文件名		: leddevice.c
描述		: RK3588 LED platform_device，用于演示name匹配
***************************************************************/
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>

static void led_release(struct device *dev)
{
	printk("platled: platform device released!\r\n");
}

static struct platform_device leddevice = {
	.name = "rk3588-led",
	.id = -1,
	.dev = {
		.release = led_release,
	},
};

static int __init leddevice_init(void)
{
	return platform_device_register(&leddevice);
}

static void __exit leddevice_exit(void)
{
	platform_device_unregister(&leddevice);
}

module_init(leddevice_init);
module_exit(leddevice_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("lhb");
MODULE_INFO(intree, "Y");
