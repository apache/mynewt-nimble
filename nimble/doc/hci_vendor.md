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
Selected TX power is returned <br>
Setting 0xFF restores controller setting to default <br>

| Command           | OCF    | Params     | Return params |
|-------------------|--------|------------|---------------|
| Set_Tx_Pwr        | 0x0002 | Tx_Pwr     | Sel_Tx_Pwr    |

<br>
Tx_Pwr

| Value                      | Description            |
|----------------------------|------------------------|
| 0xXX                       | Desired TX Power Level |

*Size: 1 octet*

<br>
Sel_Tx_Pwr

| Value            | Description                  |
|------------------|------------------------------|
| 0xXX             | Controller Selected TX power |

*Size: 1 octet*


Configure Connection Strict Scheduling
--------------------------------------

Configure Connection Strict Scheduling

| Command           | OCF    | Params     | Return params |
|-------------------|--------|------------|---------------|
| CSS_Configure     | 0x0003 | Slot_us    | *none*        |
|                   |        | Num_Slots  |               |

<br>
Slot_us

| Value                      | Description           |
|----------------------------|-----------------------|
| 0xXXXXXX                   | Slot duration in msec |

*Size: 4 octets*

<br>
Number_Of_Slots

| Value                      | Description            |
|----------------------------|------------------------|
| 0xXXXXXX                   | Number of period slots |

*Size: 4 octets*


Connection strict scheduling enable
-----------------------------------

Enable/Disable Connection Strict Scheduling

| Command      | OCF    | Params       | Return params |
|--------------|--------|--------------|---------------|
| CSS_Enable   | 0x0004 |Enable/Disale | *none*        |


<br>
Enable/Disable

| Value                      | Description        |
|----------------------------|--------------------|
| 0xXX                       | Enable/Disable CSS |

*Size: 1 octet*


Connection Strict Scheduling - Select Next Slot
-----------------------------------------------

Set next slot index for future connection

| Command           | OCF    | Params       | Return params |
|-------------------|--------|--------------|---------------|
| CSS_Set_Next_Slot | 0x0005 |Next_Slot_Idx | *none*        |


<br>
Next_Slot_Idx

| Value                      | Description  |
|----------------------------|--------------|
| 0xXXXX                     | Next Slot ID |

*Size: 2 octets*


Connection Strict Scheduling - Select Slot For Specific Connection
------------------------------------------------------------------

Set slot index for current connection

| Command           | OCF    | Params     | Return params |
|-------------------|--------|------------|---------------|
| CSS_Configure     | 0x0006 | Conn_Hdl   | *none*        |
|                   |        | Slot_Idx   |               |

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
| 0xXXXX                     | Slot ID     |

*Size: 2 octets*


Connection Strict Scheduling - Read Connection Slot
---------------------------------------------------

Read current connection slot index

| Command           | OCF    | Params       | Return params |
|-------------------|--------|--------------|---------------|
| CSS_Set_Next_Slot | 0x0007 | Conn_Hdl     | Conn_Hdl      |
|                   |        |              | Slot_Idx      |

<br>
Conn_Hdl

| Value                      | Description       |
|----------------------------|-------------------|
| 0xXXXX                     | Connection Handle |

*Size: 2 octets*

<br>
Slot_Idx

| Value              | Description |
|--------------------|-------------|
| 0xXXXX             | Slot_ID     |
*Size: 2 octets*


Set Data Length
---------------

Change TX/RX values.
Waits for Data Length Changed event.

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

| Value                      | Description     |
|----------------------------|-----------------|
| 0xXXXX                     | Transmit octets |

*Size: 2 octets*

<br> 
Tx_Time

| Value                      | Description       |
|----------------------------|-------------------|
| 0xXXXX                     | Transmission time |

*Size: 2 octets*

<br>
Rx_Octets

| Value                      | Description     |
|----------------------------|-----------------|
| 0xXXXX                     | Receiver octets |

*Size: 2 octets*

<br>
Rx_Time

| Value                      | Description   |
|----------------------------|---------------|
| 0xXXXX                     | Receiver time |

*Size: 2 octets*


Set Antenna Location
--------------------

| Command       | OCF    | Params         | Return params |
|---------------|--------|----------------|---------------|
| Set_Ant_Loc   | 0x0009 | Ant_Loc        | *none*        |


<br>
Ant_Loc

| Value                      | Description      |
|----------------------------|------------------|
| 0xXX                       | Antenna location |

*Size: 1 ocet*


Set Local Identity Resolving Key
--------------------------------

Set own address type & identity resolving key

| Command         | OCF    | Params        | Return params |
|-----------------|--------|---------------|---------------|
| Set_Local_IRK   | 0x000A | Own_Addr_Type | *none*        |
|                 |        | IRK           |               |

<br>
Rx_Time

| Value                      | Description         |
|----------------------------|---------------------|
| 0xXX                       | Device Address Type |

*Size: 1 ocet*

<br>
IRK

| Value              | Description            |
|--------------------|------------------------|
| 0xXX(16)           | Identity Resolving Key |

*Size: 16 ocets*


Set Scan Configuration
----------------------

| Command         | OCF    | Params        | Return params |
|-----------------|--------|---------------|---------------|
| Set_Scan_Cfg    | 0x000B | Flags         | *none*        |
|                 |        | Rssi_Tres     |               |

<br>
Flags

| Value                      | Description             |
|----------------------------|-------------------------|
| 0x00000001<br>             | No legacy advertising   |
| 0x00000002                 | No extended advertising |

*Size: 4 octets*

<br>
Rssi_Tres

| Value                      | Description         |
|----------------------------|---------------------|
| 0xXX                       | RSSI treshold value |

*Size: 1 octet*
