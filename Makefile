all: build/ ninja

build/: ./configure_wifi.sh
	./configure_wifi.sh

ninja: build/
	ninja -C build/

build/furnace.uf2: ninja
flash: build/furnace.uf2
	scripts/rpiflash.sh $(RPI_USB)

clean:
	rm -rf build/

.PHONY: ninja flash clean
