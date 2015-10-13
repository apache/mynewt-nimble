/**
 * Copyright (c) 2015 Stack Inc.
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

#ifndef H_BLE_PHY_
#define H_BLE_PHY_

struct os_mbuf;

/* Channel/Frequency defintions */
#define BLE_PHY_NUM_CHANS           (40)
#define BLE_PHY_NUM_DATA_CHANS      (37)
#define BLE_PHY_CHAN0_FREQ_MHZ      (2402)
#define BLE_PHY_DATA_CHAN0_FREQ_MHZ (2404)
#define BLE_PHY_CHAN_SPACING_MHZ    (2)
#define BLE_PHY_NUM_ADV_CHANS       (3)
#define BLE_PHY_ADV_CHAN_START      (37)

/* Power */
#define BLE_PHY_MAX_PWR_DBM         (10)

/* Deviation */
#define BLE_PHY_DEV_KHZ             (185)
#define BLE_PHY_BINARY_ZERO         (-BLE_PHY_DEV)
#define BLE_PHY_BINARY_ONE          (BLE_PHY_DEV)

/* Max. clock drift */
#define BLE_PHY_MAX_DRIFT_PPM       (50)

/* Data rate */
#define BLE_PHY_BIT_RATE_BPS        (1000000)

/* Macros */
#define BLE_IS_ADV_CHAN(chan)   (chan >= BLE_PHY_ADV_CHAN_START)
#define BLE_IS_DATA_CHAN(chan)  (chan < BLE_PHY_ADV_CHAN_START)

/* PHY MODES */
#define BLE_PHY_MODE_IDLE   (0)
#define BLE_PHY_MODE_RX     (1)
#define BLE_PHY_MODE_TX     (2)
#define BLE_PHY_MODE_TX_RX  (3)

/* API */
int ble_phy_init(void);
int ble_phy_setchan(uint8_t chan);
int ble_phy_tx(struct os_mbuf *, uint8_t mode);
int ble_phy_rx(struct os_mbuf *);
int ble_phy_rssi_get(void);
int ble_phy_txpwr_set(int dbm);
int ble_phy_txpwr_get(void);
void ble_phy_disable(void);

#endif /* H_BLE_PHY_ */
