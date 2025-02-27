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

<br>

***OGF = 0x003F***

<br>

Read Static Address Command
---------------------------

*Read the static random address assigned to the controller*

***Command structure:***

| Command           | OCF    | Params     |Return params |
|-------------------|--------|------------|--------------|
| Read_Addr         | 0x0001 | *none*     | Static_Addr  |

<br>

***Return parameters:***

| Name        | Value                       | Description        |
|-------------|-----------------------------|--------------------|
| Static_Addr | 0xXXXXXXXXXXXX *(6 bytes)*  | Static address     |

<br>
<br>

Set Default Transmit Power
--------------------------

*Set default transmit power level* <br>
*Selected TX power is returned* <br>
*Setting 0xFF restores controller setting to default* <br>

***Command structure:***

| Command           | OCF    | Params     | Return params |
|-------------------|--------|------------|---------------|
| Set_Tx_Pwr        | 0x0002 | Tx_Pwr     | Sel_Tx_Pwr    |

<br>

***Command parameters:***

| Name     | Value                      | Description            |
|----------|----------------------------|------------------------|
| Tx_Pwr   | 0xXX *(1 byte)*            | Desired TX Power Level |

<br>

***Return parameters:***

| Name       | Value            | Description                  |
|------------|------------------|------------------------------|
| Sel_Tx_Pwr | 0xXX *(1 byte)*  | Controller Selected TX power |

<br>
<br>

Configure Connection Strict Scheduling
--------------------------------------

*Configure Connection Strict Scheduling*

***Command structure:***

| Command           | OCF    | Params     | Return params |
|-------------------|--------|------------|---------------|
| CSS_Configure     | 0x0003 | Slot_us    | *none*        |
|                   |        | Num_Slots  |               |

<br>

***Command parameters:***

| Name      | Value                      | Description                    |
|-----------|----------------------------|--------------------------------|
| Slot_us   | 0xXXXXXXXX *(4 bytes)*     | Slot duration in milliseconds  |
| Num_Slots | 0xXXXXXXXX *(4 bytes)*     | Number of period slots         |

<br>
<br>

Connection Strict Scheduling - Enable/Disable
-----------------------------------

*Enable/Disable Connection Strict Scheduling*

***Command structure:***

| Command      | OCF    | Params         | Return params |
|--------------|--------|----------------|---------------|
| CSS_Enable   | 0x0004 | Enable/Disable | *none*        |

<br>

***Command parameters:***

| Name           | Value                      | Description        |
|----------------|----------------------------|--------------------|
| Enable/Disable | 0xXX *(1 byte)*            | Enable/Disable CSS |

<br>
<br>

Connection Strict Scheduling - Select Next Slot
-----------------------------------------------

*Set next slot index for future connection*

***Command structure:***

| Command           | OCF    | Params        | Return params |
|-------------------|--------|---------------|---------------|
| CSS_Set_Next_Slot | 0x0005 | Next_Slot_Idx | *none*        |

<br>

***Command parameters:***

| Name          | Value                      | Description  |
|---------------|----------------------------|--------------|
| Next_Slot_Idx | 0xXXXX *(2 bytes)*         | Next Slot ID |

<br>
<br>

Connection Strict Scheduling - Select Slot For Specific Connection
------------------------------------------------------------------

*Set slot index for current connection*

***Command structure:***

| Command           | OCF    | Params     | Return params |
|-------------------|--------|------------|---------------|
| CSS_Configure     | 0x0006 | Conn_Hdl   | *none*        |
|                   |        | Slot_Idx   |               |

<br>

***Command parameters:***

| Name     | Value                      | Description       |
|----------|----------------------------|-------------------|
| Conn_Hdl | 0xXXXX *(2 bytes)*         | Connection Handle |
| Slot_Idx | 0xXXXX *(2 bytes)*         | Slot ID           |

<br>
<br>

Connection Strict Scheduling - Read Connection Slot
---------------------------------------------------

