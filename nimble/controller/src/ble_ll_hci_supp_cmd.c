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

#include <stdint.h>
#include <string.h>
#include <syscfg/syscfg.h>
#include <controller/ble_ll.h>
#include <controller/ble_ll_hci.h>

/* Magic macros */
#define CMD_BIT(n)  (1 << (n)) |
#define OCTET(x)    (0 | x 0)

static const uint8_t octet_0 = OCTET(
#if MYNEWT_VAL(BLE_LL_ROLE_PERIPHERAL) || MYNEWT_VAL(BLE_LL_ROLE_CENTRAL)
    CMD_BIT(5) /* HCI Disconnect */
#endif
);

static const uint8_t octet_2 = OCTET(
#if MYNEWT_VAL(BLE_LL_ROLE_PERIPHERAL) || MYNEWT_VAL(BLE_LL_ROLE_CENTRAL)
    CMD_BIT(7) /* HCI Read Remote Version Information */
#endif
);

static const uint8_t octet_5 = OCTET(
    CMD_BIT(6) /* HCI Set Event Mask */
    CMD_BIT(7) /* HCI Reset */
);

static const uint8_t octet_10 = OCTET(
#if MYNEWT_VAL(BLE_LL_ROLE_PERIPHERAL) || MYNEWT_VAL(BLE_LL_ROLE_CENTRAL)
    CMD_BIT(2) /* HCI Read Transmit Power Level */
#endif
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_CTRL_TO_HOST_FLOW_CONTROL)
    CMD_BIT(5) /* HCI Set Controller To Host Flow Control */
    CMD_BIT(6) /* HCI Host Buffer Size */
    CMD_BIT(7) /* HCI Host Number Of Completed Packets */
#endif
);

static const uint8_t octet_14 = OCTET(
    CMD_BIT(3) /* HCI Read Local Version Information */
    CMD_BIT(5) /* HCI Read Local Supported Features */
);

static const uint8_t octet_15 = OCTET(
    CMD_BIT(1) /* HCI Read BD ADDR */
#if MYNEWT_VAL(BLE_LL_ROLE_PERIPHERAL) || MYNEWT_VAL(BLE_LL_ROLE_CENTRAL)
    CMD_BIT(5) /* HCI Read RSSI */
#endif
);

static const uint8_t octet_22 = OCTET(
    CMD_BIT(2) /* HCI Set Event Mask Page 2 */
);

static const uint8_t octet_25 = OCTET(
    CMD_BIT(0) /* HCI LE Set Event Mask */
    CMD_BIT(1) /* HCI LE Read Buffer Size [v1] */
    CMD_BIT(2) /* HCI LE Read Local Supported Features */
    CMD_BIT(4) /* HCI LE Set Random Address */
#if MYNEWT_VAL(BLE_LL_ROLE_BROADCASTER)
    CMD_BIT(5) /* HCI LE Set Advertising Parameters */
    CMD_BIT(6) /* HCI LE Read Advertising Physical Channel Tx Power */
    CMD_BIT(7) /* HCI LE Set Advertising Data */
#endif
);

static const uint8_t octet_26 = OCTET(
#if MYNEWT_VAL(BLE_LL_ROLE_BROADCASTER)
    CMD_BIT(0) /* HCI LE Set Scan Response Data */
    CMD_BIT(1) /* HCI LE Set Advertising Enable */
#endif
#if MYNEWT_VAL(BLE_LL_ROLE_OBSERVER)
    CMD_BIT(2) /* HCI LE Set Scan Parameters */
    CMD_BIT(3) /* HCI LE Set Scan Enable */
#endif
#if MYNEWT_VAL(BLE_LL_ROLE_CENTRAL)
    CMD_BIT(4) /* HCI LE Create Connection */
    CMD_BIT(5) /* HCI LE Create Connection Cancel */
#endif
    CMD_BIT(6) /* HCI LE Read Filter Accept List Size */
    CMD_BIT(7) /* HCI LE Clear Filter Accept List */
);

