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

/* bttester.c - Bluetooth Tester */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "syscfg/syscfg.h"
#include "console/console.h"

#include "bttester_pipe.h"
#include "btp/btp.h"

#define CMD_QUEUED 2

static struct os_eventq avail_queue;
static struct os_eventq *cmds_queue;
static struct os_event bttester_ev[CMD_QUEUED];
static struct btp_buf *delayed_cmd;

struct btp_buf {
    struct os_event *ev;
    union {
        uint8_t data[BTP_MTU];
        struct btp_hdr hdr;
    };
    uint8_t rsp[BTP_MTU];
};

static struct btp_buf cmd_buf[CMD_QUEUED];

static struct {
    const struct btp_handler *handlers;
    uint8_t num;
} service_handler[BTP_SERVICE_ID_MAX + 1];


void
tester_mbuf_reset(struct os_mbuf *buf)
{
    buf->om_data = &buf->om_databuf[buf->om_pkthdr_len];
    buf->om_len = 0;
}

static void
tester_send_with_index(uint8_t service, uint8_t opcode, uint8_t index,
                       const uint8_t *data, size_t len);
static void
tester_rsp_with_index(uint8_t service, uint8_t opcode, uint8_t index,
                      uint8_t status);

void
tester_register_command_handlers(uint8_t service,
                                 const struct btp_handler *handlers,
                                 size_t num)
{
    assert(service <= BTP_SERVICE_ID_MAX);
    assert(service_handler[service].handlers == NULL);

    service_handler[service].handlers = handlers;
    service_handler[service].num = num;
}

const char *
string_from_bytes(const void *buf, size_t len)
{
    static const char hex[] = "0123456789abcdef";
    static char hexbufs[4][137];
    static uint8_t curbuf;
    const uint8_t *b = buf;
    char *str;
    int i;

    str = hexbufs[curbuf++];
    curbuf %= ARRAY_SIZE(hexbufs);

    len = min(len, (sizeof(hexbufs[0]) - 1) / 2);

    for (i = 0; i < len; i++) {
        str[i * 2] = hex[b[i] >> 4];
        str[i * 2 + 1] = hex[b[i] & 0xf];
    }

    str[i * 2] = '\0';

    return str;
}

static const struct btp_handler *
find_btp_handler(uint8_t service, uint8_t opcode)
{
    if ((service > BTP_SERVICE_ID_MAX) ||
        (service_handler[service].handlers == NULL)) {
        return NULL;
    }

    for (uint8_t i = 0; i < service_handler[service].num; i++) {
        if (service_handler[service].handlers[i].opcode == opcode) {
            return &service_handler[service].handlers[i];
        }
    }

    return NULL;
}

static void
cmd_handler(struct os_event *ev)
{
    const struct btp_handler *btp;
    uint16_t len;
    struct btp_buf *cmd;
    uint8_t status;
    uint16_t rsp_len = 0;

    if (!ev || !ev->ev_arg) {
        return;
    }

    cmd = ev->ev_arg;

    len = le16toh(cmd->hdr.len);
    if (MYNEWT_VAL(BTTESTER_BTP_LOG)) {
        console_printf("[DBG] received %d bytes: %s\n",
                       sizeof(cmd->hdr) + len,
                       string_from_bytes(cmd->data,
                                         sizeof(cmd->hdr) + len));
    }

    btp = find_btp_handler(cmd->hdr.service, cmd->hdr.opcode);
    if (btp) {
        if (btp->index != cmd->hdr.index) {
            status = BTP_STATUS_FAILED;
        } else if ((btp->expect_len >= 0) && (btp->expect_len != len)) {
            status = BTP_STATUS_FAILED;
        } else {
            status = btp->func(cmd->hdr.data, len,
                               cmd->rsp, &rsp_len);
        }

        assert((rsp_len + sizeof(struct btp_hdr)) <= BTP_MTU);
    } else {
        status = BTP_STATUS_UNKNOWN_CMD;
    }

    /* Allow to delay only 1 command. This is for convenience only
     * of using cmd data without need of copying those in async
     * functions. Should be not needed eventually.
     */
    if (status == BTP_STATUS_DELAY_REPLY) {
        assert(delayed_cmd == NULL);
        delayed_cmd = cmd;
        return;
    }

    if ((status == BTP_STATUS_SUCCESS) && rsp_len > 0) {
        tester_send_with_index(cmd->hdr.service, cmd->hdr.opcode,
                               cmd->hdr.index, cmd->rsp, rsp_len);
    } else {
        tester_rsp_with_index(cmd->hdr.service, cmd->hdr.opcode,
                              cmd->hdr.index, status);
    }

    os_eventq_put(&avail_queue, ev);
}

