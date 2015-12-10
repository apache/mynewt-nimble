/**
 * Copyright (c) 2015 Runtime Inc.
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

#ifndef H_BLE_LL_CONN_
#define H_BLE_LL_CONN_

#include "os/os.h"
#include "hal/hal_cputime.h"

/* Channel map size */
#define BLE_LL_CONN_CHMAP_LEN           (5)

/* Definitions for source clock accuracy */
#define BLE_MASTER_SCA_251_500_PPM      (0)
#define BLE_MASTER_SCA_151_250_PPM      (1)
#define BLE_MASTER_SCA_101_150_PPM      (2)
#define BLE_MASTER_SCA_76_100_PPM       (3)
#define BLE_MASTER_SCA_51_75_PPM        (4)
#define BLE_MASTER_SCA_31_50_PPM        (5)
#define BLE_MASTER_SCA_21_30_PPM        (6)
#define BLE_MASTER_SCA_0_20_PPM         (7)

/* Definitions for max rx/tx time/bytes for connections */
#define BLE_LL_CONN_SUPP_TIME_MIN           (328)   /* usecs */
#define BLE_LL_CONN_SUPP_TIME_MAX           (2120)  /* usecs */
#define BLE_LL_CONN_SUPP_BYTES_MIN          (27)    /* bytes */
#define BLE_LL_CONN_SUPP_BYTES_MAX          (251)   /* bytes */

/* 
 * Length of empty pdu mbuf. Each connection state machine contains an
 * empty pdu since we dont want to allocate a full mbuf for an empty pdu
 * and we always want to have one available. The empty pdu length is of
 * type uint32_t so we have 4 byte alignment.
 */ 
#define BLE_LL_EMPTY_PDU_MBUF_SIZE  (((BLE_MBUF_PKT_OVERHEAD + 4) + 3) / 4)

/* Connection state machine */
struct ble_ll_conn_sm
{
    /* Current connection state and role */
    uint8_t conn_state;
    uint8_t conn_role;

    /* Connection data length management */
    uint8_t max_tx_octets;
    uint8_t max_rx_octets;
    uint8_t rem_max_tx_octets;
    uint8_t rem_max_rx_octets;
    uint8_t eff_max_tx_octets;
    uint8_t eff_max_rx_octets;
    uint16_t max_tx_time;
    uint16_t max_rx_time;
    uint16_t rem_max_tx_time;
    uint16_t rem_max_rx_time;
    uint16_t eff_max_tx_time;
    uint16_t eff_max_rx_time;

    /* Used to calculate data channel index for connection */
    uint8_t chanmap[BLE_LL_CONN_CHMAP_LEN];
    uint8_t hop_inc;
    uint8_t data_chan_index;
    uint8_t unmapped_chan;
    uint8_t last_unmapped_chan;
    uint8_t num_used_chans;

    /* Ack/Flow Control */
    uint8_t tx_seqnum;          /* note: can be 1 bit */
    uint8_t next_exp_seqnum;    /* note: can be 1 bit */
    uint8_t last_txd_md;        /* note can be 1 bit */
    uint8_t cons_rxd_bad_crc;   /* note: can be 1 bit */
    uint8_t last_rxd_sn;        /* note: cant be 1 bit given current code */
    uint8_t last_rxd_hdr_byte;  /* note: possibly can make 1 bit since we
                                   only use the MD bit now */

    /* connection event timing/mgmt */
    uint8_t pdu_txd;            /* note: can be 1 bit */
    uint8_t rsp_rxd;            /* note: can be 1 bit */
    uint8_t pkt_rxd;            /* note: can be 1 bit */
    uint8_t master_sca;
    uint8_t tx_win_size;
    uint8_t allow_slave_latency;    /* note: can be 1 bit  */
    uint8_t slave_set_last_anchor;  /* note: can be 1 bit */
    uint8_t cur_ctrl_proc;
    uint16_t pending_ctrl_procs;
    uint16_t conn_itvl;
    uint16_t slave_latency;
    uint16_t tx_win_off;
    uint16_t min_ce_len;
    uint16_t max_ce_len;
    uint16_t event_cntr;
    uint16_t supervision_tmo;
    uint16_t conn_handle;
    uint32_t access_addr;
    uint32_t crcinit;           /* only low 24 bits used */
    uint32_t anchor_point;
    uint32_t last_anchor_point;
    uint32_t ce_end_time;   /* cputime at which connection event should end */
    uint32_t slave_cur_tx_win_usecs;
    uint32_t slave_cur_window_widening;

    /* address information */
    uint8_t own_addr_type;
    uint8_t peer_addr_type;
    uint8_t peer_addr[BLE_DEV_ADDR_LEN];

    /* connection supervisor timer */
    struct cpu_timer conn_spvn_timer;

    /* connection supervision timeout event */
    struct os_event conn_spvn_ev;

    /* Connection end event */
    struct os_event conn_ev_end;

    /* Packet transmit queue */
    STAILQ_HEAD(conn_txq_head, os_mbuf_pkthdr) conn_txq;

    /* List entry for active/free connection pools */
    union {
        SLIST_ENTRY(ble_ll_conn_sm) act_sle;
        STAILQ_ENTRY(ble_ll_conn_sm) free_stqe;
    };

    /* empty pdu for connection */
    uint32_t conn_empty_pdu[BLE_LL_EMPTY_PDU_MBUF_SIZE];

    /* LL control procedure response timer */
    struct os_callout_func ctrl_proc_rsp_timer;
};

/* API */
struct ble_ll_len_req;
int ble_ll_conn_create(uint8_t *cmdbuf);
int ble_ll_conn_create_cancel(void);
int ble_ll_conn_is_peer_adv(uint8_t addr_type, uint8_t *adva);
int ble_ll_conn_request_send(uint8_t addr_type, uint8_t *adva);
void ble_ll_init_rx_pdu_proc(uint8_t *rxbuf, struct ble_mbuf_hdr *ble_hdr);
int ble_ll_init_rx_pdu_end(struct os_mbuf *rxpdu);
int ble_ll_conn_slave_start(uint8_t *rxbuf);
void ble_ll_conn_spvn_timeout(void *arg);
void ble_ll_conn_event_end(void *arg);
void ble_ll_conn_init(void);
void ble_ll_conn_rx_pdu_start(void);
int ble_ll_conn_rx_pdu_end(struct os_mbuf *rxpdu, uint8_t crcok);
void ble_ll_conn_rx_data_pdu(struct os_mbuf *rxpdu, uint8_t crcok);
void ble_ll_conn_tx_pkt_in(struct os_mbuf *om, uint16_t handle, uint16_t len);
void ble_ll_conn_end(struct ble_ll_conn_sm *connsm, uint8_t ble_err);
void ble_ll_conn_enqueue_pkt(struct ble_ll_conn_sm *connsm, struct os_mbuf *om);
void ble_ll_conn_datalen_update(struct ble_ll_conn_sm *connsm, 
                                struct ble_ll_len_req *req);


#endif /* H_BLE_LL_CONN_ */