static const uint8_t octet_27 = OCTET(
    CMD_BIT(0) /* HCI LE Add Device To Filter Accept List */
    CMD_BIT(1) /* HCI LE Remove Device From Filter Accept List */
#if MYNEWT_VAL(BLE_LL_ROLE_PERIPHERAL) || MYNEWT_VAL(BLE_LL_ROLE_CENTRAL)
    CMD_BIT(2) /* HCI LE Connection Update */
#endif
    CMD_BIT(3) /* HCI LE Set Host Channel Classification */
#if MYNEWT_VAL(BLE_LL_ROLE_PERIPHERAL) || MYNEWT_VAL(BLE_LL_ROLE_CENTRAL)
    CMD_BIT(4) /* HCI LE Read Channel Map */
    CMD_BIT(5) /* HCI LE Read Remote Features */
#endif
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LE_ENCRYPTION)
    CMD_BIT(6) /* HCI LE Encrypt */
#endif
    CMD_BIT(7) /* HCI LE Rand */
);

static const uint8_t octet_28 = OCTET(
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LE_ENCRYPTION)
#if MYNEWT_VAL(BLE_LL_ROLE_CENTRAL)
    CMD_BIT(0) /* HCI LE Enable Encryption */
#endif
#if MYNEWT_VAL(BLE_LL_ROLE_PERIPHERAL)
    CMD_BIT(1) /* HCI LE Long Term Key Request Reply */
    CMD_BIT(2) /* HCI LE Long Term Key Request Negative Reply */
#endif
#endif
    CMD_BIT(3) /* HCI LE Read Supported States */
#if MYNEWT_VAL(BLE_LL_DTM)
    CMD_BIT(4) /* HCI LE Receiver Test [v1] */
    CMD_BIT(5) /* HCI LE Transmitter Test [v1] */
    CMD_BIT(6) /* HCI LE Test End */
#endif
);

static const uint8_t octet_32 = OCTET(
#if (MYNEWT_VAL(BLE_LL_ROLE_PERIPHERAL) || MYNEWT_VAL(BLE_LL_ROLE_CENTRAL)) && \
    MYNEWT_VAL(BLE_LL_CFG_FEAT_LE_PING)
    CMD_BIT(4) /* HCI Read Authenticated Payload Timeout */
    CMD_BIT(5) /* HCI Write Authenticated Payload Timeout */
#endif
);

static const uint8_t octet_33 = OCTET(
#if MYNEWT_VAL(BLE_LL_ROLE_PERIPHERAL) || MYNEWT_VAL(BLE_LL_ROLE_CENTRAL)
    CMD_BIT(4) /* HCI LE Remote Connection Parameter Request Reply */
    CMD_BIT(5) /* HCI LE Remote Connection Parameter Request Negative Reply */
#endif
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_DATA_LEN_EXT)
    CMD_BIT(6) /* HCI LE Set Data Length */
    CMD_BIT(7) /* HCI LE Read Suggested Default Data Length */
#endif
);

static const uint8_t octet_34 = OCTET(
#if MYNEWT_VAL(BLE_LL_ROLE_PERIPHERAL) || MYNEWT_VAL(BLE_LL_ROLE_CENTRAL)
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_DATA_LEN_EXT)
    CMD_BIT(0) /* HCI LE Write Suggested Data Length */
#endif
#endif
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY)
    CMD_BIT(3) /* HCI LE Add Device To Resolving List */
    CMD_BIT(4) /* HCI LE Remove Device From Resolving List */
    CMD_BIT(5) /* HCI LE Clear Resolving List */
    CMD_BIT(6) /* HCI LE Read Resolving List Size */
    CMD_BIT(7) /* HCI LE Read Peer Resolvable Address */
#endif
);

static const uint8_t octet_35 = OCTET(
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY)
    CMD_BIT(0) /* HCI LE Read Local Resolvable Address */
    CMD_BIT(1) /* HCI LE Set Address Resolution Enable */
    CMD_BIT(2) /* HCI LE Set Resolvable Private Address Timeout */
