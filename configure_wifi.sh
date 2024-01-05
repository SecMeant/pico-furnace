#!/bin/sh

cmake -B build -S . -DCMAKE_EXPORT_COMPILE_COMMANDS=Y -GNinja -DPICO_BOARD=pico_w \
-DCMAKE_BUILD_TYPE=Release \
-DPICO_SDK_PATH=${PICO_SDK_PATH} -DPICO_EXTRAS_PATH=${PICO_EXTRAS_PATH} \
-DWIFI_SSID=$(awk -F '=' '$1=="ssid" {print $2}' wlan.ini) \
-DWIFI_PASSWORD=$(awk -F '=' '$1=="pass" {print $2}' wlan.ini)
