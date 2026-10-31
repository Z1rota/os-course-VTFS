#!/bin/bash
set -e

cd "$(dirname "$0")"

make clean
make
sudo insmod vtfs.ko
sudo mount -t vtfs "bigbadonkas228" /mnt/vtfs