#endif
    CMD_BIT(3) /* HCI LE Read Maximum Data Length */
#if MYNEWT_VAL(BLE_LL_PHY)
#if MYNEWT_VAL(BLE_LL_ROLE_PERIPHERAL) || MYNEWT_VAL(BLE_LL_ROLE_CENTRAL)
    CMD_BIT(4) /* HCI LE Read PHY */
    CMD_BIT(5) /* HCI LE Set Default PHY */
    CMD_BIT(6) /* HCI LE Set PHY */
#endif
#endif
#if MYNEWT_VAL(BLE_LL_DTM)
    CMD_BIT(7) /* HCI LE Receiver Test [v2] */
#endif
);

static const uint8_t octet_36 = OCTET(
#if MYNEWT_VAL(BLE_LL_DTM)
    CMD_BIT(0) /* HCI LE Transmitter Test [v2] */
#endif
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV) && MYNEWT_VAL(BLE_LL_ROLE_BROADCASTER)
    CMD_BIT(1) /* HCI LE Set Advertising Set Random Address */
    CMD_BIT(2) /* HCI LE Set Extended Advertising Parameters */
    CMD_BIT(3) /* HCI LE Set Extended Advertising Data */
    CMD_BIT(4) /* HCI LE Set Extended Scan Response Data */
    CMD_BIT(5) /* HCI LE Set Extended Advertising Enable */
    CMD_BIT(6) /* HCI LE Read Maximum Advertising Data Length */
    CMD_BIT(7) /* HCI LE Read Number of Supported Advertising Sets */
#endif
);

static const uint8_t octet_37 = OCTET(
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV) && MYNEWT_VAL(BLE_LL_ROLE_BROADCASTER)
    CMD_BIT(0) /* HCI LE Remove Advertising Set */
    CMD_BIT(1) /* HCI LE Clear Advertising Sets */
#endif
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PERIODIC_ADV) && MYNEWT_VAL(BLE_LL_ROLE_BROADCASTER)
    CMD_BIT(2) /* HCI LE Set Periodic Advertising Parameters */
    CMD_BIT(3) /* HCI LE Set Periodic Advertising Data */
    CMD_BIT(4) /* HCI LE Set Periodic Advertising Enable */
#endif
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
#if MYNEWT_VAL(BLE_LL_ROLE_OBSERVER)
    CMD_BIT(5) /* HCI LE Set Extended Scan Parameters */
    CMD_BIT(6) /* HCI LE Set Extended Scan Enable */
#endif
#if MYNEWT_VAL(BLE_LL_ROLE_CENTRAL)
    CMD_BIT(7) /* HCI LE Extended Create Connection */
#endif
#endif
);

static const uint8_t octet_38 = OCTET(
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PERIODIC_ADV) && MYNEWT_VAL(BLE_LL_ROLE_OBSERVER)
    CMD_BIT(0) /* HCI LE Periodic Advertising Create Sync */
    CMD_BIT(1) /* HCI LE Periodic Advertising Create Sync Cancel */
    CMD_BIT(2) /* HCI LE Periodic Advertising Terminate Sync */
    CMD_BIT(3) /* HCI LE Add Device To Periodic Advertiser List */
    CMD_BIT(4) /* HCI LE Remove Device From Periodic Advertiser List */
    CMD_BIT(5) /* HCI LE Clear Periodic Advertiser List */
    CMD_BIT(6) /* HCI LE Read Periodic Advertiser List Size */
#endif
    CMD_BIT(7) /* HCI LE Read Transmit Power */
);

static const uint8_t octet_39 = OCTET(
    CMD_BIT(0) /* HCI LE Read RF Path Compensation */
    CMD_BIT(1) /* HCI LE Write RF Path Compensation */
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PRIVACY)
    CMD_BIT(2) /* HCI LE Set Privacy Mode */
#endif
);

