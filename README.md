# pico-furnace
Raspberry PI pico wireless controller for electric furnace.

# Building

## Dependencies
 - [pico sdk](https://github.com/raspberrypi/pico-sdk) (tested on tag: 1.5.1)
 - [pico extras](https://github.com/raspberrypi/pico-extras) (tested on tag: sdk-1.5.1)

## Configure project options

Create `.config` with values for all CONFIG_* variables that user is intrested in changing.
User can also use one of the predefined config files from `configs/` by copying one of the files into `.config` file.
Configuring a project for the typical furnace target would look like:
```
cp configs/furnace .config
```
Make sure you always `make clean` after changing `.config` file to make sure new options are used to build the project.

## Configure wlan options

Create `wlan.ini` file and fill SSID and password of the WiFi network to which pico should connect when booting.
Example contents of the file:
```
ssid=my_wifi_network_name
pass=super_secret_password
```

## Configure and build

```console
export PICO_SDK_PATH=<path to pico sdk>
export PICO_EXTRAS_PATH=<path to pico extras>
make
```

## Flashing the binary

Connect pico W board USB while pressing the boot button and run:

```console
make flash
```
