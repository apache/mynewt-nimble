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

#ifndef H_BLE_LL_SCAN_
#define H_BLE_LL_SCAN_

#define BLE_LL_SCAN_CFG_NUM_DUP_ADVS         (8)
#define BLE_LL_SCAN_CFG_NUM_SCAN_RSP_ADVS    (8)

/* Dont allow more than 255 of these entries */
#if BLE_LL_SCAN_CFG_NUM_DUP_ADVSS > 255
    #error "Cannot have more than 255 duplicate entries!"
#endif
#if BLE_LL_SCAN_CFG_NUM_SCAN_RSP_ADVS > 255
    #error "Cannot have more than 255 scan response entries!"
#endif

/* Maximum amount of scan response data in a scan response */
#define BLE_SCAN_RSP_DATA_MAX_LEN       (31)

/*
 * SCAN_REQ 
 *      -> ScanA    (6 bytes)
 *      -> AdvA     (6 bytes)
 * 
 *  ScanA is the scanners public (TxAdd=0) or random (TxAdd = 1) address
 *  AdvaA is the advertisers public (RxAdd=0) or random (RxAdd=1) address.
 * 
 * Sent by the LL in the Scanning state; received by the LL in the advertising
 * state. The advertising address is the intended recipient of this frame.
 */
#define BLE_SCAN_REQ_LEN                (12)
#define BLE_SCAN_REQ_TXTIME_USECS       (176)

/*
 * SCAN_RSP
 *      -> AdvA         (6 bytes)
 *      -> ScanRspData  (0 - 31 bytes) 
 * 
 *  AdvaA is the advertisers public (TxAdd=0) or random (TxAdd=1) address.
 *  ScanRspData may contain any data from the advertisers host.
 * 
 * Sent by the LL in the advertising state; received by the LL in the
 * scanning state. 
 */
#define BLE_SCAN_RSP_MIN_LEN            (6)
#define BLE_SCAN_RSP_MAX_LEN            (6 + BLE_SCAN_RSP_DATA_MAX_LEN)

/*---- HCI ----*/
/* Set scanning parameters */
int ble_ll_scan_set_scan_params(uint8_t *cmd);

/* Turn scanning on/off */
int ble_ll_scan_set_enable(uint8_t *cmd);

/*--- Controller Internal API ---*/
/* Process scan window end event */
void ble_ll_scan_win_end_proc(void *arg);

/* Initialize the scanner */
void ble_ll_scan_init(void);

/* Called when Link Layer starts to receive a PDU and is in scanning state */
int ble_ll_scan_rx_pdu_start(uint8_t pdu_type, struct os_mbuf *rxpdu);

/* Called when Link Layer has finished receiving a PDU while scanning */
int ble_ll_scan_rx_pdu_end(uint8_t *rxbuf);

/* Process a scan response PDU */
void ble_ll_scan_rx_pdu_process(uint8_t pdu_type, uint8_t *rxbuf);

#endif /* H_LL_SCAN_ */
