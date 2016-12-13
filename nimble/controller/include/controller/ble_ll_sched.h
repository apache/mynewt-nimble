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

#ifndef H_BLE_LL_SCHED_
#define H_BLE_LL_SCHED_

#ifdef __cplusplus
extern "C" {
#endif

/* Time per BLE scheduler slot */
#define BLE_LL_SCHED_USECS_PER_SLOT (1250)

/*
 * Worst case time needed for scheduled advertising item. This is the longest
 * possible time to receive a scan request and send a scan response (with the
 * appropriate IFS time between them). This number is calculated using the
 * following formula: IFS + SCAN_REQ + IFS + SCAN_RSP = 150 + 176 + 150 + 376.
 * Note: worst case time to tx adv, rx scan req and send scan rsp is 1228 usecs.
 * This assumes maximum sized advertising PDU and scan response PDU.
 *
 * For connectable advertising events no scan request is allowed. In this case
 * we just need to receive a connect request PDU: IFS + CONNECT_REQ = 150 + 352.
 * Note: worst-case is 376 + 150 + 352 = 878 usecs
 *
 * NOTE: The advertising PDU transmit time is NOT included here since we know
 * how long that will take (worst-case is 376 usecs).
 */
#define BLE_LL_SCHED_ADV_MAX_USECS          (852)
#define BLE_LL_SCHED_DIRECT_ADV_MAX_USECS   (502)
#define BLE_LL_SCHED_MAX_ADV_PDU_USECS      (376)

/*
 * This is the number of slots needed to transmit and receive a maximum
 * size PDU, including an IFS time before each. The actual time is
 * 2120 usecs for tx/rx and 150 for IFS = 4540 usecs.
 */
#define BLE_LL_SCHED_MAX_TXRX_SLOT  (4 * BLE_LL_SCHED_USECS_PER_SLOT)

/* BLE scheduler errors */
#define BLE_LL_SCHED_ERR_OVERLAP    (1)

/* Types of scheduler events */
#define BLE_LL_SCHED_TYPE_ADV       (1)
#define BLE_LL_SCHED_TYPE_SCAN      (2)
#define BLE_LL_SCHED_TYPE_CONN      (3)

/* Return values for schedule callback. */
#define BLE_LL_SCHED_STATE_RUNNING  (0)
#define BLE_LL_SCHED_STATE_DONE     (1)

/* Callback function */
struct ble_ll_sched_item;
typedef int (*sched_cb_func)(struct ble_ll_sched_item *sch);

struct ble_ll_sched_item
{
    uint8_t         sched_type;
    uint8_t         enqueued;
    uint32_t        start_time;
    uint32_t        end_time;
    void            *cb_arg;
    sched_cb_func   sched_cb;
    TAILQ_ENTRY(ble_ll_sched_item) link;
};

/* Initialize the scheduler */
int ble_ll_sched_init(void);

/* Remove item(s) from schedule */
void ble_ll_sched_rmv_elem(struct ble_ll_sched_item *sch);

/* Get a schedule item */
struct ble_ll_sched_item *ble_ll_sched_get_item(void);

/* Free a schedule item */
void ble_ll_sched_free_item(struct ble_ll_sched_item *sch);

/* Schedule a new master connection */
struct ble_ll_conn_sm;
int ble_ll_sched_master_new(struct ble_ll_conn_sm *connsm, uint32_t adv_rxend,
                            uint8_t req_slots);

/* Schedule a new slave connection */
int ble_ll_sched_slave_new(struct ble_ll_conn_sm *connsm);

/* Schedule a new advertising event */
int ble_ll_sched_adv_new(struct ble_ll_sched_item *sch);

/* Reschedule an advertising event */
int ble_ll_sched_adv_reschedule(struct ble_ll_sched_item *sch, uint32_t *start,
                                uint32_t max_delay_ticks);

/* Reschedule and advertising pdu */
int ble_ll_sched_adv_resched_pdu(struct ble_ll_sched_item *sch);

/* Reschedule a connection that had previously been scheduled or that is over */
int ble_ll_sched_conn_reschedule(struct ble_ll_conn_sm * connsm);

/**
 * Called to determine when the next scheduled event will occur.
 *
 * If there are not scheduled events this function returns 0; otherwise it
 * returns 1 and *next_event_time is set to the start time of the next event.
 *
 * @param next_event_time cputime at which next scheduled event will occur
 *
 * @return int 0: No events are scheduled 1: there is an upcoming event
 */
int ble_ll_sched_next_time(uint32_t *next_event_time);

/* Stop the scheduler */
void ble_ll_sched_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* H_LL_SCHED_ */
