#include "stdio.h"
#include "unistd.h"
#include "sys/types.h"
#include "sys/stat.h"
#include "fcntl.h"
#include "stdlib.h"

#define LEDOFF	0
#define LEDON	1
#define LED_NUM	4

int main(int argc, char *argv[])
{
	int fd, retvalue;
	int led_id, led_state;
	unsigned char databuf[2];

	if (argc != 4) {
		printf("Usage: %s /dev/platled <led_id:0-3> <0|1>\n", argv[0]);
		return -1;
	}

	led_id = atoi(argv[2]);
	led_state = atoi(argv[3]);
	if (led_id < 0 || led_id >= LED_NUM ||
	    (led_state != LEDOFF && led_state != LEDON)) {
		printf("Invalid argument: led_id must be 0-3, state must be 0 or 1\n");
		return -1;
	}

	fd = open(argv[1], O_RDWR);
	if (fd < 0) {
		printf("file %s open failed!\n", argv[1]);
		return -1;
	}

	databuf[0] = led_id;
	databuf[1] = led_state;
	retvalue = write(fd, databuf, sizeof(databuf));
	if (retvalue < 0) {
		printf("LED Control Failed!\n");
		close(fd);
		return -1;
	}

	retvalue = close(fd);
	if (retvalue < 0) {
		printf("file %s close failed!\n", argv[1]);
		return -1;
	}

	return 0;
}
