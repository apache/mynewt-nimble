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
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include "os/os.h"
#include "console/console.h"
#include "nimble/hci_common.h"
#include "nimble/ble_hci_trans.h"
#include "ble_hs_dbg_priv.h"
#include "ble_hs_priv.h"

static int
ble_hs_hci_cmd_transport(uint8_t *cmdbuf)
{
    int rc;

    rc = ble_hci_trans_hs_cmd_tx(cmdbuf);
    switch (rc) {
    case 0:
        return 0;

    case BLE_ERR_MEM_CAPACITY:
        return BLE_HS_ENOMEM_EVT;

    default:
        return BLE_HS_EUNKNOWN;
    }
}

void
ble_hs_hci_cmd_write_hdr(uint8_t ogf, uint16_t ocf, uint8_t len, void *buf)
{
    uint16_t opcode;
    uint8_t *u8ptr;

    u8ptr = buf;

    opcode = (ogf << 10) | ocf;
    put_le16(u8ptr, opcode);
    u8ptr[2] = len;
}

int
ble_hs_hci_cmd_send(uint8_t ogf, uint16_t ocf, uint8_t len, const void *cmddata)
{
    uint8_t *buf;
    int rc;

    buf = ble_hci_trans_buf_alloc(BLE_HCI_TRANS_BUF_CMD);
    BLE_HS_DBG_ASSERT(buf != NULL);

    put_le16(buf, ogf << 10 | ocf);
    buf[2] = len;
    if (len != 0) {
        memcpy(buf + BLE_HCI_CMD_HDR_LEN, cmddata, len);
    }

    BLE_HS_LOG(DEBUG, "ble_hs_hci_cmd_send: ogf=0x%02x ocf=0x%04x len=%d\n",
               ogf, ocf, len);
    ble_hs_log_flat_buf(buf, len + BLE_HCI_CMD_HDR_LEN);
    BLE_HS_LOG(DEBUG, "\n");
    rc = ble_hs_hci_cmd_transport(buf);

    if (rc == 0) {
        STATS_INC(ble_hs_stats, hci_cmd);
    } else {
        BLE_HS_LOG(DEBUG, "ble_hs_hci_cmd_send failure; rc=%d\n", rc);
    }

    return rc;
}

int
ble_hs_hci_cmd_send_buf(void *buf)
{
    uint16_t opcode;
    uint8_t *u8ptr;
    uint8_t len;
    int rc;

    switch (ble_hs_sync_state) {
    case BLE_HS_SYNC_STATE_BAD:
        return BLE_HS_ENOTSYNCED;

    case BLE_HS_SYNC_STATE_BRINGUP:
        if (!ble_hs_is_parent_task()) {
            return BLE_HS_ENOTSYNCED;
        }
        break;

    case BLE_HS_SYNC_STATE_GOOD:
        break;

    default:
        BLE_HS_DBG_ASSERT(0);
        return BLE_HS_EUNKNOWN;
    }

    u8ptr = buf;

    opcode = get_le16(u8ptr + 0);
    len = u8ptr[2];

    rc = ble_hs_hci_cmd_send(BLE_HCI_OGF(opcode), BLE_HCI_OCF(opcode), len,
                             u8ptr + BLE_HCI_CMD_HDR_LEN);
    return rc;
}


/**
 * Send a LE command from the host to the controller.
 *
 * @param ocf
 * @param len
 * @param cmddata
 *
 * @return int
 */
static int
ble_hs_hci_cmd_le_send(uint16_t ocf, uint8_t len, void *cmddata)
{
    int rc;
    rc = ble_hs_hci_cmd_send(BLE_HCI_OGF_LE, ocf, len, cmddata);
    return rc;
}

/**
 * Read BD_ADDR
 *
 * OGF = 0x04 (Informational parameters)
 * OCF = 0x0009
 */
void
ble_hs_hci_cmd_build_read_bd_addr(uint8_t *dst, int dst_len)
{
    BLE_HS_DBG_ASSERT(dst_len >= BLE_HCI_CMD_HDR_LEN);
    ble_hs_hci_cmd_write_hdr(BLE_HCI_OGF_INFO_PARAMS,
                             BLE_HCI_OCF_IP_RD_BD_ADDR,
                             0, dst);
}

static int
ble_hs_hci_cmd_body_le_whitelist_chg(const uint8_t *addr, uint8_t addr_type,
                                     uint8_t *dst)
{
    if (addr_type > BLE_ADDR_RANDOM) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    dst[0] = addr_type;
    memcpy(dst + 1, addr, BLE_DEV_ADDR_LEN);

    return 0;
}

static int
ble_hs_hci_cmd_body_le_set_adv_params(const struct hci_adv_params *adv,
                                      uint8_t *dst)
{
    uint16_t itvl;

    BLE_HS_DBG_ASSERT(adv != NULL);

    /* Make sure parameters are valid */
    if ((adv->adv_itvl_min > adv->adv_itvl_max) ||
        (adv->own_addr_type > BLE_HCI_ADV_OWN_ADDR_MAX) ||
        (adv->peer_addr_type > BLE_HCI_ADV_PEER_ADDR_MAX) ||
        (adv->adv_filter_policy > BLE_HCI_ADV_FILT_MAX) ||
        (adv->adv_type > BLE_HCI_ADV_TYPE_MAX) ||
        (adv->adv_channel_map == 0) ||
        ((adv->adv_channel_map & 0xF8) != 0)) {
        /* These parameters are not valid */
        return -1;
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
            return -1;
        }
    }

    put_le16(dst, adv->adv_itvl_min);
    put_le16(dst + 2, adv->adv_itvl_max);
    dst[4] = adv->adv_type;
    dst[5] = adv->own_addr_type;
    dst[6] = adv->peer_addr_type;
    memcpy(dst + 7, adv->peer_addr, BLE_DEV_ADDR_LEN);
    dst[13] = adv->adv_channel_map;
    dst[14] = adv->adv_filter_policy;

    return 0;
}

