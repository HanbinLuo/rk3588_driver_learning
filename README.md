# RK3588 USB ADB + RNDIS 复合设备

## 方案

使用一个 USB composite gadget，在同一个 USB 口上同时提供：

```text
ADB + RNDIS USB 网卡
```

当前验证结果：

```text
板端 usb0: 192.168.110.1/24
DHCP 地址池: 192.168.110.2 - 192.168.110.20
主机可自动获取 IPv4 地址，也可以手动配置 192.168.110.2/24
ADB: 正常
```

## 关键结论

- 不再使用“USB0 ADB + USB1 独立 RNDIS”的双 gadget 方案。
- 双 gadget 方案曾在 `fc400000.usb` 枚举时触发内核 Oops。
- 当前方案让 SDK 原有 `usbdevice` 创建一个复合 gadget，更符合 Rockchip SDK 的设计。
- `usb0` 是 RNDIS 网卡接口名，不代表 USB0 物理口。

## 备份文件

### `rk3588_sdk/kernel/arch/arm64/boot/dts/rockchip/rk3588-atk-devkit.dtsi`

原始路径：

```text
/home/lhb/linux/rk3588_driver/rk3588_sdk/kernel/arch/arm64/boot/dts/rockchip/rk3588-atk-devkit.dtsi
```

作用：

- 保证 USB0 DWC3/OTG gadget 控制器可用。
- 当前 USB0 节点：

```dts
&usbdrd_dwc3_0 {
	dr_mode = "otg";
	status = "okay";
	usb-role-switch;
};
```

当前备份文件中 USB1 gadget/device 节点为 disabled，用于避免 `fc400000.usb` 再作为独立 gadget 枚举。

### `rk3588_sdk/kernel/arch/arm64/configs/rockchip_linux_defconfig`

原始路径：

```text
/home/lhb/linux/rk3588_driver/rk3588_sdk/kernel/arch/arm64/configs/rockchip_linux_defconfig
```

新增/保留配置：

```text
CONFIG_USB_CONFIGFS_NCM=y
CONFIG_USB_CONFIGFS_ECM=y
CONFIG_USB_CONFIGFS_RNDIS=y
```

作用：

- 让内核 configfs gadget 支持 USB 网卡相关 function。
- 当前实际使用的是 RNDIS。

### `rk3588_sdk/device/rockchip/.chips/rk3588/alientek_rk3588_defconfig`

原始路径：

```text
/home/lhb/linux/rk3588_driver/rk3588_sdk/device/rockchip/.chips/rk3588/alientek_rk3588_defconfig
```

关键配置：

```text
RK_USB_ENABLED=y
RK_USB_ADBD=y
RK_USB_RNDIS=y
RK_USB_HOOKS="usb-rndis-ip.sh"
```

作用：

- 安装并启用 SDK 原有 `usbdevice` 服务。
- 让 `usbdevice` 在同一个 gadget 中创建 `adb + rndis`。
- 安装 hook，用于给 RNDIS 网卡配置静态 IP 并启动 DHCP 服务。

### `rk3588_sdk/buildroot/configs/rockchip/alientek.config`

原始路径：

```text
/home/lhb/linux/rk3588_driver/rk3588_sdk/buildroot/configs/rockchip/alientek.config
```

关键配置：

```text
BR2_PACKAGE_BUSYBOX_CONFIG_FRAGMENT_FILES+=" board/rockchip/alientek/busybox.fragment"
```

作用：

- 引入 ALIENTEK 专用 BusyBox 配置片段。
- 保证 rootfs 里会编译出 `udhcpd`，供 USB RNDIS DHCP 使用。

### `rk3588_sdk/buildroot/board/rockchip/alientek/busybox.fragment`

原始路径：

```text
/home/lhb/linux/rk3588_driver/rk3588_sdk/buildroot/board/rockchip/alientek/busybox.fragment
```

内容：

```text
CONFIG_UDHCPD=y
CONFIG_FEATURE_UDHCPD_WRITE_LEASES_EARLY=y
CONFIG_DHCPD_LEASES_FILE="/tmp/udhcpd.leases"
```

作用：

- 打开 BusyBox 的 DHCP server applet。
- 让 `usb-rndis-ip.sh` 可以启动 `udhcpd` 给主机分配地址。

### `rk3588_sdk/external/rkscript/usbdevice`

原始路径：

```text
/home/lhb/linux/rk3588_driver/rk3588_sdk/external/rkscript/usbdevice
```

关键改动：

```sh
source "/etc/usbdevice.d/$hook"
USB_UDC=${USB_UDC:-$(ls /sys/class/udc/ | grep -m 1 fc000000 || true)}
USB_UDC=${USB_UDC:-$(ls /sys/class/udc/ | head -n 1)}
```

