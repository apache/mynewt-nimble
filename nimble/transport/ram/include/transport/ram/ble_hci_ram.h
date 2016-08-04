#ifndef H_BLE_HCI_RAM_
#define H_BLE_HCI_RAM_

#include "nimble/ble_hci_trans.h"

struct ble_hci_ram_cfg {
    uint16_t num_evt_bufs;
    uint16_t evt_buf_sz;
};

extern const struct ble_hci_ram_cfg ble_hci_ram_cfg_dflt;

int ble_hci_ram_init(const struct ble_hci_ram_cfg *cfg);

#endif
