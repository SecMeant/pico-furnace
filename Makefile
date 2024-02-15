ESC := $(shell printf '\e')
RED := $(ESC)[0;31m
GREEN := $(ESC)[0;32m
PURPLE := $(ESC)[0;35m
ZERO := $(ESC)[0;0m

CONFIG_THERMO := Ktype
CONFIG_MAGNETRON  := 0
CONFIG_HOSTNAME   := "pico_furnace"
CONFIG_WATER := 1

CFLAGS += -DCONFIG_THERMO=$(CONFIG_THERMO)
CFLAGS += -DCONFIG_MAGNETRON=$(CONFIG_MAGNETRON)
CFLAGS += -DCONFIG_HOSTNAME=${CONFIG_HOSTNAME}
CFLAGS += -DCONFIG_WATER=${CONFIG_WATER}

all: build/ ninja

wlan.ini:
	@ echo -ne "$(RED)Error: $(PURPLE)wlan.ini$(RED) file not found! "
	@ echo -e "$(ZERO)Check $(GREEN)README.md$(ZERO) for instructions."
	@ exit 1

checkenv_%:
	@ if [ "${${*}}" = "" ]; then \
		echo -ne "$(RED)Error: Environment variable $(PURPLE)$*$(ZERO) not set. "; \
		echo -e "$(ZERO)Check $(GREEN)README.md$(ZERO) for instructions."; \
		exit 1; \
	fi

pico_sdk: checkenv_PICO_SDK_PATH checkenv_PICO_EXTRAS_PATH

build/: ./configure_wifi.sh ./scripts/build_native.sh wlan.ini pico_sdk
	./scripts/build_native.sh
	CFLAGS="$(CFLAGS)" ./configure_wifi.sh

ninja: build/
	ninja -C build/

build/furnace.uf2: ninja
flash: build/furnace.uf2
	scripts/rpiflash.sh $(RPI_USB)

clean:
	rm -rf build/
	rm -rf native/build/
	rm consteval_header.h

.PHONY: ninja flash clean pico_sdk
