# 12_noblockio 非阻塞 IO 按键驱动原理说明

本文说明 `12_noblockio` 目录下三按键非阻塞 IO 驱动的工作原理。当前版本支持自定义设备树中的 3 个 GPIO 按键，驱动文件是 `noblockio.c`，测试程序是 `noblockioApp.c`。

## 1. 实验目标

这个实验是在 `11_blockio` 阻塞 IO 的基础上继续扩展，核心目标是：

1. 从设备树 `/lhb-keys` 节点解析 3 个按键。
2. 为每个按键申请 GPIO 和中断。
3. 使用 15ms 定时器完成按键防抖。
4. 产生 `struct key_event` 事件，包含按键编号和按键状态。
5. 支持阻塞读取。
6. 支持非阻塞读取。
7. 支持 `poll/select` 机制，让应用层可以等待设备变为可读。

这里的“非阻塞 IO”指的是：应用程序用 `O_NONBLOCK` 打开设备后，如果当前没有按键事件，`read()` 不会睡眠等待，而是立刻返回 `-EAGAIN`。应用程序可以自己决定继续做其他事情，或者配合 `select()` 等待设备可读。

## 2. 和 11_blockio 的主要区别

`11_blockio` 的 `read()` 逻辑是：

```text
没有事件 -> 进程睡眠 -> 有事件后被唤醒 -> read 返回事件
```

`12_noblockio` 在此基础上增加了：

```text
O_NONBLOCK 打开时：
没有事件 -> read 立刻返回 -EAGAIN
有事件 -> read 返回事件
```

同时增加 `.poll` 文件操作：

```c
.poll = key_poll,
```

这样应用程序可以使用：

```c
select(fd + 1, &readfds, NULL, NULL, NULL);
```

等待 `/dev/key` 变成可读状态。

## 3. 设备树结构

驱动查找固定路径：

```c
key.nd = of_find_node_by_path("/lhb-keys");
```

要求 compatible 为：

```c
"lhb,gpio-keys"
```

设备树结构大致如下：

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

最大按键数量由宏控制：

```c
#define KEY_NUM 3
```

驱动通过 `for_each_available_child_of_node()` 遍历 `/lhb-keys` 下的可用子节点，每个子节点对应一个按键。

## 4. 关键数据结构

### 4.1 按键状态

```c
enum key_status {
	KEY_PRESS = 0,
	KEY_RELEASE,
	KEY_KEEP,
};
```

| 状态 | 含义 |
| --- | --- |
| `KEY_PRESS` | 按键按下 |
| `KEY_RELEASE` | 按键松开 |
| `KEY_KEEP` | 状态保持，不向应用层上报 |

### 4.2 应用层读取到的事件

```c
struct key_event {
	int id;
	int status;
};
```

| 字段 | 含义 |
| --- | --- |
| `id` | 按键编号，从 0 开始 |
| `status` | 按键状态，按下或松开 |

由于当前驱动支持 3 个按键，所以不能只返回状态，还必须返回按键编号。否则应用层无法判断到底是哪个按键触发。

### 4.3 单个按键对象

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

每个按键都有独立的 GPIO、中断号、上一次状态和防抖定时器。

### 4.4 整个设备对象

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
};
```

其中和非阻塞 IO 关系最密切的是：

| 成员 | 作用 |
| --- | --- |
| `event` | 最近一次按键事件 |
| `has_event` | 是否有事件可读 |
| `spinlock` | 保护 `event` 和 `has_event` |
| `waitq` | 给阻塞读和 poll/select 使用 |

## 5. 驱动初始化流程

模块加载时进入：

```c
mykey_init()
```

整体流程如下：

```text
mykey_init()
 ├── spin_lock_init()
 ├── init_waitqueue_head()
 ├── 初始化 event / has_event
 ├── key_parse_dt()
 │    └── key_parse_one()
 ├── alloc_chrdev_region()
 ├── cdev_init()
 ├── cdev_add()
 ├── class_create()
 └── device_create()，生成 /dev/key
```

初始化成功后，用户程序可以打开 `/dev/key`。

## 6. GPIO 和中断初始化

每个按键由 `key_parse_one()` 初始化。

### 6.1 获取 GPIO 和有效电平

```c
data->gpio = of_get_named_gpio_flags(child, "gpios", 0, &gpio_flags);
data->active_low = !!(gpio_flags & OF_GPIO_ACTIVE_LOW);
```

如果设备树中写的是 `GPIO_ACTIVE_LOW`，说明按键按下时 GPIO 为低电平。驱动读取 GPIO 后会转换成统一的逻辑状态：

```c
pressed = data->active_low ? !value : value;
```

这样后续代码只需要判断 `pressed`，不需要关心硬件到底是高电平有效还是低电平有效。

### 6.2 申请 GPIO 并设置为输入

```c
gpio_request(data->gpio, data->label);
gpio_direction_input(data->gpio);
```

按键属于输入设备，所以 GPIO 必须配置为输入方向。

### 6.3 获取中断号

```c
data->irq_num = irq_of_parse_and_map(child, 0);
if (!data->irq_num)
	data->irq_num = gpio_to_irq(data->gpio);
