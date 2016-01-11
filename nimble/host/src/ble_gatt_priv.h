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
struct ble_att_read_type_adata;
struct ble_att_find_type_value_hinfo;
struct ble_att_find_info_idata;
struct ble_att_read_group_type_adata;

#define BLE_GATT_CHR_DECL_SZ_16     5
#define BLE_GATT_CHR_DECL_SZ_128    19

#define BLE_GATT_DSC_CLT_CFG_UUID16 0x2902

typedef uint8_t ble_gatts_conn_flags;

struct ble_gatts_conn {
    struct ble_gatts_clt_cfg *clt_cfgs;
    int num_clt_cfgs;
    ble_gatts_conn_flags flags;
};

#define BLE_GATTS_CONN_F_INDICATION_TXED        0x01

/*** @client. */
int ble_gattc_write(uint16_t conn_handle, uint16_t attr_handle, void *value,
                    uint16_t value_len, ble_gatt_attr_fn *cb, void *cb_arg);
int ble_gattc_indicate(uint16_t conn_handle, uint16_t chr_val_handle,
                       ble_gatt_attr_fn *cb, void *cb_arg);
int ble_gattc_notify(struct ble_hs_conn *conn, uint16_t chr_val_handle);

void ble_gattc_rx_err(uint16_t conn_handle, struct ble_att_error_rsp *rsp);
void ble_gattc_wakeup(void);
void ble_gattc_rx_mtu(struct ble_hs_conn *conn, uint16_t chan_mtu);
void ble_gattc_rx_read_type_adata(struct ble_hs_conn *conn,
                                  struct ble_att_read_type_adata *adata);
void ble_gattc_rx_read_type_complete(struct ble_hs_conn *conn, int status);
void ble_gattc_rx_read_rsp(struct ble_hs_conn *conn, int status, void *value,
                           int value_len);
void ble_gattc_rx_read_blob_rsp(struct ble_hs_conn *conn, int status,
                                void *value, int value_len);
void ble_gattc_rx_read_mult_rsp(struct ble_hs_conn *conn, int status,
                                void *value, int value_len);
void ble_gattc_rx_read_group_type_adata(
    struct ble_hs_conn *conn, struct ble_att_read_group_type_adata *adata);
void ble_gattc_rx_read_group_type_complete(struct ble_hs_conn *conn, int rc);
void ble_gattc_rx_find_type_value_hinfo(struct ble_hs_conn *conn,
                                        struct ble_att_find_type_value_hinfo *hinfo);
void ble_gattc_rx_find_type_value_complete(struct ble_hs_conn *conn, int rc);
void ble_gattc_rx_write_rsp(struct ble_hs_conn *conn);
void ble_gattc_rx_indicate_rsp(struct ble_hs_conn *conn);
void ble_gattc_rx_find_info_idata(struct ble_hs_conn *conn,
                                  struct ble_att_find_info_idata *idata);
void ble_gattc_rx_find_info_complete(struct ble_hs_conn *conn, int status);
void ble_gattc_connection_txable(uint16_t conn_handle);
void ble_gattc_connection_broken(uint16_t conn_handle);

int ble_gattc_any_jobs(void);
void ble_gattc_started(void);
int ble_gattc_init(void);

/*** @server. */
#define BLE_GATTS_CLT_CFG_F_NOTIFY              0x0001
#define BLE_GATTS_CLT_CFG_F_INDICATE            0x0002
#define BLE_GATTS_CLT_CFG_F_UPDATED             0x0080 /* Internal only. */
#define BLE_GATTS_CLT_CFG_F_RESERVED            0xfffc

#define BLE_GATTS_INC_SVC_LEN_NO_UUID           4
#define BLE_GATTS_INC_SVC_LEN_UUID              6

void ble_gatts_send_notifications(struct ble_hs_conn *conn);

/*** @misc. */
void ble_gatts_conn_deinit(struct ble_gatts_conn *gatts_conn);
int ble_gatts_conn_init(struct ble_gatts_conn *gatts_conn);
int ble_gatts_init(void);

#endif
