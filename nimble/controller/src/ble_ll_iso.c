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
#include <syscfg/syscfg.h>
#include <nimble/hci_common.h>
#include <controller/ble_ll.h>
#include <controller/ble_ll_isoal.h>
#include <controller/ble_ll_iso.h>
#include <controller/ble_ll_iso_big.h>

#if MYNEWT_VAL(BLE_LL_ISO)

int
ble_ll_iso_setup_iso_data_path(const uint8_t *cmdbuf, uint8_t cmdlen,
                               uint8_t *rspbuf, uint8_t *rsplen)
{
    const struct ble_hci_le_setup_iso_data_path_cp *cmd = (const void *)cmdbuf;
    struct ble_hci_le_setup_iso_data_path_rp *rsp = (void *)rspbuf;
    struct ble_ll_iso_bis *bis;
    uint16_t conn_handle;

    conn_handle = le16toh(cmd->conn_handle);
    switch (BLE_LL_CONN_HANDLE_TYPE(conn_handle)) {
    case BLE_LL_CONN_HANDLE_TYPE_BIS:
        bis = ble_ll_iso_big_find_bis_by_handle(conn_handle);
        if (bis) {
            break;
        }
    default:
        return BLE_ERR_UNK_CONN_ID;
    }

    /* Only input for now since we only support BIS */
    if (cmd->data_path_dir) {
        return BLE_ERR_CMD_DISALLOWED;
    }

    /* We do not (yet) support any vendor-specific data path */
    if (cmd->data_path_id) {
        return BLE_ERR_CMD_DISALLOWED;
    }

    rsp->conn_handle = cmd->conn_handle;
    *rsplen = sizeof(*rsp);

    return 0;
}

int
ble_ll_iso_remove_iso_data_path(const uint8_t *cmdbuf, uint8_t cmdlen,
                                uint8_t *rspbuf, uint8_t *rsplen)
{
    const struct ble_hci_le_remove_iso_data_path_cp *cmd = (const void *)cmdbuf;
    struct ble_hci_le_remove_iso_data_path_rp *rsp = (void *)rspbuf;

    /* XXX accepts anything for now */
    rsp->conn_handle = cmd->conn_handle;
    *rsplen = sizeof(*rsp);

    return 0;
}

int
ble_ll_iso_read_tx_sync(const uint8_t *cmdbuf, uint8_t cmdlen,
                        uint8_t *rspbuf, uint8_t *rsplen)
{
    const struct ble_hci_le_read_iso_tx_sync_cp *cmd = (const void *)cmdbuf;
    struct ble_hci_le_read_iso_tx_sync_rp *rsp = (void *)rspbuf;
    struct ble_ll_isoal_mux *mux;
    uint16_t handle;

    handle = le16toh(cmd->conn_handle);
    mux = ble_ll_iso_find_mux_by_handle(handle);
    if (!mux) {
        return BLE_ERR_UNK_CONN_ID;
    }

    rsp->conn_handle = cmd->conn_handle;
    rsp->packet_seq_num = htole16(mux->last_tx_packet_seq_num);
    rsp->tx_timestamp = htole32(mux->last_tx_timestamp);
    put_le24(rsp->time_offset, 0);

    *rsplen = sizeof(*rsp);

    return 0;
}

struct ble_ll_isoal_mux *
ble_ll_iso_find_mux_by_handle(uint16_t conn_handle)
{
    switch (BLE_LL_CONN_HANDLE_TYPE(conn_handle)) {
        case BLE_LL_CONN_HANDLE_TYPE_BIS:
            return ble_ll_iso_big_find_mux_by_handle(conn_handle);
        default:
            return NULL;
    }
}

#endif /* BLE_LL_ISO */
