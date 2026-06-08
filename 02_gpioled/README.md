# 03_gpioled

本实验实现一个简单的 GPIO LED 字符设备驱动。设备树负责描述 LED 使用的 GPIO 引脚，内核模块读取设备树后申请 GPIO，并创建 `/dev/gpioled` 设备节点。用户态 APP 向 `/dev/gpioled` 写入 `0` 或 `1` 来控制 LED 关闭或打开。

## 文件说明

- `gpioled.c`：GPIO LED 内核模块。
- `ledApp.c`：用户态测试程序。
- `Makefile`：编译内核模块的 Makefile。
- `rk3588-atk-devkit.dtsi`：本实验用到的设备树修改参考。

## 设备树修改

在 `rk3588_sdk/kernel/arch/arm64/boot/dts/rockchip/rk3588-atk-devkit.dtsi` 的根节点 `/` 下添加 `gpioled` 节点：

```dts
gpioled {
	compatible = "lhb,led";
	led-gpio = <&gpio1 RK_PA3 GPIO_ACTIVE_HIGH>;
	pinctrl-names = "default";
	pinctrl-0 = <&gpioled_gpio>;
	status = "okay";
};
```

在 `&pinctrl` 节点下添加引脚配置：

```dts
&pinctrl {
	leds {
		gpioled_gpio: gpioled-gpio {
			rockchip,pins = <1 RK_PA3 RK_FUNC_GPIO &pcfg_pull_down>;
		};
	};
};
```

关键属性说明：

- `compatible = "lhb,led"`：驱动会读取并比较该字符串，必须和 `gpioled.c` 中的判断一致。
- `led-gpio`：驱动通过 `of_get_named_gpio(gpioled.nd, "led-gpio", 0)` 获取该属性，所以属性名不能随意修改。
- `<&gpio1 RK_PA3 GPIO_ACTIVE_HIGH>`：表示使用 `GPIO1_A3`，高电平有效。
- `pinctrl-0 = <&gpioled_gpio>`：把 `GPIO1_A3` 配置为 GPIO 功能，并设置为下拉。
- `status = "okay"`：驱动会检查该属性，只有为 `okay` 时才继续初始化。

注意：板级设备树中原本还有 `&work_led` 节点使用同一个 `GPIO1_A3`，当前它是 `status = "disabled"`。如果以后启用 `&work_led`，就不能再同时让本实验的 `gpioled` 使用同一个引脚。

## 驱动原理

驱动初始化函数是 `led_init()`，主要流程如下：

1. 通过 `of_find_node_by_path("/gpioled")` 查找设备树中的 `/gpioled` 节点。
2. 读取 `status` 属性，确认节点状态为 `okay`。
3. 读取 `compatible` 属性，确认值为 `lhb,led`。
4. 通过 `of_get_named_gpio(..., "led-gpio", 0)` 从设备树获取 LED GPIO 编号。
5. 使用 `gpio_request()` 向 GPIO 子系统申请该 GPIO。
6. 使用 `gpio_direction_output()` 把 GPIO 设置为输出，默认输出低电平关闭 LED。
7. 通过 `alloc_chrdev_region()` 申请字符设备号。
8. 使用 `cdev_init()` 和 `cdev_add()` 注册字符设备。
9. 使用 `class_create()` 和 `device_create()` 创建设备节点 `/dev/gpioled`。

用户态打开 `/dev/gpioled` 后，驱动的 `led_open()` 会把全局设备结构体保存到 `filp->private_data`。APP 写入数据时会进入 `led_write()`：

```c
databuf[0] = 1; /* 打开 LED */
databuf[0] = 0; /* 关闭 LED */
```

`led_write()` 从用户空间拷贝 1 字节数据：

- 写入 `1`：调用 `gpio_set_value(dev->led_gpio, 1)`，输出高电平，打开 LED。
- 写入 `0`：调用 `gpio_set_value(dev->led_gpio, 0)`，输出低电平，关闭 LED。

模块卸载时，`led_exit()` 会删除字符设备、销毁 `/dev/gpioled`、销毁 class，并释放 GPIO。

## 编译

在 `03_gpioled` 目录下执行：

```sh
make
```

生成的内核模块为：

```sh
gpioled.ko
```

清理编译产物：

```sh
make clean
```

## 使用方法

先加载驱动：

```sh
insmod gpioled.ko
```

确认设备节点已经生成：

```sh
ls -l /dev/gpioled
```

运行测试 APP：

```sh
./ledApp /dev/gpioled 1
```

打开 LED。

```sh
./ledApp /dev/gpioled 0
```

关闭 LED。

卸载驱动：

```sh
rmmod gpioled
```

如果加载失败，可以查看内核日志：

```sh
dmesg | tail
```
