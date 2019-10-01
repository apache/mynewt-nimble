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

#include <iostream>
#include <cstring>
#include "raspberry_osio_client.h"

using namespace std;

/*
 * Internal callback for processing incoming messages. 
 * It calls more friendly callback supplied by user.
 */
static void callbackMessageReceived(mosquitto *data, void * context, const struct mosquitto_message * message)
{
  RaspberryOSIOClient * _this = (RaspberryOSIOClient *)context;

  if (_this->onMessage != 0) {
    mosquitto_message * destination = new mosquitto_message();
    mosquitto_message_copy(destination, message);
    _this->onMessage(destination->topic, (char*)destination->payload, destination->payloadlen);
  }
}


/*
 * Internal callback for processing disconnect.
 * We reset authenticated flag to force restore server connection next time.
 */
static void callbackDisconnected(mosquitto *data, void * context, int reason)
{
  RaspberryOSIOClient * _this = (RaspberryOSIOClient *)context;
  _this->resetConnectedState();
}


/*
 * Some constructors placed below. In functions where there is no "serverName" parameter
 * we use "opensensors.io". If you don't use constructor with "onMessage" parameter
 * you will not have ability to receive messages from topic you subscribed.
 */

RaspberryOSIOClient::RaspberryOSIOClient(char * userName, 
                                         char * deviceId, 
                                         char * devicePassword)
{
  this->initialize(userName, deviceId, devicePassword, (char *)OSIO_SERVERNAME, 0);
}


RaspberryOSIOClient::RaspberryOSIOClient(char * userName, 
                                         char * deviceId, 
                                         char * devicePassword, 
                                         char * serverName)
{
  this->initialize(userName, deviceId, devicePassword, serverName, 0);  
}


RaspberryOSIOClient::RaspberryOSIOClient(char * userName, 
                                         char * deviceId, 
                                         char * devicePassword, 
                                         void (*callback)(char*,char*,unsigned int))
{
  this->initialize(userName, deviceId, devicePassword, (char *)OSIO_SERVERNAME, callback);
}


RaspberryOSIOClient::RaspberryOSIOClient(char * userName, 
                                         char * deviceId, 
                                         char * devicePassword, 
                                         char * serverName, 
                                         void (*onMessage)(char*,char*,unsigned int))
{
  this->initialize(userName, deviceId, devicePassword, serverName, onMessage);  
}


/*
 * Startup initializer (called from constructors).
 * Do not use it directly.
 */
void RaspberryOSIOClient::initialize(char * userName,
                                     char * deviceId,
                                     char * devicePassword,
                                     char * serverName,
                                     void (*onMessage)(char*,char*,unsigned int))
{
  mosquitto_lib_init();

  this->_data = mosquitto_new(deviceId, true, this);

  this->_userName = userName;
  this->_devicePassword = devicePassword;
  this->_serverName = serverName;
  this->_authenticatedInServer = false;
  this->onMessage = onMessage;

  mosquitto_message_callback_set(this->_data, callbackMessageReceived);
  mosquitto_disconnect_callback_set(this->_data, callbackDisconnected);
}


/*
 * Free allocated resources.
 */
RaspberryOSIOClient::~RaspberryOSIOClient()
{
  mosquitto_disconnect(this->_data);
  mosquitto_destroy(this->_data);
  mosquitto_lib_cleanup();
}


/*
 * Perform connection to server (if not already connected) and return TRUE if success.
 */
bool RaspberryOSIOClient::connectIfNecessary()
{
  if (this->_authenticatedInServer) {
    return true;
  }

  // Call it before mosquitto_connect to supply additional user credentials.
  mosquitto_username_pw_set(this->_data, this->_userName, this->_devicePassword);

  int result = mosquitto_connect(this->_data, this->_serverName, MQTT_PORT, 60);
  this->_authenticatedInServer = result == MOSQ_ERR_SUCCESS;
  return this->_authenticatedInServer;
}


/*
 * Reset connection flag to "not connected".
 */
void RaspberryOSIOClient::resetConnectedState()
{
  this->_authenticatedInServer = false;
}


/*
 * Publish message to topic.
 */
bool RaspberryOSIOClient::publish(char* topic, char* payload) 
{
  if (!this->connectIfNecessary())
  {
    return false;
  }

  int result = mosquitto_publish(this->_data, 0, topic, strlen(payload), (const uint8_t*)payload, 0, false);

  return result == MOSQ_ERR_SUCCESS;
}


/*
 * Subscribe for topic.
 */
bool RaspberryOSIOClient::subscribe(char* topic)
{
  if (!this->connectIfNecessary()) {
    return false;
  }

  int result = mosquitto_subscribe(this->_data, 0, topic, 0);

  return result == MOSQ_ERR_SUCCESS;
}


/*
 * The main network loop for the client. You must call this frequently in order
 * to keep communications between the client and broker working.
 */
bool RaspberryOSIOClient::loop()
{
  // We use -1 for default 1000ms waiting for network activity.
  int result = mosquitto_loop_forever(this->_data, -1, 1);

  return result == MOSQ_ERR_SUCCESS;
}


/*
 * Disconnect from server. It may be useful to get FALSE from
 * RaspberryOSIOClient::loop() and break from main communication cycle.
 */
bool RaspberryOSIOClient::disconnect()
{
  int result = mosquitto_disconnect(this->_data);
  return result = MOSQ_ERR_SUCCESS;
}
