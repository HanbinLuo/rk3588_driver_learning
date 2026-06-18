# 10_irq GPIO Key Interrupt Demo

本实验演示 RK3588 GPIO 按键中断。驱动从设备树解析 `lhb-keys` 子节点，为每个按键申请 GPIO 和 IRQ。按键中断触发后，驱动启动 15ms 定时器做防抖，确认按下/松开事件后唤醒阻塞在 `read()` 的应用程序。

## 设备树改动

位置：

```text
rk3588_sdk/kernel/arch/arm64/boot/dts/rockchip/rk3588-atk-devkit.dtsi
```

当前已给 `key0~key2` 添加中断属性：

```dts
lhb-keys {
	compatible = "lhb,gpio-keys";
	pinctrl-names = "default";
	pinctrl-0 = <&lhb_keys_gpio>;
	status = "okay";

	key0 {
		label = "key0";
		linux,code = <KEY_0>;
		gpios = <&gpio1 RK_PA4 GPIO_ACTIVE_LOW>;
		interrupt-parent = <&gpio1>;
		interrupts = <RK_PA4 IRQ_TYPE_EDGE_BOTH>;
		debounce-interval = <10>;
	};
};
```

`interrupt-parent` 指定这个按键属于哪个 GPIO 控制器，`interrupts` 指定 GPIO 引脚和触发方式。这里使用 `IRQ_TYPE_EDGE_BOTH`，所以按下和松开都会触发中断。

驱动最多支持 4 个按键。当前设备树只有 3 个按键；如果确认第 4 个按键的真实 GPIO，可按下面模板补充：

```dts
key3 {
	label = "key3";
	linux,code = <KEY_3>;
	gpios = <&gpioX RK_PYz GPIO_ACTIVE_LOW>;
	interrupt-parent = <&gpioX>;
	interrupts = <RK_PYz IRQ_TYPE_EDGE_BOTH>;
	debounce-interval = <10>;
};
```

同时把同一个引脚加进 `lhb_keys_gpio`：

```dts
lhb_keys_gpio: lhb-keys-gpio {
	rockchip,pins =
		<1 RK_PA4 RK_FUNC_GPIO &pcfg_pull_up>,
		<1 RK_PA2 RK_FUNC_GPIO &pcfg_pull_up>,
		<0 RK_PB2 RK_FUNC_GPIO &pcfg_pull_up>;
};
```

## 驱动原理

驱动文件：

```text
keyirq.c
```

核心流程：

```text
mykey_init()
  -> key_parse_dt()
     -> for_each_available_child_of_node()
        遍历 /lhb-keys 下可用的 key0/key1/key2/key3
     -> key_parse_one()
        读取 gpios、label、interrupts
        gpio_request()
        gpio_direction_input()
        request_irq()
        timer_setup()
  -> alloc_chrdev_region()
  -> cdev_add()
  -> device_create()
     创建 /dev/key
```

每个按键都有一个 `struct key_data`，里面保存：

```c
int id;
int gpio;
int irq_num;
struct timer_list timer;
```

中断函数只做很轻的工作：

```c
static irqreturn_t key_interrupt(int irq, void *dev_id)
{
	struct key_data *data = dev_id;
	mod_timer(&data->timer, jiffies + msecs_to_jiffies(15));
	return IRQ_HANDLED;
}
```

15ms 后进入定时器回调，读取 GPIO 稳定状态，判断按下或松开：

```c
dev->event.id = data->id;
dev->event.status = status;
dev->has_event = 1;
wake_up_interruptible(&dev->waitq);
```

`data->id` 就是按键编号，来自设备树遍历顺序：`key0` 为 0，`key1` 为 1，依次类推。

## 应用原理

应用文件：

```text
keyirqApp.c
```

应用打开 `/dev/key` 后循环读取：

```c
ret = read(fd, &event, sizeof(event));
```

这是阻塞读。没有按键事件时，驱动里的 `wait_event_interruptible()` 会让应用睡眠；按键中断发生并防抖确认后，驱动调用 `wake_up_interruptible()` 唤醒应用。因此 APP 虽然写了循环，但不是空转轮询，不会一直占 CPU。

事件格式：

```c
struct key_event {
	int id;      /* KEY编号 */
	int status;  /* KEY_PRESS 或 KEY_RELEASE */
};
```

## 编译

在主机执行：

```bash
cd /home/lhb/linux/rk3588_driver/rk3588_driver_learning/10_irq
make clean
make
```

生成：

```text
keyirq.ko
keyirqApp
```

## 加载和运行

把 `keyirq.ko` 和 `keyirqApp` 放到开发板后执行：

```bash
insmod keyirq.ko
ls -l /dev/key
./keyirqApp /dev/key
```

查看驱动日志：

```bash
dmesg | tail -50
```

卸载模块：

```bash
rmmod keyirq
```

如果重新加载失败，可先确认旧模块是否还在：

```bash
lsmod | grep keyirq
rmmod keyirq
insmod keyirq.ko
```
