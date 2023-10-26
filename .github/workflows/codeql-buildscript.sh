#!/usr/bin/env bash

sudo apt-get update
sudo apt-get install -y make ccache gcc-multilib g++-multilib

make -C porting/examples/dummy/ clean all
make -C porting/examples/linux/ clean all
make -C porting/examples/linux_blemesh/ clean all
