#ifndef __BLE_PHY_PALNA_H
#define __BLE_PHY_PALNA_H

#include "syscfg/syscfg.h"

#if (MYNEWT_VAL(BLE_BLE_PHY_PALNA_PA_ENABLE_PIN) || MYNEWT_VAL(BLE_PHY_PALNA_LNA_ENABLE_PIN))
    #define BLE_PHY_PALNA_PPI_CHANNEL_RADIO_READY       0
    #define BLE_PHY_PALNA_PPI_CHANNEL_RADIO_DISABLED    1
    #define BLE_PHY_PALNA_GPIOTE_CHANNEL                0

void ble_phy_palna_init(void);
void ble_phy_palna_tx_prepare(void);
void ble_phy_palna_rx_prepare(void);
void ble_phy_palna_idle(void);
#endif

#endif