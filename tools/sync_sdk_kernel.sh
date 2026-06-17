#!/bin/bash
# Sync selected RK3588 config/device-tree files between rk3588_kernel and rk3588_sdk.

set -u

KERNEL_ROOT="/home/lhb/linux/rk3588_driver/rk3588_kernel"
SDK_ROOT="/home/lhb/linux/rk3588_driver/rk3588_sdk"

SDK_FILES=(
    "buildroot/configs/rockchip_atk_dlrk3588_defconfig"
    "buildroot/board/rockchip/rk3588/fs-overlay/etc/inittab"
    "kernel/arch/arm64/boot/dts/rockchip/rk3588-linux.dtsi"
    "kernel/arch/arm64/configs/rockchip_linux_defconfig"
)

DEVKIT_DTSI="kernel/arch/arm64/boot/dts/rockchip/rk3588-atk-devkit.dtsi"

function usage() {
    echo "Usage: $0 [sdk|kernel]"
    echo
    echo "  sdk     Update selected files in rk3588_sdk from rk3588_kernel"
    echo "  kernel  Backup rk3588-atk-devkit.dtsi from rk3588_sdk to rk3588_kernel"
}

function check_root() {
    local root="$1"
    local name="$2"

    if [ ! -d "$root" ]; then
        echo "Error: $name not found: $root"
        exit 1
    fi
}

function copy_file() {
    local src="$1"
    local dst="$2"

    if [ ! -f "$src" ]; then
        echo "Skip: source file not found: $src"
        return 1
    fi

    mkdir -p "$(dirname "$dst")"
    cp -av "$src" "$dst"
}

function update_sdk() {
    local failed=0

    echo "Update SDK from kernel:"
    echo "  KERNEL_ROOT=$KERNEL_ROOT"
    echo "  SDK_ROOT=$SDK_ROOT"
    echo

    for relpath in "${SDK_FILES[@]}"; do
        if ! copy_file "${KERNEL_ROOT}/${relpath}" "${SDK_ROOT}/${relpath}"; then
            failed=1
        fi
    done

    return "$failed"
}

function backup_to_kernel() {
    echo "Backup rk3588-atk-devkit.dtsi from SDK to kernel:"
    echo "  SDK_ROOT=$SDK_ROOT"
    echo "  KERNEL_ROOT=$KERNEL_ROOT"
    echo

    copy_file "${SDK_ROOT}/${DEVKIT_DTSI}" "${KERNEL_ROOT}/${DEVKIT_DTSI}"
}

function select_mode() {
    echo "===========================================" >&2
    echo "   RK3588 SDK/Kernel Sync Script" >&2
    echo "===========================================" >&2
    echo "1. Update SDK from rk3588_kernel" >&2
    echo "2. Backup rk3588-atk-devkit.dtsi to rk3588_kernel" >&2
    echo "q. Quit" >&2
    echo "===========================================" >&2
    read -r -p "Please select [1/2/q]: " choice

    case "$choice" in
        1)
            echo "sdk"
            ;;
        2)
            echo "kernel"
            ;;
        q|Q)
            echo "quit"
            ;;
        *)
            echo "Invalid selection."
            exit 1
            ;;
    esac
}

check_root "$KERNEL_ROOT" "KERNEL_ROOT"
check_root "$SDK_ROOT" "SDK_ROOT"

mode="${1:-}"
if [ -z "$mode" ]; then
    mode="$(select_mode)"
fi

case "$mode" in
    sdk|update-sdk)
        update_sdk
        ;;
    kernel|backup-kernel)
        backup_to_kernel
        ;;
    quit)
        exit 0
        ;;
    -h|--help|help)
        usage
        ;;
    *)
        usage
        exit 1
        ;;
esac
