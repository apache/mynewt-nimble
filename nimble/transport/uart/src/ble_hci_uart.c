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

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include "bsp/bsp.h"
#include "os/os.h"
#include "bsp/bsp.h"
#include "hal/hal_gpio.h"
#include "hal/hal_cputime.h"
#include "hal/hal_uart.h"

/* BLE */
#include "nimble/ble.h"
#include "nimble/nimble_opt.h"
#include "nimble/hci_common.h"
#include "nimble/ble_hci_trans.h"

#include "transport/uart/ble_hci_uart.h"

/***
 * NOTE:
 * The UART HCI transport doesn't use event buffer priorities.  All incoming
 * and outgoing events and commands use buffers from the same pool.
 */

#define BLE_HCI_UART_H4_NONE        0x00
#define BLE_HCI_UART_H4_CMD         0x01
#define BLE_HCI_UART_H4_ACL         0x02
#define BLE_HCI_UART_H4_SCO         0x03
#define BLE_HCI_UART_H4_EVT         0x04

/** Default configuration. */
const struct ble_hci_uart_cfg ble_hci_uart_cfg_dflt = {
    .uart_port = 0,
    .baud = 1000000,
    .flow_ctrl = HAL_UART_FLOW_CTL_RTS_CTS,
    .data_bits = 8,
    .stop_bits = 1,
    .parity = HAL_UART_PARITY_NONE,

    .num_evt_bufs = 8,
    .evt_buf_sz = BLE_HCI_TRANS_CMD_SZ,
};

static ble_hci_trans_rx_cmd_fn *ble_hci_uart_rx_cmd_cb;
static void *ble_hci_uart_rx_cmd_arg;

static ble_hci_trans_rx_acl_fn *ble_hci_uart_rx_acl_cb;
static void *ble_hci_uart_rx_acl_arg;

static struct os_mempool ble_hci_uart_evt_pool;
static void *ble_hci_uart_evt_buf;

static struct os_mempool ble_hci_uart_pkt_pool;
static void *ble_hci_uart_pkt_buf;

#define BLE_HCI_UART_LOG_SZ 1024
static uint8_t ble_hci_uart_tx_log[BLE_HCI_UART_LOG_SZ];
static int ble_hci_uart_tx_log_sz;
static uint8_t ble_hci_uart_rx_log[BLE_HCI_UART_LOG_SZ];
static int ble_hci_uart_rx_log_sz;

/**
 * An incoming or outgoing command or event.
 */
struct ble_hci_uart_cmd {
    uint8_t *data;      /* Pointer to ble_hci_uart_cmd data */
    uint16_t cur;       /* Number of bytes read/written */
    uint16_t len;       /* Total number of bytes to read/write */
};

/**
 * An incoming ACL data packet.
 */
struct ble_hci_uart_acl {
    struct os_mbuf *buf; /* Buffer containing the data */
    uint16_t len;        /* Target size when buf is considered complete */
};

/**
 * A packet to be sent over the UART.  This can be a command, an event, or ACL
 * data.
 */
struct ble_hci_uart_pkt {
    STAILQ_ENTRY(ble_hci_uart_pkt) next;
    void *data;
    uint8_t type;
};

static struct {
    /*** State of data received over UART. */
    uint8_t rx_type;    /* Pending packet type. 0 means nothing pending */
    union {
        struct ble_hci_uart_cmd rx_cmd;
        struct ble_hci_uart_acl rx_acl;
    };

    /*** State of data transmitted over UART. */
    uint8_t tx_type;    /* Pending packet type. 0 means nothing pending */
    union {
        struct ble_hci_uart_cmd tx_cmd;
        struct os_mbuf *tx_acl;
    };
    STAILQ_HEAD(, ble_hci_uart_pkt) tx_pkts; /* Packet queue to send to UART */
} ble_hci_uart_state;

static struct ble_hci_uart_cfg ble_hci_uart_cfg;

static int
ble_hci_uart_acl_tx(struct os_mbuf *om)
{
    struct ble_hci_uart_pkt *pkt;
    os_sr_t sr;

    pkt = os_memblock_get(&ble_hci_uart_pkt_pool);
    if (pkt == NULL) {
        os_mbuf_free_chain(om);
        return BLE_ERR_MEM_CAPACITY;
    }

    pkt->type = BLE_HCI_UART_H4_ACL;
    pkt->data = om;

    OS_ENTER_CRITICAL(sr);
    STAILQ_INSERT_TAIL(&ble_hci_uart_state.tx_pkts, pkt, next);
    OS_EXIT_CRITICAL(sr);

    hal_uart_start_tx(ble_hci_uart_cfg.uart_port);

    return 0;
}

