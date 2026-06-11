export ARCH=arm64
export CROSS_COMPILE=/home/lhb/linux/rk3588_driver/rk3588_sdk/prebuilts/gcc/linux-x86/aarch64/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-
KERNELDIR := /home/lhb/linux/rk3588_driver/rk3588_sdk/kernel
CURRENT_PATH := $(shell pwd)
CC := $(CROSS_COMPILE)gcc
APP_TARGET := ledApp_semaphore
APP_SRCS := ledApp_semaphore.c
obj-m := gpioled_semaphore.o

build: kernel_modules app

kernel_modules:
	$(MAKE) -C $(KERNELDIR) M=$(CURRENT_PATH) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules

app: $(APP_TARGET)

$(APP_TARGET): $(APP_SRCS)
	$(CC) -O2 -Wall -o $@ $^

clean:
	$(MAKE) -C $(KERNELDIR) M=$(CURRENT_PATH) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) clean
	rm -f $(APP_TARGET)

.PHONY: build kernel_modules app clean
