ESC := $(shell printf '\e')
RED := $(ESC)[0;31m
GREEN := $(ESC)[0;32m
PURPLE := $(ESC)[0;35m
ZERO := $(ESC)[0;0m

CONFVAL_THERMO_NONE  = 0
CONFVAL_THERMO_KTYPE = 1
CONFVAL_THERMO_PT100 = 2
CONFVAL_THERMO_INVALID = 3 # Has to be last

CONFVAL_AUTO_NONE = 0
CONFVAL_AUTO_MAPPER = 1
CONFVAL_AUTO_PILOT = 2
CONFVAL_AUTO_INVALID = 3 # Has to be last

CONFIG_THERMO := ktype
CONFIG_MAGNETRON  := 0
CONFIG_HOSTNAME   := "pico_furnace"
CONFIG_WATER := 1
CONFIG_FURNACE_FIRE_PIN := 21
CONFIG_FURNACE_DEADLINE_MS := 21000
CONFIG_MAX_PWM := 50U
CONFIG_SHUTTER := 0
CONFIG_AUTO := pilot
CONFIG_STIRRER := 0

TMP_CONFIG_FILE = /tmp/pico_furnace_config

ifneq ($(wildcard .config),)
include .config
else
ifeq (,$(filter clean distclean,$(MAKECMDGOALS)))
$(error Create .config file or use one from configs/)
endif
endif

ifeq ($(CONFIG_THERMO),ktype)
	CONFIG_THERMO_INTERNAL=$(CONFVAL_THERMO_KTYPE)
else ifeq ($(CONFIG_THERMO),pt100)
	CONFIG_THERMO_INTERNAL=$(CONFVAL_THERMO_PT100)
else ifeq ($(CONFIG_THERMO),none)
	CONFIG_THERMO_INTERNAL=$(CONFVAL_THERMO_NONE)
else
	CONFIG_THERMO_INTERNAL=$(CONFVAL_THERMO_INVALID)
endif

ifeq ($(CONFIG_AUTO),pilot)
	CONFIG_AUTO_INTERNAL=$(CONFVAL_AUTO_PILOT)
else ifeq ($(CONFIG_AUTO),mapper)
	CONFIG_AUTO_INTERNAL=$(CONFVAL_AUTO_MAPPER)
else ifeq ($(CONFIG_AUTO),none)
	CONFIG_AUTO_INTERNAL=$(CONFVAL_AUTO_NONE)
else
	CONFIG_AUTO_INTERNAL=$(CONFVAL_AUTO_INVALID)
endif

CFLAGS += -DCONFIG_THERMO_NONE=$(CONFVAL_THERMO_NONE)
CFLAGS += -DCONFIG_THERMO_KTYPE=$(CONFVAL_THERMO_KTYPE)
CFLAGS += -DCONFIG_THERMO_PT100=$(CONFVAL_THERMO_PT100)
CFLAGS += -DCONFIG_THERMO_MAX=$(CONFVAL_THERMO_PT100)

CFLAGS += -DCONFIG_AUTO_NONE=${CONFVAL_AUTO_NONE}
CFLAGS += -DCONFIG_AUTO_MAPPER=${CONFVAL_AUTO_MAPPER}
CFLAGS += -DCONFIG_AUTO_PILOT=${CONFVAL_AUTO_PILOT}

CFLAGS += -DCONFIG_THERMO=$(CONFIG_THERMO_INTERNAL)
CFLAGS += -DCONFIG_MAGNETRON=$(CONFIG_MAGNETRON)
CFLAGS += -DCONFIG_HOSTNAME=${CONFIG_HOSTNAME}
CFLAGS += -DCONFIG_WATER=${CONFIG_WATER}
CFLAGS += -DCONFIG_FURNACE_FIRE_PIN=${CONFIG_FURNACE_FIRE_PIN}
CFLAGS += -DCONFIG_FURNACE_DEADLINE_MS=${CONFIG_FURNACE_DEADLINE_MS}
CFLAGS += -DCONFIG_MAX_PWM=${CONFIG_MAX_PWM}
CFLAGS += -DCONFIG_SHUTTER=${CONFIG_SHUTTER}
CFLAGS += -DCONFIG_AUTO=${CONFIG_AUTO_INTERNAL}
CFLAGS += -DCONFIG_STIRRER=${CONFIG_STIRRER}

all: print_config build/ ninja

print_config:
	$(foreach var,$(.VARIABLES),$(if $(filter CONFIG_%,$(var)),\
		$(info $(var)=$($(var)))))

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

build/: ./configure_wifi.sh ./scripts/build_native.sh wlan.ini pico_sdk check_cflags
	CFLAGS="$(CFLAGS)" ./scripts/build_native.sh
	CFLAGS="$(CFLAGS)" CONFIG_HOSTNAME=$(CONFIG_HOSTNAME) ./configure_wifi.sh

ninja: build/
	ninja -C build/

build/furnace.uf2: ninja
flash: build/furnace.uf2
	scripts/rpiflash.sh $(RPI_USB)

clean:
	rm -rf build/
	rm -rf native/build/
	rm -f consteval_header.h
	rm -f .cflags .cflags.prev

distclean: clean
	rm -f .config

check_cflags:
	@ if [ -e .cflags.prev ] && ! cmp -s .cflags .cflags.prev; then \
		echo "Error: CFLAGS changed"; \
		exit 1; \
	fi

	@ if [ -e .cflags ]; then \
		mv -f .cflags .cflags.prev; \
	fi

	@ echo "$(CFLAGS)" > .cflags

.PHONY: ninja flash clean distclean pico_sdk print_config check_cflags
