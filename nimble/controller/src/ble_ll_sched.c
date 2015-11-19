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
#include <assert.h>
#include <string.h>
#include "os/os.h"
#include "controller/ble_phy.h"
#include "controller/ble_ll.h"
#include "controller/ble_ll_sched.h"
#include "hal/hal_cputime.h"

/* XXX: this is temporary. Not sure what I want to do here */
struct cpu_timer g_ble_ll_sched_timer;

/* XXX: TODO:
 *  1) Add a "priority" scheme possibly. This would allow items to be
 *  added if they overlapped.
 *  2) Add some accounting to the schedule code to see how late we are
 *  (min/max?)
 */
#define BLE_LL_CFG_SCHED_ITEMS      (8)
#define BLE_LL_SCHED_POOL_SIZE      \
    OS_MEMPOOL_SIZE(BLE_LL_CFG_SCHED_ITEMS, sizeof(struct ble_ll_sched_item))

struct os_mempool g_ble_ll_sched_pool;
os_membuf_t g_ble_ll_sched_mem[BLE_LL_SCHED_POOL_SIZE];

/* Queue for timers */
TAILQ_HEAD(ll_sched_qhead, ble_ll_sched_item) g_ble_ll_sched_q;

/**
 * Executes a schedule item by calling the schedule callback function.
 * 
 * @param sch Pointer to schedule item
 * 
 * @return int 0: schedule item is not over; otherwise schedule item is done.
 */
int
ble_ll_sched_execute(struct ble_ll_sched_item *sch)
{
    int rc;

    assert(sch->sched_cb);
    rc = sch->sched_cb(sch);
    return rc;
}


/**
 * Allocate a schedule item
 * 
 * @return struct ble_ll_sched_item* 
 */
struct ble_ll_sched_item *
ble_ll_sched_get_item(void)
{
    struct ble_ll_sched_item *sch;

    sch = os_memblock_get(&g_ble_ll_sched_pool);
    if (sch) {
        memset(sch, 0, sizeof(struct ble_ll_sched_item));
    }
    return sch;
}

/**
 * Free a schedule item
 * 
 * @param sch 
 */
void
ble_ll_sched_free_item(struct ble_ll_sched_item *sch)
{
    os_error_t err;
    err = os_memblock_put(&g_ble_ll_sched_pool, sch);
    assert(err == OS_OK);
}

/**
 * Add a schedule item to the schedule list
 * 
 * @param sch 
 * 
 * @return int 
 */
int
ble_ll_sched_add(struct ble_ll_sched_item *sch)
{
    int rc;
    os_sr_t sr;
    struct ble_ll_sched_item *entry;

    /* Determine if we are able to add this to the schedule */
    OS_ENTER_CRITICAL(sr);

    rc = 0;
    if (TAILQ_EMPTY(&g_ble_ll_sched_q)) {
        TAILQ_INSERT_HEAD(&g_ble_ll_sched_q, sch, link);
    } else {
        cputime_timer_stop(&g_ble_ll_sched_timer);
        TAILQ_FOREACH(entry, &g_ble_ll_sched_q, link) {
            if ((int32_t)(sch->start_time - entry->start_time) < 0) {
                /* Make sure this event does not overlap current event */
                if ((int32_t)(sch->end_time - entry->start_time) < 0) {
                    TAILQ_INSERT_BEFORE(entry, sch, link);   
                } else {
                    rc = BLE_LL_SCHED_ERR_OVERLAP;
                }
                break;
            } else {
                /* Check for overlap */
                if ((int32_t)(sch->start_time - entry->end_time) < 0) {
                    rc = BLE_LL_SCHED_ERR_OVERLAP;
                    break;
                }
            }
        }
        if (!entry) {
            TAILQ_INSERT_TAIL(&g_ble_ll_sched_q, sch, link);
        }
    }

    OS_EXIT_CRITICAL(sr);

    cputime_timer_start(&g_ble_ll_sched_timer, sch->start_time);

    return rc;
}

/**
 * Remove a schedule item. You can use this function to: 
 *  1) Remove all schedule items of type 'sched_type' (cb_arg = NULL)
 *  2) Remove schedule items of type 'sched_type' and matching callback args
 * 
 * 
 * @param sched_type 
 * 
 * @return int 
 */
int
ble_ll_sched_rmv(uint8_t sched_type, void *cb_arg)
{
    os_sr_t sr;
    struct ble_ll_sched_item *entry;
    struct ble_ll_sched_item *next;

    OS_ENTER_CRITICAL(sr);

    entry = TAILQ_FIRST(&g_ble_ll_sched_q);
    if (entry) {
        cputime_timer_stop(&g_ble_ll_sched_timer);
        while (entry) {
            next = TAILQ_NEXT(entry, link);
            if (entry->sched_type == sched_type) {
                if ((cb_arg == NULL) || (cb_arg == entry->cb_arg)) {
                    TAILQ_REMOVE(&g_ble_ll_sched_q, entry, link);
                    os_memblock_put(&g_ble_ll_sched_pool, entry);
                }
            } 
            entry = next;
        }

        /* Start the timer if there is an item */
        entry = TAILQ_FIRST(&g_ble_ll_sched_q);
        if (entry) {
            cputime_timer_start(&g_ble_ll_sched_timer, entry->start_time);
        }
    }

    OS_EXIT_CRITICAL(sr);

    return 0;
}

/**
 * Run the BLE scheduler. Iterate through all items on the schedule queue.
 *  
 * Context: interrupt (scheduler) 
 * 
 * @return int 
 */
void
ble_ll_sched_run(void *arg)
{
    int rc;
    struct ble_ll_sched_item *sch;

    /* Look through schedule queue */
    while ((sch = TAILQ_FIRST(&g_ble_ll_sched_q)) != NULL) {
        /* Make sure we have passed the start time of the first event */
        if ((int32_t)(cputime_get32() - sch->start_time) >= 0) {
            /* Execute the schedule item */
            rc = ble_ll_sched_execute(sch);
            if (rc) {
                TAILQ_REMOVE(&g_ble_ll_sched_q, sch, link);
                os_memblock_put(&g_ble_ll_sched_pool, sch);
            } else {
                /* Event is not over; schedule next wakeup time */
                cputime_timer_start(&g_ble_ll_sched_timer, sch->next_wakeup);
                break;
            }
        } else {
            cputime_timer_start(&g_ble_ll_sched_timer, sch->start_time);
            break;
        }
    }
}

/**
 * Initialize the scheduler. Should only be called once and should be called 
 * before any of the scheduler API are called. 
 * 
 * @return int 
 */
int
ble_ll_sched_init(void)
{
    os_error_t err;

    err = os_mempool_init(&g_ble_ll_sched_pool, BLE_LL_CFG_SCHED_ITEMS, 
                          sizeof(struct ble_ll_sched_item), 
                          g_ble_ll_sched_mem, "ll_sched");
    assert(err == OS_OK);

    /* Initialize cputimer for the scheduler */
    cputime_timer_init(&g_ble_ll_sched_timer, ble_ll_sched_run, NULL);

    return err;
}
