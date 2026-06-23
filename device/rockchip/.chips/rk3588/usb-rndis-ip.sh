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
		# Do not advertise a default gateway, so the host keeps WiFi/Ethernet for Internet access.
		# echo "option router $RNDIS_IP"
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
