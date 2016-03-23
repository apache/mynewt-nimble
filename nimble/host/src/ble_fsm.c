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

#include <stddef.h>
#include <assert.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include "os/os_mempool.h"
#include "nimble/ble.h"
#include "host/ble_uuid.h"
#include "host/ble_gap.h"
#include "ble_hs_priv.h"

/*****************************************************************************
 * $mutex                                                                    *
 *****************************************************************************/

void
ble_fsm_lock(struct ble_fsm *fsm)
{
    struct os_task *owner;
    int rc;

    owner = fsm->mutex.mu_owner;
    if (owner != NULL) {
        assert(owner != os_sched_get_current_task());
    }

    rc = os_mutex_pend(&fsm->mutex, 0xffffffff);
    assert(rc == 0 || rc == OS_NOT_STARTED);
}

void
ble_fsm_unlock(struct ble_fsm *fsm)
{
    int rc;

    rc = os_mutex_release(&fsm->mutex);
    assert(rc == 0 || rc == OS_NOT_STARTED);
}

int
ble_fsm_locked_by_cur_task(struct ble_fsm *fsm)
{
    struct os_task *owner;

    owner = fsm->mutex.mu_owner;
    return owner != NULL && owner == os_sched_get_current_task();
}

/*****************************************************************************
 * $debug                                                                    *
 *****************************************************************************/

/**
 * Ensures all procedure entries are in a valid state.
 *
 * Lock restrictions:
 *     o Caller locks fsm.
 */
static void
ble_fsm_assert_sanity(struct ble_fsm *fsm)
{
#if !BLE_HS_DEBUG
    return;
#endif

    struct ble_fsm_proc *proc;
    unsigned mask;
    int num_set;

    STAILQ_FOREACH(proc, &fsm->procs, next) {
        /* Ensure exactly one flag is set. */
        num_set = 0;
        mask = 0x01;
        while (mask <= UINT8_MAX) {
            if (proc->flags & mask) {
                num_set++;
            }
            mask <<= 1;
        }

        assert(num_set == 1);
    }
}

/*****************************************************************************
 * $proc                                                                     *
 *****************************************************************************/

/**
 * Removes the specified proc entry from a list without freeing it.
 *
 * Lock restrictions:
 *     o Caller locks fsm if the source list is fsm->procs.
 *
 * @param src_list              The list to remove the proc from.
 * @param proc                  The proc to remove.
 * @param prev                  The proc that is previous to "proc" in the
 *                                  list; null if "proc" is the list head.
 */
void
ble_fsm_proc_remove(struct ble_fsm_proc_list *src_list,
                    struct ble_fsm_proc *proc,
                    struct ble_fsm_proc *prev)
{
    if (prev == NULL) {
        assert(STAILQ_FIRST(src_list) == proc);
        STAILQ_REMOVE_HEAD(src_list, next);
    } else {
        assert(STAILQ_NEXT(prev, next) == proc);
        STAILQ_NEXT(prev, next) = STAILQ_NEXT(proc, next);
    }
}

/**
 * Concatenates the specified list onto the end of the main proc list.
 *
 * Lock restrictions:
 *     o Caller unlocks fsm.
 */
void
ble_fsm_proc_concat(struct ble_fsm *fsm, struct ble_fsm_proc_list *tail_list)
{
    struct ble_fsm_proc *proc;

    if (!STAILQ_EMPTY(tail_list)) {
        ble_fsm_lock(fsm);

        if (STAILQ_EMPTY(&fsm->procs)) {
            fsm->procs = *tail_list;
        } else {
            proc = STAILQ_LAST(&fsm->procs, ble_fsm_proc, next);
            STAILQ_NEXT(proc, next) = STAILQ_FIRST(tail_list);
            fsm->procs.stqh_last = tail_list->stqh_last;
        }

        ble_fsm_unlock(fsm);
    }
}

/**
 * Determines if the specified proc entry's "pending" flag can be set.
 *
 * Lock restrictions: None.
 */
int
ble_fsm_proc_can_pend(struct ble_fsm_proc *proc)
{
    return !(proc->flags & (BLE_FSM_PROC_F_CONGESTED    |
                            BLE_FSM_PROC_F_NO_MEM       |
                            BLE_FSM_PROC_F_EXPECTING));
}

/**
 * Sets the specified proc entry's "pending" flag (i.e., indicates that the
 * FSM procedure is stalled until it transmits its next packet).
 *
 * Lock restrictions:
 *     o Caller locks fsm.
 */
void
ble_fsm_proc_set_pending(struct ble_fsm_proc *proc)
{
    assert(!(proc->flags & BLE_FSM_PROC_F_PENDING));

    proc->flags &= ~BLE_FSM_PROC_F_EXPECTING;
    proc->flags |= BLE_FSM_PROC_F_PENDING;
}

