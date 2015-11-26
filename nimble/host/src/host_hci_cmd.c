/**
 * Copyright (c) 2015 Runtime Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include "os/os.h"
#include "console/console.h"
#include "nimble/hci_common.h"
#include "nimble/hci_transport.h"
#include "host/ble_hs.h"
#include "host/host_hci.h"
#include "host_dbg.h"
#include "ble_hs_ack.h"
#include "ble_hs_conn.h"
#include "ble_l2cap.h"

static int
host_hci_cmd_send(uint8_t *cmdbuf)
{
#ifdef ARCH_sim
    return 0;
#else
    return ble_hci_transport_host_cmd_send(cmdbuf);
#endif
}

static int
host_hci_le_cmd_send(uint16_t ocf, uint8_t len, void *cmddata)
{
    int rc;
    uint8_t *cmd;
    uint16_t opcode;

    /* Don't allow multiple commands "in flight." */
    assert(host_hci_outstanding_opcode == 0);

    rc = -1;
    cmd = os_memblock_get(&g_hci_cmd_pool);
    if (cmd) {
        opcode = (BLE_HCI_OGF_LE << 10) | ocf;
        htole16(cmd, opcode);
        cmd[2] = len;
        if (len) {
            memcpy(cmd + BLE_HCI_CMD_HDR_LEN, cmddata, len);
        }
        rc = host_hci_cmd_send(cmd);
        if (rc == 0) {
            host_hci_outstanding_opcode = opcode;
        }
    }

    return rc;
}

static int
host_hci_cmd_le_whitelist_chg(uint8_t *addr, uint8_t addr_type, uint8_t ocf)
{
    int rc;
    uint8_t cmd[BLE_HCI_CHG_WHITE_LIST_LEN];

    if (addr_type <= BLE_ADDR_TYPE_RANDOM) {
        cmd[0] = addr_type;
        memcpy(cmd + 1, addr, BLE_DEV_ADDR_LEN);
        rc = host_hci_le_cmd_send(ocf, BLE_HCI_CHG_WHITE_LIST_LEN, cmd);
    } else {
        rc = BLE_ERR_INV_HCI_CMD_PARMS;
    }
    return rc;
}

int
host_hci_cmd_le_set_adv_params(struct hci_adv_params *adv)
{
    int rc;
    uint16_t itvl;
    uint8_t cmd[BLE_HCI_SET_ADV_PARAM_LEN];

    assert(adv != NULL);

    /* Make sure parameters are valid */
    rc = -1;
    if ((adv->adv_itvl_min > adv->adv_itvl_max) ||
        (adv->own_addr_type > BLE_HCI_ADV_OWN_ADDR_MAX) ||
        (adv->peer_addr_type > BLE_HCI_ADV_PEER_ADDR_MAX) ||
        (adv->adv_filter_policy > BLE_HCI_ADV_FILT_MAX) ||
        (adv->adv_type > BLE_HCI_ADV_TYPE_MAX) ||
        (adv->adv_channel_map == 0) ||
        ((adv->adv_channel_map & 0xF8) != 0)) {
        /* These parameters are not valid */
        return rc;
    }

    /* Make sure interval is valid for advertising type. */
    if ((adv->adv_type == BLE_HCI_ADV_TYPE_ADV_NONCONN_IND) || 
        (adv->adv_type == BLE_HCI_ADV_TYPE_ADV_SCAN_IND)) {
        itvl = BLE_HCI_ADV_ITVL_NONCONN_MIN;
    } else {
        itvl = BLE_HCI_ADV_ITVL_MIN;
    }

    /* Do not check if high duty-cycle directed */
    if (adv->adv_type != BLE_HCI_ADV_TYPE_ADV_DIRECT_IND_HD) {
        if ((adv->adv_itvl_min < itvl) || 
            (adv->adv_itvl_min > BLE_HCI_ADV_ITVL_MAX)) {
            return rc;
        }
    }

    htole16(cmd, adv->adv_itvl_min);
    htole16(cmd + 2, adv->adv_itvl_max);
    cmd[4] = adv->adv_type;
    cmd[5] = adv->own_addr_type;
    cmd[6] = adv->peer_addr_type;
    memcpy(cmd + 7, adv->peer_addr, BLE_DEV_ADDR_LEN);
    cmd[13] = adv->adv_channel_map;
    cmd[14] = adv->adv_filter_policy;

    rc = host_hci_le_cmd_send(BLE_HCI_OCF_LE_SET_ADV_PARAMS, 
                              BLE_HCI_SET_ADV_PARAM_LEN, cmd);

    return rc;
}

