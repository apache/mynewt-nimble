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

#ifndef H_BLE_LL_SCHED_
#define H_BLE_LL_SCHED_

/* BLE scheduler errors */
#define BLE_LL_SCHED_ERR_OVERLAP    (1)

/* Types of scheduler events */
#define BLE_LL_SCHED_TYPE_ADV       (0)
#define BLE_LL_SCHED_TYPE_SCAN      (1)
#define BLE_LL_SCHED_TYPE_TX        (2)
#define BLE_LL_SCHED_TYPE_RX        (3)

/* Return values for schedule callback. */
#define BLE_LL_SCHED_STATE_RUNNING  (0)
#define BLE_LL_SCHED_STATE_DONE     (1)

/* Callback function */
struct ble_ll_sched_item;
typedef int (*sched_cb_func)(struct ble_ll_sched_item *sch);

struct ble_ll_sched_item
{
    int             sched_type;
    uint32_t        start_time;
    uint32_t        end_time;
    uint32_t        next_wakeup;
    void            *cb_arg;
    sched_cb_func   sched_cb;
    TAILQ_ENTRY(ble_ll_sched_item) link;
};

/* Add an item to the schedule */
int ble_ll_sched_add(struct ble_ll_sched_item *sch);

/* Remove item(s) from schedule */
int ble_ll_sched_rmv(uint8_t sched_type);

/* Initialize the scheduler */
int ble_ll_sched_init(void);

/* Get a schedule item */
struct ble_ll_sched_item *ble_ll_sched_get_item(void);

/* Free a schedule item */
void ble_ll_sched_free_item(struct ble_ll_sched_item *sch);

#endif /* H_LL_SCHED_ */