```

驱动优先从设备树 `interrupts` 属性解析中断号。如果没有解析到，就使用 `gpio_to_irq()` 从 GPIO 转换为 IRQ。

### 6.4 设置中断触发方式

```c
irq_flags = irq_get_trigger_type(data->irq_num);
if (irq_flags == IRQF_TRIGGER_NONE)
	irq_flags = IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING;
```

按键需要同时识别按下和松开，所以通常使用双边沿触发。设备树没有指定触发类型时，驱动默认使用上升沿和下降沿。

### 6.5 申请中断

```c
request_irq(data->irq_num, key_interrupt, irq_flags, data->label, data);
```

最后一个参数传入 `data`，这样中断处理函数可以通过 `dev_id` 知道是哪一个按键触发。

## 7. 中断和防抖

中断处理函数如下：

```c
static irqreturn_t key_interrupt(int irq, void *dev_id)
{
	struct key_data *data = dev_id;

	mod_timer(&data->timer, jiffies + msecs_to_jiffies(KEY_DEBOUNCE_MS));
	return IRQ_HANDLED;
}
```

中断里不直接读取 GPIO，而是启动 15ms 定时器。这样可以过滤机械按键抖动。

防抖时间由宏定义：

```c
#define KEY_DEBOUNCE_MS 15
```

如果按键抖动导致短时间内多次中断，`mod_timer()` 会不断重新设置定时器到“最后一次中断后的 15ms”，最终只在电平稳定后执行一次检测。

## 8. 定时器生成按键事件

定时器函数会：

1. 通过 `from_timer()` 找到当前按键。
2. 读取 GPIO 电平。
3. 根据 `active_low` 转换为逻辑按下状态。
4. 与 `last_pressed` 比较。
5. 判断是按下、松开还是保持。
6. 如果状态变化，保存事件并唤醒等待队列。

核心代码：

```c
if (pressed && !data->last_pressed)
	status = KEY_PRESS;
else if (!pressed && data->last_pressed)
	status = KEY_RELEASE;
else
	status = KEY_KEEP;
```

有有效事件时：

```c
spin_lock_irqsave(&dev->spinlock, flags);
dev->event.id = data->id;
dev->event.status = status;
dev->has_event = 1;
spin_unlock_irqrestore(&dev->spinlock, flags);

wake_up_interruptible(&dev->waitq);
```

`wake_up_interruptible()` 不只会唤醒阻塞在 `read()` 的进程，也会唤醒通过 `poll_wait()` 挂到这个等待队列上的 `select/poll` 进程。

## 9. open 的作用

打开设备时执行：

```c
static int key_open(struct inode *inode, struct file *filp)
```

主要做两件事。

第一，保存设备结构体：

```c
filp->private_data = &key;
```

后续 `read()` 和 `poll()` 都可以通过 `filp->private_data` 找到设备对象。

第二，清除旧事件：

```c
key.event.id = -1;
key.event.status = KEY_KEEP;
key.has_event = 0;
```

这样应用刚打开设备时，不会读到历史残留事件。

## 10. read 的非阻塞逻辑

`noblockio.c` 的 `key_read()` 同时支持阻塞和非阻塞。

### 10.1 判断用户缓冲区大小

```c
if (cnt < sizeof(event))
	return -EINVAL;
```

应用层必须传入至少一个 `struct key_event` 大小的缓冲区。

### 10.2 判断是否非阻塞打开

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

这里是 `12_noblockio` 的核心。

如果用户程序用下面方式打开：

```c
open("/dev/key", O_RDONLY | O_NONBLOCK);
```

那么 `filp->f_flags` 中就会带有 `O_NONBLOCK`。

这时如果没有事件：

```c
return -EAGAIN;
```

含义是：当前没有数据，请稍后再试。

如果不是非阻塞打开，则行为和 `11_blockio` 一样，会进入等待队列睡眠。

### 10.3 二次检查 has_event

```c
spin_lock_irqsave(&dev->spinlock, flags);
if (!dev->has_event) {
	spin_unlock_irqrestore(&dev->spinlock, flags);
	return -EAGAIN;
}
event = dev->event;
dev->has_event = 0;
spin_unlock_irqrestore(&dev->spinlock, flags);
```

这里加锁后再次检查 `has_event`，是为了避免竞争。

例如：

1. 某个进程判断有事件。
2. 另一个进程先一步读走并清空事件。
3. 当前进程加锁后发现事件已经没有了。

这种情况下返回 `-EAGAIN` 是合理的。

### 10.4 拷贝事件到用户空间

```c
copy_to_user(buf, &event, sizeof(event));
```

成功后返回：

```c
return sizeof(event);
```

## 11. poll/select 的核心原理

驱动提供了：

```c
static unsigned int key_poll(struct file *filp, struct poll_table_struct *wait)
```

并注册到文件操作表：

```c
.poll = key_poll,
```

### 11.1 poll_wait 的作用

```c
poll_wait(filp, &dev->waitq, wait);
```

这行代码的作用不是直接睡眠，而是把当前文件和等待队列关联起来。应用程序调用 `select()` 或 `poll()` 时，内核会通过这个接口知道：

```text
如果 /dev/key 暂时不可读，就可以睡在 dev->waitq 这个等待队列上。
```

后续按键事件到来，定时器函数执行：

```c
wake_up_interruptible(&dev->waitq);
```

内核就会重新检查 `key_poll()` 返回的状态。

### 11.2 返回可读状态

```c
if (dev->has_event)
	mask = POLLIN | POLLRDNORM;
