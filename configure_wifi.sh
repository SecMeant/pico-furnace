#!/bin/sh

cmake -B build -S . -DCMAKE_EXPORT_COMPILE_COMMANDS=Y -GNinja -DPICO_BOARD=pico_w -DPICO_SDK_PATH=/home/j00r/pico/pico-sdk/ -DPICO_EXTRAS_PATH=/home/j00r/pico/pico-extras/ -DWIFI_SSID=$(awk -F '=' '$1=="ssid" {print $2}' wlan.ini) -DWIFI_PASSWORD=$(awk -F '=' '$1=="pass" {print $2}' wlan.ini)