作用：

- 正确从 `/etc/usbdevice.d/` 加载 hook。
- 优先把 composite gadget 绑定到 `fc000000.usb`。

### `rk3588_sdk/device/rockchip/.chips/rk3588/usb-rndis-ip.sh`

原始路径：

```text
/home/lhb/linux/rk3588_driver/rk3588_sdk/device/rockchip/.chips/rk3588/usb-rndis-ip.sh
```

内容：

```sh
RNDIS_IP=${RNDIS_IP:-192.168.110.1}
RNDIS_NETMASK=${RNDIS_NETMASK:-255.255.255.0}
RNDIS_DHCP_START=${RNDIS_DHCP_START:-192.168.110.2}
RNDIS_DHCP_END=${RNDIS_DHCP_END:-192.168.110.20}
RNDIS_DHCP_LEASE_TIME=${RNDIS_DHCP_LEASE_TIME:-86400}
RNDIS_DHCP_CONF=${RNDIS_DHCP_CONF:-/tmp/udhcpd-usb0.conf}
RNDIS_DHCP_PID=${RNDIS_DHCP_PID:-/tmp/udhcpd-usb0.pid}
RNDIS_DHCP_LEASES=${RNDIS_DHCP_LEASES:-/tmp/udhcpd-usb0.leases}

rndis_dhcp_stop()
{
	if [ -f "$RNDIS_DHCP_PID" ]; then
		kill "$(cat "$RNDIS_DHCP_PID")" 2>/dev/null || true
		rm -f "$RNDIS_DHCP_PID"
	fi

	killall udhcpd 2>/dev/null || true
}

rndis_dhcp_start()
{
	if ! command -v udhcpd >/dev/null 2>&1; then
		echo "udhcpd not found, skip USB RNDIS DHCP server"
		return 0
	fi

	rndis_dhcp_stop
	touch "$RNDIS_DHCP_LEASES"

	{
		echo "start $RNDIS_DHCP_START"
		echo "end $RNDIS_DHCP_END"
		echo "interface usb0"
		echo "option subnet $RNDIS_NETMASK"
		echo "option router $RNDIS_IP"
		echo "option lease $RNDIS_DHCP_LEASE_TIME"
		echo "lease_file $RNDIS_DHCP_LEASES"
		echo "pidfile $RNDIS_DHCP_PID"
	} > "$RNDIS_DHCP_CONF"

	udhcpd "$RNDIS_DHCP_CONF"
}

rndis_start()
{
	ifconfig usb0 "$RNDIS_IP" netmask "$RNDIS_NETMASK" up
	rndis_dhcp_start
}

rndis_stop()
{
	rndis_dhcp_stop
	ifconfig usb0 down 2>/dev/null || true
}
```

作用：

- RNDIS function 启动后配置板端 USB 网卡 IP。
- 启动 `udhcpd`，给 Windows/Linux 主机自动分配 `192.168.110.x` 地址。
- 不创建第二个独立 USB gadget。

默认网络：

```text
板端 IP:       192.168.110.1
DHCP 起始地址: 192.168.110.2
DHCP 结束地址: 192.168.110.20
子网掩码:      255.255.255.0
```

## 编译

如果改动包含 DTS 或 kernel config：

```sh
cd /home/lhb/linux/rk3588_driver/rk3588_sdk
./build.sh kernel
./build.sh buildroot
./build.sh firmware
./build.sh updateimg
```

如果只改 rootfs 脚本或板级 rootfs 配置：

```sh
./build.sh buildroot
./build.sh firmware
./build.sh updateimg
```

## 板端验证

```sh
cat /var/log/usbdevice.log | grep "Starting functions"
cat /sys/kernel/config/usb_gadget/rockchip/UDC
ls /sys/kernel/config/usb_gadget/rockchip/functions
ifconfig usb0
```

期望：

```text
Starting functions: rndis adb
fc000000.usb
ffs.adb
rndis.gs0
192.168.110.1
udhcpd
```

也可以确认 DHCP 服务：

```sh
which udhcpd
ps | grep udhcpd
cat /tmp/udhcpd-usb0.conf
```

## 主机端验证

主机 USB 网卡配置：

```text
IPv4: 自动获取
```

测试：

```sh
adb devices
ping 192.168.110.1
ssh root@192.168.110.1
```

Windows 上如果之前手动填过 IPv4，需要改回“自动获得 IP 地址”，或者禁用/启用一次该 USB 网卡让它重新 DHCP。

已验证 ping 结果：

```text
11 packets transmitted, 11 received, 0% packet loss
rtt avg about 0.333 ms
```


