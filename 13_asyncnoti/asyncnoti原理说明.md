# 13_asyncnoti 异步通知按键驱动原理说明

本文说明 `13_asyncnoti` 目录下三按键异步通知驱动的工作原理。当前版本支持自定义设备树中的 3 个 GPIO 按键，驱动文件是 `asyncnoti.c`，测试程序是 `asyncnotiApp.c`。

## 1. 实验目标

这个实验是在阻塞 IO 和非阻塞 IO 的基础上继续扩展，核心目标是：

1. 从设备树 `/lhb-keys` 节点解析 3 个按键。
2. 为每个按键申请 GPIO 和中断。
3. 使用 15ms 定时器完成按键防抖。
4. 产生 `struct key_event` 事件，包含按键编号和按键状态。
5. 支持阻塞读取。
6. 支持非阻塞读取。
7. 支持 `poll/select`。
8. 支持异步通知，也就是按键事件到来时由内核向应用进程发送 `SIGIO` 信号。

异步通知的重点是：应用程序不需要一直阻塞在 `read()` 或 `select()` 上等待事件。它可以先注册 `SIGIO` 信号处理函数，然后继续做其他事情。按键事件到来后，驱动主动通知应用程序，应用程序在信号处理函数中读取事件。

## 2. 和 12_noblockio 的主要区别

`12_noblockio` 的典型流程是：

```text
应用程序 select() 等待
 └── 有按键事件后 select() 返回
      └── 应用程序 read() 读取事件
```

`13_asyncnoti` 的典型流程是：

```text
应用程序注册 SIGIO
 └── 驱动有按键事件后 kill_fasync()
      └── 应用程序收到 SIGIO
           └── 信号处理函数 read() 读取事件
```

驱动比 `12_noblockio` 多了：

```c
struct fasync_struct *async_queue;
```

以及文件操作：

```c
.fasync = key_fasync,
```

并在产生事件后调用：

```c
kill_fasync(&dev->async_queue, SIGIO, POLL_IN);
```

## 3. 设备树结构

驱动查找固定路径：

```c
key.nd = of_find_node_by_path("/lhb-keys");
```

要求 compatible 为：

```c
"lhb,gpio-keys"
```

设备树大致结构如下：

```dts
lhb_keys: lhb-keys {
	compatible = "lhb,gpio-keys";
	status = "okay";

	key0 {
		label = "key0";
		gpios = <&gpioX RK_PXn GPIO_ACTIVE_LOW>;
		interrupt-parent = <&gpioX>;
		interrupts = <RK_PXn IRQ_TYPE_EDGE_BOTH>;
	};

	key1 {
		label = "key1";
		gpios = <&gpioX RK_PXn GPIO_ACTIVE_LOW>;
		interrupt-parent = <&gpioX>;
		interrupts = <RK_PXn IRQ_TYPE_EDGE_BOTH>;
	};

	key2 {
		label = "key2";
		gpios = <&gpioX RK_PXn GPIO_ACTIVE_LOW>;
		interrupt-parent = <&gpioX>;
		interrupts = <RK_PXn IRQ_TYPE_EDGE_BOTH>;
	};
};
```

最大按键数量：

```c
#define KEY_NUM 3
```

## 4. 关键数据结构

### 4.1 按键事件

```c
struct key_event {
	int id;
	int status;
};
```

| 字段 | 含义 |
| --- | --- |
| `id` | 按键编号，从 0 开始 |
| `status` | `KEY_PRESS` 或 `KEY_RELEASE` |

三按键场景下，必须返回 `id`，否则用户空间无法判断是哪一个按键触发。

### 4.2 单个按键对象

```c
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
```

每个按键都有自己的 GPIO、中断号、防抖定时器和状态记录。

### 4.3 整个设备对象

```c
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
	struct fasync_struct *async_queue;
};
```

本章最重要的新增成员是：

```c
struct fasync_struct *async_queue;
```

它用于保存已经开启异步通知的用户进程信息。驱动后面调用 `kill_fasync()` 时，就靠它找到需要发送 `SIGIO` 的进程。

## 5. 驱动初始化流程

模块加载进入：

```c
mykey_init()
```

流程如下：

