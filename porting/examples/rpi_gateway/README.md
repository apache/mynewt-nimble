## Overview
This project is to demonstrate RPi 3 as Gateway which establish connection between Internet and BLE Mesh Network. This is useful for controlling or monitoring BLE Mesh nodes from remote places. It has been built using open source BLE Stack from https://github.com/apache/mynewt-nimble and MQTT from https://mosquitto.org/.

## Hardware
- RPi 3 or PC with BLE Controller

## Software
- RPi OS or Ubuntu 18.04
- MQTT Mosquitto
```
$ sudo apt install libmosquitto-dev
```
- CPP JSON Library
```
$ sudo apt install libjsoncpp-dev
```

## Run Application
- go to mynewt-nimble/porting/examples/rpi_gateway
```
$ make
```
