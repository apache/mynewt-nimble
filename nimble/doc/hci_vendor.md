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



Nimble Vendor Supported Commands
================================

*OGF = 0x003F*

Vendor specific commands are enabled with `BLE_LL_HCI_VS` (defaults to
`BLE_HCI_VS`). Some commands require additional syscfg options, those are
listed in command description.

OCF values listed below are relative to `BLE_HCI_VS_OCF_OFFSET` (default 0),
i.e. actual OCF is `BLE_HCI_VS_OCF_OFFSET` + listed value.

All multi-octet parameters are little-endian.


Read Static Address Command
---------------------------

Read the static random address assigned to the controller

| Command           | OCF    | Params     |Return params |
|-------------------|--------|------------|--------------|
| Read_Addr         | 0x0001 | *none*     | Static_Addr  |

<br>
Static_Addr

| Value              | Description |
|--------------------|-------------|
| 0xXXXXXXXXXXXX     | Address     |

*Size: 6 octets*


Set Default Transmit Power
--------------------------

Set default transmit power level <br>
Requested value is limited to `BLE_LL_TX_PWR_MAX_DBM` and rounded to
power level supported by radio. Selected TX power is returned <br>
Setting 0x7F (127) restores controller setting to reset default <br>
Command is disallowed if controller is advertising, scanning, initiating,
synchronized to periodic advertising or has any connection <br>

| Command           | OCF    | Params     | Return params |
|-------------------|--------|------------|---------------|
| Set_Tx_Pwr        | 0x0002 | Tx_Pwr     | Sel_Tx_Pwr    |

<br>
Tx_Pwr

| Value                      | Description                                 |
|----------------------------|---------------------------------------------|
| 0xXX                       | Desired TX Power Level in dBm (signed)      |
| 0x7F                       | Restore reset default (`BLE_LL_TX_PWR_DBM`) |

*Size: 1 octet*

<br>
Sel_Tx_Pwr

| Value            | Description                                   |
|------------------|-----------------------------------------------|
| 0xXX             | Controller Selected TX power in dBm (signed)  |

*Size: 1 octet*


Configure Connection Strict Scheduling
--------------------------------------

Configure Connection Strict Scheduling <br>
Requires `BLE_LL_HCI_VS_CONN_STRICT_SCHED` enabled and
`BLE_LL_CONN_STRICT_SCHED_FIXED` disabled <br>
Command is disallowed if Connection Strict Scheduling is enabled <br>

| Command           | OCF    | Params       | Return params |
|-------------------|--------|--------------|---------------|
| CSS_Configure     | 0x0003 | Slot_us      | *none*        |
|                   |        | Period_Slots |               |

<br>
Slot_us

| Value                      | Description                                                   |
|----------------------------|---------------------------------------------------------------|
| 0xXXXXXXXX                 | Slot duration in microseconds, non-zero multiple of 1250      |

*Size: 4 octets*

<br>
Period_Slots

| Value                      | Description                       |
|----------------------------|-----------------------------------|
| 0xXXXXXXXX                 | Number of slots in period, non-zero |

*Size: 4 octets*


Connection Strict Scheduling Enable
-----------------------------------

Enable/Disable Connection Strict Scheduling <br>
Requires `BLE_LL_HCI_VS_CONN_STRICT_SCHED` enabled <br>
Command is disallowed if there are any connections <br>

| Command      | OCF    | Params       | Return params |
|--------------|--------|--------------|---------------|
| CSS_Enable   | 0x0004 | Enable       | *none*        |


<br>
Enable

| Value                      | Description |
|----------------------------|-------------|
| 0x00                       | Disable CSS |
| 0x01                       | Enable CSS  |

*Size: 1 octet*


Connection Strict Scheduling - Select Next Slot
-----------------------------------------------

Set slot index for next connection to be created <br>
Requires `BLE_LL_HCI_VS_CONN_STRICT_SCHED` enabled <br>

| Command           | OCF    | Params       | Return params |
|-------------------|--------|--------------|---------------|
| CSS_Set_Next_Slot | 0x0005 | Slot_Idx     | *none*        |


<br>
Slot_Idx

| Value                      | Description                                       |
|----------------------------|---------------------------------------------------|
| 0xXXXX                     | Slot index, shall be less than Period_Slots       |
| 0xFFFF                     | No slot selected, controller selects free slot    |

*Size: 2 octets*


Connection Strict Scheduling - Select Slot For Specific Connection
------------------------------------------------------------------

Move existing connection to specified slot <br>
Requires `BLE_LL_HCI_VS_CONN_STRICT_SCHED` enabled <br>
Command is disallowed if Connection Strict Scheduling is not enabled <br>
CSS Slot Changed event is sent when connection is moved to new slot <br>

| Command           | OCF    | Params     | Return params |
|-------------------|--------|------------|---------------|
| CSS_Set_Conn_Slot | 0x0006 | Conn_Hdl   | *none*        |
|                   |        | Slot_Idx   |               |

