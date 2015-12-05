#ifndef H_BLE_GATT_
#define H_BLE_GATT_

#include <inttypes.h>
struct ble_hs_conn;
struct ble_att_error_rsp;
struct ble_att_clt_adata;

struct ble_gatt_service {
    uint16_t start_handle;
    uint16_t end_handle;
    uint8_t uuid128[16];
};

struct ble_gatt_attr {
    uint16_t handle;
    uint8_t value_len;
    void *value;
};

typedef int ble_gatt_disc_service_fn(uint16_t conn_handle, int status,
                                     struct ble_gatt_service *service,
                                     void *arg);
typedef int ble_gatt_attr_fn(uint16_t conn_handle, int status,
                             struct ble_gatt_attr *attr, void *arg);

int ble_gatt_disc_all_services(uint16_t conn_handle,
                               ble_gatt_disc_service_fn *cb,
                               void *cb_arg);
int ble_gatt_disc_service_by_uuid(uint16_t conn_handle, void *service_uuid128,
                                  ble_gatt_disc_service_fn *cb, void *cb_arg);
int ble_gatt_disc_all_chars(uint16_t conn_handle, uint16_t start_handle,
                            uint16_t end_handle, ble_gatt_attr_fn *cb,
                            void *cb_arg);

void ble_gatt_rx_err(struct ble_hs_conn *conn, struct ble_att_error_rsp *rsp);
void ble_gatt_wakeup(void);
void ble_gatt_rx_mtu(struct ble_hs_conn *conn, uint16_t chan_mtu);
int ble_gatt_mtu(uint16_t conn_handle);
void ble_gatt_rx_find_info(struct ble_hs_conn *conn, int status,
                           uint16_t last_handle_id);
void ble_gatt_rx_read_type_adata(struct ble_hs_conn *conn,
                                 struct ble_att_clt_adata *adata);
void ble_gatt_rx_read_type_complete(struct ble_hs_conn *conn, int rc);
void ble_gatt_rx_read_group_type_adata(struct ble_hs_conn *conn,
                                       struct ble_att_clt_adata *adata);
void ble_gatt_rx_read_group_type_complete(struct ble_hs_conn *conn, int rc);
void ble_gatt_rx_find_type_value_hinfo(struct ble_hs_conn *conn,
                                       struct ble_att_clt_adata *adata);
void ble_gatt_rx_find_type_value_complete(struct ble_hs_conn *conn, int rc);
int ble_gatt_find_info(uint16_t conn_handle_id, uint16_t att_start_handle,
                       uint16_t att_end_handle);
int ble_gatt_init(void);

#endif
