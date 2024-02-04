#!/bin/sh

cmake -B native/build/ -S native/ -GNinja &&
ninja -C native/build/  &&
./native/build/consteval