int
ble_hs_hci_cmd_build_le_set_adv_params(const struct hci_adv_params *adv,
                                       uint8_t *dst, int dst_len)
{
    int rc;

    BLE_HS_DBG_ASSERT(
        dst_len >= BLE_HCI_CMD_HDR_LEN + BLE_HCI_SET_ADV_PARAM_LEN);

    ble_hs_hci_cmd_write_hdr(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_SET_ADV_PARAMS,
                       BLE_HCI_SET_ADV_PARAM_LEN, dst);
    dst += BLE_HCI_CMD_HDR_LEN;

    rc = ble_hs_hci_cmd_body_le_set_adv_params(adv, dst);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

/**
 * Set advertising data
 *
 * OGF = 0x08 (LE)
 * OCF = 0x0008
 *
 * @param data
 * @param len
 * @param dst
 *
 * @return int
 */
static int
ble_hs_hci_cmd_body_le_set_adv_data(const uint8_t *data, uint8_t len,
                                    uint8_t *dst)
{
    /* Check for valid parameters */
    if (((data == NULL) && (len != 0)) || (len > BLE_HCI_MAX_ADV_DATA_LEN)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    memset(dst, 0, BLE_HCI_SET_ADV_DATA_LEN);
    dst[0] = len;
    memcpy(dst + 1, data, len);

    return 0;
}

/**
 * Set advertising data
 *
 * OGF = 0x08 (LE)
 * OCF = 0x0008
 *
 * @param data
 * @param len
 * @param dst
 *
 * @return int
 */
int
ble_hs_hci_cmd_build_le_set_adv_data(const uint8_t *data, uint8_t len,
                                     uint8_t *dst, int dst_len)
{
    int rc;

    BLE_HS_DBG_ASSERT(
        dst_len >= BLE_HCI_CMD_HDR_LEN + BLE_HCI_SET_ADV_DATA_LEN);

    ble_hs_hci_cmd_write_hdr(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_SET_ADV_DATA,
                       BLE_HCI_SET_ADV_DATA_LEN, dst);
    dst += BLE_HCI_CMD_HDR_LEN;

    rc = ble_hs_hci_cmd_body_le_set_adv_data(data, len, dst);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

static int
ble_hs_hci_cmd_body_le_set_scan_rsp_data(const uint8_t *data, uint8_t len,
                                         uint8_t *dst)
{
    /* Check for valid parameters */
    if (((data == NULL) && (len != 0)) ||
         (len > BLE_HCI_MAX_SCAN_RSP_DATA_LEN)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    memset(dst, 0, BLE_HCI_SET_SCAN_RSP_DATA_LEN);
    dst[0] = len;
    memcpy(dst + 1, data, len);

    return 0;
}

int
ble_hs_hci_cmd_build_le_set_scan_rsp_data(const uint8_t *data, uint8_t len,
                                          uint8_t *dst, int dst_len)
{
    int rc;

    BLE_HS_DBG_ASSERT(
        dst_len >= BLE_HCI_CMD_HDR_LEN + BLE_HCI_SET_SCAN_RSP_DATA_LEN);

    ble_hs_hci_cmd_write_hdr(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_SET_SCAN_RSP_DATA,
                       BLE_HCI_SET_SCAN_RSP_DATA_LEN, dst);
    dst += BLE_HCI_CMD_HDR_LEN;

    rc = ble_hs_hci_cmd_body_le_set_scan_rsp_data(data, len, dst);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

static void
ble_hs_hci_cmd_body_set_event_mask(uint64_t event_mask, uint8_t *dst)
{
    put_le64(dst, event_mask);
}

void
ble_hs_hci_cmd_build_set_event_mask(uint64_t event_mask,
                                    uint8_t *dst, int dst_len)
{
    BLE_HS_DBG_ASSERT(
        dst_len >= BLE_HCI_CMD_HDR_LEN + BLE_HCI_SET_EVENT_MASK_LEN);

    ble_hs_hci_cmd_write_hdr(BLE_HCI_OGF_CTLR_BASEBAND,
                       BLE_HCI_OCF_CB_SET_EVENT_MASK,
                       BLE_HCI_SET_EVENT_MASK_LEN, dst);
    dst += BLE_HCI_CMD_HDR_LEN;

    ble_hs_hci_cmd_body_set_event_mask(event_mask, dst);
}

void
ble_hs_hci_cmd_build_set_event_mask2(uint64_t event_mask,
                                     uint8_t *dst, int dst_len)
{
    BLE_HS_DBG_ASSERT(
        dst_len >= BLE_HCI_CMD_HDR_LEN + BLE_HCI_SET_EVENT_MASK_LEN);

    ble_hs_hci_cmd_write_hdr(BLE_HCI_OGF_CTLR_BASEBAND,
                       BLE_HCI_OCF_CB_SET_EVENT_MASK2,
                       BLE_HCI_SET_EVENT_MASK_LEN, dst);
    dst += BLE_HCI_CMD_HDR_LEN;

    ble_hs_hci_cmd_body_set_event_mask(event_mask, dst);
}

static void
ble_hs_hci_cmd_body_disconnect(uint16_t handle, uint8_t reason, uint8_t *dst)
{
    put_le16(dst + 0, handle);
    dst[2] = reason;
}

void
ble_hs_hci_cmd_build_disconnect(uint16_t handle, uint8_t reason,
                                uint8_t *dst, int dst_len)
{
    BLE_HS_DBG_ASSERT(
        dst_len >= BLE_HCI_CMD_HDR_LEN + BLE_HCI_DISCONNECT_CMD_LEN);

    ble_hs_hci_cmd_write_hdr(BLE_HCI_OGF_LINK_CTRL, BLE_HCI_OCF_DISCONNECT_CMD,
                             BLE_HCI_DISCONNECT_CMD_LEN, dst);
    dst += BLE_HCI_CMD_HDR_LEN;

    ble_hs_hci_cmd_body_disconnect(handle, reason, dst);
}

static void
ble_hs_hci_cmd_body_le_set_event_mask(uint64_t event_mask, uint8_t *dst)
{
    put_le64(dst, event_mask);
}

void
ble_hs_hci_cmd_build_le_set_event_mask(uint64_t event_mask,
                                       uint8_t *dst, int dst_len)
{
    BLE_HS_DBG_ASSERT(
        dst_len >= BLE_HCI_CMD_HDR_LEN + BLE_HCI_SET_LE_EVENT_MASK_LEN);

    ble_hs_hci_cmd_write_hdr(BLE_HCI_OGF_LE,
                       BLE_HCI_OCF_LE_SET_EVENT_MASK,
                       BLE_HCI_SET_LE_EVENT_MASK_LEN, dst);
    dst += BLE_HCI_CMD_HDR_LEN;

    ble_hs_hci_cmd_body_le_set_event_mask(event_mask, dst);
}

/**
 * LE Read buffer size
 *
 * OGF = 0x08 (LE)
 * OCF = 0x0002
 *
 * @return int
 */
void
ble_hs_hci_cmd_build_le_read_buffer_size(uint8_t *dst, int dst_len)
{
    BLE_HS_DBG_ASSERT(dst_len >= BLE_HCI_CMD_HDR_LEN);
    ble_hs_hci_cmd_write_hdr(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_RD_BUF_SIZE,
                             0, dst);
}

/**
 * LE Read buffer size
 *
 * OGF = 0x08 (LE)
 * OCF = 0x0002
 *
 * @return int
 */
int
ble_hs_hci_cmd_le_read_buffer_size(void)
{
    int rc;

    rc = ble_hs_hci_cmd_le_send(BLE_HCI_OCF_LE_RD_BUF_SIZE, 0, NULL);
    return rc;
}

/**
 * OGF=LE, OCF=0x0003
 */
void
ble_hs_hci_cmd_build_le_read_loc_supp_feat(uint8_t *dst, uint8_t dst_len)
{
    BLE_HS_DBG_ASSERT(dst_len >= BLE_HCI_CMD_HDR_LEN);
    ble_hs_hci_cmd_write_hdr(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_RD_LOC_SUPP_FEAT,
                       0, dst);
}

static void
ble_hs_hci_cmd_body_le_set_adv_enable(uint8_t enable, uint8_t *dst)
{
    dst[0] = enable;
}

void
ble_hs_hci_cmd_build_le_set_adv_enable(uint8_t enable, uint8_t *dst,
                                       int dst_len)
{
    BLE_HS_DBG_ASSERT(
        dst_len >= BLE_HCI_CMD_HDR_LEN + BLE_HCI_SET_ADV_ENABLE_LEN);

    ble_hs_hci_cmd_write_hdr(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_SET_ADV_ENABLE,
                       BLE_HCI_SET_ADV_ENABLE_LEN, dst);
    dst += BLE_HCI_CMD_HDR_LEN;

    ble_hs_hci_cmd_body_le_set_adv_enable(enable, dst);
}

static int
ble_hs_hci_cmd_body_le_set_scan_params(
    uint8_t scan_type, uint16_t scan_itvl, uint16_t scan_window,
    uint8_t own_addr_type, uint8_t filter_policy, uint8_t *dst) {

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

    dst[0] = scan_type;
    put_le16(dst + 1, scan_itvl);
    put_le16(dst + 3, scan_window);
    dst[5] = own_addr_type;
    dst[6] = filter_policy;

    return 0;
}

int
ble_hs_hci_cmd_build_le_set_scan_params(uint8_t scan_type,
                                        uint16_t scan_itvl,
                                        uint16_t scan_window,
                                        uint8_t own_addr_type,
                                        uint8_t filter_policy,
                                        uint8_t *dst, int dst_len)
{
    int rc;

    BLE_HS_DBG_ASSERT(
        dst_len >= BLE_HCI_CMD_HDR_LEN + BLE_HCI_SET_SCAN_PARAM_LEN);

    ble_hs_hci_cmd_write_hdr(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_SET_SCAN_PARAMS,
                       BLE_HCI_SET_SCAN_PARAM_LEN, dst);
    dst += BLE_HCI_CMD_HDR_LEN;

    rc = ble_hs_hci_cmd_body_le_set_scan_params(scan_type, scan_itvl,
                                              scan_window, own_addr_type,
                                              filter_policy, dst);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

static void
ble_hs_hci_cmd_body_le_set_scan_enable(uint8_t enable, uint8_t filter_dups,
                                       uint8_t *dst)
{
    dst[0] = enable;
    dst[1] = filter_dups;
}

void
ble_hs_hci_cmd_build_le_set_scan_enable(uint8_t enable, uint8_t filter_dups,
                                        uint8_t *dst, uint8_t dst_len)
{
    BLE_HS_DBG_ASSERT(
        dst_len >= BLE_HCI_CMD_HDR_LEN + BLE_HCI_SET_SCAN_ENABLE_LEN);

    ble_hs_hci_cmd_write_hdr(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_SET_SCAN_ENABLE,
                       BLE_HCI_SET_SCAN_ENABLE_LEN, dst);

    ble_hs_hci_cmd_body_le_set_scan_enable(enable, filter_dups,
                                         dst + BLE_HCI_CMD_HDR_LEN);
}

static int
ble_hs_hci_cmd_body_le_create_connection(const struct hci_create_conn *hcc,
                                         uint8_t *cmd)
{
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

    put_le16(cmd + 0, hcc->scan_itvl);
    put_le16(cmd + 2, hcc->scan_window);
    cmd[4] = hcc->filter_policy;
    cmd[5] = hcc->peer_addr_type;
    memcpy(cmd + 6, hcc->peer_addr, BLE_DEV_ADDR_LEN);
    cmd[12] = hcc->own_addr_type;
    put_le16(cmd + 13, hcc->conn_itvl_min);
    put_le16(cmd + 15, hcc->conn_itvl_max);
    put_le16(cmd + 17, hcc->conn_latency);
    put_le16(cmd + 19, hcc->supervision_timeout);
    put_le16(cmd + 21, hcc->min_ce_len);
    put_le16(cmd + 23, hcc->max_ce_len);

    return 0;
}

int
ble_hs_hci_cmd_build_le_create_connection(const struct hci_create_conn *hcc,
                                          uint8_t *cmd, int cmd_len)
{
    int rc;

    BLE_HS_DBG_ASSERT(
        cmd_len >= BLE_HCI_CMD_HDR_LEN + BLE_HCI_CREATE_CONN_LEN);

    ble_hs_hci_cmd_write_hdr(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_CREATE_CONN,
                       BLE_HCI_CREATE_CONN_LEN, cmd);

    rc = ble_hs_hci_cmd_body_le_create_connection(hcc,
                                                cmd + BLE_HCI_CMD_HDR_LEN);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

void
ble_hs_hci_cmd_build_le_clear_whitelist(uint8_t *dst, int dst_len)
{
    BLE_HS_DBG_ASSERT(dst_len >= BLE_HCI_CMD_HDR_LEN);
    ble_hs_hci_cmd_write_hdr(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_CLEAR_WHITE_LIST,
                       0, dst);
}

int
ble_hs_hci_cmd_build_le_add_to_whitelist(const uint8_t *addr,
                                         uint8_t addr_type,
                                         uint8_t *dst, int dst_len)
{
    int rc;

    BLE_HS_DBG_ASSERT(
        dst_len >= BLE_HCI_CMD_HDR_LEN + BLE_HCI_CHG_WHITE_LIST_LEN);

    ble_hs_hci_cmd_write_hdr(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_ADD_WHITE_LIST,
                       BLE_HCI_CHG_WHITE_LIST_LEN, dst);
    dst += BLE_HCI_CMD_HDR_LEN;

    rc = ble_hs_hci_cmd_body_le_whitelist_chg(addr, addr_type, dst);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

void
ble_hs_hci_cmd_build_reset(uint8_t *dst, int dst_len)
{
    BLE_HS_DBG_ASSERT(dst_len >= BLE_HCI_CMD_HDR_LEN);
    ble_hs_hci_cmd_write_hdr(BLE_HCI_OGF_CTLR_BASEBAND, BLE_HCI_OCF_CB_RESET,
                       0, dst);
}

/**
 * Reset the controller and link manager.
 *
 * @return int
 */
int
ble_hs_hci_cmd_reset(void)
{
    int rc;

    rc = ble_hs_hci_cmd_send(BLE_HCI_OGF_CTLR_BASEBAND, BLE_HCI_OCF_CB_RESET,
                             0, NULL);
    return rc;
}

void
ble_hs_hci_cmd_build_read_adv_pwr(uint8_t *dst, int dst_len)
{
    BLE_HS_DBG_ASSERT(dst_len >= BLE_HCI_CMD_HDR_LEN);
    ble_hs_hci_cmd_write_hdr(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_RD_ADV_CHAN_TXPWR,
                             0, dst);
}

/**
 * Read the transmit power level used for LE advertising channel packets.
 *
 * @return int
 */
int
ble_hs_hci_cmd_read_adv_pwr(void)
{
    int rc;

    rc = ble_hs_hci_cmd_send(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_RD_ADV_CHAN_TXPWR,
                             0, NULL);
    return rc;
}

void
ble_hs_hci_cmd_build_le_create_conn_cancel(uint8_t *dst, int dst_len)
{
    BLE_HS_DBG_ASSERT(dst_len >= BLE_HCI_CMD_HDR_LEN);
    ble_hs_hci_cmd_write_hdr(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_CREATE_CONN_CANCEL,
                       0, dst);
}

int
ble_hs_hci_cmd_le_create_conn_cancel(void)
{
    int rc;

    rc = ble_hs_hci_cmd_send(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_CREATE_CONN_CANCEL,
                           0, NULL);
    return rc;
}

static int
ble_hs_hci_cmd_body_le_conn_update(const struct hci_conn_update *hcu,
                                   uint8_t *dst)
{
    /* XXX: add parameter checking later */
    put_le16(dst + 0, hcu->handle);
    put_le16(dst + 2, hcu->conn_itvl_min);
    put_le16(dst + 4, hcu->conn_itvl_max);
    put_le16(dst + 6, hcu->conn_latency);
    put_le16(dst + 8, hcu->supervision_timeout);
    put_le16(dst + 10, hcu->min_ce_len);
    put_le16(dst + 12, hcu->max_ce_len);

    return 0;
}

int
ble_hs_hci_cmd_build_le_conn_update(const struct hci_conn_update *hcu,
                                    uint8_t *dst, int dst_len)
{
    int rc;

    BLE_HS_DBG_ASSERT(
        dst_len >= BLE_HCI_CMD_HDR_LEN + BLE_HCI_CONN_UPDATE_LEN);

    ble_hs_hci_cmd_write_hdr(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_CONN_UPDATE,
                       BLE_HCI_CONN_UPDATE_LEN, dst);
    dst += BLE_HCI_CMD_HDR_LEN;

    rc = ble_hs_hci_cmd_body_le_conn_update(hcu, dst);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

int
ble_hs_hci_cmd_le_conn_update(const struct hci_conn_update *hcu)
{
    uint8_t cmd[BLE_HCI_CONN_UPDATE_LEN];
    int rc;

    rc = ble_hs_hci_cmd_body_le_conn_update(hcu, cmd);
    if (rc != 0) {
        return rc;
    }

    rc = ble_hs_hci_cmd_le_send(BLE_HCI_OCF_LE_CONN_UPDATE,
                              BLE_HCI_CONN_UPDATE_LEN, cmd);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

static void
ble_hs_hci_cmd_body_le_lt_key_req_reply(const struct hci_lt_key_req_reply *hkr,
                                        uint8_t *dst)
{
    put_le16(dst + 0, hkr->conn_handle);
    memcpy(dst + 2, hkr->long_term_key, sizeof hkr->long_term_key);
}

/**
 * Sends the long-term key (LTK) to the controller.
 *
 * Note: This function expects the 128-bit key to be in little-endian byte
 * order.
 *
 * OGF = 0x08 (LE)
 * OCF = 0x001a
 *
 * @param key
 * @param pt
 *
 * @return int
 */
void
ble_hs_hci_cmd_build_le_lt_key_req_reply(
    const struct hci_lt_key_req_reply *hkr, uint8_t *dst, int dst_len)
{
    BLE_HS_DBG_ASSERT(
        dst_len >= BLE_HCI_CMD_HDR_LEN + BLE_HCI_LT_KEY_REQ_REPLY_LEN);

    ble_hs_hci_cmd_write_hdr(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_LT_KEY_REQ_REPLY,
                       BLE_HCI_LT_KEY_REQ_REPLY_LEN, dst);
    dst += BLE_HCI_CMD_HDR_LEN;

    ble_hs_hci_cmd_body_le_lt_key_req_reply(hkr, dst);
}

void
ble_hs_hci_cmd_build_le_lt_key_req_neg_reply(uint16_t conn_handle,
                                             uint8_t *dst, int dst_len)
{
    BLE_HS_DBG_ASSERT(
        dst_len >= BLE_HCI_CMD_HDR_LEN + BLE_HCI_LT_KEY_REQ_NEG_REPLY_LEN);

    ble_hs_hci_cmd_write_hdr(BLE_HCI_OGF_LE,
                             BLE_HCI_OCF_LE_LT_KEY_REQ_NEG_REPLY,
                             BLE_HCI_LT_KEY_REQ_NEG_REPLY_LEN, dst);
    dst += BLE_HCI_CMD_HDR_LEN;

    put_le16(dst + 0, conn_handle);
}

static void
ble_hs_hci_cmd_body_le_conn_param_reply(const struct hci_conn_param_reply *hcr,
                                        uint8_t *dst)
{
    put_le16(dst + 0, hcr->handle);
    put_le16(dst + 2, hcr->conn_itvl_min);
    put_le16(dst + 4, hcr->conn_itvl_max);
    put_le16(dst + 6, hcr->conn_latency);
    put_le16(dst + 8, hcr->supervision_timeout);
    put_le16(dst + 10, hcr->min_ce_len);
    put_le16(dst + 12, hcr->max_ce_len);
}

void
ble_hs_hci_cmd_build_le_conn_param_reply(
    const struct hci_conn_param_reply *hcr, uint8_t *dst, int dst_len)
{
    BLE_HS_DBG_ASSERT(
        dst_len >= BLE_HCI_CMD_HDR_LEN + BLE_HCI_CONN_PARAM_REPLY_LEN);

    ble_hs_hci_cmd_write_hdr(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_REM_CONN_PARAM_RR,
                       BLE_HCI_CONN_PARAM_REPLY_LEN, dst);
    dst += BLE_HCI_CMD_HDR_LEN;

    ble_hs_hci_cmd_body_le_conn_param_reply(hcr, dst);
}

int
ble_hs_hci_cmd_le_conn_param_reply(const struct hci_conn_param_reply *hcr)
{
    uint8_t cmd[BLE_HCI_CONN_PARAM_REPLY_LEN];
    int rc;

    put_le16(cmd + 0, hcr->handle);
    put_le16(cmd + 2, hcr->conn_itvl_min);
    put_le16(cmd + 4, hcr->conn_itvl_max);
    put_le16(cmd + 6, hcr->conn_latency);
    put_le16(cmd + 8, hcr->supervision_timeout);
    put_le16(cmd + 10, hcr->min_ce_len);
    put_le16(cmd + 12, hcr->max_ce_len);

    rc = ble_hs_hci_cmd_le_send(BLE_HCI_OCF_LE_REM_CONN_PARAM_RR,
                              BLE_HCI_CONN_PARAM_REPLY_LEN, cmd);
    return rc;
}

static void
ble_hs_hci_cmd_body_le_conn_param_neg_reply(
    const struct hci_conn_param_neg_reply *hcn, uint8_t *dst)
{
    put_le16(dst + 0, hcn->handle);
    dst[2] = hcn->reason;
}


void
ble_hs_hci_cmd_build_le_conn_param_neg_reply(
    const struct hci_conn_param_neg_reply *hcn, uint8_t *dst, int dst_len)
{
    BLE_HS_DBG_ASSERT(
        dst_len >= BLE_HCI_CMD_HDR_LEN + BLE_HCI_CONN_PARAM_NEG_REPLY_LEN);

    ble_hs_hci_cmd_write_hdr(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_REM_CONN_PARAM_NRR,
                       BLE_HCI_CONN_PARAM_NEG_REPLY_LEN, dst);
    dst += BLE_HCI_CMD_HDR_LEN;

    ble_hs_hci_cmd_body_le_conn_param_neg_reply(hcn, dst);
}

int
ble_hs_hci_cmd_le_conn_param_neg_reply(
    const struct hci_conn_param_neg_reply *hcn)
{
    uint8_t cmd[BLE_HCI_CONN_PARAM_NEG_REPLY_LEN];
    int rc;

    ble_hs_hci_cmd_body_le_conn_param_neg_reply(hcn, cmd);

    rc = ble_hs_hci_cmd_le_send(BLE_HCI_OCF_LE_REM_CONN_PARAM_NRR,
                              BLE_HCI_CONN_PARAM_NEG_REPLY_LEN, cmd);
    return rc;
}

/**
 * Get random data
 *
 * OGF = 0x08 (LE)
 * OCF = 0x0018
 *
 * @return int
 */
void
ble_hs_hci_cmd_build_le_rand(uint8_t *dst, int dst_len)
{
    BLE_HS_DBG_ASSERT(dst_len >= BLE_HCI_CMD_HDR_LEN);
    ble_hs_hci_cmd_write_hdr(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_RAND, 0, dst);
}

static void
ble_hs_hci_cmd_body_le_start_encrypt(const struct hci_start_encrypt *cmd,
                                     uint8_t *dst)
{
    put_le16(dst + 0, cmd->connection_handle);
    put_le64(dst + 2, cmd->random_number);
    put_le16(dst + 10, cmd->encrypted_diversifier);
    memcpy(dst + 12, cmd->long_term_key, sizeof cmd->long_term_key);
}

/*
 * OGF=0x08 OCF=0x0019
 */
void
ble_hs_hci_cmd_build_le_start_encrypt(const struct hci_start_encrypt *cmd,
                                      uint8_t *dst, int dst_len)
{
    BLE_HS_DBG_ASSERT(
        dst_len >= BLE_HCI_CMD_HDR_LEN + BLE_HCI_LE_START_ENCRYPT_LEN);

    ble_hs_hci_cmd_write_hdr(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_START_ENCRYPT,
                       BLE_HCI_LE_START_ENCRYPT_LEN, dst);
    dst += BLE_HCI_CMD_HDR_LEN;

    ble_hs_hci_cmd_body_le_start_encrypt(cmd, dst);
}

/**
 * Read the RSSI for a given connection handle
 *
 * NOTE: OGF=0x05 OCF=0x0005
 *
 * @param handle
 *
 * @return int
 */
static void
ble_hs_hci_cmd_body_read_rssi(uint16_t handle, uint8_t *dst)
{
    put_le16(dst, handle);
}

void
ble_hs_hci_cmd_build_read_rssi(uint16_t handle, uint8_t *dst, int dst_len)
{
    BLE_HS_DBG_ASSERT(
        dst_len >= BLE_HCI_CMD_HDR_LEN + BLE_HCI_READ_RSSI_LEN);

    ble_hs_hci_cmd_write_hdr(BLE_HCI_OGF_STATUS_PARAMS, BLE_HCI_OCF_RD_RSSI,
                       BLE_HCI_READ_RSSI_LEN, dst);
    dst += BLE_HCI_CMD_HDR_LEN;

    ble_hs_hci_cmd_body_read_rssi(handle, dst);
}

/**
 * LE Set Host Channel Classification
 *
 * OGF = 0x08 (LE)
 * OCF = 0x0014
 */
void
ble_hs_hci_cmd_build_le_set_host_chan_class(const uint8_t *chan_map,
                                            uint8_t *dst, int dst_len)
{
    BLE_HS_DBG_ASSERT(
        dst_len >= BLE_HCI_CMD_HDR_LEN + BLE_HCI_SET_HOST_CHAN_CLASS_LEN);

    ble_hs_hci_cmd_write_hdr(BLE_HCI_OGF_LE,
                             BLE_HCI_OCF_LE_SET_HOST_CHAN_CLASS,
                             BLE_HCI_SET_HOST_CHAN_CLASS_LEN,
                             dst);
    dst += BLE_HCI_CMD_HDR_LEN;

    memcpy(dst, chan_map, BLE_HCI_SET_HOST_CHAN_CLASS_LEN);
}

/**
 * LE Read Channel Map
 *
 * OGF = 0x08 (LE)
 * OCF = 0x0015
 */
void
ble_hs_hci_cmd_build_le_read_chan_map(uint16_t conn_handle,
                                      uint8_t *dst, int dst_len)
{
    BLE_HS_DBG_ASSERT(
        dst_len >= BLE_HCI_CMD_HDR_LEN + BLE_HCI_RD_CHANMAP_LEN);

    ble_hs_hci_cmd_write_hdr(BLE_HCI_OGF_LE,
                             BLE_HCI_OCF_LE_RD_CHAN_MAP,
                             BLE_HCI_RD_CHANMAP_LEN,
                             dst);
    dst += BLE_HCI_CMD_HDR_LEN;

    put_le16(dst + 0, conn_handle);
}

static int
ble_hs_hci_cmd_body_set_data_len(uint16_t connection_handle,
                                 uint16_t tx_octets,
                                 uint16_t tx_time, uint8_t *dst)
{

    if (tx_octets < BLE_HCI_SET_DATALEN_TX_OCTETS_MIN ||
        tx_octets > BLE_HCI_SET_DATALEN_TX_OCTETS_MAX) {

        return BLE_HS_EINVAL;
    }

    if (tx_time < BLE_HCI_SET_DATALEN_TX_TIME_MIN ||
        tx_time > BLE_HCI_SET_DATALEN_TX_TIME_MAX) {

        return BLE_HS_EINVAL;
    }

    put_le16(dst + 0, connection_handle);
    put_le16(dst + 2, tx_octets);
    put_le16(dst + 4, tx_time);

    return 0;
}

/*
 * OGF=0x08 OCF=0x0022
 */
int
ble_hs_hci_cmd_build_set_data_len(uint16_t connection_handle,
                                  uint16_t tx_octets, uint16_t tx_time,
                                  uint8_t *dst, int dst_len)
{
    int rc;

    BLE_HS_DBG_ASSERT(
        dst_len >= BLE_HCI_CMD_HDR_LEN + BLE_HCI_SET_DATALEN_LEN);

    ble_hs_hci_cmd_write_hdr(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_SET_DATA_LEN,
                       BLE_HCI_SET_DATALEN_LEN, dst);
    dst += BLE_HCI_CMD_HDR_LEN;

    rc = ble_hs_hci_cmd_body_set_data_len(connection_handle, tx_octets,
                                          tx_time, dst);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

/**
 * IRKs are in little endian.
 */
static int
ble_hs_hci_cmd_body_add_to_resolv_list(uint8_t addr_type, const uint8_t *addr,
                                       const uint8_t *peer_irk,
                                       const uint8_t *local_irk,
                                       uint8_t *dst)
{
    if (addr_type > BLE_ADDR_RANDOM) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    dst[0] = addr_type;
    memcpy(dst + 1, addr, BLE_DEV_ADDR_LEN);
    memcpy(dst + 1 + 6, peer_irk , 16);
    memcpy(dst + 1 + 6 + 16, local_irk , 16);
    /* 16 + 16 + 6 + 1 == 39 */
    return 0;
}

/**
 * OGF=0x08 OCF=0x0027
 *
 * IRKs are in little endian.
 */
int
ble_hs_hci_cmd_build_add_to_resolv_list(
    const struct hci_add_dev_to_resolving_list *padd,
    uint8_t *dst,
    int dst_len)
{
    int rc;

    BLE_HS_DBG_ASSERT(
        dst_len >= BLE_HCI_CMD_HDR_LEN + BLE_HCI_ADD_TO_RESOLV_LIST_LEN);

    ble_hs_hci_cmd_write_hdr(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_ADD_RESOLV_LIST,
                       BLE_HCI_ADD_TO_RESOLV_LIST_LEN, dst);

    rc = ble_hs_hci_cmd_body_add_to_resolv_list(
        padd->addr_type, padd->addr, padd->peer_irk, padd->local_irk,
        dst + BLE_HCI_CMD_HDR_LEN);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

static int
ble_hs_hci_cmd_body_remove_from_resolv_list(uint8_t addr_type,
                                            const uint8_t *addr,
                                            uint8_t *dst)
{
    if (addr_type > BLE_ADDR_RANDOM) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    dst[0] = addr_type;
    memcpy(dst + 1, addr, BLE_DEV_ADDR_LEN);
    return 0;
}


int
ble_hs_hci_cmd_build_remove_from_resolv_list(uint8_t addr_type,
                                             const uint8_t *addr,
                                             uint8_t *dst, int dst_len)
{
    int rc;

    BLE_HS_DBG_ASSERT(
        dst_len >= BLE_HCI_CMD_HDR_LEN + BLE_HCI_RMV_FROM_RESOLV_LIST_LEN);

    ble_hs_hci_cmd_write_hdr(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_RMV_RESOLV_LIST,
                       BLE_HCI_RMV_FROM_RESOLV_LIST_LEN, dst);

    rc = ble_hs_hci_cmd_body_remove_from_resolv_list(addr_type, addr,
                                                dst + BLE_HCI_CMD_HDR_LEN);
    if (rc != 0) {
        return rc;
    }
    return 0;
}

int
ble_hs_hci_cmd_build_clear_resolv_list(uint8_t *dst, int dst_len)
{
    BLE_HS_DBG_ASSERT(dst_len >= BLE_HCI_CMD_HDR_LEN);

    ble_hs_hci_cmd_write_hdr(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_CLR_RESOLV_LIST,
                       0, dst);

    return 0;
}

int
ble_hs_hci_cmd_build_read_resolv_list_size(uint8_t *dst, int dst_len)
{
    BLE_HS_DBG_ASSERT(dst_len >= BLE_HCI_CMD_HDR_LEN);

    ble_hs_hci_cmd_write_hdr(BLE_HCI_OGF_LE,
                             BLE_HCI_OCF_LE_RD_RESOLV_LIST_SIZE,
                             0, dst);

    return 0;
}

static int
ble_hs_hci_cmd_body_read_peer_resolv_addr(uint8_t peer_identity_addr_type,
                                          const uint8_t *peer_identity_addr,
                                          uint8_t *dst)
{
    if (peer_identity_addr_type > BLE_ADDR_RANDOM) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    dst[0] = peer_identity_addr_type;
    memcpy(dst + 1, peer_identity_addr, BLE_DEV_ADDR_LEN);
    return 0;
}

int
ble_hs_hci_cmd_build_read_peer_resolv_addr(uint8_t peer_identity_addr_type,
                                           const uint8_t *peer_identity_addr,
                                           uint8_t *dst,
                                           int dst_len)
{
    int rc;

    BLE_HS_DBG_ASSERT(
        dst_len >= BLE_HCI_CMD_HDR_LEN + BLE_HCI_RD_PEER_RESOLV_ADDR_LEN);

    ble_hs_hci_cmd_write_hdr(BLE_HCI_OGF_LE,
                             BLE_HCI_OCF_LE_RD_PEER_RESOLV_ADDR,
                             BLE_HCI_RD_PEER_RESOLV_ADDR_LEN, dst);

    rc = ble_hs_hci_cmd_body_read_peer_resolv_addr(peer_identity_addr_type,
                                                   peer_identity_addr,
                                                   dst + BLE_HCI_CMD_HDR_LEN);
    if (rc != 0) {
        return rc;
    }
    return 0;
}

static int
ble_hs_hci_cmd_body_read_lcl_resolv_addr(
    uint8_t local_identity_addr_type,
    const uint8_t *local_identity_addr,
    uint8_t *dst)
{
    if (local_identity_addr_type > BLE_ADDR_RANDOM) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    dst[0] = local_identity_addr_type;
    memcpy(dst + 1, local_identity_addr, BLE_DEV_ADDR_LEN);
    return 0;
}

/*
 * OGF=0x08 OCF=0x002c
 */
int
ble_hs_hci_cmd_build_read_lcl_resolv_addr(uint8_t local_identity_addr_type,
                                          const uint8_t *local_identity_addr,
                                          uint8_t *dst,
                                          int dst_len)
{
    int rc;

    BLE_HS_DBG_ASSERT(
        dst_len >= BLE_HCI_CMD_HDR_LEN + BLE_HCI_RD_LOC_RESOLV_ADDR_LEN);

    ble_hs_hci_cmd_write_hdr(BLE_HCI_OGF_LE,
                             BLE_HCI_OCF_LE_RD_LOCAL_RESOLV_ADDR,
                             BLE_HCI_RD_LOC_RESOLV_ADDR_LEN, dst);

    rc = ble_hs_hci_cmd_body_read_lcl_resolv_addr(local_identity_addr_type,
                                                  local_identity_addr,
                                                  dst + BLE_HCI_CMD_HDR_LEN);
    if (rc != 0) {
        return rc;
    }
    return 0;
}

static int
ble_hs_hci_cmd_body_set_addr_res_en(uint8_t enable, uint8_t *dst)
{
    if (enable > 1) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    dst[0] = enable;
    return 0;
}

/*
 * OGF=0x08 OCF=0x002d
 */
int
ble_hs_hci_cmd_build_set_addr_res_en(uint8_t enable, uint8_t *dst, int dst_len)
{
    int rc;

    BLE_HS_DBG_ASSERT(
        dst_len >= BLE_HCI_CMD_HDR_LEN + BLE_HCI_SET_ADDR_RESOL_ENA_LEN);

    ble_hs_hci_cmd_write_hdr(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_SET_ADDR_RES_EN,
                       BLE_HCI_SET_ADDR_RESOL_ENA_LEN, dst);

    rc = ble_hs_hci_cmd_body_set_addr_res_en(enable,
                                             dst + BLE_HCI_CMD_HDR_LEN);
    if (rc != 0) {
        return rc;
    }
    return 0;
}

static int
ble_hs_hci_cmd_body_set_resolv_priv_addr_timeout(uint16_t timeout,
                                                 uint8_t *dst)
{
    if (timeout == 0 || timeout > 0xA1B8) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    put_le16(dst, timeout);
    return 0;
}

/*
 * OGF=0x08 OCF=0x002e
 */
int
ble_hs_hci_cmd_build_set_resolv_priv_addr_timeout(
    uint16_t timeout, uint8_t *dst, int dst_len)
{
    BLE_HS_DBG_ASSERT(
        dst_len >= BLE_HCI_CMD_HDR_LEN + BLE_HCI_SET_RESOLV_PRIV_ADDR_TO_LEN);

    ble_hs_hci_cmd_write_hdr(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_SET_RPA_TMO,
                       BLE_HCI_SET_RESOLV_PRIV_ADDR_TO_LEN, dst);

    return ble_hs_hci_cmd_body_set_resolv_priv_addr_timeout(
        timeout, dst + BLE_HCI_CMD_HDR_LEN);
}

static int
ble_hs_hci_cmd_body_set_random_addr(const struct hci_rand_addr *paddr,
                                    uint8_t *dst)
{
    memcpy(dst, paddr->addr, BLE_DEV_ADDR_LEN);
    return 0;
}

int
ble_hs_hci_cmd_build_set_random_addr(const uint8_t *addr,
                                     uint8_t *dst, int dst_len)
{
    struct hci_rand_addr r_addr;
    int rc;

    memcpy(r_addr.addr, addr, sizeof(r_addr.addr));

    BLE_HS_DBG_ASSERT(
        dst_len >= BLE_HCI_CMD_HDR_LEN + BLE_HCI_SET_RAND_ADDR_LEN);

    ble_hs_hci_cmd_write_hdr(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_SET_RAND_ADDR,
                       BLE_HCI_SET_RAND_ADDR_LEN, dst);

    rc = ble_hs_hci_cmd_body_set_random_addr(&r_addr,
                                             dst + BLE_HCI_CMD_HDR_LEN);
    if (rc != 0) {
        return rc;
    }

    return 0;
}
