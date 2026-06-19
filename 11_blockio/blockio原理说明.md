# 11_blockio 阻塞 IO 按键驱动原理说明

本文说明 `11_blockio` 目录下三按键阻塞 IO 驱动的工作原理。当前版本支持自定义设备树中的 3 个 GPIO 按键，驱动文件是 `blockio.c`，测试程序是 `blockioApp.c`。

## 1. 实验目标

这个实验的核心目标是：

1. 从设备树 `/lhb-keys` 节点解析 3 个按键。
2. 为每个按键申请 GPIO 和中断。
3. 按键触发中断后，使用内核定时器做 15ms 防抖。
4. 防抖确认后生成一个按键事件。
5. 用户程序调用 `read()` 时，如果没有按键事件就睡眠等待。
6. 有按键事件后唤醒用户程序，并把事件返回给用户空间。

这里的“阻塞 IO”指的是：应用层调用 `read()` 后，如果驱动里暂时没有数据，`read()` 不会立刻返回，而是让当前进程进入睡眠状态。直到按键事件到来，驱动唤醒等待队列，`read()` 才继续执行并返回数据。

## 2. 设备树结构

驱动会查找固定路径：

```c
key.nd = of_find_node_by_path("/lhb-keys");
```

并要求该节点兼容字符串为：

```c
"lhb,gpio-keys"
```

也就是说设备树大致需要类似下面这种结构：

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

实际 GPIO 控制器、引脚和触发类型要按你的硬件连接填写。驱动最大支持数量由下面这个宏控制：

```c
#define KEY_NUM 3
```

驱动会用 `for_each_available_child_of_node()` 遍历 `/lhb-keys` 下可用的子节点，每个子节点对应一个按键。

## 3. 关键数据结构

### 3.1 按键状态

```c
enum key_status {
	KEY_PRESS = 0,
	KEY_RELEASE,
	KEY_KEEP,
};
```

含义如下：

| 状态 | 数值 | 含义 |
| --- | --- | --- |
| `KEY_PRESS` | 0 | 按键按下 |
| `KEY_RELEASE` | 1 | 按键松开 |
| `KEY_KEEP` | 2 | 状态未变化 |

`KEY_KEEP` 不会返回给用户程序，它主要用于驱动内部判断“这次定时器检测没有产生新事件”。

### 3.2 返回给用户空间的事件

```c
struct key_event {
	int id;
	int status;
};
```

因为现在有 3 个按键，只返回“按下”或“松开”是不够的，用户空间还需要知道是哪一个按键。所以事件结构包含两个字段：

| 字段 | 含义 |
| --- | --- |
| `id` | 按键编号，按设备树子节点遍历顺序从 0 开始 |
| `status` | 按键状态，`KEY_PRESS` 或 `KEY_RELEASE` |

应用程序读到事件后，就能打印：

```text
KEY0 Press
KEY1 Release
KEY2 Press
```

### 3.3 单个按键结构体

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

每个按键都有一份 `struct key_data`：

| 成员 | 作用 |
| --- | --- |
| `id` | 当前按键编号 |
| `gpio` | Linux GPIO 编号 |
| `irq_num` | 当前 GPIO 对应的中断号 |
| `active_low` | 是否低电平有效 |
| `last_pressed` | 上一次稳定状态，用来判断按下或松开 |
| `label` | 按键名字，优先来自设备树 `label` |
| `timer` | 当前按键自己的防抖定时器 |
| `parent` | 指向总设备结构体 `key_dev` |

这里每个按键都有独立的定时器。这样 KEY0 抖动不会影响 KEY1、KEY2 的防抖逻辑。

### 3.4 整个设备结构体

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

重要成员如下：

| 成员 | 作用 |
| --- | --- |
| `devid` | 字符设备号 |
| `cdev` | 字符设备对象 |
| `class` / `device` | 用于自动创建设备节点 `/dev/key` |
| `nd` | `/lhb-keys` 设备树节点 |
| `keys[KEY_NUM]` | 保存 3 个按键的信息 |
| `key_num` | 实际解析到的按键数量 |
| `event` | 最近一次按键事件 |
| `has_event` | 是否有新事件可读 |
| `spinlock` | 保护 `event` 和 `has_event` |
| `waitq` | 读等待队列，用于阻塞 IO |

## 4. 驱动初始化流程

模块加载时会执行：

```c
module_init(mykey_init);
```

也就是进入 `mykey_init()`。

初始化流程如下：

```text
mykey_init()
 ├── 初始化自旋锁 spin_lock_init()
 ├── 初始化等待队列 init_waitqueue_head()
 ├── 初始化 event 和 has_event
 ├── 解析设备树 key_parse_dt()
 │    └── 遍历每个按键 key_parse_one()
 ├── 申请字符设备号 alloc_chrdev_region()
 ├── 初始化 cdev
 ├── 添加 cdev
 ├── 创建 class
 └── 创建设备节点 /dev/key
```

