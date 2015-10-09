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
#include <stdint.h>
#include "os/os.h"
#include "controller/phy.h"
#include "controller/ll.h"
#include "controller/ll_adv.h"
#include "controller/ll_sched.h"

/* XXX: use the sanity task! */

/* XXX: find a place for this */
void ll_hci_cmd_proc(struct os_event *ev);

/* Connection related define */
#define BLE_LL_CONN_INIT_MAX_REMOTE_OCTETS  (27)
#define BLE_LL_CONN_INIT_MAX_REMOTE_TIME    (238)

/* The global BLE LL data object */
struct ll_obj g_ll_data;

/* The BLE LL task data structure */
#define BLE_LL_TASK_PRI     (OS_TASK_PRI_HIGHEST)
#define BLE_LL_STACK_SIZE   (128)
struct os_task g_ll_task;
os_stack_t g_ll_stack[BLE_LL_STACK_SIZE];

/* XXX: this is just temporary; used to calculate the channel index */
struct ll_sm_connection
{
    /* Data channel index for connection */
    uint8_t unmapped_chan;
    uint8_t last_unmapped_chan;
    uint8_t num_used_channels;

    /* Flow control */
    uint8_t tx_seq;
    uint8_t next_exp_seq;

    /* Parameters kept by the link-layer per connection */
    uint8_t max_tx_octets;
    uint8_t max_rx_octets;
    uint8_t max_tx_time;
    uint8_t max_rx_time;
    uint8_t remote_max_tx_octets;
    uint8_t remote_max_rx_octets;
    uint8_t remote_max_tx_time;
    uint8_t remote_max_rx_time;
    uint8_t effective_max_tx_octets;
    uint8_t effective_max_rx_octets;
    uint8_t effective_max_tx_time;
    uint8_t effective_max_rx_time;

    /* The connection request data */
    struct ble_conn_req_data req_data; 
};

uint8_t
ll_next_data_channel(struct ll_sm_connection *cnxn)
{
    int     i;
    int     j;
    uint8_t curchan;
    uint8_t remap_index;
    uint8_t bitpos;
    uint8_t cntr;
    uint8_t mask;
    uint8_t usable_chans;

    /* Get next un mapped channel */
    curchan = (cnxn->last_unmapped_chan + cnxn->req_data.hop_inc) % 
              BLE_PHY_NUM_DATA_CHANS;

    /* Set the current unmapped channel */
    cnxn->unmapped_chan = curchan;

    /* Is this a valid channel? */
    bitpos = 1 << (curchan & 0x07);
    if ((cnxn->req_data.chanmap[curchan >> 3] & bitpos) == 0) {

        /* Calculate remap index */
        remap_index = curchan % cnxn->num_used_channels;

        /* Iterate through channel map to find this channel */
        cntr = 0;
        for (i = 0; i < 5; i++) {
            usable_chans = cnxn->req_data.chanmap[i];
            if (usable_chans != 0) {
                mask = 0x01;
                for (j = 0; j < 8; j++) {
                    if (usable_chans & mask) {
                        if (cntr == remap_index) {
                            return cntr;
                        }
                        ++cntr;
                    }
                    mask <<= 1;
                }
            }
        }
    }

    return curchan;
}

/* Called when a connection gets initialized */
int
ble_init_conn_sm(struct ll_sm_connection *cnxn)
{
    cnxn->max_tx_time = g_ll_data.ll_parms.conn_init_max_tx_time;
    cnxn->max_rx_time = g_ll_data.ll_parms.supp_max_rx_time;
    cnxn->max_tx_octets = g_ll_data.ll_parms.conn_init_max_tx_octets;
    cnxn->max_rx_octets = g_ll_data.ll_parms.supp_max_rx_octets;
    cnxn->remote_max_rx_octets = BLE_LL_CONN_INIT_MAX_REMOTE_OCTETS;
    cnxn->remote_max_tx_octets = BLE_LL_CONN_INIT_MAX_REMOTE_OCTETS;
    cnxn->remote_max_rx_time = BLE_LL_CONN_INIT_MAX_REMOTE_TIME;
    cnxn->remote_max_tx_time = BLE_LL_CONN_INIT_MAX_REMOTE_TIME;

    return 0;
}

void
ll_task(void *arg)
{
    struct os_event *ev;

    /* Init ble phy */
    ble_phy_init();

    /* Set output power to 1mW (0 dBm) */
    ble_phy_txpwr_set(0);

    /* Wait for an event */
    while (1) {
        ev = os_eventq_get(&g_ll_data.ll_evq);
        switch (ev->ev_type) {
        case OS_EVENT_T_TIMER:
            break;
        case BLE_LL_EVENT_HCI_CMD:
            /* Process HCI command */
            ll_hci_cmd_proc(ev);
            break;
        case BLE_LL_EVENT_ADV_TXDONE:
            ll_adv_tx_done(ev->ev_arg);
            break;
        default:
            /* XXX: unknown event? What should we do? */
            break;
        }

        /* XXX: we can possibly take any finished schedule items and
           free them here. Have a queue for them. */
    }
}

int
ll_init(void)
{
    /* Initialize eventq */
    os_eventq_init(&g_ll_data.ll_evq);

    /* Initialize the LL task */
    os_task_init(&g_ll_task, "ble_ll", ll_task, NULL, BLE_LL_TASK_PRI, 
                 OS_WAIT_FOREVER, g_ll_stack, BLE_LL_STACK_SIZE);

    /* Init the scheduler */
    ll_sched_init();

    /* Initialize advertising code */
    ll_adv_init();

    return 0;
}

