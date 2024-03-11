<!--
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
#  KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#
-->

# Sample applications

## advertiser

This is the simplest example of advertising. Application sets NRPA, configures
advertisement parameters: general discoverable and not connectable and fills
advertisement fields. Transmitted data contains only flags, tx power level and
device name, which fits in 31B limit of single package. With this data set,
device advertises for 10 seconds, terminates advertisement and repeats process
again infinitely.

## scanner

This application shows how to perform simple scan. Device performs discovery
procedure, during which receives advertising reports (if any devices are
advertising nearby). These reports are being parsed and results are printed to
serial port. Application starts new discovery every second.

## peripheral

Peripheral application is based on advertiser, but has added capability of
connecting with other devices. As peripheral, device doesn't initiate any
connection by itself; instead, advertises infinitely and accepts any connection
request it receives. Because we cannot use any 16 or 32 bit UUIDs, as these are
reserved by Bluetooth SIG, we are forced to use 128-bit one. Including such
long UUID in advertising data consumes large part of available payload, so this
data is split in advertising data and response data.

## central

This application works in pair with peripheral. It's based on scanner
application - the difference is, that if there was detected device with UUID
fitting to the one predefined in central application, connection is initiated.

## pairing_central

Standard central application with additional basic security API presentation.
First it scans for the device with matching 128-bit UUID in advertising packet and
connects to it. Unlike in standard central-peripheral apps, here after connection,
security procedure is carried out. If the devices are connecting for the first time,
they are not bonded yet and require pairing in order to start the encryption.
User is required to confirm that displayed 6-digit passkeys are the same on both
devices. To do this user has to enter command in the console (the application
also shows how to configure simple custom shell commands). After the successful
pairing the devices will be bonded and will be able to connect, start the 
encryption and disconnect in cycle, without having to confirm passkey anymore.

## pairing_periph

Peripheral app to work with the pairing_central. Note that the first time
the devices connect, the user must confirm the passkeys in BOTH applications
in order to successfully pair them.