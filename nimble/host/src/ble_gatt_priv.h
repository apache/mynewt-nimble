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

#ifndef H_BLE_GATT_PRIV_
#define H_BLE_GATT_PRIV_

#include "host/ble_gatt.h"

#define BLE_GATT_CHR_DECL_SZ_16     5
#define BLE_GATT_CHR_DECL_SZ_128    19

void ble_gatt_rx_err(uint16_t conn_handle, struct ble_att_error_rsp *rsp);
void ble_gatt_wakeup(void);
void ble_gatt_rx_mtu(struct ble_hs_conn *conn, uint16_t chan_mtu);
void ble_gatt_rx_read_type_adata(struct ble_hs_conn *conn,
                                 struct ble_att_clt_adata *adata);
void ble_gatt_rx_read_type_complete(struct ble_hs_conn *conn, int rc);
void ble_gatt_rx_read_rsp(struct ble_hs_conn *conn, int status, void *value,
                          int value_len);
void ble_gatt_rx_read_group_type_adata(struct ble_hs_conn *conn,
                                       struct ble_att_clt_adata *adata);
void ble_gatt_rx_read_group_type_complete(struct ble_hs_conn *conn, int rc);
void ble_gatt_rx_find_type_value_hinfo(struct ble_hs_conn *conn,
                                       struct ble_att_clt_adata *adata);
void ble_gatt_rx_find_type_value_complete(struct ble_hs_conn *conn, int rc);
void ble_gatt_rx_write_rsp(struct ble_hs_conn *conn);
void ble_gatt_connection_txable(uint16_t conn_handle);
void ble_gatt_connection_broken(uint16_t conn_handle);

#endif