static int
ble_hci_uart_cmdevt_tx(uint8_t *hci_ev, uint8_t h4_type)
{
    struct ble_hci_uart_pkt *pkt;
    os_sr_t sr;

    pkt = os_memblock_get(&ble_hci_uart_pkt_pool);
    if (pkt == NULL) {
        ble_hci_trans_buf_free(hci_ev);
        return BLE_ERR_MEM_CAPACITY;
    }

    pkt->type = h4_type;
    pkt->data = hci_ev;

    OS_ENTER_CRITICAL(sr);
    STAILQ_INSERT_TAIL(&ble_hci_uart_state.tx_pkts, pkt, next);
    OS_EXIT_CRITICAL(sr);

    hal_uart_start_tx(ble_hci_uart_cfg.uart_port);

    return 0;
}

/**
 * @return                      The packet type to transmit on success;
 *                              -1 if there is nothing to send.
 */
static int
ble_hci_uart_tx_pkt_type(void)
{
    struct ble_hci_uart_pkt *pkt;
    os_sr_t sr;
    int rc;

    OS_ENTER_CRITICAL(sr);

    pkt = STAILQ_FIRST(&ble_hci_uart_state.tx_pkts);
    if (!pkt) {
        OS_EXIT_CRITICAL(sr);
        return -1;
    }

    STAILQ_REMOVE(&ble_hci_uart_state.tx_pkts, pkt, ble_hci_uart_pkt, next);

    OS_EXIT_CRITICAL(sr);

    rc = pkt->type;
    switch (pkt->type) {
    case BLE_HCI_UART_H4_CMD:
        ble_hci_uart_state.tx_type = BLE_HCI_UART_H4_CMD;
        ble_hci_uart_state.tx_cmd.data = pkt->data;
        ble_hci_uart_state.tx_cmd.cur = 0;
        ble_hci_uart_state.tx_cmd.len = ble_hci_uart_state.tx_cmd.data[2] +
                                        BLE_HCI_CMD_HDR_LEN;
        break;

    case BLE_HCI_UART_H4_EVT:
        ble_hci_uart_state.tx_type = BLE_HCI_UART_H4_EVT;
        ble_hci_uart_state.tx_cmd.data = pkt->data;
        ble_hci_uart_state.tx_cmd.cur = 0;
        ble_hci_uart_state.tx_cmd.len = ble_hci_uart_state.tx_cmd.data[1] +
                                        BLE_HCI_EVENT_HDR_LEN;
        break;

    case BLE_HCI_UART_H4_ACL:
        ble_hci_uart_state.tx_type = BLE_HCI_UART_H4_ACL;
        ble_hci_uart_state.tx_acl = pkt->data;
        break;

    default:
        rc = -1;
        break;
    }

    os_memblock_put(&ble_hci_uart_pkt_pool, pkt);

    return rc;
}

/**
 * @return                      The byte to transmit on success;
 *                              -1 if there is nothing to send.
 */
static int
ble_hci_uart_tx_char(void *arg)
{
    int rc = -1;

    switch (ble_hci_uart_state.tx_type) {
    case BLE_HCI_UART_H4_NONE: /* No pending packet, pick one from the queue */
        rc = ble_hci_uart_tx_pkt_type();
        break;

    case BLE_HCI_UART_H4_CMD:
    case BLE_HCI_UART_H4_EVT:
        rc = ble_hci_uart_state.tx_cmd.data[ble_hci_uart_state.tx_cmd.cur++];

        if (ble_hci_uart_state.tx_cmd.cur == ble_hci_uart_state.tx_cmd.len) {
            ble_hci_trans_buf_free(ble_hci_uart_state.tx_cmd.data);
            ble_hci_uart_state.tx_type = BLE_HCI_UART_H4_NONE;
        }
        break;

    case BLE_HCI_UART_H4_ACL:
        rc = *OS_MBUF_DATA(ble_hci_uart_state.tx_acl, uint8_t *);
        os_mbuf_adj(ble_hci_uart_state.tx_acl, 1);
        if (!OS_MBUF_PKTLEN(ble_hci_uart_state.tx_acl)) {
            os_mbuf_free_chain(ble_hci_uart_state.tx_acl);
            ble_hci_uart_state.tx_type = BLE_HCI_UART_H4_NONE;
        }
        break;
    }

    if (rc != -1) {
        ble_hci_uart_tx_log[ble_hci_uart_tx_log_sz++] = rc;
        if (ble_hci_uart_tx_log_sz == sizeof ble_hci_uart_tx_log) {
            ble_hci_uart_tx_log_sz = 0;
        }
    }

    return rc;
}

