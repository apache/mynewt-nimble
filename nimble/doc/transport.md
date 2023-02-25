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

# NimBLE HCI transport

## Overview

Transport is split into host (HS) and controller (LL) sides. Those do not
necessarily represent actual host/controller, they can be just interfaces to
external host (e.g. UART or USB) or  controller (e.g.IPC to LL running on
another core).

```
+----------+                       +----------+
| cmd pool |                       | evt pool |
+----------+                       +----------+
| acl pool |                       | acl pool |
+----------+                       +----------+
  ||                                       ||
+----+                                   +----+
|    | <--- ble_transport_to_hs_acl ---- |    |
|    | <--- ble_transport_to_hs_evt ---- |    |
| HS |                                   | LL |
|    | ---- ble_transport_to_ll_cmd ---> |    |
|    | ---- ble_transport_to_ll_acl ---> |    |
+----+                                   +----+
```

HS side allocates buffers for HCI commands and ACL data from dedicated pools
using `ble_transport_alloc_cmd` and `ble_transport_alloc_acl_from_hs` calls
respectively, then sends them to LL side using `ble_transport_to_ll_cmd` and
`ble_transport_to_ll_acl`.

Similarly, LL side allocates buffers for HCI events and ACL data from dedicated
pools using `ble_transport_alloc_evt` and `ble_transport_alloc_acl_from_ll`
calls respectively, then sends them to HS side using `ble_transport_to_hs_evt`
and `ble_transport_to_hs_acl`.

Both HCI command and events buffers are freed using `ble_transport_free`, ACL
data are freed as regular `os_mbuf`.

Selecting `native` transport for either HS or LL side will use actual NimBLE
host or controller respectively directly instead of transport implementation.
Both NimBLE host and controller do not use decidated pools for ACL data and
allocate data directly from msys pool - relevant ACL pools will be disabled
automatically.

Actual transport implementation for each side can be set using `BLE_TRANSPORT_HS`
and `BLE_TRANSPORT_LL` syscfg for HS and LL sides respectively. Selecting
transport in either direction will automatically add dependencies to required
transport implementation packages, there's no need to do this manually.
Selecting `native` transport for HS and/or LL side will automatically add
dependencies to NimBLE host and/or controller packages. 

The order of initialization is defined as follows:
- `ble_transport_init` - generic transport initialization
- `ble_transport_hs_init` - HS side initialization
- `ble_transport_ll_init` - LL side initialization

Initialization functions for HS and LL sides shall be implemented by transport
implementation. There's no need to define those functions as sysinit stages
since this is already done by generic transport implementation along with
proper dependencies.


## Application configuration

To ensure that application can be easily run on different BSPs, it's strongly
recommended not to put hard dependencies to any transport in `pkg.yml` and
use automatic dependencies instead. That means an application that uses NimBLE
host should only include `nimble/host` in its dependencies (i.e. no direct
dependency to `nimble/controler` or any transport implementation). This will
pull `nimble/transport` automatically, force `BLE_TRANSPORT_HS: native` and
allow changing LL side using `BLE_TRANSPORT_LL` to any supported controller.


## Multicore SoCs

On multicore SoCs with dedicated application and network cores (e.g. nRF5340,
DA1469x) NimBLE host and controller will run on different cores. In such setup
application core uses LL transport implementation instead of an actual NimBLE
controller and similarly network core uses HS transport implementation instead
of NimBLE host. Both sides of transport implementation are provided by the same
transport, e.g. `nrf5340` for nRF5340 or `dialog_cmac` for DA1469x, and exchange
data via IPC. This process is transparent from application point of view,
assuming it's properly configured (see above).

```
     Application core           |            Network core
+----+               +----+     |     +----+               +----+
|    |               | LL |     |     | HS |               |    |
|    | <- acl/evt -- |    |     |     |    | <- acl/evt -- |    |
| HS |               | tr | <- ipc -> | tr |               | LL |
|    | -- cmd/acl -> | an |     |     | an | -- cmd/acl -> |    |
|    |               | sp |     |     | sp |               |    |
+----+               +----+     |     +----+               +----+
```


## Build configurations

### Combined build

This setup runs both NimBLE host and controller on the same core. It's a typical
configuration when running application on SoCs like nRF51 or nRF52.

Note: this is the default configuration, no need to set it explicitly.

```yaml
BLE_TRANSPORT_HS: native
BLE_TRANSPORT_LL: native
```

### Controller-only build

This setup makes NimBLE controller accessible to external host connected via
e.g. UART or USB, so it can be used as an external Bluetooth LE controller.
The controller runs on the same core as external interface. It's typically
used with `blehci` application running on SoCs like nRF51 or nRF52.

```yaml
BLE_TRANSPORT_HS: uart
BLE_TRANSPORT_LL: native
```


### Multicore build

This is a variant of combined build but with NimBLE host and controller running
on different cores, like e.g. nRF5340 or DA1469x. Application core can run
any application while network core runs `blehci`.

Note: BSPs for nRF5340 and DA1469x will automatically select proper transport
      for LL side if NimBLE host or transport is included in build, so usually
      there's no need to configure manually. 

#### Application core
```yaml
BLE_TRANSPORT_HS: native
BLE_TRANSPORT_LL: dialog_cmac
```

#### Network core
```yaml
BLE_TRANSPORT_HS: dialog_cmac
BLE_TRANSPORT_LL: native
```


### Bridge build

This is a variant of controller-only build but with NimBLE controller running
on different core than external interface used to access it, like e.g. nRF5340
or DA1469x. In this setup both cores run `blehci` application.

Note: BSPs for nRF5340 and DA1469x will automatically select proper transport
      for LL side if NimBLE host or transport is included in build, so usually
      there's only need to select required transport for external interface on
      application core.

#### Application core
```yaml
BLE_TRANSPORT_HS: uart
BLE_TRANSPORT_LL: nrf5340
```

#### Network core
```yaml
BLE_TRANSPORT_HS: nrf5340
BLE_TRANSPORT_LL: native
```
