#!/bin/sh

set -e

RPI_USB=$1

if [ -z "${RPI_USB}" ]
then
	RPI_USB=( $(find /dev/disk/by-id/ \( -name "*RPI_RP2*" \! -name "*-part[0-9]*" \)) )
	[ ${#RPI_USB[@]} -eq 0 ] && (echo "No rpi device found"; exit 1)
	[ ${#RPI_USB[@]} -ge 2 ] &&
		(echo "More than one rpi device found. Specify with RPI_USB="; exit 1)
else
	[ -e ${RPI_USB[0]} ] || (echo "File does not exist: ${RPI_USB[0]}"; exit 1)
fi

# Maybe we shouldn't use sudo, but It's convenient for now...
sudo dd if=./build/furnace.uf2 of="$RPI_USB" bs=4096 oflag=sync status=progress