```text
mykey_init()
 ├── spin_lock_init()
 ├── init_waitqueue_head()
 ├── 初始化 event / has_event
 ├── key.async_queue = NULL
 ├── key_parse_dt()
 │    └── key_parse_one()
 ├── alloc_chrdev_region()
 ├── cdev_init()
 ├── cdev_add()
 ├── class_create()
 └── device_create()，生成 /dev/key
```

其中：

```c
key.async_queue = NULL;
```

表示模块刚加载时，还没有应用程序注册异步通知。

## 6. GPIO 和中断初始化

每个按键通过 `key_parse_one()` 初始化。

### 6.1 读取 GPIO 和有效电平

```c
data->gpio = of_get_named_gpio_flags(child, "gpios", 0, &gpio_flags);
data->active_low = !!(gpio_flags & OF_GPIO_ACTIVE_LOW);
```

驱动支持 `GPIO_ACTIVE_LOW`。读取原始 GPIO 后，会转换成逻辑上的按下状态：

```c
pressed = data->active_low ? !value : value;
```

这样无论硬件是低电平有效还是高电平有效，后面都统一用 `pressed = 1` 表示按下。

### 6.2 申请 GPIO 和 IRQ

```c
gpio_request(data->gpio, data->label);
gpio_direction_input(data->gpio);

data->irq_num = irq_of_parse_and_map(child, 0);
if (!data->irq_num)
	data->irq_num = gpio_to_irq(data->gpio);
```

驱动优先从设备树中解析中断号，失败时用 GPIO 转 IRQ。

### 6.3 中断触发方式

```c
irq_flags = irq_get_trigger_type(data->irq_num);
if (irq_flags == IRQF_TRIGGER_NONE)
	irq_flags = IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING;
```

双边沿触发可以同时识别按下和松开。

### 6.4 申请中断

```c
request_irq(data->irq_num, key_interrupt, irq_flags, data->label, data);
```

最后一个参数传入 `data`，中断函数可以知道是哪一个按键触发。

## 7. 中断和防抖

中断处理函数：

```c
static irqreturn_t key_interrupt(int irq, void *dev_id)
{
	struct key_data *data = dev_id;

	mod_timer(&data->timer, jiffies + msecs_to_jiffies(KEY_DEBOUNCE_MS));
	return IRQ_HANDLED;
}
```

按键中断只启动定时器，不直接判断按键状态。

防抖时间：

```c
#define KEY_DEBOUNCE_MS 15
```

如果机械按键在几毫秒内抖动出多次边沿，`mod_timer()` 会反复刷新定时器，最终等电平稳定后再进入定时器函数。

## 8. 定时器生成事件并发送异步通知

定时器函数 `key_timer_function()` 负责最终确认按键状态。

### 8.1 判断按下和松开

```c
value = gpio_get_value(data->gpio);
pressed = data->active_low ? !value : value;

if (pressed && !data->last_pressed)
	status = KEY_PRESS;
else if (!pressed && data->last_pressed)
	status = KEY_RELEASE;
else
	status = KEY_KEEP;
```

如果状态没有变化：

```c
if (status == KEY_KEEP)
	return;
```

### 8.2 保存事件并唤醒等待队列

```c
spin_lock_irqsave(&dev->spinlock, flags);
dev->event.id = data->id;
dev->event.status = status;
dev->has_event = 1;
spin_unlock_irqrestore(&dev->spinlock, flags);

wake_up_interruptible(&dev->waitq);
```

这部分和 `12_noblockio` 一样，用于支持阻塞 `read()` 和 `poll/select`。

### 8.3 发送 SIGIO

本章新增的关键代码是：

```c
if (dev->async_queue)
	kill_fasync(&dev->async_queue, SIGIO, POLL_IN);
```

含义是：

1. 如果有用户进程注册了异步通知。
2. 驱动向该进程发送 `SIGIO` 信号。
3. `POLL_IN` 表示当前设备有普通数据可读。

用户程序收到 `SIGIO` 后，会执行之前注册的信号处理函数。

## 9. read 的作用

即使使用异步通知，事件数据本身仍然需要通过 `read()` 读取。

`SIGIO` 只是通知应用：

```text
现在 /dev/key 有数据可读了
```