/**
 * @return                      The type of packet to follow success;
 *                              -1 if there is no valid packet to receive.
 */
static int
ble_hci_uart_rx_pkt_type(uint8_t data)
{
    ble_hci_uart_state.rx_type = data;

    /* XXX: For now we assert that buffer allocation succeeds.  The correct
     * thing to do is return -1 on allocation failure so that flow control is
     * engaged.  Then, we will need to tell the UART to start receiving again
     * as follows:
     *     o flat buf: when we free a buffer.
     *     o mbuf: periodically? (which task executes the callout?)
     */
    switch (ble_hci_uart_state.rx_type) {
    case BLE_HCI_UART_H4_CMD:
        ble_hci_uart_state.rx_cmd.data =
            ble_hci_trans_buf_alloc(BLE_HCI_TRANS_BUF_CMD);
        assert(ble_hci_uart_state.rx_cmd.data != NULL);

        ble_hci_uart_state.rx_cmd.len = 0;
        ble_hci_uart_state.rx_cmd.cur = 0;
        break;

    case BLE_HCI_UART_H4_EVT:
        ble_hci_uart_state.rx_cmd.data =
            ble_hci_trans_buf_alloc(BLE_HCI_TRANS_BUF_EVT_HI);
        assert(ble_hci_uart_state.rx_cmd.data != NULL);

        ble_hci_uart_state.rx_cmd.len = 0;
        ble_hci_uart_state.rx_cmd.cur = 0;
        break;

    case BLE_HCI_UART_H4_ACL:
        ble_hci_uart_state.rx_acl.buf =
            os_msys_get_pkthdr(BLE_HCI_DATA_HDR_SZ, 0);
        assert(ble_hci_uart_state.rx_acl.buf != NULL);

        ble_hci_uart_state.rx_acl.len = 0;
        break;

    default:
        ble_hci_uart_state.rx_type = BLE_HCI_UART_H4_NONE;
        return -1;
    }

    return 0;
}

static void
ble_hci_uart_rx_cmd(uint8_t data)
{
    int rc;

    ble_hci_uart_state.rx_cmd.data[ble_hci_uart_state.rx_cmd.cur++] = data;

    if (ble_hci_uart_state.rx_cmd.cur < BLE_HCI_CMD_HDR_LEN) {
        return;
    }

    if (ble_hci_uart_state.rx_cmd.cur == BLE_HCI_CMD_HDR_LEN) {
        ble_hci_uart_state.rx_cmd.len = ble_hci_uart_state.rx_cmd.data[2] +
                                         BLE_HCI_CMD_HDR_LEN;
    }

    if (ble_hci_uart_state.rx_cmd.cur == ble_hci_uart_state.rx_cmd.len) {
        assert(ble_hci_uart_rx_cmd_cb != NULL);
        rc = ble_hci_uart_rx_cmd_cb(ble_hci_uart_state.rx_cmd.data,
                                    ble_hci_uart_rx_cmd_arg);
        if (rc != 0) {
            ble_hci_trans_buf_free(ble_hci_uart_state.rx_cmd.data);
        }
        ble_hci_uart_state.rx_type = BLE_HCI_UART_H4_NONE;
    }
}

static void
ble_hci_uart_rx_evt(uint8_t data)
{
    int rc;

    ble_hci_uart_state.rx_cmd.data[ble_hci_uart_state.rx_cmd.cur++] = data;

    if (ble_hci_uart_state.rx_cmd.cur < BLE_HCI_EVENT_HDR_LEN) {
        return;
    }

    if (ble_hci_uart_state.rx_cmd.cur == BLE_HCI_EVENT_HDR_LEN) {
        ble_hci_uart_state.rx_cmd.len = ble_hci_uart_state.rx_cmd.data[1] +
                                        BLE_HCI_EVENT_HDR_LEN;
    }

    if (ble_hci_uart_state.rx_cmd.cur == ble_hci_uart_state.rx_cmd.len) {
        assert(ble_hci_uart_rx_cmd_cb != NULL);
        rc = ble_hci_uart_rx_cmd_cb(ble_hci_uart_state.rx_cmd.data,
                                    ble_hci_uart_rx_cmd_arg);
        if (rc != 0) {
            ble_hci_trans_buf_free(ble_hci_uart_state.rx_cmd.data);
        }
        ble_hci_uart_state.rx_type = BLE_HCI_UART_H4_NONE;
    }
}