<br>
Conn_Hdl

| Value                      | Description       |
|----------------------------|-------------------|
| 0xXXXX                     | Connection Handle |

*Size: 2 octets*

<br>
Slot_Idx

| Value                      | Description                                 |
|----------------------------|---------------------------------------------|
| 0xXXXX                     | Slot index, shall be less than Period_Slots |

*Size: 2 octets*


Connection Strict Scheduling - Read Connection Slot
---------------------------------------------------

Read current connection slot index <br>
Requires `BLE_LL_HCI_VS_CONN_STRICT_SCHED` enabled <br>
Command is disallowed if Connection Strict Scheduling is not enabled <br>

| Command            | OCF    | Params       | Return params |
|--------------------|--------|--------------|---------------|
| CSS_Read_Conn_Slot | 0x0007 | Conn_Hdl     | Conn_Hdl      |
|                    |        |              | Slot_Idx      |

<br>
Conn_Hdl

| Value                      | Description       |
|----------------------------|-------------------|
| 0xXXXX                     | Connection Handle |

*Size: 2 octets*

<br>
Slot_Idx

| Value              | Description                   |
|--------------------|-------------------------------|
| 0xXXXX             | Slot index                    |
| 0xFFFF             | Connection has no slot        |

*Size: 2 octets*


Set Data Length
---------------

Change TX/RX data length values for connection. <br>
Requires `BLE_LL_CFG_FEAT_DATA_LEN_EXT` enabled <br>
Command completes immediately and Data Length Update procedure is started
if needed. LE Data Length Change event is sent when procedure completes and
values have changed. <br>

| Command      | OCF    | Params          | Return params |
|--------------|--------|-----------------|---------------|
| Set_Data_Len | 0x0008 | Conn_Hdl<br>    | Conn_Hdl      |
|              |        | Tx_Octets<br>   |               |
|              |        | Tx_Time<br>     |               |
|              |        | Rx_Octets<br>   |               |
|              |        | Rx_Time<br>     |               |

<br>
Conn_Hdl

| Value                      | Description       |
|----------------------------|-------------------|
| 0xXXXX                     | Connection Handle |

*Size: 2 octets*

<br>
Tx_Octets

| Value                      | Description                           |
|----------------------------|---------------------------------------|
| 0xXXXX                     | Maximum transmit octets (27-251)      |

*Size: 2 octets*

<br>
Tx_Time

| Value                      | Description                                        |
|----------------------------|----------------------------------------------------|
| 0xXXXX                     | Maximum transmit time in microseconds (328-17040)  |

*Size: 2 octets*

<br>
Rx_Octets

| Value                      | Description                           |
|----------------------------|---------------------------------------|
| 0xXXXX                     | Maximum receive octets (27-251)       |

*Size: 2 octets*

<br>
Rx_Time

| Value                      | Description                                       |
|----------------------------|---------------------------------------------------|
| 0xXXXX                     | Maximum receive time in microseconds (328-17040)  |

*Size: 2 octets*


Set Antenna Location
--------------------

Select antenna used by Front End Module <br>
Requires `BLE_FEM_ANTENNA` enabled and FEM driver support <br>
Command is disallowed if controller is advertising, scanning, initiating,
synchronized to periodic advertising or has any connection <br>

| Command       | OCF    | Params         | Return params |
|---------------|--------|----------------|---------------|
| Set_Ant_Loc   | 0x0009 | Ant_Loc        | *none*        |


<br>
Ant_Loc

| Value                      | Description      |
|----------------------------|------------------|
| 0x00                       | Default antenna  |
| 0x01                       | Antenna port 1   |
| 0x02                       | Antenna port 2   |

*Size: 1 octet*


Set Local Identity Resolving Key
--------------------------------

Set local identity resolving key for own address type. The local IRK is
used by controller to generate RPA when own address type is set to 0x02 or
0x03 and peer is not on resolving list. <br>
Requires `BLE_LL_HCI_VS_LOCAL_IRK` enabled <br>
Command is disallowed if controller is advertising, scanning, initiating
or synchronized to periodic advertising <br>

| Command         | OCF    | Params        | Return params |
|-----------------|--------|---------------|---------------|
| Set_Local_IRK   | 0x000A | Own_Addr_Type | *none*        |
|                 |        | IRK           |               |

<br>
Own_Addr_Type

| Value                      | Description                 |
|----------------------------|-----------------------------|
| 0x00                       | Public Device Address       |
| 0x01                       | Random Device Address       |

*Size: 1 octet*

<br>
IRK

| Value              | Description                                |
|--------------------|--------------------------------------------|
| 0xXX(16)           | Identity Resolving Key                     |
| 0x00(16)           | Clear local IRK                            |

*Size: 16 octets*


Set Scan Configuration
----------------------

Set global PDU filter for scanner <br>
Requires `BLE_LL_HCI_VS_SET_SCAN_CFG` enabled <br>
Command is disallowed if controller is scanning or initiating <br>