*Read current connection slot index*

***Command structure:***

| Command           | OCF    | Params       | Return params |
|-------------------|--------|--------------|---------------|
| CSS_Set_Next_Slot | 0x0007 | Conn_Hdl     | Conn_Hdl      |
|                   |        |              | Slot_Idx      |

<br>

***Command parameters:***

| Name     | Value                      | Description       |
|----------|----------------------------|-------------------|
| Conn_Hdl | 0xXXXX *(2 bytes)*         | Connection Handle |

<br>

***Return parameters:***

| Name     | Value               | Description           |
|----------|---------------------|-----------------------|
| Conn_Hdl | 0xXXXX *(2 bytes)*  | Connection Handle     |
| Slot_Idx | 0xXXXX *(2 bytes)*  | Slot ID               |

<br>
<br>

Set Data Length
---------------

*Customize TX/RX values* <br>
*Waits for Data Length Changed event*

***Command structure:***

| Command      | OCF    | Params          | Return params |
|--------------|--------|-----------------|---------------|
| Set_Data_Len | 0x0008 | Conn_Hdl<br>    | Conn_Hdl      |
|              |        | Tx_Octets<br>   |               |
|              |        | Tx_Time<br>     |               |
|              |        | Rx_Octets<br>   |               |
|              |        | Rx_Time<br>     |               |

<br>

***Command parameters:***

| Name      | Value                      | Description       |
|-----------|----------------------------|-------------------|
| Conn_Hdl  | 0xXXXX *(2 bytes)*          | Connection Handle |
| Tx_Octets | 0xXXXX *(2 bytes)*          | Transmit octets   |
| Tx_Time   | 0xXXXX *(2 bytes)*          | Transmission time |
| Rx_Octets | 0xXXXX *(2 bytes)*          | Receiver octets   |
| Rx_Time   | 0xXXXX *(2 bytes)*          | Receiver time     |

<br>

***Return parameters:***

| Name     | Value               | Description           |
|----------|---------------------|-----------------------|
| Conn_Hdl | 0xXXXX *(2 bytes)*  | Connection Handle     |

<br>
<br>

Set Antenna Location
--------------------

***Command structure:***

| Command       | OCF    | Params         | Return params |
|---------------|--------|----------------|---------------|
| Set_Ant_Loc   | 0x0009 | Ant_Loc        | *none*        |

<br>

***Command parameters:***

| Name    | Value                      | Description      |
|---------|----------------------------|------------------|
| Ant_Loc | 0xXX *(1 byte)*            | Antenna location |

<br>
<br>

Set Local Identity Resolving Key
--------------------------------

*Set own address type & identity resolving key*

***Command structure:***

| Command         | OCF    | Params        | Return params |
|-----------------|--------|---------------|---------------|
| Set_Local_IRK   | 0x000A | Own_Addr_Type | *none*        |
|                 |        | IRK           |               |

<br>

***Command parameters:***

| Name          | Value                      | Description            |
|---------------|----------------------------|------------------------|
| Own_Addr_Type | 0xXX *(1 byte)*            | Device Address Type    |
| IRK           | 0x(16)XX *(16 bytes)*      | Identity Resolving Key |

<br>
<br>

Set Scan Configuration
----------------------

*Set preferred advertisement type*

***Command structure:***

| Command         | OCF    | Params        | Return params |
|-----------------|--------|---------------|---------------|
| Set_Scan_Cfg    | 0x000B | Flags         | *none*        |
|                 |        | Rssi_Tres     |               |

<br>

***Command parameters:***

| Name     | Value                      | Description             |
|----------|----------------------------|-------------------------|
| Flags    | 0x00000001 *(4 bytes)*     | No legacy advertising   |
|          | 0x00000002 *(4 bytes)*     | No extended advertising |
|Rssi_Tres | 0xXX *(1 byte)*            | RSSI threshold value    |
