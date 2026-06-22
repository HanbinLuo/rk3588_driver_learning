#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <linux/input.h>

/*
 * @description   	: main主程序
 * @param – argc 	: argv数组元素个数
 * @param – argv  	: 具体参数
 * @return       	: 0 成功;其他 失败
 */
int main(int argc, char *argv[])
{
    int fd, ret;
    struct input_event ev;

    if(2 != argc) {
        printf("Usage:\n"
             "\t./keyinputApp /dev/input/eventX    @ Open Key\n"
        );
        return -1;
    }

    /* 打开设备 */
    fd = open(argv[1], O_RDWR);
    if(0 > fd) {
        printf("Error: file %s open failed!\r\n", argv[1]);
        return -1;
    }

    /* 读取按键数据 */
    for ( ; ; ) {
        ret = read(fd, &ev, sizeof(struct input_event));
        if (ret > 0) {
            if (ev.type == EV_KEY) {
                switch (ev.code) {
                    case KEY_0:
                        printf("Key 0: %s\n", ev.value ? "Pressed" : "Released");
                        break;
                    case KEY_1:
                        printf("Key 1: %s\n", ev.value ? "Pressed" : "Released");
                        break;
                    case KEY_2:
                        printf("Key 2: %s\n", ev.value ? "Pressed" : "Released");
                        break;
                    default:
                        printf("Unknown Key (code %d): %s\n", ev.code, ev.value ? "Pressed" : "Released");
                        break;
                }
            }
        }
        else if (ret < 0) {
            perror("read event");
            break;
        }
    }

    /* 关闭设备 */
    close(fd);
    return 0;
}