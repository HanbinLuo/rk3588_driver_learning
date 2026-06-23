#!/bin/bash

set -e

SDK_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
UPGRADE_TOOL="${SDK_ROOT}/tools/linux/Linux_Upgrade_Tool/Linux_Upgrade_Tool/upgrade_tool"
ROCKDEV_PATH="${SDK_ROOT}/rockdev"
PARAMETER_FILE="${ROCKDEV_PATH}/parameter.txt"

die()
{
	echo "Error: $*" >&2
	exit 1
}

check_file()
{
	[ -f "$1" ] || die "$1 not found"
}

run_upgrade()
{
	echo
	echo "Command: sudo $UPGRADE_TOOL $*"
	sudo "$UPGRADE_TOOL" "$@"
}

partition_flag()
{
	case "$1" in
		boot) echo "-b" ;;
		recovery) echo "-r" ;;
		parameter) echo "-p" ;;
		*) echo "-$1" ;;
	esac
}

parse_partitions()
{
	local cmdline parts entry name

	cmdline="$(grep '^CMDLINE:' "$PARAMETER_FILE" || true)"
	[ -n "$cmdline" ] || die "CMDLINE not found in $PARAMETER_FILE"

	parts="${cmdline#*mtdparts=:}"
	[ "$parts" != "$cmdline" ] || die "mtdparts not found in CMDLINE"

	PARTITIONS=()
	IFS=',' read -r -a entries <<< "$parts"
	for entry in "${entries[@]}"; do
		if [[ "$entry" =~ \(([^:\)]+) ]]; then
			name="${BASH_REMATCH[1]}"
			PARTITIONS+=("$name")
		fi
	done

	[ "${#PARTITIONS[@]}" -gt 0 ] || die "No partitions parsed from $PARAMETER_FILE"
}

flash_update_img()
{
	local img="${ROCKDEV_PATH}/update.img"
	check_file "$img"
	echo "Flashing full update.img..."
	run_upgrade UF "$img"
}

flash_loader()
{
	local img="${ROCKDEV_PATH}/MiniLoaderAll.bin"
	check_file "$img"
	echo "Flashing loader..."
	run_upgrade UL "$img"
}

flash_parameter()
{
	check_file "$PARAMETER_FILE"
	echo "Flashing parameter.txt..."
	run_upgrade DI -p "$PARAMETER_FILE"
}

flash_partition()
{
	local part="$1"
	local img="${ROCKDEV_PATH}/${part}.img"
	local flag

	check_file "$img"
	flag="$(partition_flag "$part")"
	echo "Flashing ${part} from ${img}..."
	run_upgrade DI "$flag" "$img"
}

reset_device()
{
	run_upgrade RD
}

print_header()
{
	echo "==========================================="
	echo "   RK3588 Flash Script"
	echo "==========================================="
	echo "SDK root : $SDK_ROOT"
	echo "Images   : $ROCKDEV_PATH"
	echo "Param    : $PARAMETER_FILE"
	echo "==========================================="
}

build_menu()
{
	MENU_TYPES=()
	MENU_VALUES=()
	MENU_TEXTS=()

	if [ -f "${ROCKDEV_PATH}/update.img" ]; then
		MENU_TYPES+=("update")
		MENU_VALUES+=("update")
		MENU_TEXTS+=("Flash full update.img")
	fi

	if [ -f "${ROCKDEV_PATH}/MiniLoaderAll.bin" ]; then
		MENU_TYPES+=("loader")
		MENU_VALUES+=("loader")
		MENU_TEXTS+=("Flash loader MiniLoaderAll.bin")
	fi

	if [ -f "$PARAMETER_FILE" ]; then
		MENU_TYPES+=("parameter")
		MENU_VALUES+=("parameter")
		MENU_TEXTS+=("Flash parameter.txt")
	fi

	for part in "${PARTITIONS[@]}"; do
		if [ -f "${ROCKDEV_PATH}/${part}.img" ]; then
			MENU_TYPES+=("partition")
			MENU_VALUES+=("$part")
			MENU_TEXTS+=("Flash ${part}.img")
		else
			MISSING_PARTITIONS+=("$part")
		fi
	done

	MENU_TYPES+=("reset")
	MENU_VALUES+=("reset")
	MENU_TEXTS+=("Reset device")
}

print_menu()
{
	local i

	for i in "${!MENU_TEXTS[@]}"; do
		printf "%2d. %s\n" "$((i + 1))" "${MENU_TEXTS[$i]}"
	done

	if [ "${#MISSING_PARTITIONS[@]}" -gt 0 ]; then
		echo "-------------------------------------------"
		echo "Partitions without image in rockdev:"
		printf "    %s\n" "${MISSING_PARTITIONS[@]}"
	fi

	echo " q. Quit"
	echo "==========================================="
}

handle_choice()
{
	local choice="$1"
	local index type value

	case "$choice" in
		q|Q) exit 0 ;;
	esac

	[[ "$choice" =~ ^[0-9]+$ ]] || die "Invalid selection: $choice"
	index=$((choice - 1))

	[ "$index" -ge 0 ] && [ "$index" -lt "${#MENU_TYPES[@]}" ] || \
		die "Invalid selection: $choice"

	type="${MENU_TYPES[$index]}"
	value="${MENU_VALUES[$index]}"

	case "$type" in
		update) flash_update_img ;;
		loader) flash_loader ;;
		parameter) flash_parameter ;;
		partition) flash_partition "$value" ;;
		reset) reset_device ;;
		*) die "Unknown menu type: $type" ;;
	esac
}

check_file "$UPGRADE_TOOL"
[ -d "$ROCKDEV_PATH" ] || die "rockdev directory not found at $ROCKDEV_PATH"
check_file "$PARAMETER_FILE"

PARTITIONS=()
MISSING_PARTITIONS=()
parse_partitions
build_menu

print_header
print_menu
read -r -p "Please select: " choice
handle_choice "$choice"
