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
#include <string.h>
#include <unistd.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include "raspberry_osio_client.h"
#include <jsoncpp/json/json.h>
#include "bt_mesh_node.h"

using namespace std;

static RaspberryOSIOClient * client = 0;
static Json::Reader reader;
static mutex mtx, mtxq;
static condition_variable cv;
static volatile bool msg_evt = false;
static queue<char*> mqtt_msg_q;
/*
 * Handler for incoming messages.
 */
void onMessage(char* topic, char* payload, unsigned int length)
{
	char *msg = new char[length + 1];
	memset(msg, 0, length + 1);
	memcpy(msg, payload, length);

	mtxq.lock();

	mqtt_msg_q.push(msg);

	mtxq.unlock();

	cout << "Topic: " << topic << ", message: " << msg;

	unique_lock<mutex> lck(mtx);
	msg_evt = true;
	cv.notify_all();
}

void mqtt_message_handler(void)
{
	cout << "mqtt message handler is running" << endl;

	unique_lock<mutex> lck(mtx);
	while(!msg_evt) {

		cout << "waiting for next msg" << endl;
		cv.wait(lck);

		cout << "received msg" << endl;

		msg_evt = false;

		while (!mqtt_msg_q.empty()) {
			Json::Value obj;

			mtxq.lock();
			char *msg = mqtt_msg_q.front();
			mqtt_msg_q.pop();
			mtxq.unlock();

			reader.parse(msg, obj);

			if (!strcmp("powerState", obj["name"].asCString())) {
				if (!strcmp("ON", obj["value"].asCString())) {
					publish_gen_onoff_set(0x1);
				} else if (!strcmp("OFF", obj["value"].asCString())) {
					publish_gen_onoff_set(0x0);
				}
			}

			if (msg) {
				cout << "freeing memory" << endl;
				delete msg;
				msg = NULL;
			}
		}
	}
}

void mosquitto_mqtt_init(void *param)
{
	  // Our raspberry MQTT client instance.
	  client = new RaspberryOSIOClient((char *)"imuser", (char *)"1884", (char *)"secret?", (char *)"192.168.1.112", onMessage);

	  cout << "Client started. When \"exit\" message is received, the program will publish test message to topic and finish its work." << endl;

	  // Subscribe for topic.
	  bool result = client->subscribe((char *)"home/gateway");

	  cout << "Subscribing result: " << (result == true ? "success" : "error") << endl;

	  thread mqtt_msg_handler_thread(mqtt_message_handler);

	  // Main communication loop to process messages.
	  do {
	    // Save loop iteration state (TRUE if all ok).
	    result = client->loop();
	    // Just show that we are alive.
	    cout << ".\r\n";
	    // Wait 1 second.
	    sleep(1);
	  } while(result == true); // Break if loop returned FALSE.

	  delete client;

	  cout << "Bye!" << endl;
}