/**
 * Sets the specified proc entry's "expecting" flag (i.e., indicates that the
 * FSM procedure is stalled until it receives a packet).
 *
 * Lock restrictions: None.
 */
static void
ble_fsm_proc_set_expecting(struct ble_fsm_proc *proc,
                           struct ble_fsm_proc *prev)
{
    assert(!(proc->flags & BLE_FSM_PROC_F_EXPECTING));

    proc->flags &= ~BLE_FSM_PROC_F_PENDING;
    proc->flags |= BLE_FSM_PROC_F_EXPECTING;
    proc->tx_time = os_time_get();
}

/**
 * Postpones tx for the specified proc entry if appropriate.  The determination
 * of whether tx should be postponed is based on the return code of the
 * previous transmit attempt.  This function should be called immediately after
 * transmission fails.  A tx can be postponed if the failure was caused by
 * congestion or memory exhaustion.  All other failures cannot be postponed,
 * and the procedure should be aborted entirely.
 *
 * Lock restrictions: None.
 *
 * @param proc                  The proc entry to check for postponement.
 * @param rc                    The return code of the previous tx attempt.
 *
 * @return                      1 if the transmit should be postponed; else 0.
 */
int
ble_fsm_tx_postpone_chk(struct ble_fsm_proc *proc, int rc)
{
    switch (rc) {
    case BLE_HS_ECONGESTED:
        proc->flags |= BLE_FSM_PROC_F_CONGESTED;
        return 1;

    case BLE_HS_ENOMEM:
        proc->flags |= BLE_FSM_PROC_F_NO_MEM;
        return 1;

    default:
        return 0;
    }
}

/**
 * Called after an incoming packet is done being processed.  If the status of
 * the response processing is 0, the proc entry is re-inserted into the main
 * proc list.  Otherwise, the proc entry is freed.
 *
 * Lock restrictions:
 *     o Caller unlocks fsm.
 */
void
ble_fsm_process_rx_status(struct ble_fsm *fsm, struct ble_fsm_proc *proc,
                          int status)
{
    if (status != 0) {
        fsm->free_cb(proc);
    } else {
        ble_fsm_lock(fsm);
        STAILQ_INSERT_HEAD(&fsm->procs, proc, next);
        ble_fsm_unlock(fsm);
    }
}

/**
 * Searches an fsm's proc list for the first entry that that fits a custom set
 * of criteria.  The supplied callback is applied to each entry in the list
 * until it indicates a match.  If a matching entry is found, it is removed
 * from the list and returned.
 *
 * Lock restrictions:
 *     o Caller unlocks fsm.
 *
 * @param fsm                   The fsm whose list should be searched.
 * @param out_proc              On success, the address of the matching proc is
 *                                  written here.
 * @extract_cb                  The callback to apply to each proc entry.
 * @arg                         An argument that gets passed to each invocation
 *                                  of the callback.
 *
 * @return                      The matching proc entry on success;
 *                                  null on failure.
 */
int
ble_fsm_proc_extract(struct ble_fsm *fsm,
                     struct ble_fsm_proc **out_proc,
                     ble_fsm_extract_fn *extract_cb, void *arg)
{
    struct ble_fsm_proc *prev;
    struct ble_fsm_proc *proc;
    int rc;

    ble_fsm_lock(fsm);

    ble_fsm_assert_sanity(fsm);

    prev = NULL;

    STAILQ_FOREACH(proc, &fsm->procs, next) {
        rc = extract_cb(proc, arg);
        switch (rc) {
        case BLE_FSM_EXTRACT_EMOVE_CONTINUE:
        case BLE_FSM_EXTRACT_EMOVE_STOP:
            ble_fsm_proc_remove(&fsm->procs, proc, prev);
            *out_proc = proc;
            rc = 0;
            goto done;

        case BLE_FSM_EXTRACT_EKEEP_CONTINUE:
            break;

        case BLE_FSM_EXTRACT_EKEEP_STOP:
            rc = 1;
            goto done;

        default:
            assert(0);
            rc = 1;
            goto done;
        }

        prev = proc;
    }

    rc = 1;

done:
    ble_fsm_unlock(fsm);
    return rc;
}

/**
 * Searches an fsm's proc list for all entries that that fits a custom set of
 * criteria.  The supplied callback is applied to each entry in the list to
 * determine if it matches. Each matching entry is removed from the list and
 * inserted into a secondary list.
 *
 * Lock restrictions:
 *     o Caller unlocks fsm.
 *
 * @param fsm                   The fsm whose list should be searched.
 * @param dst_list              Matching procs get appended to this list.
 * @extract_cb                  The callback to apply to each proc entry.
 * @arg                         An argument that gets passed to each invocation
 *                                  of the callback.
 *
 * @return                      The number of matching procs (i.e., the size of
 *                                  the destination list).
 */