驱动加载成功后，用户空间可以通过 `/dev/key` 访问这个按键设备。

## 5. 设备树解析流程

### 5.1 查找根节点

```c
key.nd = of_find_node_by_path("/lhb-keys");
```

如果找不到 `/lhb-keys`，驱动加载失败。

### 5.2 检查节点是否启用

```c
of_device_is_available(key.nd)
```

设备树中需要有：

```dts
status = "okay";
```

如果节点被 disabled，驱动加载失败。

### 5.3 检查 compatible

```c
of_device_is_compatible(key.nd, "lhb,gpio-keys")
```

这样可以避免驱动误绑定到错误的设备树节点。

### 5.4 遍历子节点

```c
for_each_available_child_of_node(key.nd, child)
```

每个可用子节点都会调用：

```c
key_parse_one(child, i);
```

`i` 就是按键编号，依次为 0、1、2。

## 6. 单个按键初始化流程

`key_parse_one()` 负责初始化一个按键。

### 6.1 获取 GPIO

```c
data->gpio = of_get_named_gpio_flags(child, "gpios", 0, &gpio_flags);
```

这里读取的是子节点里的 `gpios` 属性。第三个参数 `0` 表示读取第 0 个 GPIO。

同时，`gpio_flags` 会保存设备树里的 GPIO 标志，比如：

```dts
GPIO_ACTIVE_LOW
```

### 6.2 判断是否低电平有效

```c
data->active_low = !!(gpio_flags & OF_GPIO_ACTIVE_LOW);
```

如果硬件按键按下时 GPIO 为低电平，就应该在设备树里写 `GPIO_ACTIVE_LOW`。驱动后面会用 `active_low` 把原始电平转换成逻辑上的“是否按下”。

转换逻辑如下：

```c
pressed = data->active_low ? !value : value;
```

含义是：

| 设备树配置 | GPIO 电平 | pressed |
| --- | --- | --- |
| `GPIO_ACTIVE_LOW` | 0 | 1，表示按下 |
| `GPIO_ACTIVE_LOW` | 1 | 0，表示松开 |
| `GPIO_ACTIVE_HIGH` | 1 | 1，表示按下 |
| `GPIO_ACTIVE_HIGH` | 0 | 0，表示松开 |

### 6.3 申请 GPIO 并设置为输入

```c
gpio_request(data->gpio, data->label);
gpio_direction_input(data->gpio);
```

按键 GPIO 必须设置为输入模式，因为驱动只读取外部按键状态，不主动输出电平。

### 6.4 获取中断号

```c
data->irq_num = irq_of_parse_and_map(child, 0);
if (!data->irq_num)
	data->irq_num = gpio_to_irq(data->gpio);
```

驱动优先从设备树 `interrupts` 属性解析中断号。如果设备树没有写中断属性，则退回使用 `gpio_to_irq()` 从 GPIO 转换得到中断号。

### 6.5 获取中断触发方式

```c
irq_flags = irq_get_trigger_type(data->irq_num);
if (irq_flags == IRQF_TRIGGER_NONE)
	irq_flags = IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING;
```

如果设备树中配置了触发类型，就使用设备树配置。如果没有配置，就默认使用上升沿和下降沿都触发。

按键通常需要双边沿触发，因为：

1. 按下会产生一次电平变化。
2. 松开也会产生一次电平变化。

这样驱动才能同时报告 `KEY_PRESS` 和 `KEY_RELEASE`。

### 6.6 初始化上一次状态

```c
data->last_pressed = data->active_low ?
		     !gpio_get_value(data->gpio) :
		     gpio_get_value(data->gpio);
```

驱动加载时先读取一次当前按键状态，保存到 `last_pressed`。后面定时器检测时，会把新状态和旧状态比较：

1. 旧状态未按下，新状态按下：产生 `KEY_PRESS`。
2. 旧状态按下，新状态未按下：产生 `KEY_RELEASE`。
3. 新旧状态一样：产生 `KEY_KEEP`，不通知用户。

### 6.7 初始化定时器并申请中断

```c
timer_setup(&data->timer, key_timer_function, 0);
request_irq(data->irq_num, key_interrupt, irq_flags, data->label, data);
```

注意 `request_irq()` 的最后一个参数传的是：

```c
data
```

中断发生时，内核会把这个指针传给中断处理函数：

```c
static irqreturn_t key_interrupt(int irq, void *dev_id)
```

这样中断处理函数就能知道“到底是哪一个按键触发了中断”。

## 7. 中断处理和按键防抖

### 7.1 为什么需要防抖

机械按键按下或松开时，电平不会马上稳定，通常会在几毫秒内来回抖动。如果中断里直接读取 GPIO，可能一次按下被识别成多次按下或松开。

