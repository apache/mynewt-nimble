#ifndef H_BLE_GATT_
#define H_BLE_GATT_

#include <inttypes.h>
struct ble_hs_conn;
struct ble_hs_att_error_rsp;

void ble_gatt_rx_error(struct ble_hs_conn *conn,
                       struct ble_hs_att_error_rsp *rsp);
void ble_gatt_wakeup(void);
void ble_gatt_rx_mtu(struct ble_hs_conn *conn, uint16_t chan_mtu);
int ble_gatt_mtu(uint16_t conn_handle);
void ble_gatt_rx_find_info(struct ble_hs_conn *conn, int status,
                           uint16_t last_handle_id);
int ble_gatt_find_info(uint16_t conn_handle_id, uint16_t att_start_handle,
                       uint16_t att_end_handle);
int ble_gatt_init(void);

#endif
