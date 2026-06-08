export ARCH=arm64
export CROSS_COMPILE=/home/lhb/linux/rk3588_driver/rk3588_sdk/prebuilts/gcc/linux-x86/aarch64/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-
KERNELDIR := /home/lhb/linux/rk3588_driver/rk3588_sdk/kernel
CURRENT_PATH := $(shell pwd)
obj-m := gpioled.o

build: kernel_modules

kernel_modules:
	$(MAKE) -C $(KERNELDIR) M=$(CURRENT_PATH) modules
clean:
	$(MAKE) -C $(KERNELDIR) M=$(CURRENT_PATH) clean