```

含义是：

| 返回标志 | 含义 |
| --- | --- |
| `POLLIN` | 普通数据可读 |
| `POLLRDNORM` | 普通优先级数据可读 |

当 `has_event = 1` 时，`select()` 就会返回，应用层再调用 `read()` 读取事件。

## 12. 应用程序工作流程

`noblockioApp.c` 使用非阻塞方式打开设备：

```c
fd = open(argv[1], O_RDONLY | O_NONBLOCK);
```

然后使用 `select()` 等待设备可读：

```c
FD_ZERO(&readfds);
FD_SET(fd, &readfds);
ret = select(fd + 1, &readfds, NULL, NULL, NULL);
```

如果 `select()` 返回，并且确认 fd 可读：

```c
if (FD_ISSET(fd, &readfds)) {
	read(fd, &event, sizeof(event));
}
```

读取到事件后打印：

```c
if (event.status == KEY_PRESS)
	printf("KEY%d Press\n", event.id);
else if (event.status == KEY_RELEASE)
	printf("KEY%d Release\n", event.id);
```

完整流程如下：

```text
noblockioApp
 ├── open(/dev/key, O_RDONLY | O_NONBLOCK)
 ├── select() 等待设备可读
 │    └── 驱动 key_poll()
 │         ├── poll_wait() 挂到 waitq
 │         └── has_event 为 0，暂不可读
 ├── 按键中断
 ├── 定时器防抖
 ├── 保存 key_event
 ├── has_event = 1
 ├── wake_up_interruptible()
 ├── select() 返回
 ├── read() 读取事件
 └── 打印 KEYx Press/Release
```

## 13. 非阻塞 read 和 select 的关系

非阻塞 IO 不一定必须配合 `select()`，应用也可以一直循环：

```c
ret = read(fd, &event, sizeof(event));
if (ret < 0 && errno == EAGAIN)
	continue;
```

但这样会导致程序一直占用 CPU 空转。

当前测试程序使用 `select()` 的好处是：

1. 没有事件时进程可以睡眠。
2. 有事件时由内核唤醒。
3. 不需要应用层反复轮询消耗 CPU。
4. 同一个程序未来可以同时监听多个文件描述符。

所以 `O_NONBLOCK + select()` 是很常见的用户态事件等待方式。

## 14. 字符设备注册

驱动仍然使用标准字符设备框架：

```c
alloc_chrdev_region(&key.devid, 0, KEY_CNT, KEY_NAME);
cdev_init(&key.cdev, &key_fops);
cdev_add(&key.cdev, key.devid, KEY_CNT);
class_create(THIS_MODULE, KEY_NAME);
device_create(key.class, NULL, key.devid, NULL, KEY_NAME);
```

最终设备名为：

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
};
```

其中 `.poll` 是本章相对于 `11_blockio` 新增的重点。

## 15. Makefile 说明

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
$(CC) -O2 -Wall -o noblockioApp noblockioApp.c
```

常用命令：

```sh
make build
make clean
```

编译成功后会生成：

```text
noblockio.ko
noblockioApp
```

## 16. 当前实现的特点

当前驱动只保存一个最新事件：

```c
struct key_event event;
int has_event;
```

这对普通按键实验足够，但如果极短时间内多个按键连续触发，后来的事件可能覆盖前面的事件。更完整的实现可以使用循环队列保存多个事件。

本实验重点是理解：

1. 非阻塞 `read()` 如何返回 `-EAGAIN`。
2. `poll_wait()` 如何把进程挂到等待队列。
3. `select()` 如何等待设备可读。
4. 按键事件如何唤醒 `select()`。

## 17. 总结

`12_noblockio` 的核心可以概括为：

按键中断触发后，驱动通过定时器防抖生成 `key_event`，设置 `has_event = 1` 并唤醒等待队列；应用程序用 `O_NONBLOCK` 打开设备，通过 `select()` 等待设备可读，再用 `read()` 取走事件。

相比 `11_blockio`，本章新增的关键点是：

1. `O_NONBLOCK` 非阻塞打开。
2. 无数据时 `read()` 返回 `-EAGAIN`。
3. `.poll` 文件操作。
4. `poll_wait()` 和等待队列配合。
5. 用户态使用 `select()` 等待按键事件。