真正的数据还是在：

```c
struct key_event event;
```

应用收到信号后调用：

```c
read(fd, &event, sizeof(event));
```

才能拿到按键编号和按键状态。

`key_read()` 同时支持阻塞和非阻塞：

```c
if (filp->f_flags & O_NONBLOCK) {
	if (!dev->has_event)
		return -EAGAIN;
} else {
	ret = wait_event_interruptible(dev->waitq, dev->has_event);
	if (ret)
		return ret;
}
```

当前 `asyncnotiApp.c` 使用的是非阻塞打开，所以没有事件时 `read()` 会返回 `-EAGAIN`。

## 10. fasync 的核心原理

驱动注册了：

```c
.fasync = key_fasync,
```

对应函数：

```c
static int key_fasync(int fd, struct file *filp, int on)
{
	struct key_dev *dev = filp->private_data;

	return fasync_helper(fd, filp, on, &dev->async_queue);
}
```

### 10.1 fasync_helper 的作用

`fasync_helper()` 是内核提供的辅助函数，用来维护异步通知队列。

当应用程序执行：

```c
fcntl(fd, F_SETFL, flags | FASYNC);
```

内核会调用驱动的 `.fasync` 函数。驱动通过 `fasync_helper()` 把当前进程加入 `async_queue`。

后面驱动调用：

```c
kill_fasync(&dev->async_queue, SIGIO, POLL_IN);
```

内核就知道应该给哪个进程发送 `SIGIO`。

### 10.2 release 时关闭异步通知

```c
static int key_release(struct inode *inode, struct file *filp)
{
	return key_fasync(-1, filp, 0);
}
```

应用关闭设备时，驱动会把该文件从异步通知队列中移除。这样可以避免文件关闭后，驱动还向无效对象发送信号。

## 11. poll/select 仍然可用

虽然本章重点是异步通知，但驱动仍然保留了 `.poll`：

```c
static unsigned int key_poll(struct file *filp, struct poll_table_struct *wait)
```

文件操作表：

```c
static const struct file_operations key_fops = {
	.owner = THIS_MODULE,
	.open = key_open,
	.read = key_read,
	.poll = key_poll,
	.release = key_release,
	.fasync = key_fasync,
};
```

这意味着这个驱动同时支持：

1. 阻塞 `read()`。
2. 非阻塞 `read()`。
3. `select/poll`。
4. `SIGIO` 异步通知。

只是当前测试程序使用的是第 4 种方式。

## 12. 应用程序如何开启异步通知

`asyncnotiApp.c` 的打开方式：

```c
fd = open(argv[1], O_RDONLY | O_NONBLOCK);
```

然后注册 `SIGIO` 信号处理函数：

```c
signal(SIGIO, sigio_signal_func);
```

接着告诉内核，当前进程是这个文件描述符的拥有者：

```c
fcntl(fd, F_SETOWN, getpid());
```

然后取出当前文件状态标志：

```c
flags = fcntl(fd, F_GETFL);
```

最后设置 `FASYNC`：

```c
fcntl(fd, F_SETFL, flags | FASYNC);
```

这一步会触发驱动里的 `.fasync`，也就是：

```c
key_fasync()
```

从此以后，驱动有事件时就可以给当前进程发送 `SIGIO`。

## 13. SIGIO 信号处理函数

测试程序中的信号处理函数：

```c
static void sigio_signal_func(int signum)
{
	int ret;
	struct key_event event;

	do {
		ret = read(fd, &event, sizeof(event));
		if (ret == sizeof(event)) {
			if (event.status == KEY_PRESS)
				printf("KEY%d Press\n", event.id);
			else if (event.status == KEY_RELEASE)
				printf("KEY%d Release\n", event.id);
		}
	} while (ret == sizeof(event));
}
```

收到 `SIGIO` 后，应用程序会尝试读取事件。

这里使用 `do...while` 的原因是：如果之后驱动改成事件队列，一次信号里可能可以读出多个事件。当前驱动只有一个事件缓存，所以通常读一次就会读空，下一次 `read()` 返回 `-EAGAIN` 后退出循环。

## 14. 完整运行流程

完整流程如下：

