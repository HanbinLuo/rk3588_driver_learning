#!/bin/bash
# 放在SDK根目录下执行，用于Linux系统烧录RK3588，RK3588需要处于Loader模式
# 定义路径
SDK_ROOT=$(pwd)
UPGRADE_TOOL="${SDK_ROOT}/tools/linux/Linux_Upgrade_Tool/Linux_Upgrade_Tool/upgrade_tool"
ROCKDEV_PATH="${SDK_ROOT}/rockdev"

# 检查工具是否存在
if [ ! -f "$UPGRADE_TOOL" ]; then
    echo "Error: upgrade_tool not found at $UPGRADE_TOOL"
    exit 1
fi

# 检查烧录镜像目录
if [ ! -d "$ROCKDEV_PATH" ]; then
    echo "Error: rockdev directory not found at $ROCKDEV_PATH"
    exit 1
fi

function flash_update_img() {
    local img="${ROCKDEV_PATH}/update.img"
    if [ ! -f "$img" ]; then
        echo "Error: $img not found!"
        return 1
    fi
    echo "Starting to flash complete update.img..."
    sudo "$UPGRADE_TOOL" UF "$img"
}

function flash_boot_img() {
    local img="${ROCKDEV_PATH}/boot.img"
    if [ ! -f "$img" ]; then
        echo "Error: $img not found!"
        return 1
    fi
    echo "Starting to flash boot.img..."
    sudo "$UPGRADE_TOOL" DI -b "$img"
}

echo "==========================================="
echo "   RK3588 Flash Script"
echo "==========================================="
echo "1. Flash Full update.img"
echo "2. Flash Kernel boot.img"
echo "q. Quit"
echo "==========================================="
read -p "Please select [1/2/q]: " choice

case $choice in
    1)
        flash_update_img
        ;;
    2)
        flash_boot_img
        ;;
    q|Q)
        exit 0
        ;;
    *)
        echo "Invalid selection."
        exit 1
        ;;
esac