static void
ble_hci_uart_rx_acl(uint8_t data)
{
    uint16_t pktlen;

    os_mbuf_append(ble_hci_uart_state.rx_acl.buf, &data, 1);

    pktlen = OS_MBUF_PKTLEN(ble_hci_uart_state.rx_acl.buf);

    if (pktlen < BLE_HCI_DATA_HDR_SZ) {
        return;
    }

    if (pktlen == BLE_HCI_DATA_HDR_SZ) {
        os_mbuf_copydata(ble_hci_uart_state.rx_acl.buf, 2,
                         sizeof(ble_hci_uart_state.rx_acl.len),
                         &ble_hci_uart_state.rx_acl.len);
        ble_hci_uart_state.rx_acl.len =
            le16toh(&ble_hci_uart_state.rx_acl.len) + BLE_HCI_DATA_HDR_SZ;
    }

    if (pktlen == ble_hci_uart_state.rx_acl.len) {
        assert(ble_hci_uart_rx_cmd_cb != NULL);
        ble_hci_uart_rx_acl_cb(ble_hci_uart_state.rx_acl.buf,
                               ble_hci_uart_rx_acl_arg);
        ble_hci_uart_state.rx_type = BLE_HCI_UART_H4_NONE;
    }
}

static int
ble_hci_uart_rx_char(void *arg, uint8_t data)
{
    ble_hci_uart_rx_log[ble_hci_uart_rx_log_sz++] = data;
    if (ble_hci_uart_rx_log_sz == sizeof ble_hci_uart_rx_log) {
        ble_hci_uart_rx_log_sz = 0;
    }

    switch (ble_hci_uart_state.rx_type) {
    case BLE_HCI_UART_H4_NONE:
        return ble_hci_uart_rx_pkt_type(data);
    case BLE_HCI_UART_H4_CMD:
        ble_hci_uart_rx_cmd(data);
        return 0;
    case BLE_HCI_UART_H4_EVT:
        ble_hci_uart_rx_evt(data);
        return 0;
    case BLE_HCI_UART_H4_ACL:
        ble_hci_uart_rx_acl(data);
        return 0;
    default:
        return -1;
    }
}

static void
ble_hci_uart_set_rx_cbs(ble_hci_trans_rx_cmd_fn *cmd_cb,
                        void *cmd_arg,
                        ble_hci_trans_rx_acl_fn *acl_cb,
                        void *acl_arg)
{
    ble_hci_uart_rx_cmd_cb = cmd_cb;
    ble_hci_uart_rx_cmd_arg = cmd_arg;
    ble_hci_uart_rx_acl_cb = acl_cb;
    ble_hci_uart_rx_acl_arg = acl_arg;
}

static void
ble_hci_uart_free_pkt(uint8_t type, uint8_t *cmdevt, struct os_mbuf *acl)
{
    switch (type) {
    case BLE_HCI_UART_H4_NONE:
        break;

    case BLE_HCI_UART_H4_CMD:
    case BLE_HCI_UART_H4_EVT:
        ble_hci_trans_buf_free(cmdevt);
        break;

    case BLE_HCI_UART_H4_ACL:
        os_mbuf_free_chain(acl);
        break;

    default:
        assert(0);
        break;
    }
}

static void
ble_hci_uart_free_mem(void)
{
    free(ble_hci_uart_evt_buf);
    ble_hci_uart_evt_buf = NULL;

    free(ble_hci_uart_pkt_buf);
    ble_hci_uart_pkt_buf = NULL;
}

static int
ble_hci_uart_config(void)
{
    int rc;

    rc = hal_uart_init_cbs(ble_hci_uart_cfg.uart_port,
                           ble_hci_uart_tx_char, NULL,
                           ble_hci_uart_rx_char, NULL);
    if (rc != 0) {
        return BLE_ERR_UNSPECIFIED;
    }

    rc = hal_uart_config(ble_hci_uart_cfg.uart_port,
                         ble_hci_uart_cfg.baud,
                         ble_hci_uart_cfg.data_bits,
                         ble_hci_uart_cfg.stop_bits,
                         ble_hci_uart_cfg.parity,
                         ble_hci_uart_cfg.flow_ctrl);
    if (rc != 0) {
        return BLE_ERR_HW_FAIL;
    }

    return 0;
}

