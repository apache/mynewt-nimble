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

/* bttester.h - Bluetooth tester headers */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 * Copyright (C) 2023 Codecoup
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __BTTESTER_H__
#define __BTTESTER_H__

#include "syscfg/syscfg.h"
#include "host/ble_gatt.h"
#include "os/os_mbuf.h"
#include <sys/types.h>

#define BIT(n)  (1UL << (n))

/* Reset os_mbuf to reusable state */
void
tester_mbuf_reset(struct os_mbuf *buf);

const char *
string_from_bytes(const void *buf, size_t len);

static inline void
tester_set_bit(uint8_t *addr, unsigned int bit)
{
    uint8_t *p = addr + (bit / 8);

    *p |= BIT(bit % 8);
}

static inline uint8_t
tester_test_bit(const uint8_t *addr, unsigned int bit)
{
    const uint8_t *p = addr + (bit / 8);

    return *p & BIT(bit % 8);
}

static inline void
tester_clear_bit(uint8_t *addr, unsigned int bit)
{
    uint8_t *p = addr + (bit / 8);

    *p &= ~BIT(bit % 8);
}


void
tester_init(void);
void
tester_rsp(uint8_t service, uint8_t opcode, uint8_t status);
void
tester_rsp_full(uint8_t service, uint8_t opcode, const void *rsp, size_t len);
void
tester_event(uint8_t service, uint8_t opcode, const void *data, size_t len);

/* Used to indicate that command length is variable and that validation will
 * be done in handler.
 */
#define BTP_HANDLER_LENGTH_VARIABLE (-1)

struct btp_handler {
    uint8_t opcode;
    uint8_t index;
    ssize_t expect_len;
    uint8_t (*func)(const void *cmd, uint16_t cmd_len,
                    void *rsp, uint16_t *rsp_len);
};

void tester_register_command_handlers(uint8_t service,
                                      const struct btp_handler *handlers,
                                      size_t num);
void
tester_send_buf(uint8_t service, uint8_t opcode, uint8_t index,
                struct os_mbuf *buf);

uint8_t
tester_init_gap(void);
uint8_t
tester_unregister_gap(void);
void
tester_init_core(void);
uint8_t
tester_init_gatt(void);
uint8_t
tester_unregister_gatt(void);
int
tester_gattc_notify_rx_ev(uint16_t conn_handle, uint16_t attr_handle,
                          uint8_t indication, struct os_mbuf *om);
int
tester_gatt_subscribe_ev(uint16_t conn_handle,
                         uint16_t attr_handle,
                         uint8_t reason,
                         uint8_t prev_notify,
                         uint8_t cur_notify,
                         uint8_t prev_indicate,
                         uint8_t cur_indicate);

#if MYNEWT_VAL(BLE_L2CAP_COC_MAX_NUM)
uint8_t
tester_init_l2cap(void);
uint8_t
tester_unregister_l2cap(void);
#endif

#if MYNEWT_VAL(BLE_MESH)
uint8_t
tester_init_mesh(void);
uint8_t
tester_unregister_mesh(void);
#endif /* MYNEWT_VAL(BLE_MESH) */
uint8_t
tester_init_gatt_cl(void);
uint8_t
tester_unregister_gatt_cl(void);
void
gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg);

int
gatt_svr_init(void);
#endif /* __BTTESTER_H__ */