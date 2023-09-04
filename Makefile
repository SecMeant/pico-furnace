all: build/ ninja

build/: ./configure_wifi.sh
	./configure_wifi.sh

ninja: build/
	ninja -C build/

build/furnace.uf2: ninja
flash: build/furnace.uf2
	# TODO: change rpi ID so that it works for other ones too lol
	[ -L /dev/disk/by-id/usb-RPI_RP2_E0C9125B0D9B-0\:0 ] && sudo dd if=./build/furnace.uf2 of=/dev/disk/by-id/usb-RPI_RP2_E0C9125B0D9B-0\:0 bs=4096 oflag=sync status=progress

clean:
	rm -rf build/

.PHONY: ninja flash clean