/**
 * Sends an HCI event from the controller to the host.
 *
 * @param cmd                   The HCI event to send.  This buffer must be
 *                                  allocated via ble_hci_trans_buf_alloc().
 *
 * @return                      0 on success;
 *                              A BLE_ERR_[...] error code on failure.
 */
int
ble_hci_trans_ll_evt_tx(uint8_t *cmd)
{
    int rc;

    rc = ble_hci_uart_cmdevt_tx(cmd, BLE_HCI_UART_H4_EVT);
    return rc;
}

/**
 * Sends ACL data from controller to host.
 *
 * @param om                    The ACL data packet to send.
 *
 * @return                      0 on success;
 *                              A BLE_ERR_[...] error code on failure.
 */
int
ble_hci_trans_ll_acl_tx(struct os_mbuf *om)
{
    int rc;

    rc = ble_hci_uart_acl_tx(om);
    return rc;
}

/**
 * Sends an HCI command from the host to the controller.
 *
 * @param cmd                   The HCI command to send.  This buffer must be
 *                                  allocated via ble_hci_trans_buf_alloc().
 *
 * @return                      0 on success;
 *                              A BLE_ERR_[...] error code on failure.
 */
int
ble_hci_trans_hs_cmd_tx(uint8_t *cmd)
{
    int rc;

    rc = ble_hci_uart_cmdevt_tx(cmd, BLE_HCI_UART_H4_CMD);
    return rc;
}

/**
 * Sends ACL data from host to controller.
 *
 * @param om                    The ACL data packet to send.
 *
 * @return                      0 on success;
 *                              A BLE_ERR_[...] error code on failure.
 */
int
ble_hci_trans_hs_acl_tx(struct os_mbuf *om)
{
    int rc;

    rc = ble_hci_uart_acl_tx(om);
    return rc;
}

/**
 * Configures the HCI transport to call the specified callback upon receiving
 * HCI packets from the controller.  This function should only be called by by
 * host.
 *
 * @param cmd_cb                The callback to execute upon receiving an HCI
 *                                  event.
 * @param cmd_arg               Optional argument to pass to the command
 *                                  callback.
 * @param acl_cb                The callback to execute upon receiving ACL
 *                                  data.
 * @param acl_arg               Optional argument to pass to the ACL
 *                                  callback.
 */
void
ble_hci_trans_cfg_hs(ble_hci_trans_rx_cmd_fn *cmd_cb,
                            void *cmd_arg,
                            ble_hci_trans_rx_acl_fn *acl_cb,
                            void *acl_arg)
{
    ble_hci_uart_set_rx_cbs(cmd_cb, cmd_arg, acl_cb, acl_arg);
}

/**
 * Configures the HCI transport to operate with a host.  The transport will
 * execute specified callbacks upon receiving HCI packets from the controller.
 *
 * @param cmd_cb                The callback to execute upon receiving an HCI
 *                                  event.
 * @param cmd_arg               Optional argument to pass to the command
 *                                  callback.
 * @param acl_cb                The callback to execute upon receiving ACL
 *                                  data.
 * @param acl_arg               Optional argument to pass to the ACL
 *                                  callback.
 */
void
ble_hci_trans_cfg_ll(ble_hci_trans_rx_cmd_fn *cmd_cb,
                            void *cmd_arg,
                            ble_hci_trans_rx_acl_fn *acl_cb,
                            void *acl_arg)
{
    ble_hci_uart_set_rx_cbs(cmd_cb, cmd_arg, acl_cb, acl_arg);
}

/**
 * Allocates a flat buffer of the specified type.
 *
 * @param type                  The type of buffer to allocate; one of the
 *                                  BLE_HCI_TRANS_BUF_[...] constants.
 *
 * @return                      The allocated buffer on success;
 *                              NULL on buffer exhaustion.
 */
uint8_t *
ble_hci_trans_buf_alloc(int type)
{
    uint8_t *buf;

    switch (type) {
    case BLE_HCI_TRANS_BUF_CMD:
    case BLE_HCI_TRANS_BUF_EVT_LO:
    case BLE_HCI_TRANS_BUF_EVT_HI:
        buf = os_memblock_get(&ble_hci_uart_evt_pool);
        break;

    default:
        assert(0);
        buf = NULL;
    }

    return buf;
}

