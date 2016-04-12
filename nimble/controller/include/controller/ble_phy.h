/**
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

#ifndef H_BLE_PHY_
#define H_BLE_PHY_

/* Forward declarations */
struct os_mbuf;

/*
 * XXX: Transceiver definitions. These dont belong here and will be moved
 * once we finalize transceiver specific support.
 */
#define XCVR_RX_START_DELAY_USECS     (140)
#define XCVR_TX_START_DELAY_USECS     (140)
#define XCVR_PROC_DELAY_USECS         (50)
#define XCVR_TX_SCHED_DELAY_USECS     \
    (XCVR_TX_START_DELAY_USECS + XCVR_PROC_DELAY_USECS)
#define XCVR_RX_SCHED_DELAY_USECS     \
    (XCVR_RX_START_DELAY_USECS + XCVR_PROC_DELAY_USECS)

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
#define BLE_IS_ADV_CHAN(chan)       (chan >= BLE_PHY_ADV_CHAN_START)
#define BLE_IS_DATA_CHAN(chan)      (chan < BLE_PHY_ADV_CHAN_START)

/* PHY states */
#define BLE_PHY_STATE_IDLE          (0)
#define BLE_PHY_STATE_RX            (1)
#define BLE_PHY_STATE_TX            (2)

/* BLE PHY transitions */
#define BLE_PHY_TRANSITION_NONE     (0)
#define BLE_PHY_TRANSITION_RX_TX    (1)
#define BLE_PHY_TRANSITION_TX_RX    (2)

/* PHY error codes */
#define BLE_PHY_ERR_RADIO_STATE     (1)
#define BLE_PHY_ERR_INIT            (2)
#define BLE_PHY_ERR_INV_PARAM       (3)
#define BLE_PHY_ERR_NO_BUFS         (4)

/* Maximun PDU length. Includes LL header of 2 bytes and 255 bytes payload. */
#define BLE_PHY_MAX_PDU_LEN         (257)

/* Wait for response timer */
typedef void (*ble_phy_tx_end_func)(void *arg);

/* Initialize the PHY */
int ble_phy_init(void);

/* Reset the PHY */
int ble_phy_reset(void);

/* Set the PHY channel */
int ble_phy_setchan(uint8_t chan, uint32_t access_addr, uint32_t crcinit);

/* Set the transmit end callback and argument */
void ble_phy_set_txend_cb(ble_phy_tx_end_func txend_cb, void *arg);

/* Place the PHY into transmit mode */
int ble_phy_tx(struct os_mbuf *txpdu, uint8_t beg_trans, uint8_t end_trans);

/* Place the PHY into receive mode */
int ble_phy_rx(void);

/* Get an RSSI reading */
int ble_phy_rssi_get(void);

/* Set the transmit power */
int ble_phy_txpwr_set(int dbm);

/* Get the transmit power */
int ble_phy_txpwr_get(void);

/* Disable the PHY */
void ble_phy_disable(void);

/* Gets the current state of the PHY */
int ble_phy_state_get(void);

/* Returns 'true' if a reception has started */
int ble_phy_rx_started(void);

/* Gets the current access address */
uint32_t ble_phy_access_addr_get(void);

#endif /* H_BLE_PHY_ */
