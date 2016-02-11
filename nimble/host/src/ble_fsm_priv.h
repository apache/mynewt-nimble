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

#ifndef H_BLE_FSM_PRIV_
#define H_BLE_FSM_PRIV_

struct ble_fsm_proc {
    STAILQ_ENTRY(ble_fsm_proc) next;

    uint8_t op;
    uint8_t flags;
    uint16_t conn_handle;
    uint32_t tx_time; /* OS ticks. */
};

/** Procedure has a tx pending. */
#define BLE_FSM_PROC_F_PENDING     0x01

/** Procedure currently expects a response. */
#define BLE_FSM_PROC_F_EXPECTING   0x02

/** Procedure failed to tx due to too many outstanding txes. */
#define BLE_FSM_PROC_F_CONGESTED   0x04

/** Procedure failed to tx due to memory exhaustion. */
#define BLE_FSM_PROC_F_NO_MEM      0x08

STAILQ_HEAD(ble_fsm_proc_list, ble_fsm_proc);

typedef int ble_fsm_kick_fn(struct ble_fsm_proc *proc);
typedef void ble_fsm_free_fn(struct ble_fsm_proc *proc);
typedef int ble_fsm_extract_fn(struct ble_fsm_proc *proc, void *arg);

#define BLE_FSM_EXTRACT_EMOVE_CONTINUE      0
#define BLE_FSM_EXTRACT_EMOVE_STOP          1
#define BLE_FSM_EXTRACT_EKEEP_CONTINUE      2
#define BLE_FSM_EXTRACT_EKEEP_STOP          3

struct ble_fsm {
    struct os_mutex mutex;
    struct ble_fsm_proc_list procs;

    ble_fsm_kick_fn *kick_cb;
    ble_fsm_free_fn *free_cb;
};

void ble_fsm_lock(struct ble_fsm *fsm);
void ble_fsm_unlock(struct ble_fsm *fsm);
int ble_fsm_locked_by_cur_task(struct ble_fsm *fsm);
void ble_fsm_proc_remove(struct ble_fsm_proc_list *src_list,
                         struct ble_fsm_proc *proc,
                         struct ble_fsm_proc *prev);
void ble_fsm_proc_concat(struct ble_fsm *fsm,
                         struct ble_fsm_proc_list *tail_list);
int ble_fsm_proc_can_pend(struct ble_fsm_proc *proc);
void ble_fsm_proc_set_pending(struct ble_fsm_proc *proc);
void ble_fsm_proc_set_expecting(struct ble_fsm_proc *proc,
                                struct ble_fsm_proc *prev);
int ble_fsm_tx_postpone_chk(struct ble_fsm_proc *proc, int rc);
void ble_fsm_process_rx_status(struct ble_fsm *fsm, struct ble_fsm_proc *proc,
                               int status);
int ble_fsm_proc_extract(struct ble_fsm *fsm,
                         struct ble_fsm_proc **out_proc,
                         ble_fsm_extract_fn *extract_cb, void *arg);
int ble_fsm_proc_extract_list(struct ble_fsm *fsm,
                              struct ble_fsm_proc_list *dst_list,
                              ble_fsm_extract_fn *extract_cb, void *arg);
void ble_fsm_wakeup(struct ble_fsm *fsm);
int ble_fsm_new(struct ble_fsm *fsm, ble_fsm_kick_fn *kick_cb,
                ble_fsm_free_fn *free_cb);

#endif