```text
asyncnotiApp
 ├── open(/dev/key, O_RDONLY | O_NONBLOCK)
 ├── signal(SIGIO, sigio_signal_func)
 ├── fcntl(fd, F_SETOWN, getpid())
 ├── fcntl(fd, F_SETFL, flags | FASYNC)
 │    └── 驱动 key_fasync()
 │         └── fasync_helper() 注册进 async_queue
 ├── 主循环 sleep()
 │
 ├── 按键电平变化
 │    └── GPIO 中断
 │         └── key_interrupt()
 │              └── 启动 15ms 防抖定时器
 │                   └── key_timer_function()
 │                        ├── 读取 GPIO
 │                        ├── 判断 KEY_PRESS / KEY_RELEASE
 │                        ├── 保存 struct key_event
 │                        ├── has_event = 1
 │                        ├── wake_up_interruptible()
 │                        └── kill_fasync(SIGIO)
 │
 └── 应用收到 SIGIO
      └── sigio_signal_func()
           ├── read(fd, &event, sizeof(event))
           └── 打印 KEYx Press/Release
```

## 15. 异步通知和阻塞/非阻塞的关系

异步通知不是用来直接传输按键数据的，它只负责通知。

可以这样理解：

| 机制 | 作用 |
| --- | --- |
| `read()` | 真正读取事件数据 |
| `O_NONBLOCK` | 没有数据时不睡眠，返回 `-EAGAIN` |
| `poll/select` | 应用主动等待设备可读 |
| `FASYNC + SIGIO` | 驱动主动通知应用设备可读 |

所以异步通知流程中仍然需要 `read()`。`SIGIO` 只是告诉应用：

```text
你现在可以来读了
```

## 16. 字符设备注册

驱动使用标准字符设备框架：

```c
alloc_chrdev_region(&key.devid, 0, KEY_CNT, KEY_NAME);
cdev_init(&key.cdev, &key_fops);
cdev_add(&key.cdev, key.devid, KEY_CNT);
class_create(THIS_MODULE, KEY_NAME);
device_create(key.class, NULL, key.devid, NULL, KEY_NAME);
```

最终设备节点：

```text
/dev/key
```

文件操作表：

```c
static const struct file_operations key_fops = {
	.owner = THIS_MODULE,
	.open = key_open,
	.read = key_read,
	.poll = key_poll,
	.release = key_release,
	.fasync = key_fasync,
};
```

其中 `.fasync` 是本章最重要的新增点。

## 17. Makefile 说明

当前 Makefile 会同时编译内核模块和应用程序：

```makefile
build: kernel_modules app
```

编译模块：

```makefile
$(MAKE) -C $(KERNELDIR) M=$(CURRENT_PATH) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules
```

编译 App：

```makefile
$(CC) -O2 -Wall -o asyncnotiApp asyncnotiApp.c
```

常用命令：

```sh
make build
make clean
```

编译成功后会生成：

```text
asyncnoti.ko
asyncnotiApp
```

## 18. 当前实现的特点

当前驱动只保存一个最新事件：

```c
struct key_event event;
int has_event;
```

普通按键实验中这足够使用。但如果多个按键在极短时间内连续触发，后一个事件可能覆盖前一个事件。产品级实现通常会加入循环队列，保存多个未读事件。

另外，当前测试程序在信号处理函数中调用了 `read()` 和 `printf()`，用于实验演示很直观。实际复杂应用中，信号处理函数一般应该尽量短，只设置标志位或写 pipe，然后在主循环中处理，避免在信号上下文做复杂工作。

## 19. 总结

`13_asyncnoti` 的核心可以概括为：

应用程序通过 `FASYNC` 注册异步通知，驱动在按键防抖确认后生成 `key_event`，然后调用 `kill_fasync()` 给应用发送 `SIGIO`，应用在信号处理函数里调用 `read()` 取走按键事件。

相比 `12_noblockio`，本章新增的关键点是：

1. `struct fasync_struct *async_queue`。
2. `.fasync` 文件操作。
3. `fasync_helper()` 注册异步通知。
4. `kill_fasync()` 发送 `SIGIO`。
5. 用户态 `F_SETOWN` 和 `FASYNC`。
6. 信号处理函数中读取按键事件。
