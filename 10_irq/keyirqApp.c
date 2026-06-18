/***************************************************************
Copyright © ALIENTEK Co., Ltd. 1998-2029. All rights reserved.
文件名              : keyApp.c
作者                : 正点原子Linux团队
版本                : V1.0
描述                : Linux中断驱动实验
其他                : 无
使用方法            : ./keyirqApp /dev/key
论坛                : www.openedv.com
日志                : 初版V1.0 2019/1/30 正点原子Linux团队创建
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

/*
 * @description		: main主程序
 * @param – argc		: argv数组元素个数
 * @param – argv		: 具体参数
 * @return			: 0 成功;其他 失败
 */
int main(int argc, char *argv[])
{
    int fd, ret;
    struct key_event event;

    /* 判断传参个数是否正确 */
    if(2 != argc) {
        printf("Usage:\n"
             "\t./keyApp /dev/key\n"
            );
        return -1;
    }

    /* 打开设备 */
    fd = open(argv[1], O_RDONLY);
    if(0 > fd) {
        printf("ERROR: %s file open failed!\n", argv[1]);
        return -1;
    }

    /* 循环读取按键数据 */
    for ( ; ; ) {
        ret = read(fd, &event, sizeof(event));
        if (ret < 0) {
            perror("read");
            break;
        }
        if (ret != sizeof(event))
            continue;

        if (KEY_PRESS == event.status)
            printf("KEY%d Press\n", event.id);
        else if (KEY_RELEASE == event.status)
            printf("KEY%d Release\n", event.id);
    }

    /* 关闭设备 */
    close(fd);
    return 0;
}

