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
- Compiler cache
```
$ sudo apt install ccache
```

## Update MQTT broker IP
- Update "192.168.1.112" as per your broker IP address at following line in mqtt.cc to subscribe
```
client = new RaspberryOSIOClient((char *)"imuser", (char *)"1884", (char *)"secret?", (char *)"192.168.1.112", onMessage);
```

## Compile and Run Application
- Open terminal
- Go to mynewt-nimble/porting/examples/rpi_gateway
- Run following command to compile the source code
```
$ make
```
- Make sure Bluetooth is ON
- Run application using following command
```
$ sudo ./rpi_gateway
```
## Add this gateway node to BLE Mesh Network
## Send MQTT message to a bulb
- Send MQTT message in JSON format
```
{
    "namespace":"Alexa.PowerController",
    "name":"powerState",
    "value":"ON",
    "timeOfSample":"2018-09-03T16:20:50.52Z",
    "uncertaintyInMilliseconds":50
}
```
```
$ mosquitto_pub -h 192.168.1.112 -t home/gateway -m {"namespace":"Alexa.PowerController","name":"powerState","value":"ON","timeOfSample":"2018-09-03T16:20:50.52Z","uncertaintyInMilliseconds":50}
```