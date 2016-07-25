#ifndef H_BLE_SVC_GATT_
#define H_BLE_SVC_GATT_

struct ble_hs_cfg;

#define BLE_SVC_GATT_CHR_SERVICE_CHANGED_UUID16     0x2a05

int ble_svc_gatt_init(struct ble_hs_cfg *cfg);

#endif
