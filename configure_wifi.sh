#!/bin/sh

cmake -B build -S . -DCMAKE_EXPORT_COMPILE_COMMANDS=Y -GNinja -DPICO_BOARD=pico_w -DPICO_SDK_PATH=/home/holz/etc/git/pico-sdk/ -DPICO_EXTRAS_PATH=/home/holz/etc/git/pico-extras/ -DWIFI_SSID=$(awk -F '=' '$1=="ssid" {print $2}' wlan.ini) -DWIFI_PASSWORD=$(awk -F '=' '$1=="pass" {print $2}' wlan.ini)
