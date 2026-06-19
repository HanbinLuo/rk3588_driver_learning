/***************************************************************
Copyright (C) 2026 lhb.
文件名		: asyncnotiApp.c
描述		: 异步通知方式读取3个GPIO按键事件
使用方法	: ./asyncnotiApp /dev/key
***************************************************************/

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>

enum key_status {
	KEY_PRESS = 0,
	KEY_RELEASE,
	KEY_KEEP,
};

struct key_event {
	int id;
	int status;
};

static int fd;

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

int main(int argc, char *argv[])
{
	int flags = 0;

	if (argc != 2) {
		printf("Usage:\n"
		       "\t./asyncnotiApp /dev/key\n");
		return -1;
	}

	fd = open(argv[1], O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		printf("ERROR: %s file open failed!\n", argv[1]);
		return -1;
	}

	signal(SIGIO, sigio_signal_func);
	fcntl(fd, F_SETOWN, getpid());
	flags = fcntl(fd, F_GETFL);
	fcntl(fd, F_SETFL, flags | FASYNC);

	for (;;)
		sleep(2);

	close(fd);
	return 0;
}
