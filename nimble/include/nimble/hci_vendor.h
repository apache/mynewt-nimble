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

#ifndef H_BLE_HCI_VENDOR_
#define H_BLE_HCI_VENDOR_

#ifdef __cplusplus
extern "C" {
#endif

/* Here is a list of the vendor specific OCFs */
#define BLE_HCI_OCF_VENDOR_CAPS         (0x153)
#define BLE_HCI_OCF_MULTI_ADV           (0x154)

/* Multi-advertiser sub-commands */
#define BLE_HCI_MULTI_ADV_PARAMS        (0x01)
#define BLE_HCI_MULTI_ADV_DATA          (0x02)
#define BLE_HCI_MULTI_ADV_SCAN_RSP_DATA (0x03)
#define BLE_HCI_MULTI_ADV_SET_RAND_ADDR (0x04)
#define BLE_HCI_MULTI_ADV_ENABLE        (0x05)

/* Command lengths. Includes sub-command opcode */
#define BLE_HCI_MULTI_ADV_PARAMS_LEN        (24)
#define BLE_HCI_MULTI_ADV_DATA_LEN          (34)
#define BLE_HCI_MULTI_ADV_SCAN_RSP_DATA_LEN (34)
#define BLE_HCI_MULTI_ADV_SET_RAND_ADDR_LEN (8)
#define BLE_HCI_MULTI_ADV_ENABLE_LEN        (3)

/* Vendor specific events (LE meta events) */
#define BLE_HCI_LE_SUBEV_ADV_STATE_CHG      (0x55)

/* Data structures associated with vendor specific commands */
struct hci_vendor_capabilities
{
    uint8_t max_advt_instances;
    uint8_t offloaded_resolution_of_priv_addr;
    uint16_t total_scan_results_bytes;
    uint8_t max_irk_list_sz;
    uint8_t filtering_support;
    uint8_t max_filters;
    uint8_t activity_energy_info_support;
    uint16_t version_supported;
    uint16_t total_adv_tracked;
    uint8_t extended_scan_support;
    uint8_t debug_logging_supported;
};

/* NOTE: these are not in command order */
struct hci_multi_adv_params
{
    uint8_t adv_type;
    uint8_t adv_channel_map;
    uint8_t own_addr_type;
    uint8_t peer_addr_type;
    uint8_t adv_filter_policy;
    int8_t adv_tx_pwr;          /* -70 to +20 */
    uint8_t adv_instance;
    uint16_t adv_itvl_min;
    uint16_t adv_itvl_max;
    uint8_t peer_addr[BLE_DEV_ADDR_LEN];
    uint8_t own_addr[BLE_DEV_ADDR_LEN];
};

/*
 * NOTE: structures are not defined for the following sub commands.
 * The format of these commands is:
 *
 * Multi-adv Set Advertising Data:
 *  - Advertising data length (1)
 *  - Advertising data (31)
 *  - Advertising Instance (1)
 *
 *  Multi-adv Set Scan Response Data:
 *  - Scan response data length (1)
 *  - Scan response data (31)
 *  - Advertising Instance (1)
 *
 *  Multi-adv Set Random Address:
 *  - Random Address (6)
 *  - Advertising Instance (1)
 *
 *  Multi-adv Set Advertising Enable:
 *  - Random Address (6)
 *  - Advertising Instance (1)
 *
 *  All of these commands generate a Command Complete with this format:
 *  - Status (1)
 *  - Multi-adv opcode (1)
 */


#ifdef __cplusplus
}
#endif

#endif /* H_BLE_HCI_VENDOR_ */
