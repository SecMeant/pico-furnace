ESC := $(shell printf '\e')
RED := $(ESC)[0;31m
GREEN := $(ESC)[0;32m
PURPLE := $(ESC)[0;35m
ZERO := $(ESC)[0;0m

CONFIG_THERMO_NONE  = 0
CONFIG_THERMO_KTYPE = 1
CONFIG_THERMO_PT100 = 2
CONFIG_THERMO_INVALID = 3 # Has to be last

CONFIG_THERMO := ktype
CONFIG_MAGNETRON  := 0
CONFIG_HOSTNAME   := "pico_furnace"
CONFIG_WATER := 1
CONFIG_FURNACE_FIRE_PIN := 21

ifeq ($(CONFIG_THERMO),ktype)
	CONFIG_THERMO_INTERNAL=$(CONFIG_THERMO_KTYPE)
else ifeq ($(CONFIG_THERMO),pt100)
	CONFIG_THERMO_INTERNAL=$(CONFIG_THERMO_PT100)
else ifeq ($(CONFIG_THERMO),none)
	CONFIG_THERMO_INTERNAL=$(CONFIG_THERMO_NONE)
else
	CONFIG_THERMO_INTERNAL=$(CONFIG_THERMO_INVALID)
endif

CFLAGS += -DCONFIG_THERMO_NONE=$(CONFIG_THERMO_NONE)
CFLAGS += -DCONFIG_THERMO_KTYPE=$(CONFIG_THERMO_KTYPE)
CFLAGS += -DCONFIG_THERMO_PT100=$(CONFIG_THERMO_PT100)
CFLAGS += -DCONFIG_THERMO_MAX=$(CONFIG_THERMO_PT100)

CFLAGS += -DCONFIG_THERMO=$(CONFIG_THERMO_INTERNAL)
CFLAGS += -DCONFIG_MAGNETRON=$(CONFIG_MAGNETRON)
CFLAGS += -DCONFIG_HOSTNAME=${CONFIG_HOSTNAME}
CFLAGS += -DCONFIG_WATER=${CONFIG_WATER}
CFLAGS += -DCONFIG_FURNACE_FIRE_PIN=${CONFIG_FURNACE_FIRE_PIN}

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
	rm -f consteval_header.h

.PHONY: ninja flash clean pico_sdk