static uint8_t *
recv_cb(uint8_t *buf, size_t *off)
{
    struct btp_hdr *cmd = (void *) buf;
    struct os_event *new_ev;
    struct btp_buf *new_buf, *old_buf;
    uint16_t len;

    if (*off < sizeof(*cmd)) {
        return buf;
    }

    len = le16toh(cmd->len);
    if (len > BTP_MTU - sizeof(*cmd)) {
        *off = 0;
        return buf;
    }

    if (*off < sizeof(*cmd) + len) {
        return buf;
    }

    new_ev = os_eventq_get_no_wait(&avail_queue);
    if (!new_ev) {
        SYS_LOG_ERR("BT tester: RX overflow");
        *off = 0;
        return buf;
    }

    old_buf = CONTAINER_OF(buf, struct btp_buf, data);
    os_eventq_put(cmds_queue, old_buf->ev);

    new_buf = new_ev->ev_arg;
    *off = 0;
    return new_buf->data;
}

static void
avail_queue_init(void)
{
    int i;

    os_eventq_init(&avail_queue);

    for (i = 0; i < CMD_QUEUED; i++) {
        cmd_buf[i].ev = &bttester_ev[i];
        bttester_ev[i].ev_cb = cmd_handler;
        bttester_ev[i].ev_arg = &cmd_buf[i];

        os_eventq_put(&avail_queue, &bttester_ev[i]);
    }
}

void
bttester_evq_set(struct os_eventq *evq)
{
    cmds_queue = evq;
}

void
tester_init(void)
{
    struct os_event *ev;
    struct btp_buf *buf;

    avail_queue_init();
    bttester_evq_set(os_eventq_dflt_get());

    ev = os_eventq_get(&avail_queue);
    buf = ev->ev_arg;

    if (bttester_pipe_init()) {
        SYS_LOG_ERR("Failed to initialize pipe");
        return;
    }

    bttester_pipe_register(buf->data, BTP_MTU, recv_cb);

    /* core service is always available */
    tester_init_core();

    tester_send_with_index(BTP_SERVICE_ID_CORE, BTP_CORE_EV_IUT_READY,
                           BTP_INDEX_NONE, NULL, 0);
}

static void
tester_send_with_index(uint8_t service, uint8_t opcode, uint8_t index,
                       const uint8_t *data, size_t len)
{
    struct btp_hdr msg;

    msg.service = service;
    msg.opcode = opcode;
    msg.index = index;
    msg.len = htole16(len);

    bttester_pipe_send((uint8_t *) &msg, sizeof(msg));
    if (data && len) {
        bttester_pipe_send(data, len);
    }

    if (MYNEWT_VAL(BTTESTER_BTP_LOG)) {
        console_printf("[DBG] send %d bytes hdr: %s\n", sizeof(msg),
                       string_from_bytes((char *) &msg, sizeof(msg)));
        if (data && len) {
            console_printf("[DBG] send %d bytes data: %s\n", len,
                           string_from_bytes((char *) data, len));
        }
    }
}

void
tester_send_buf(uint8_t service, uint8_t opcode, uint8_t index,
                struct os_mbuf *data)
{
    struct btp_hdr msg;

    msg.service = service;
    msg.opcode = opcode;
    msg.index = index;
    msg.len = os_mbuf_len(data);

    bttester_pipe_send((uint8_t *) &msg, sizeof(msg));
    if (data && msg.len) {
        bttester_pipe_send_buf(data);
    }
}

static void
tester_rsp_with_index(uint8_t service, uint8_t opcode, uint8_t index,
                      uint8_t status)
{
    struct btp_status s;

    if (status == BTP_STATUS_SUCCESS) {
        tester_send_with_index(service, opcode, index, NULL, 0);
        return;
    }

    s.code = status;
    tester_send_with_index(service, BTP_STATUS, index, (uint8_t *) &s, sizeof(s));
}

void
tester_event(uint8_t service, uint8_t opcode, const void *data, size_t len)
{
    assert(opcode >= 0x80);
    tester_send_with_index(service, opcode, BTP_INDEX, data, len);
}

void
tester_rsp_full(uint8_t service, uint8_t opcode, const void *rsp, size_t len)
{
    struct btp_buf *cmd;

    assert(opcode < 0x80);
    assert(delayed_cmd != NULL);

    tester_send_with_index(service, opcode, BTP_INDEX, rsp, len);

    cmd = delayed_cmd;
    delayed_cmd = NULL;

    (void)memset(cmd, 0, sizeof(*cmd));

    os_eventq_put(&avail_queue,
                  CONTAINER_OF(cmd, struct btp_buf, data)->ev);
}

void
tester_rsp(uint8_t service, uint8_t opcode, uint8_t status)
{
    struct btp_buf *cmd;

    assert(opcode < 0x80);
    assert(delayed_cmd != NULL);

    tester_rsp_with_index(service, opcode, BTP_INDEX, status);

    cmd = delayed_cmd;
    delayed_cmd = NULL;

    (void)memset(cmd, 0, sizeof(*cmd));
    os_eventq_put(&avail_queue,
                  CONTAINER_OF(cmd, struct btp_buf, data)->ev);
}