所以驱动采用常见做法：

1. 中断发生时不马上判断按键状态。
2. 启动一个 15ms 定时器。
3. 等电平稳定后，在定时器函数里读取 GPIO。

防抖时间由宏控制：

```c
#define KEY_DEBOUNCE_MS 15
```

### 7.2 中断处理函数

```c
static irqreturn_t key_interrupt(int irq, void *dev_id)
{
	struct key_data *data = dev_id;

	mod_timer(&data->timer, jiffies + msecs_to_jiffies(KEY_DEBOUNCE_MS));
	return IRQ_HANDLED;
}
```

中断处理函数只做一件事：启动或重新启动当前按键的防抖定时器。

这里使用 `mod_timer()` 的好处是：如果按键抖动连续触发多次中断，定时器会被不断推迟到最后一次中断后的 15ms。这样最终只会在电平稳定后读取一次 GPIO。

## 8. 定时器函数如何生成事件

定时器到期后执行：

```c
static void key_timer_function(struct timer_list *arg)
```

### 8.1 找回当前按键结构体

```c
struct key_data *data = from_timer(data, arg, timer);
```

因为每个按键都有自己的 `timer`，通过 `from_timer()` 可以从 `timer_list` 指针反推出所属的 `struct key_data`。

### 8.2 读取并转换按键状态

```c
value = gpio_get_value(data->gpio);
pressed = data->active_low ? !value : value;
```

`value` 是原始 GPIO 电平，`pressed` 是转换后的逻辑状态：

1. `pressed = 1` 表示按下。
2. `pressed = 0` 表示松开。

### 8.3 判断按下、松开还是保持

```c
if (pressed && !data->last_pressed)
	status = KEY_PRESS;
else if (!pressed && data->last_pressed)
	status = KEY_RELEASE;
else
	status = KEY_KEEP;
```

然后更新旧状态：

```c
data->last_pressed = pressed;
```

如果是 `KEY_KEEP`，说明状态没有变化，直接返回：

```c
if (status == KEY_KEEP)
	return;
```

### 8.4 保存事件

如果产生了有效事件，就保存到全局设备结构体中：

```c
spin_lock_irqsave(&dev->spinlock, flags);
dev->event.id = data->id;
dev->event.status = status;
dev->has_event = 1;
spin_unlock_irqrestore(&dev->spinlock, flags);
```

这里使用自旋锁保护 `event` 和 `has_event`，原因是：

1. 定时器函数可能在软中断上下文执行。
2. 用户进程的 `read()` 也会访问同一份数据。
3. 如果不加锁，可能出现读写交叉，拿到不完整或过期的数据。

`has_event = 1` 表示驱动中已经有一个新事件可以被用户读取。

### 8.5 唤醒等待队列

```c
wake_up_interruptible(&dev->waitq);
```

如果用户程序正在 `read()` 中睡眠等待，这里会把它唤醒。

## 9. 阻塞 read 的核心原理

用户程序调用：

```c
read(fd, &event, sizeof(event));
```

会进入驱动的：

```c
static ssize_t key_read(struct file *filp, char __user *buf,
			size_t cnt, loff_t *offt)
```

### 9.1 检查用户缓冲区大小

```c
if (cnt < sizeof(event))
	return -EINVAL;
```

用户至少要提供一个 `struct key_event` 大小的缓冲区。

### 9.2 没有事件就睡眠

```c
ret = wait_event_interruptible(dev->waitq, dev->has_event);
if (ret)
	return ret;
```

这就是阻塞 IO 的核心。

`wait_event_interruptible()` 的意思是：

1. 判断条件 `dev->has_event` 是否为真。
2. 如果为真，继续往下执行。
3. 如果为假，当前进程进入可被信号打断的睡眠状态。
4. 等待 `wake_up_interruptible()` 唤醒。
5. 唤醒后再次检查 `dev->has_event`。

所以应用层没有按键事件时不会空转占用 CPU。

### 9.3 取出事件并清除标志

```c
spin_lock_irqsave(&dev->spinlock, flags);
event = dev->event;
dev->has_event = 0;
spin_unlock_irqrestore(&dev->spinlock, flags);
```

取出事件后，把 `has_event` 清 0。这样下一次 `read()` 又会进入等待状态，直到新的按键事件到来。

### 9.4 拷贝到用户空间

```c
if (copy_to_user(buf, &event, sizeof(event)))
	return -EFAULT;
```

内核空间不能直接访问用户空间指针，所以必须使用 `copy_to_user()`。

成功后返回：

```c
return sizeof(event);
```

应用层就能拿到完整事件。

## 10. open 的作用

应用程序打开 `/dev/key` 时会进入：

```c
static int key_open(struct inode *inode, struct file *filp)
```

主要做两件事。

