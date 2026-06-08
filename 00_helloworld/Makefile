CROSS_COMPILE ?= /home/lhb/linux/rk3588_driver/rk3588_sdk/prebuilts/gcc/linux-x86/aarch64/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-
CC := $(CROSS_COMPILE)gcc

TARGET := helloworld
SRCS := main.c

build: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) -O2 -Wall -o $@ $^

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: build run clean
