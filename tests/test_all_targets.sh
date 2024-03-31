#! /bin/sh

#
# Tests if the code compiles, nothing really more
#

set -e

cd $(git rev-parse --show-toplevel)

for config in $(ls configs)
do
	make distclean
	cp configs/${config} .config
	make
done
