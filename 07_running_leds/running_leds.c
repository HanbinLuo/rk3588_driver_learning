#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define LED_CNT 4
#define LEDOFF 0
#define LEDON 1

static int write_led(int fd, unsigned char state)
{
	int ret;

	ret = write(fd, &state, 1);
	if (ret != 1)
		return -1;

	return 0;
}

int main(int argc, char *argv[])
{
	int i;
	int current = 0;
	int delay_ms = 200;
	int fd[LED_CNT];
	char devname[LED_CNT][16];

	for (i = 0; i < LED_CNT; i++)
		fd[i] = -1;

	if (argc == 2)
		delay_ms = atoi(argv[1]);

	if (delay_ms <= 0)
		delay_ms = 200;

	for (i = 0; i < LED_CNT; i++) {
		snprintf(devname[i], sizeof(devname[i]), "/dev/led%d", i);
		fd[i] = open(devname[i], O_RDWR);
		if (fd[i] < 0) {
			printf("open %s failed: %s\n", devname[i], strerror(errno));
			goto close_leds;
		}

		if (write_led(fd[i], LEDOFF) < 0) {
			printf("turn off %s failed: %s\n", devname[i],
			       strerror(errno));
			goto close_leds;
		}
	}

	while (1) {
		for (i = 0; i < LED_CNT; i++) {
			if (write_led(fd[i], i == current ? LEDON : LEDOFF) < 0) {
				printf("write %s failed: %s\n", devname[i],
				       strerror(errno));
				goto close_leds;
			}
		}

		usleep(delay_ms * 1000);
		current = (current + 1) % LED_CNT;
	}

close_leds:
	for (i = 0; i < LED_CNT; i++) {
		if (fd[i] >= 0) {
			write_led(fd[i], LEDOFF);
			close(fd[i]);
		}
	}

	return -1;
}
