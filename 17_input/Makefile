export ARCH=arm64
export CROSS_COMPILE=/home/lhb/linux/rk3588_driver/rk3588_sdk/prebuilts/gcc/linux-x86/aarch64/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-
KERNELDIR := /home/lhb/linux/rk3588_driver/rk3588_sdk/kernel
CURRENT_PATH := $(shell pwd)
CC := $(CROSS_COMPILE)gcc
APP_TARGET := keyinputApp
APP_SRCS := keyinputApp.c

build: app

app: $(APP_TARGET)

$(APP_TARGET): $(APP_SRCS)
	$(CC) -O2 -Wall -o $@ $^

clean:
	rm -f $(APP_TARGET)

.PHONY: build app clean
