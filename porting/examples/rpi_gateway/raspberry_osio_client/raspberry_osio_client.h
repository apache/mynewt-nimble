/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

/* 
 * Following code has been referenced from
 * https://github.com/OpenSensorsIO/raspberry-pi-mqtt
 * for learning purpose
 */

#ifndef raspberry_osio_client_h
#define raspberry_osio_client_h

#include <mosquitto.h>

// Default MQTT port
#define MQTT_PORT 1883

// Default server name
#define OSIO_SERVERNAME "mqtt.opensensors.io"

class RaspberryOSIOClient 
{
private:

  mosquitto * _data;
  char * _userName;
  char * _deviceId;
  char * _devicePassword;
  char * _serverName;
  bool _authenticatedInServer;
  bool connectIfNecessary();
  void initialize(char * userName, char * deviceId, char * devicePassword, char * serverName, void (*callback)(char*,char*,unsigned int));
  
public:

  RaspberryOSIOClient(char * userName, char * deviceId, char * devicePassword); 
  RaspberryOSIOClient(char * userName, char * deviceId, char * devicePassword, char * serverName);
  RaspberryOSIOClient(char * userName, char * deviceId, char * devicePassword, void (*callback)(char*,char*,unsigned int));
  RaspberryOSIOClient(char * userName, char * deviceId, char * devicePassword, char * serverName, void (*callback)(char*,char*,unsigned int));
  ~RaspberryOSIOClient();
  bool publish(char * topic, char * payload);
  bool subscribe(char * topic);
  bool loop();
  bool disconnect();

  // We don't recommend use these functions directly.
  // They are for internal purposes.
  void (*onMessage)(char*,char*,unsigned int);
  void (*onDisconnect)(void*);
  void resetConnectedState();
};

#endif
