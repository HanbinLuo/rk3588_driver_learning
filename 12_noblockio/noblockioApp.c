/***************************************************************
Copyright (C) 2026 lhb.
文件名		: noblockioApp.c
描述		: 非阻塞方式读取3个GPIO按键事件
使用方法	: ./noblockioApp /dev/key
***************************************************************/

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/select.h>

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
	fd_set readfds;
	int fd;
	int ret;
	struct key_event event;

	if (argc != 2) {
		printf("Usage:\n"
		       "\t./noblockioApp /dev/key\n");
		return -1;
	}

	fd = open(argv[1], O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		printf("ERROR: %s file open failed!\n", argv[1]);
		return -1;
	}

	for (;;) {
		FD_ZERO(&readfds);
		FD_SET(fd, &readfds);

		ret = select(fd + 1, &readfds, NULL, NULL, NULL);
		if (ret < 0) {
			perror("select");
			break;
		}

		if (!FD_ISSET(fd, &readfds))
			continue;

		ret = read(fd, &event, sizeof(event));
		if (ret < 0) {
			if (errno == EAGAIN)
				continue;
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
