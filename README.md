# pico-furnace
Raspberry PI pico wireless controller for electric furnace.

# Building

## Dependencies
 - [pico sdk](https://github.com/raspberrypi/pico-sdk) (tested on tag: 1.5.1)
 - [pico extras](https://github.com/raspberrypi/pico-extras) (tested on tag: sdk-1.5.1)

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
