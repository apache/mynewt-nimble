#ifndef H_BLE_HCI_RAM_
#define H_BLE_HCI_RAM_

#include "nimble/ble_hci_trans.h"

struct ble_hci_ram_cfg {
    /** Number of high-priority event buffers. */
    uint16_t num_evt_hi_bufs;

    /** Number of low-priority event buffers. */
    uint16_t num_evt_lo_bufs;

    /** Size of each event buffer, in bytes. */
    uint16_t evt_buf_sz;

    /* Note: For information about high-priority vs. low-priority event
     * buffers, see net/nimble/include/nimble/ble_hci_trans.h.
     */

    /* Note: host-to-controller command buffers are not configurable.  The RAM
     * transport only allows one outstanding command, so it uses a single
     * statically-allocated buffer.
     */
};

extern const struct ble_hci_ram_cfg ble_hci_ram_cfg_dflt;

int ble_hci_ram_init(const struct ble_hci_ram_cfg *cfg);

#endif
