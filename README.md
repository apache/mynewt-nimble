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

# Apache Mynewt NimBLE

## Overview

See (https://mynewt.apache.org/network/ble/ble_intro/).

## Building

NimBLE is usually built as a part of Apache Mynewt OS, but ports for
other RTOS-es are also available.

### Apache Mynewt

(tbd)

### FreeRTOS on nRF52 DK

1. Download and install NRF5 SDK

You will need to download nRF5 SDK from Nordic Semiconductor website:
(https://www.nordicsemi.com/eng/Products/Bluetooth-low-energy/nRF5-SDK).

2. Make nRF5 SDK available for sample application

Sample application expects nRF5 SDK to be available at location specified
by `NRF5_SDK_ROOT` variable. By default it points to `./nrf5_sdk` directory
(i.e. subdirectory of `porting/freertos_nrf5_sdk` directory) so the easiest
way is to either copy nRF5 SDK to that location or make a symbolic link:

```no-highlight
    $ ln -s <x>/nRF5_SDK_12.3.0_d7731ad porting/freertos_nrf5_sdk/nrf5_sdk
```

Alternatively, you may want to overwrite default value of `NRF5_SDK_ROOT`
to point to proper location:

```no-highlight
    $ export NRF5_SDK_ROOT=<x>/nRF5_SDK_12.3.0_d7731ad
```

3. Build and flash FreeRTOS image

Provided Makefile is compatible with nRF5 SDK build system so sample
application is built and flashed as other examples in nRF5 SDK:

```no-highlight
    $ make -C porting/freertos_nrf5_sdk
    $ make -C porting/freertos_nrf5_sdk flash
````

