#!/bin/bash
set -e

cd "$(dirname "$0")"

sudo umount /mnt/vtfs
sudo rmmod vtfs
make clean