int
ble_fsm_proc_extract_list(struct ble_fsm *fsm,
                          struct ble_fsm_proc_list *dst_list,
                          ble_fsm_extract_fn *extract_cb, void *arg)
{
    struct ble_fsm_proc *prev;
    struct ble_fsm_proc *proc;
    struct ble_fsm_proc *next;
    int move;
    int done;
    int cnt;
    int rc;

    STAILQ_INIT(dst_list);
    cnt = 0;

    ble_fsm_lock(fsm);

    ble_fsm_assert_sanity(fsm);

    prev = NULL;
    proc = STAILQ_FIRST(&fsm->procs);
    while (proc != NULL) {
        next = STAILQ_NEXT(proc, next);

        rc = extract_cb(proc, arg);
        switch (rc) {
        case BLE_FSM_EXTRACT_EMOVE_CONTINUE:
            move = 1;
            done = 0;
            break;

        case BLE_FSM_EXTRACT_EMOVE_STOP:
            move = 1;
            done = 1;
            break;

        case BLE_FSM_EXTRACT_EKEEP_CONTINUE:
            move = 0;
            done = 0;
            break;

        case BLE_FSM_EXTRACT_EKEEP_STOP:
            move = 0;
            done = 1;
            break;

        default:
            assert(0);
            move = 0;
            done = 1;
            break;
        }

        if (move) {
            ble_fsm_proc_remove(&fsm->procs, proc, prev);
            STAILQ_INSERT_TAIL(dst_list, proc, next);
            cnt++;
        } else {
            prev = proc;
        }

        if (done) {
            break;
        }

        proc = next;
    }

    ble_fsm_unlock(fsm);

    return cnt;
}

/**
 * Triggers a transmission for each active FSM procedure with a pending send.
 *
 * Lock restrictions:
 *     o Caller unlocks all ble_hs mutexes.
 */
void
ble_fsm_wakeup(struct ble_fsm *fsm)
{
    struct ble_fsm_proc_list proc_list;
    struct ble_fsm_proc *proc;
    struct ble_fsm_proc *prev;
    struct ble_fsm_proc *next;
    int rc;

    ble_hs_misc_assert_no_locks();

    STAILQ_INIT(&proc_list);

    /* Remove all procs with pending transmits and insert them into the
     * temporary list. Once the elements are moved, they can be processed
     * without keeping the mutex locked.
     */
    ble_fsm_lock(fsm);

    prev = NULL;
    proc = STAILQ_FIRST(&fsm->procs);
    while (proc != NULL) {
        next = STAILQ_NEXT(proc, next);

        if (proc->flags & BLE_FSM_PROC_F_PENDING) {
            ble_fsm_proc_remove(&fsm->procs, proc, prev);
            STAILQ_INSERT_TAIL(&proc_list, proc, next);
        } else {
            prev = proc;
        }

        proc = next;
    }

    ble_fsm_unlock(fsm);

    /* Process each of the pending procs. */
    prev = NULL;
    proc = STAILQ_FIRST(&proc_list);
    while (proc != NULL) {
        next = STAILQ_NEXT(proc, next);

        rc = fsm->kick_cb(proc);
        switch (rc) {
        case 0:
            /* Transmit succeeded.  Response expected. */
            ble_fsm_proc_set_expecting(proc, prev);

            /* Current proc remains; reseat prev. */
            prev = proc;
            break;

        case BLE_HS_EAGAIN:
            /* Transmit failed due to resource shortage.  Reschedule. */
            proc->flags &= ~BLE_FSM_PROC_F_PENDING;

            /* Current proc remains; reseat prev. */
            prev = proc;
            break;

        case BLE_HS_EDONE:
            /* Procedure complete. */
            ble_fsm_proc_remove(&proc_list, proc, prev);
            fsm->free_cb(proc);

            /* Current proc removed; old prev still valid. */
            break;

        default:
            assert(0);
            break;
        }

        proc = next;
    }

    /* Concatenate the tempororary list to the end of the main list. */
    ble_fsm_proc_concat(fsm, &proc_list);
}

/**
 * Initializes an fsm instance.
 *
 * Lock restrictions: none.
 *
 * @return                      0 on success; nonzero on failure.
 */
int
ble_fsm_new(struct ble_fsm *fsm, ble_fsm_kick_fn *kick_cb,
            ble_fsm_free_fn *free_cb)
{
    int rc;

    assert(kick_cb != NULL);
    assert(free_cb != NULL);

    memset(fsm, 0, sizeof *fsm);

    rc = os_mutex_init(&fsm->mutex);
    if (rc != 0) {
        return BLE_HS_EOS;
    }

    STAILQ_INIT(&fsm->procs);

    fsm->kick_cb = kick_cb;
    fsm->free_cb = free_cb;

    return 0;
}
