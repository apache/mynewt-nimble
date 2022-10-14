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

#include <assert.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <sysinit/sysinit.h>
#include <syscfg/syscfg.h>
#include "os/os_mbuf.h"
#include "os/os_mempool.h"
#include "hal/hal_uart.h"
#include "nimble/transport.h"
#include "nimble/transport/hci_h4.h"

#define TX_Q_SIZE   (MYNEWT_VAL(BLE_TRANSPORT_ACL_FROM_HS_COUNT) + 1)

struct hci_uart_tx {
    uint8_t type;
    uint8_t sent_type;
    uint16_t len;
    uint16_t idx;

    struct os_mbuf *om;
    uint8_t *buf;

    STAILQ_ENTRY(hci_uart_tx) tx_q_next;
};

static STAILQ_HEAD(, hci_uart_tx) tx_q;

static struct os_mempool pool_tx_q;
static uint8_t pool_tx_q_buf[ OS_MEMPOOL_BYTES(TX_Q_SIZE,
                                               sizeof(struct hci_uart_tx)) ];

struct hci_h4_sm hci_uart_h4sm;

static int
hci_uart_frame_cb(uint8_t pkt_type, void *data)
{
    switch (pkt_type) {
    case HCI_H4_EVT:
        return ble_transport_to_hs_evt(data);
    case HCI_H4_ACL:
        return ble_transport_to_hs_acl(data);
    default:
        assert(0);
        break;
    }

    return -1;
}

static int
hci_uart_rx_char(void *arg, uint8_t data)
{
    hci_h4_sm_rx(&hci_uart_h4sm, &data, 1);

    return 0;
}

static int
hci_uart_tx_char(void *arg)
{
    struct hci_uart_tx *tx;
    uint8_t ch;
    os_sr_t sr;

    OS_ENTER_CRITICAL(sr);
    tx = STAILQ_FIRST(&tx_q);
    OS_EXIT_CRITICAL(sr);
    if (!tx) {
        return -1;
    }

    if (!tx->sent_type) {
        tx->sent_type = 1;
        return tx->type;
    }

    switch (tx->type) {
    case HCI_H4_CMD:
        ch = tx->buf[tx->idx];
        tx->idx++;
        if (tx->idx == tx->len) {
            ble_transport_free(tx->buf);
            OS_ENTER_CRITICAL(sr);
            STAILQ_REMOVE_HEAD(&tx_q, tx_q_next);
            OS_EXIT_CRITICAL(sr);
            os_memblock_put(&pool_tx_q, tx);
        }
        break;
    case HCI_H4_ACL:
        os_mbuf_copydata(tx->om, 0, 1, &ch);
        os_mbuf_adj(tx->om, 1);
        tx->len--;
        if (tx->len == 0) {
            os_mbuf_free_chain(tx->om);
            OS_ENTER_CRITICAL(sr);
            STAILQ_REMOVE_HEAD(&tx_q, tx_q_next);
            OS_EXIT_CRITICAL(sr);
            os_memblock_put(&pool_tx_q, tx);
        }
        break;
    default:
        assert(0);
        OS_ENTER_CRITICAL(sr);
        STAILQ_REMOVE_HEAD(&tx_q, tx_q_next);
        OS_EXIT_CRITICAL(sr);
        os_memblock_put(&pool_tx_q, tx);
    }

    return ch;
}

static int
hci_uart_configure(void)
{
    enum hal_uart_parity parity;
    enum hal_uart_flow_ctl flowctl;
    int rc;

    rc = hal_uart_init_cbs(MYNEWT_VAL(BLE_TRANSPORT_UART_PORT),
                           hci_uart_tx_char, NULL,
                           hci_uart_rx_char, NULL);
    if (rc != 0) {
        return -1;
    }

    if (MYNEWT_VAL_CHOICE(BLE_TRANSPORT_UART_PARITY, odd)) {
        parity = HAL_UART_PARITY_ODD;
    } else if (MYNEWT_VAL_CHOICE(BLE_TRANSPORT_UART_PARITY, even)) {
        parity = HAL_UART_PARITY_EVEN;
    } else {
        parity = HAL_UART_PARITY_NONE;
    }

    if (MYNEWT_VAL_CHOICE(BLE_TRANSPORT_UART_FLOW_CONTROL, rtscts)) {
        flowctl = HAL_UART_FLOW_CTL_RTS_CTS;
    } else {
        flowctl = HAL_UART_FLOW_CTL_NONE;
    }

    rc = hal_uart_config(MYNEWT_VAL(BLE_TRANSPORT_UART_PORT),
                         MYNEWT_VAL(BLE_TRANSPORT_UART_BAUDRATE),
                         MYNEWT_VAL(BLE_TRANSPORT_UART_DATA_BITS),
                         MYNEWT_VAL(BLE_TRANSPORT_UART_STOP_BITS),
                         parity, flowctl);
    if (rc != 0) {
        return -1;
    }

    return 0;
}

int
ble_transport_to_ll_cmd_impl(void *buf)
{
    struct hci_uart_tx *txe;
    os_sr_t sr;

    txe = os_memblock_get(&pool_tx_q);
    if (!txe) {
        assert(0);
        return -ENOMEM;
    }

    txe->type = HCI_H4_CMD;
    txe->sent_type = 0;
    txe->len = 3 + ((uint8_t *)buf)[2];
    txe->buf = buf;
    txe->idx = 0;
    txe->om = NULL;

    OS_ENTER_CRITICAL(sr);
    STAILQ_INSERT_TAIL(&tx_q, txe, tx_q_next);
    OS_EXIT_CRITICAL(sr);

    hal_uart_start_tx(MYNEWT_VAL(BLE_TRANSPORT_UART_PORT));

    return 0;
}

int
ble_transport_to_ll_acl_impl(struct os_mbuf *om)
{
    struct hci_uart_tx *txe;
    os_sr_t sr;

    txe = os_memblock_get(&pool_tx_q);
    if (!txe) {
        assert(0);
        return -ENOMEM;
    }

    txe->type = HCI_H4_ACL;
    txe->sent_type = 0;
    txe->len = OS_MBUF_PKTLEN(om);
    txe->idx = 0;
    txe->buf = NULL;
    txe->om = om;

    OS_ENTER_CRITICAL(sr);
    STAILQ_INSERT_TAIL(&tx_q, txe, tx_q_next);
    OS_EXIT_CRITICAL(sr);

    hal_uart_start_tx(MYNEWT_VAL(BLE_TRANSPORT_UART_PORT));

    return 0;
}

void
ble_transport_ll_init(void)
{
    int rc;

    SYSINIT_ASSERT_ACTIVE();

    rc = hci_uart_configure();
    SYSINIT_PANIC_ASSERT(rc == 0);

    rc = os_mempool_init(&pool_tx_q, TX_Q_SIZE, sizeof(struct hci_uart_tx),
                         pool_tx_q_buf, "hci_uart_tx_q");
    SYSINIT_PANIC_ASSERT(rc == 0);

    hci_h4_sm_init(&hci_uart_h4sm, &hci_h4_allocs_from_ll, hci_uart_frame_cb);

    STAILQ_INIT(&tx_q);
}
