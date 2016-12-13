/*
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

#ifndef H_BLE_LL_ADV_
#define H_BLE_LL_ADV_

#include "syscfg/syscfg.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The number of advertising instances */
#define BLE_LL_ADV_INSTANCES    (MYNEWT_VAL_BLE_MULTI_ADV_INSTANCES + 1)

/*
 * ADV event timing
 *      T_advEvent = advInterval + advDelay
 *
 *      advInterval: increments of 625 usecs
 *      advDelay: RAND[0, 10] msecs
 *
 */
#define BLE_LL_ADV_ITVL                 (625)           /* usecs */
#define BLE_LL_ADV_ITVL_MIN             (32)            /* units */
#define BLE_LL_ADV_ITVL_MAX             (16384)         /* units */
#define BLE_LL_ADV_ITVL_MS_MIN          (20)            /* msecs */
#define BLE_LL_ADV_ITVL_MS_MAX          (10240)         /* msecs */
#define BLE_LL_ADV_ITVL_SCAN_MIN        (160)           /* units */
#define BLE_LL_ADV_ITVL_SCAN_MS_MIN     (100)           /* msecs */
#define BLE_LL_ADV_ITVL_NONCONN_MIN     (160)           /* units */
#define BLE_LL_ADV_ITVL_NONCONN_MS_MIN  (100)           /* msecs */
#define BLE_LL_ADV_DELAY_MS_MIN         (0)             /* msecs */
#define BLE_LL_ADV_DELAY_MS_MAX         (10)            /* msecs */
#define BLE_LL_ADV_PDU_ITVL_LD_MS_MAX   (10)            /* msecs */
#define BLE_LL_ADV_PDU_ITVL_HD_MS_MAX   (3750)          /* usecs */
#define BLE_LL_ADV_STATE_HD_MAX         (1280)          /* msecs */

/* Maximum advertisement data length */
#define BLE_ADV_DATA_MAX_LEN            (31)
#define BLE_ADV_MAX_PKT_LEN             (37)

/*
 * ADV_IND
 *      -> AdvA     (6 bytes)
 *      -> AdvData  (0 - 31 bytes)
 *
 *  The advertising address (AdvA) is a public address (TxAdd=0) or random
 *  address (TxAdd = 1)
 */
#define BLE_ADV_IND_MIN_LEN             (6)
#define BLE_ADV_IND_MAX_LEN             (37)

/*
 * ADV_DIRECT_IND
 *      -> AdvA     (6 bytes)
 *      -> InitA    (6 bytes)
 *
 *  AdvA is the advertisers public address (TxAdd=0) or random address
 *  (TxAdd = 1).
 *
 *  InitA is the initiators public or random address. This is the address
 *  to which this packet is addressed.
 *
 */
#define BLE_ADV_DIRECT_IND_LEN          (12)

/*
 * ADV_NONCONN_IND
 *      -> AdvA     (6 bytes)
 *      -> AdvData  (0 - 31 bytes)
 *
 *  The advertising address (AdvA) is a public address (TxAdd=0) or random
 *  address (TxAdd = 1)
 *
 */
#define BLE_ADV_NONCONN_IND_MIN_LEN     (6)
#define BLE_ADV_NONCONN_IND_MAX_LEN     (37)

/*
 * ADV_SCAN_IND
 *      -> AdvA     (6 bytes)
 *      -> AdvData  (0 - 31 bytes)
 *
 *  The advertising address (AdvA) is a public address (TxAdd=0) or random
 *  address (TxAdd = 1)
 *
 */
#define BLE_ADV_SCAN_IND_MIN_LEN        (6)
#define BLE_ADV_SCAN_IND_MAX_LEN        (37)

/*---- HCI ----*/
struct ble_ll_adv_sm;
struct ble_ll_conn_sm;

/* Start an advertiser */
int ble_ll_adv_start_req(uint8_t adv_chanmask, uint8_t adv_type,
                         uint8_t *init_addr, uint16_t adv_itvl, void *handle);

/* Start or stop advertising */
int ble_ll_adv_set_enable(uint8_t *cmd, uint8_t instance);

/* Set advertising data */
int ble_ll_adv_set_adv_data(uint8_t *cmd, uint8_t instance);

/* Set scan response data */
int ble_ll_adv_set_scan_rsp_data(uint8_t *cmd, uint8_t instance);

/* Set advertising parameters */
int ble_ll_adv_set_adv_params(uint8_t *cmd, uint8_t instance, int is_multi);

/* Read advertising channel power */
int ble_ll_adv_read_txpwr(uint8_t *rspbuf, uint8_t *rsplen);

#if MYNEWT_VAL(BLE_MULTI_ADV_SUPPORT)
int ble_ll_adv_multi_adv_cmd(uint8_t *cmd, uint8_t cmdlen, uint8_t *rspbuf,
                             uint8_t *rsplen);
#endif

/*---- API used by BLE LL ----*/
/* Send the connection complete event */
void ble_ll_adv_send_conn_comp_ev(struct ble_ll_conn_sm *connsm,
                                  struct ble_mbuf_hdr *rxhdr);

/* Returns local resolvable private address */
uint8_t *ble_ll_adv_get_local_rpa(struct ble_ll_adv_sm *advsm);

/* Returns peer resolvable private address */
uint8_t *ble_ll_adv_get_peer_rpa(struct ble_ll_adv_sm *advsm);

/* Called to initialize advertising functionality. */
void ble_ll_adv_init(void);

/* Called when LL wait for response timer expires in advertising state */
void ble_ll_adv_wfr_timer_exp(void);

/* Called to reset the advertiser. */
void ble_ll_adv_reset(void);

/* Called on rx pdu start when in advertising state */
int ble_ll_adv_rx_isr_start(uint8_t pdu_type);

/* Called on rx pdu end when in advertising state */
int ble_ll_adv_rx_isr_end(uint8_t pdu_type, struct os_mbuf *rxpdu, int crcok);

/* Processes received packets at the link layer task */
void ble_ll_adv_rx_pkt_in(uint8_t ptype, uint8_t *rxbuf,
                          struct ble_mbuf_hdr *hdr);

/* Boolean function denoting whether or not the whitelist can be changed */
int ble_ll_adv_can_chg_whitelist(void);

/* Called when an advertising event has been scheduled */
void ble_ll_adv_scheduled(struct ble_ll_adv_sm *, uint32_t sch_start);

/*
 * Called when an advertising event has been removed from the scheduler
 * without being run.
 */
void ble_ll_adv_event_rmvd_from_sched(struct ble_ll_adv_sm *advsm);

/* Called to halt currently running advertising event */
void ble_ll_adv_halt(struct ble_ll_adv_sm *advsm);

/* Called to determine if advertising is enabled */
uint8_t ble_ll_adv_enabled(void);

#ifdef __cplusplus
}
#endif

#endif /* H_BLE_LL_ADV_ */