static const uint8_t octet_40 = OCTET(
#if MYNEWT_VAL(BLE_VERSION) >= 51 && MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PERIODIC_ADV)
#if MYNEWT_VAL(BLE_LL_ROLE_OBSERVER)
    CMD_BIT(5) /* HCI LE Set Periodic Advertising Receive Enable */
#endif
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PERIODIC_ADV_SYNC_TRANSFER)
#if MYNEWT_VAL(BLE_LL_ROLE_OBSERVER)
    CMD_BIT(6) /* HCI LE Periodic Advertising Sync Transfer */
#endif
#if MYNEWT_VAL(BLE_LL_ROLE_BROADCASTER)
    CMD_BIT(7) /* HCI LE Periodic Advertising Set Info Transfer */
#endif
#endif
#endif
);

static const uint8_t octet_41 = OCTET(
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_PERIODIC_ADV_SYNC_TRANSFER)
    CMD_BIT(0) /* HCI LE Set Periodic Advertising Sync Transfer Parameters */
    CMD_BIT(1) /* HCI LE Set Default Periodic Advertising Sync Transfer Parameters */
#endif
#if MYNEWT_VAL(BLE_LL_ISO)
    CMD_BIT(5) /* HCI LE Read Buffer Size [v2] */
    CMD_BIT(6) /* HCI LE Read ISO TX Sync */
#endif
);

static const uint8_t octet_42 = OCTET(
#if MYNEWT_VAL(BLE_LL_ISO_BROADCASTER)
    CMD_BIT(5) /* HCI LE Create BIG */
    CMD_BIT(6) /* HCI LE Create BIG Test */
    CMD_BIT(7) /* HCI LE Terminate BIG */
#endif
);

static const uint8_t octet_43 = OCTET(
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_SCA_UPDATE)
    CMD_BIT(2) /* HCI LE Request Peer SCA */
#endif
#if MYNEWT_VAL(BLE_LL_ISO)
    CMD_BIT(3) /* HCI LE Setup ISO Data Path */
    CMD_BIT(4) /* HCI LE Remove ISO Data Path */
#endif
#if MYNEWT_VAL(BLE_LL_ISO_BROADCASTER)
    CMD_BIT(5) /* HCI LE ISO Transmit Test */
#endif
);

static const uint8_t octet_44 = OCTET(
#if MYNEWT_VAL(BLE_LL_ISO)
    CMD_BIT(0) /* HCI LE ISO Test End */
#endif
#if BLE_LL_HOST_CONTROLLED_FEATURES
    CMD_BIT(1) /* HCI LE Set Host Feature */
#endif
);

static const uint8_t octet_46 = OCTET(
#if MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_ENHANCED_CONN_UPDATE)
    CMD_BIT(0) /* HCI LE Set Default Subrate */
    CMD_BIT(1) /* HCI LE Subrate Request */
#endif
#if MYNEWT_VAL(BLE_VERSION) >= 54 && MYNEWT_VAL(BLE_LL_CFG_FEAT_LL_EXT_ADV)
    CMD_BIT(2) /* HCI LE Set Extended Advertising Parameters [v2] */
#endif
);

static const uint8_t g_ble_ll_hci_supp_cmds[64] = {
    octet_0,
    0,
    octet_2,
    0,
    0,
    octet_5,
    0,
    0,
    0,
    0,
    octet_10,
    0,
    0,
    0,
    octet_14,
    octet_15,
    0,
    0,
    0,
    0,
    0,
    0,
    octet_22,
    0,
    0,
    octet_25,
    octet_26,
    octet_27,
    octet_28,
    0,
    0,
    0,
    octet_32,
    octet_33,
    octet_34,
    octet_35,
    octet_36,
    octet_37,
    octet_38,
    octet_39,
    octet_40,
    octet_41,
    octet_42,
    octet_43,
    octet_44,
    0,
    octet_46,
};

void
ble_ll_hci_supp_cmd_get(uint8_t *buf)
{
    memcpy(buf, g_ble_ll_hci_supp_cmds, sizeof(g_ble_ll_hci_supp_cmds));
}
