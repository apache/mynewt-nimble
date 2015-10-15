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
#include "controller/phy.h"
#include "controller/ll.h"
#include "controller/ll_sched.h"
#include "hal/hal_cputime.h"

/* XXX: this is temporary. Not sure what I want to do here */
struct cpu_timer g_ll_sched_timer;

/* XXX: TODO:
 *  1) Add a "priority" scheme possibly. This would allow items to be
 *  added if they overlapped.
 *  2) Add some accounting to the schedule code to see how late we are
 *  (min/max?)
 */
#define BLE_LL_CFG_SCHED_ITEMS      (8)
#define BLE_LL_SCHED_POOL_SIZE      \
    OS_MEMPOOL_SIZE(BLE_LL_CFG_SCHED_ITEMS, sizeof(struct ll_sched_item))

struct os_mempool g_ll_sched_pool;
os_membuf_t g_ll_sched_mem[BLE_LL_SCHED_POOL_SIZE];

/* Queue for timers */
TAILQ_HEAD(ll_sched_qhead, ll_sched_item) g_ll_sched_q;

/**
 * ll sched execute
 *  
 * Executes a schedule item by calling the schedule callback function.
 * 
 * @param sch Pointer to schedule item
 * 
 * @return int 0: schedule item is not over; otherwise schedule item is done.
 */
int
ll_sched_execute(struct ll_sched_item *sch)
{
    int rc;

    assert(sch->sched_cb);
    rc = sch->sched_cb(sch);
    return rc;
}

/* Get a schedule item from the event pool */
struct ll_sched_item *
ll_sched_get_item(void)
{
    struct ll_sched_item *sch;

    sch = os_memblock_get(&g_ll_sched_pool);
    if (sch) {
        memset(sch, 0, sizeof(struct ll_sched_item));
    }
    return sch;
}

/* Free the schedule item */
void
ll_sched_free_item(struct ll_sched_item *sch)
{
    os_error_t err;
    err = os_memblock_put(&g_ll_sched_pool, sch);
    assert(err == OS_OK);
}

/* Schedule a LL event */
int
ll_sched_add(struct ll_sched_item *sch)
{
    int rc;
    os_sr_t sr;
    struct ll_sched_item *entry;

    /* Determine if we are able to add this to the schedule */
    OS_ENTER_CRITICAL(sr);

    rc = 0;
    if (TAILQ_EMPTY(&g_ll_sched_q)) {
        TAILQ_INSERT_HEAD(&g_ll_sched_q, sch, link);
    } else {
        cputime_timer_stop(&g_ll_sched_timer);
        TAILQ_FOREACH(entry, &g_ll_sched_q, link) {
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
            TAILQ_INSERT_TAIL(&g_ll_sched_q, sch, link);
        }
    }

    OS_EXIT_CRITICAL(sr);

    cputime_timer_start(&g_ll_sched_timer, sch->start_time);

    return rc;
}

/* Remove an event (or events) from the scheduler */
int
ll_sched_rmv(uint8_t sched_type)
{
    os_sr_t sr;
    struct ll_sched_item *entry;
    struct ll_sched_item *next;

    OS_ENTER_CRITICAL(sr);

    entry = TAILQ_FIRST(&g_ll_sched_q);
    if (entry) {
        cputime_timer_stop(&g_ll_sched_timer);
        while (entry) {
            next = TAILQ_NEXT(entry, link);
            if (entry->sched_type == sched_type) {
                TAILQ_REMOVE(&g_ll_sched_q, entry, link);
                os_memblock_put(&g_ll_sched_pool, entry);
            } 
            entry = next;
        }

        /* Start the timer if there is an item */
        entry = TAILQ_FIRST(&g_ll_sched_q);
        if (entry) {
            cputime_timer_start(&g_ll_sched_timer, entry->start_time);
        }
    }

    OS_EXIT_CRITICAL(sr);

    return 0;
}

/**
 * ll sched run 
 *  
 * Run the BLE scheduler. Iterate through all items on the schedule queue.
 *  
 * Context: interrupt (scheduler) 
 * 
 * @return int 
 */
void
ll_sched_run(void *arg)
{
    int rc;
    struct ll_sched_item *sch;

    /* Look through schedule queue */
    while ((sch = TAILQ_FIRST(&g_ll_sched_q)) != NULL) {
        /* Make sure we have passed the start time of the first event */
        if ((int32_t)(cputime_get32() - sch->start_time) >= 0) {
            /* Execute the schedule item */
            rc = ll_sched_execute(sch);
            if (rc) {
                TAILQ_REMOVE(&g_ll_sched_q, sch, link);
                os_memblock_put(&g_ll_sched_pool, sch);
            } else {
                /* Event is not over; schedule next wakeup time */
                cputime_timer_start(&g_ll_sched_timer, sch->next_wakeup);
                break;
            }
        } else {
            cputime_timer_start(&g_ll_sched_timer, sch->start_time);
            break;
        }
    }
}

/**
 * ble ll sched init 
 *  
 * Initialize the scheduler. Should only be called once and should be called 
 * before any of the scheduler API are called. 
 * 
 * @return int 
 */
int
ll_sched_init(void)
{
    os_error_t err;

    err = os_mempool_init(&g_ll_sched_pool, BLE_LL_CFG_SCHED_ITEMS, 
                          sizeof(struct ll_sched_item), 
                          g_ll_sched_mem, "ll_sched");
    assert(err == OS_OK);

    /* Start cputimer for the scheduler */
    cputime_timer_init(&g_ll_sched_timer, ll_sched_run, NULL);

    return err;
}
