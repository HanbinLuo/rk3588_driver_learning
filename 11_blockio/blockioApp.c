/***************************************************************
Copyright (C) 2026 lhb.
文件名		: blockioApp.c
描述		: 阻塞方式读取3个GPIO按键事件
使用方法	: ./blockioApp /dev/key
***************************************************************/

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

enum key_status {
	KEY_PRESS = 0,
	KEY_RELEASE,
	KEY_KEEP,
};

struct key_event {
	int id;
	int status;
};

int main(int argc, char *argv[])
{
	int fd, ret;
	struct key_event event;

	if (argc != 2) {
		printf("Usage:\n"
		       "\t./blockioApp /dev/key\n");
		return -1;
	}

	fd = open(argv[1], O_RDONLY);
	if (fd < 0) {
		printf("ERROR: %s file open failed!\n", argv[1]);
		return -1;
	}

	for (;;) {
		ret = read(fd, &event, sizeof(event));
		if (ret < 0) {
			perror("read");
			break;
		}
		if (ret != sizeof(event))
			continue;

		if (event.status == KEY_PRESS)
			printf("KEY%d Press\n", event.id);
		else if (event.status == KEY_RELEASE)
			printf("KEY%d Release\n", event.id);
	}

	close(fd);
	return 0;
}