int
host_hci_cmd_le_set_adv_data(uint8_t *data, uint8_t len)
{
    int rc;
    uint8_t cmd[BLE_HCI_MAX_ADV_DATA_LEN + 1];

    /* Check for valid parameters */
    if (((data == NULL) && (len != 0)) || (len > BLE_HCI_MAX_ADV_DATA_LEN)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    cmd[0] = len;
    memcpy(cmd + 1, data, len);
    rc = host_hci_le_cmd_send(BLE_HCI_OCF_LE_SET_ADV_DATA, len + 1, cmd);

    return rc;
}

int
host_hci_cmd_le_set_scan_rsp_data(uint8_t *data, uint8_t len)
{
    int rc;
    uint8_t cmd[BLE_HCI_MAX_SCAN_RSP_DATA_LEN + 1];

    /* Check for valid parameters */
    if (((data == NULL) && (len != 0)) ||
         (len > BLE_HCI_MAX_SCAN_RSP_DATA_LEN)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    cmd[0] = len;
    memcpy(cmd + 1, data, len);
    rc = host_hci_le_cmd_send(BLE_HCI_OCF_LE_SET_SCAN_RSP_DATA, len + 1, cmd);

    return rc;
}

/**
 * ble host hci cmd le set rand addr
 *  
 * Sets the random address to be used in advertisements. 
 * 
 * @param addr Pointer to the random address to send to device
 * 
 * @return int 
 */
int
host_hci_cmd_le_set_rand_addr(uint8_t *addr)
{
    int rc;
    uint8_t cmd[BLE_DEV_ADDR_LEN];

    /* Check for valid parameters */
    rc = -1;
    if (addr) {
        rc = host_hci_le_cmd_send(BLE_HCI_OCF_LE_SET_RAND_ADDR,
                                  BLE_DEV_ADDR_LEN, cmd);
    }

    return rc;
}

int
host_hci_cmd_le_set_event_mask(uint64_t event_mask)
{
    int rc;
    uint8_t cmd[sizeof(uint64_t)];

    htole64(cmd, event_mask);
    rc = host_hci_le_cmd_send(BLE_HCI_OCF_LE_SET_EVENT_MASK, sizeof(uint64_t), 
                              cmd);

    return rc;
}

int
host_hci_cmd_le_read_buffer_size(void)
{
    int rc;

    rc = host_hci_le_cmd_send(BLE_HCI_OCF_LE_RD_BUF_SIZE, 0, NULL);
    return rc;
}

int
host_hci_cmd_le_set_adv_enable(uint8_t enable)
{
    int rc;
    uint8_t cmd[BLE_HCI_SET_ADV_ENABLE_LEN];

    cmd[0] = enable;
    rc = host_hci_le_cmd_send(BLE_HCI_OCF_LE_SET_ADV_ENABLE, 
                              BLE_HCI_SET_ADV_ENABLE_LEN, cmd);

    return rc;
}

int
host_hci_cmd_le_set_scan_params(uint8_t scan_type, uint16_t scan_itvl, 
                                uint16_t scan_window, uint8_t own_addr_type, 
                                uint8_t filter_policy) {
    int rc;
    uint8_t cmd[BLE_HCI_SET_SCAN_PARAM_LEN];

    /* Make sure parameters are valid */
    if ((scan_type != BLE_HCI_SCAN_TYPE_PASSIVE) && 
        (scan_type != BLE_HCI_SCAN_TYPE_ACTIVE)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Check interval and window */
    if ((scan_itvl < BLE_HCI_SCAN_ITVL_MIN) || 
        (scan_itvl > BLE_HCI_SCAN_ITVL_MAX) ||
        (scan_window < BLE_HCI_SCAN_WINDOW_MIN) ||
        (scan_window > BLE_HCI_SCAN_WINDOW_MAX) ||
        (scan_itvl < scan_window)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Check own addr type */
    if (own_addr_type > BLE_HCI_ADV_OWN_ADDR_MAX) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Check scanner filter policy */
    if (filter_policy > BLE_HCI_SCAN_FILT_MAX) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    cmd[0] = scan_type;
    htole16(cmd + 1, scan_itvl);
    htole16(cmd + 3, scan_window);
    cmd[5] = own_addr_type;
    cmd[6] = filter_policy;

    rc = host_hci_le_cmd_send(BLE_HCI_OCF_LE_SET_SCAN_PARAMS, 
                              BLE_HCI_SET_SCAN_PARAM_LEN, cmd);

    return rc;
}

int
host_hci_cmd_le_set_scan_enable(uint8_t enable, uint8_t filter_dups)
{
    int rc;
    uint8_t cmd[BLE_HCI_SET_SCAN_ENABLE_LEN];

    cmd[0] = enable;
    cmd[1] = filter_dups;
    rc = host_hci_le_cmd_send(BLE_HCI_OCF_LE_SET_SCAN_ENABLE, 
                              BLE_HCI_SET_SCAN_ENABLE_LEN, cmd);
    return rc;
}

int
host_hci_cmd_le_create_connection(struct hci_create_conn *hcc)
{
    int rc;
    uint8_t cmd[BLE_HCI_CREATE_CONN_LEN];

    /* Check scan interval and scan window */
    if ((hcc->scan_itvl < BLE_HCI_SCAN_ITVL_MIN) ||
        (hcc->scan_itvl > BLE_HCI_SCAN_ITVL_MAX) ||
        (hcc->scan_window < BLE_HCI_SCAN_WINDOW_MIN) ||
        (hcc->scan_window > BLE_HCI_SCAN_WINDOW_MAX) ||
        (hcc->scan_itvl < hcc->scan_window)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Check initiator filter policy */
    if (hcc->filter_policy > BLE_HCI_CONN_FILT_MAX) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Check peer addr type */
    if (hcc->peer_addr_type > BLE_HCI_CONN_PEER_ADDR_MAX) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Check own addr type */
    if (hcc->own_addr_type > BLE_HCI_ADV_OWN_ADDR_MAX) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Check connection interval min */
    if ((hcc->conn_itvl_min < BLE_HCI_CONN_ITVL_MIN) ||
        (hcc->conn_itvl_min > BLE_HCI_CONN_ITVL_MAX)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Check connection interval max */
    if ((hcc->conn_itvl_max < BLE_HCI_CONN_ITVL_MIN) ||
        (hcc->conn_itvl_max > BLE_HCI_CONN_ITVL_MAX) ||
        (hcc->conn_itvl_max < hcc->conn_itvl_min)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Check connection latency */
    if ((hcc->conn_latency < BLE_HCI_CONN_LATENCY_MIN) ||
        (hcc->conn_latency > BLE_HCI_CONN_LATENCY_MAX)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Check supervision timeout */
    if ((hcc->supervision_timeout < BLE_HCI_CONN_SPVN_TIMEOUT_MIN) ||
        (hcc->supervision_timeout > BLE_HCI_CONN_SPVN_TIMEOUT_MAX)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Check connection event length */
    if (hcc->min_ce_len > hcc->max_ce_len) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    htole16(cmd + 0, hcc->scan_itvl);
    htole16(cmd + 2, hcc->scan_window);
    cmd[4] = hcc->filter_policy;
    cmd[5] = hcc->peer_addr_type;
    memcpy(cmd + 6, hcc->peer_addr, BLE_DEV_ADDR_LEN);
    cmd[12] = hcc->own_addr_type;
    htole16(cmd + 13, hcc->conn_itvl_min);
    htole16(cmd + 15, hcc->conn_itvl_max);
    htole16(cmd + 17, hcc->conn_latency);
    htole16(cmd + 19, hcc->supervision_timeout);
    htole16(cmd + 21, hcc->min_ce_len);
    htole16(cmd + 23, hcc->max_ce_len);

    rc = host_hci_le_cmd_send(BLE_HCI_OCF_LE_CREATE_CONN,
                              BLE_HCI_CREATE_CONN_LEN, cmd);
    return rc;
}

/**
 * Clear the whitelist.
 * 
 * @return int 
 */
int
host_hci_cmd_le_clear_whitelist(void)
{
    int rc;

    rc = host_hci_le_cmd_send(BLE_HCI_OCF_LE_CLEAR_WHITE_LIST, 0, NULL);
    return rc;
}

/**
 * Read the whitelist size. Note that this is not how many elements have 
 * been added to the whitelist; rather it is the number of whitelist entries 
 * allowed by the controller. 
 * 
 * @return int 
 */
int
host_hci_cmd_le_read_whitelist(void)
{
    int rc;

    rc = host_hci_le_cmd_send(BLE_HCI_OCF_LE_RD_WHITE_LIST_SIZE, 0, NULL);
    return rc;
}

/**
 * Add a device to the whitelist.
 * 
 * @param addr 
 * @param addr_type 
 * 
 * @return int 
 */
int
host_hci_cmd_le_add_to_whitelist(uint8_t *addr, uint8_t addr_type)
{
    int rc;

    rc = host_hci_cmd_le_whitelist_chg(addr, addr_type, 
                                       BLE_HCI_OCF_LE_ADD_WHITE_LIST);

    return rc;
}

/**
 * Remove a device from the whitelist.
 * 
 * @param addr 
 * @param addr_type 
 * 
 * @return int 
 */
int
host_hci_cmd_le_rmv_from_whitelist(uint8_t *addr, uint8_t addr_type)
{
    int rc;

    rc = host_hci_cmd_le_whitelist_chg(addr, addr_type, 
                                       BLE_HCI_OCF_LE_RMV_WHITE_LIST);
    return rc;
}