第一，把驱动设备结构体保存到文件私有数据中：

```c
filp->private_data = &key;
```

后续 `read()` 可以通过：

```c
struct key_dev *dev = filp->private_data;
```

拿回设备对象。

第二，清除旧事件：

```c
key.event.id = -1;
key.event.status = KEY_KEEP;
key.has_event = 0;
```

这样应用刚打开设备时，不会读到打开之前残留的按键事件。

## 11. 字符设备注册

驱动使用标准字符设备框架：

```c
alloc_chrdev_region(&key.devid, 0, KEY_CNT, KEY_NAME);
cdev_init(&key.cdev, &key_fops);
cdev_add(&key.cdev, key.devid, KEY_CNT);
class_create(THIS_MODULE, KEY_NAME);
device_create(key.class, NULL, key.devid, NULL, KEY_NAME);
```

其中 `KEY_NAME` 是：

```c
#define KEY_NAME "key"
```

所以最终会创建设备节点：

```text
/dev/key
```

文件操作函数表是：

```c
static const struct file_operations key_fops = {
	.owner = THIS_MODULE,
	.open = key_open,
	.read = key_read,
	.release = key_release,
};
```

`11_blockio` 只实现阻塞读取，所以没有 `.poll`、`.fasync`。这些内容分别放在后面的非阻塞 IO 和异步通知实验里。

## 12. 应用程序工作流程

测试程序 `blockioApp.c` 的核心代码是：

```c
fd = open(argv[1], O_RDONLY);

for (;;) {
	ret = read(fd, &event, sizeof(event));

	if (event.status == KEY_PRESS)
		printf("KEY%d Press\n", event.id);
	else if (event.status == KEY_RELEASE)
		printf("KEY%d Release\n", event.id);
}
```

运行方式：

```sh
./blockioApp /dev/key
```

程序打开设备后一直循环读取。

如果没有按键事件，程序会阻塞在 `read()`，不会继续打印，也不会消耗大量 CPU。

当某个按键按下或松开时，完整流程如下：

```text
按键电平变化
 └── GPIO 触发中断
      └── key_interrupt()
           └── 启动 15ms 防抖定时器
                └── key_timer_function()
                     ├── 读取 GPIO 电平
                     ├── 判断 KEY_PRESS / KEY_RELEASE
                     ├── 保存 struct key_event
                     ├── has_event = 1
                     └── wake_up_interruptible()
                          └── read() 被唤醒
                               ├── 拷贝事件到用户空间
                               └── blockioApp 打印 KEYx Press/Release
```

## 13. Makefile 说明

当前 Makefile 会同时编译内核模块和用户程序。

```makefile
build: kernel_modules app
```

编译内核模块：

```makefile
$(MAKE) -C $(KERNELDIR) M=$(CURRENT_PATH) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules
```

编译应用程序：

```makefile
$(CC) -O2 -Wall -o blockioApp blockioApp.c
```

常用命令：

```sh
make build
make clean
```

编译成功后会生成：

```text
blockio.ko
blockioApp
```

## 14. 和单按键版本的区别

原来的单按键版本通常只需要返回一个状态值：

```c
int status;
```

现在支持 3 个按键，如果还只返回 `status`，应用程序只能知道“有按键按下或松开”，但不知道是哪一个按键。

所以当前版本使用：

```c
struct key_event {
	int id;
	int status;
};
```

这样用户空间可以同时知道：

1. `id`：哪个按键。
2. `status`：按下还是松开。

这是多按键驱动里非常重要的一点。

## 15. 当前实现的一个特点

当前驱动中只有一个全局 `event` 缓冲：

```c
struct key_event event;
int has_event;
```

这意味着驱动一次只保存一个最新事件。对于普通按键实验来说够用，因为人手按键速度很慢。

但如果多个按键在极短时间内连续触发，新的事件可能覆盖旧事件。更完整的产品级实现通常会使用循环队列保存多个事件，例如：

```text
event queue:
  [KEY0 Press] [KEY1 Press] [KEY0 Release] ...
```

本实验为了突出阻塞 IO、等待队列和中断防抖原理，使用单事件缓存，代码更简单，也更适合理解流程。

## 16. 总结

`11_blockio` 的核心机制可以概括为一句话：

按键中断只负责启动防抖定时器，定时器确认稳定状态后生成 `key_event`，然后唤醒阻塞在 `read()` 上的用户进程，用户进程拿到“按键编号 + 按键状态”并处理。

关键知识点包括：

1. 设备树多子节点解析。
2. GPIO 输入配置。
3. GPIO 到 IRQ 的映射。
4. 中断双边沿触发。
5. 定时器防抖。
6. 等待队列实现阻塞 IO。
7. `copy_to_user()` 返回事件给应用层。
8. 自旋锁保护中断/进程上下文共享数据。