/**
 * Frees the specified flat buffer.  The buffer must have been allocated via
 * ble_hci_trans_buf_alloc().
 *
 * @param buf                   The buffer to free.
 */
void
ble_hci_trans_buf_free(uint8_t *buf)
{
    int rc;

    rc = os_memblock_put(&ble_hci_uart_evt_pool, buf);
    assert(rc == 0);
}

/**
 * Resets the HCI UART transport to a clean state.  Frees all buffers and
 * reconfigures the UART.
 *
 * @return                      0 on success;
 *                              A BLE_ERR_[...] error code on failure.
 */
int
ble_hci_trans_reset(void)
{
    struct ble_hci_uart_pkt *pkt;
    int rc;

    /* Close the UART to prevent race conditions as the buffers are freed. */
    rc = hal_uart_close(ble_hci_uart_cfg.uart_port);
    if (rc != 0) {
        return BLE_ERR_HW_FAIL;
    }

    ble_hci_uart_free_pkt(ble_hci_uart_state.rx_type,
                          ble_hci_uart_state.rx_cmd.data,
                          ble_hci_uart_state.rx_acl.buf);
    ble_hci_uart_state.rx_type = BLE_HCI_UART_H4_NONE;

    ble_hci_uart_free_pkt(ble_hci_uart_state.tx_type,
                          ble_hci_uart_state.tx_cmd.data,
                          ble_hci_uart_state.tx_acl);
    ble_hci_uart_state.tx_type = BLE_HCI_UART_H4_NONE;

    while ((pkt = STAILQ_FIRST(&ble_hci_uart_state.tx_pkts)) != NULL) {
        STAILQ_REMOVE(&ble_hci_uart_state.tx_pkts, pkt, ble_hci_uart_pkt,
                      next);
        ble_hci_uart_free_pkt(pkt->type, pkt->data, pkt->data);
        os_memblock_put(&ble_hci_uart_pkt_pool, pkt);
    }

    /* Reopen the UART. */
    rc = ble_hci_uart_config();
    if (rc != 0) {
        return rc;
    }

    return 0;
}

/**
 * Initializes the UART HCI transport module.
 *
 * @param cfg                   The settings to initialize the HCI UART
 *                                  transport with.
 *
 * @return                      0 on success;
 *                              A BLE_ERR_[...] error code on failure.
 */
int
ble_hci_uart_init(const struct ble_hci_uart_cfg *cfg)
{
    int rc;

    ble_hci_uart_free_mem();

    ble_hci_uart_cfg = *cfg;

    ble_hci_uart_evt_buf = malloc(
        OS_MEMPOOL_BYTES(ble_hci_uart_cfg.num_evt_bufs,
                         ble_hci_uart_cfg.evt_buf_sz));
    if (ble_hci_uart_evt_buf == NULL) {
        rc = BLE_ERR_MEM_CAPACITY;
        goto err;
    }

    /* Create memory pool of HCI command / event buffers */
    rc = os_mempool_init(&ble_hci_uart_evt_pool, ble_hci_uart_cfg.num_evt_bufs,
                         ble_hci_uart_cfg.evt_buf_sz, ble_hci_uart_evt_buf,
                         "ble_hci_uart_evt_pool");
    if (rc != 0) {
        rc = BLE_ERR_UNSPECIFIED;
        goto err;
    }

    ble_hci_uart_pkt_buf = malloc(
        OS_MEMPOOL_BYTES(ble_hci_uart_cfg.num_evt_bufs,
        sizeof (struct os_event)));
    if (ble_hci_uart_pkt_buf == NULL) {
        rc = BLE_ERR_MEM_CAPACITY;
        goto err;
    }

    /* Create memory pool of packet list nodes. */
    rc = os_mempool_init(&ble_hci_uart_pkt_pool,
                         ble_hci_uart_cfg.num_evt_bufs,
                         sizeof (struct ble_hci_uart_pkt),
                         ble_hci_uart_pkt_buf,
                         "ble_hci_uart_pkt_pool");
    if (rc != 0) {
        rc = BLE_ERR_UNSPECIFIED;
        goto err;
    }

    rc = ble_hci_uart_config();
    if (rc != 0) {
        goto err;
    }

    memset(&ble_hci_uart_state, 0, sizeof ble_hci_uart_state);
    STAILQ_INIT(&ble_hci_uart_state.tx_pkts);

    return 0;

err:
    ble_hci_uart_free_mem();
    return rc;
}