| Command         | OCF    | Params         | Return params |
|-----------------|--------|----------------|---------------|
| Set_Scan_Cfg    | 0x000B | Flags          | *none*        |
|                 |        | Rssi_Threshold |               |

<br>
Flags

| Value                      | Description                                  |
|----------------------------|----------------------------------------------|
| 0x00000001                 | Ignore legacy advertising PDUs               |
| 0x00000002                 | Ignore extended advertising PDUs             |
| 0x00000004                 | Ignore PDUs with RSSI below Rssi_Threshold   |

Setting both 0x00000001 and 0x00000002 is not allowed.

*Size: 4 octets*

<br>
Rssi_Threshold

| Value                      | Description                                            |
|----------------------------|--------------------------------------------------------|
| 0xXX                       | RSSI threshold in dBm (signed), used if 0x00000004 set |

*Size: 1 octet*


Nimble Vendor Specific Events
=============================

*Event Code = 0xFF*

All vendor specific events start with 1 octet Subevent_Code followed by
subevent specific parameters.

| Subevent             | Subevent_Code | Params              |
|----------------------|---------------|---------------------|
| Assert               | 0x01          | Location            |
| CSS_Slot_Changed     | 0x02          | Conn_Hdl<br>        |
|                      |               | Slot_Idx            |
| ISO_HCI_Feedback     | 0x03          | BIG_Handle<br>      |
|                      |               | Count<br>           |
|                      |               | Feedback[i]         |
| LLCP_Trace           | 0x17          | Direction<br>       |
|                      |               | Conn_Hdl<br>        |
|                      |               | Event_Counter<br>   |
|                      |               | Reserved<br>        |
|                      |               | PDU                 |


Assert Event
------------

Sent when assertion fails in controller code, just before controller
halts. <br>
Requires `BLE_LL_HCI_VS_EVENT_ON_ASSERT` enabled <br>

<br>
Location

| Value              | Description                                       |
|--------------------|---------------------------------------------------|
| "file:line"        | ASCII string (not NULL-terminated)                |

*Size: variable*


Connection Strict Scheduling Slot Changed Event
-----------------------------------------------

Sent when connection was moved to new slot (e.g. as a result of
CSS_Set_Conn_Slot command). <br>
Requires `BLE_LL_HCI_VS_CONN_STRICT_SCHED` enabled <br>

<br>
Conn_Hdl

| Value                      | Description       |
|----------------------------|-------------------|
| 0xXXXX                     | Connection Handle |

*Size: 2 octets*

<br>
Slot_Idx

| Value                      | Description |
|----------------------------|-------------|
| 0xXXXX                     | Slot index  |

*Size: 2 octets*


ISO HCI Feedback Event
----------------------

ISO synchronization feedback for BIG. Sent at configured interval after
completed ISO event. <br>
Requires `BLE_LL_ISO_HCI_FEEDBACK_INTERVAL_MS` set to non-zero value
(interval in milliseconds) <br>

<br>
BIG_Handle

| Value                      | Description |
|----------------------------|-------------|
| 0xXX                       | BIG Handle  |

*Size: 1 octet*

<br>
Count

| Value                      | Description                |
|----------------------------|----------------------------|
| 0xXX                       | Number of Feedback entries |

*Size: 1 octet*

<br>
Feedback[i]

| Field                      | Size     | Description                                       |
|----------------------------|----------|---------------------------------------------------|
| BIS_Handle                 | 2 octets | BIS Connection Handle                             |
| SDU_Per_Interval           | 1 octet  | Number of SDUs expected per ISO interval          |
| Diff                       | 1 octet  | Actual minus expected number of SDUs queued in controller (signed) |

Expected number of SDUs queued after each event is number of SDUs required
for each ISO event (i.e. including pre-transmissions) minus number of SDUs
expected per ISO interval.

*Size: 4 octets per entry*


LLCP Trace Event
----------------

Sent for each LL Control PDU received or transmitted. <br>
Requires `BLE_LL_HCI_LLCP_TRACE` enabled <br>

<br>
Direction

| Value                      | Description          |
|----------------------------|----------------------|
| 0x03                       | Received PDU         |
| 0x04                       | Transmitted PDU      |

*Size: 1 octet*

<br>
Conn_Hdl

| Value                      | Description       |
|----------------------------|-------------------|
| 0xXXXX                     | Connection Handle |

*Size: 2 octets*

<br>
Event_Counter

| Value                      | Description                |
|----------------------------|----------------------------|
| 0xXXXX                     | Connection event counter   |

*Size: 2 octets*

<br>
Reserved

| Value                      | Description |
|----------------------------|-------------|
| 0x000000                   | Reserved    |

*Size: 3 octets*

<br>
PDU

| Value                      | Description                          |
|----------------------------|--------------------------------------|
| 0xXX...                    | LL Control PDU (opcode and payload)  |

*Size: variable*
