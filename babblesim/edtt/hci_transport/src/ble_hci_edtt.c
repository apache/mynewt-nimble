/*
 * Copyright (c) 2019 Oticon A/S
 * Copyright (c) 2021 Codecoup
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <bs_tracing.h>
#include "sysinit/sysinit.h"
#include "syscfg/syscfg.h"
#include "os/os_eventq.h"

/* BLE */
#include "nimble/ble.h"
#include "nimble/hci_common.h"
#include "nimble/ble_hci_trans.h"

#include "bs_symbols.h"
#include "bs_types.h"
#include "time_machine.h"
#include "ble_hci_edtt.h"
#include "edtt_driver.h"
#include "commands.h"

#define BLE_HCI_EDTT_EVT_COUNT  \
    (MYNEWT_VAL(BLE_HCI_EVT_HI_BUF_COUNT) + MYNEWT_VAL(BLE_HCI_EVT_LO_BUF_COUNT))

#define BLE_HCI_EDTT_NONE        0x00
#define BLE_HCI_EDTT_CMD         0x01
#define BLE_HCI_EDTT_ACL         0x02
#define BLE_HCI_EDTT_EVT         0x04

#define BT_HCI_OP_VS_WRITE_BD_ADDR 0xFC06

/* Callbacks for sending commands and acl data to ble_ll task */
static ble_hci_trans_rx_cmd_fn *ble_hci_edtt_rx_cmd_cb;
static void *ble_hci_edtt_rx_cmd_arg;
static ble_hci_trans_rx_acl_fn *ble_hci_edtt_rx_acl_cb;
static void *ble_hci_edtt_rx_acl_arg;

/* Memory pool for hci events (high prio). 16 blocks x 70 bytes */
static struct os_mempool ble_hci_edtt_evt_hi_pool;
static os_membuf_t ble_hci_edtt_evt_hi_buf[
    OS_MEMPOOL_SIZE(MYNEWT_VAL(BLE_HCI_EVT_HI_BUF_COUNT),
                    MYNEWT_VAL(BLE_HCI_EVT_BUF_SIZE))
];

/* Memory pool for hci events (low prio). 16 blocks x 70 bytes */
static struct os_mempool ble_hci_edtt_evt_lo_pool;
static os_membuf_t ble_hci_edtt_evt_lo_buf[
    OS_MEMPOOL_SIZE(MYNEWT_VAL(BLE_HCI_EVT_LO_BUF_COUNT),
                    MYNEWT_VAL(BLE_HCI_EVT_BUF_SIZE))
];

/* Memory pool for hci commands. Only 1 block, so supports only 1 command at once. */
static struct os_mempool ble_hci_edtt_cmd_pool;
static os_membuf_t ble_hci_edtt_cmd_buf[
    OS_MEMPOOL_SIZE(1, BLE_HCI_TRANS_CMD_SZ)
];

/*
 * The MBUF payload size must accommodate the HCI data header size plus the
 * maximum ACL data packet length. The ACL block size is the size of the
 * mbufs we will allocate.
 */
#define ACL_BLOCK_SIZE  OS_ALIGN(MYNEWT_VAL(BLE_ACL_BUF_SIZE) \
                                 + BLE_MBUF_MEMBLOCK_OVERHEAD \
                                 + BLE_HCI_DATA_HDR_SZ, OS_ALIGNMENT)

/* mbuf pool for acl data. 15 buffers x (255 bytes + some hdrs len)  */
static struct os_mbuf_pool ble_hci_edtt_acl_mbuf_pool;
static struct os_mempool_ext ble_hci_edtt_acl_pool;
static os_membuf_t ble_hci_edtt_acl_buf[
    OS_MEMPOOL_SIZE(MYNEWT_VAL(BLE_ACL_BUF_COUNT),
                    ACL_BLOCK_SIZE)
];

/* A packet for queueing EDTT/HCI commands and events */
struct ble_hci_edtt_pkt {
    struct os_event ev;
    STAILQ_ENTRY(ble_hci_edtt_pkt) next;
    uint32_t timestamp;
    uint8_t type;
    void *data;
};

static struct os_eventq edtt_q_svc;
static struct os_eventq edtt_q_data;
static struct os_eventq edtt_q_event;
static uint8_t edtt_q_event_count;

static uint16_t waiting_opcode;
static enum commands_t waiting_response;

static struct os_task edtt_poller_task;
static struct os_task edtt_service_task;

#if EDTT_HCI_LOGS
extern unsigned int global_device_nbr;
static FILE *fptr;

static void
log_hex_array(uint8_t *buf, int len)
{
    int i;
    for (i = 0; i < len; i++) {
        fprintf(fptr, "%02X ", buf[i]);
    }
}

static void
log_hci_cmd(uint16_t opcode, uint8_t *buf, int len)
{
    if (fptr) {
        fprintf(fptr, "> %llu %02d %02d ", now, BLE_HCI_OCF(opcode), (BLE_HCI_OGF(opcode)));
        log_hex_array(buf, len);
        fputs("\n\n", fptr);
        fflush(fptr);
    }
}

static void
log_hci_evt(struct ble_hci_ev *hdr)
{
    if (fptr) {
        fprintf(fptr, "< %llu %02d ", now, hdr->opcode);
        log_hex_array(hdr->data, hdr->length);
        fputs("\n\n", fptr);
        fflush(fptr);
    }
}

static void
log_hci_init()
{
    int flen = (int) strlen(MYNEWT_VAL(EDTT_HCI_LOG_FILE)) + 7;
    char *fpath = (char *) calloc(flen, sizeof(char));
    sprintf(fpath, "%s_%02d.log", MYNEWT_VAL(EDTT_HCI_LOG_FILE), global_device_nbr);
    fptr = fopen(fpath, "w");
    free(fpath);
}
#endif

/**
 * Allocates a buffer (mbuf) for ACL operation.
 *
 * @return                      The allocated buffer on success;
 *                              NULL on buffer exhaustion.
 */
static struct os_mbuf *
ble_hci_trans_acl_buf_alloc(void)
{
    struct os_mbuf *m;
    uint8_t usrhdr_len;

#if MYNEWT_VAL(BLE_CONTROLLER)
    usrhdr_len = sizeof(struct ble_mbuf_hdr);
#else
    usrhdr_len = 0;
#endif

    m = os_mbuf_get_pkthdr(&ble_hci_edtt_acl_mbuf_pool, usrhdr_len);
    return m;
}

static int
ble_hci_edtt_acl_tx(struct os_mbuf *om)
{
    struct ble_hci_edtt_pkt *pkt;

    /* If this packet is zero length, just free it */
    if (OS_MBUF_PKTLEN(om) == 0) {
        os_mbuf_free_chain(om);
        return 0;
    }

    pkt = calloc(1, sizeof(*pkt));
    pkt->type = BLE_HCI_EDTT_ACL;
    pkt->data = om;

    os_eventq_put(&edtt_q_svc, &pkt->ev);

    return 0;
}

static int
ble_hci_edtt_cmdevt_tx(uint8_t *hci_ev, uint8_t edtt_type)
{
    struct ble_hci_edtt_pkt *pkt;

    pkt = calloc(1, sizeof(*pkt));
    pkt->type = edtt_type;
    pkt->data = hci_ev;

    os_eventq_put(&edtt_q_svc, &pkt->ev);

    return 0;
}

static void
ble_hci_edtt_set_rx_cbs(ble_hci_trans_rx_cmd_fn *cmd_cb,
                        void *cmd_arg,
                        ble_hci_trans_rx_acl_fn *acl_cb,
                        void *acl_arg)
{
    ble_hci_edtt_rx_cmd_cb = cmd_cb;
    ble_hci_edtt_rx_cmd_arg = cmd_arg;
    ble_hci_edtt_rx_acl_cb = acl_cb;
    ble_hci_edtt_rx_acl_arg = acl_arg;
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

    rc = ble_hci_edtt_cmdevt_tx(cmd, BLE_HCI_EDTT_EVT);
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

    rc = ble_hci_edtt_acl_tx(om);
    return rc;
}

int
ble_hci_trans_hs_cmd_tx(uint8_t *cmd)
{
    int rc;

    rc = ble_hci_edtt_cmdevt_tx(cmd, BLE_HCI_EDTT_CMD);
    return rc;
}

int
ble_hci_trans_hs_acl_tx(struct os_mbuf *om)
{
    int rc;

    rc = ble_hci_edtt_acl_tx(om);
    return rc;
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
ble_hci_trans_buf_alloc(int type) {
    uint8_t *buf;

    switch (type) {
        case BLE_HCI_TRANS_BUF_CMD:
            buf = os_memblock_get(&ble_hci_edtt_cmd_pool);
            break;
        case BLE_HCI_TRANS_BUF_EVT_HI:
            buf = os_memblock_get(&ble_hci_edtt_evt_hi_pool);
            if (buf == NULL) {
                /* If no high-priority event buffers remain, try to grab a
                 * low-priority one.
                 */
                buf = os_memblock_get(&ble_hci_edtt_evt_lo_pool);
            }
            break;

        case BLE_HCI_TRANS_BUF_EVT_LO:
            buf = os_memblock_get(&ble_hci_edtt_evt_lo_pool);
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

    /*
     * XXX: this may look a bit odd, but the controller uses the command
     * buffer to send back the command complete/status as an immediate
     * response to the command. This was done to insure that the controller
     * could always send back one of these events when a command was received.
     * Thus, we check to see which pool the buffer came from so we can free
     * it to the appropriate pool
     */
    if (os_memblock_from(&ble_hci_edtt_evt_hi_pool, buf)) {
        rc = os_memblock_put(&ble_hci_edtt_evt_hi_pool, buf);
        assert(rc == 0);
    } else if (os_memblock_from(&ble_hci_edtt_evt_lo_pool, buf)) {
        rc = os_memblock_put(&ble_hci_edtt_evt_lo_pool, buf);
        assert(rc == 0);
    } else {
        assert(os_memblock_from(&ble_hci_edtt_cmd_pool, buf));
        rc = os_memblock_put(&ble_hci_edtt_cmd_pool, buf);
        assert(rc == 0);
    }
}

int
ble_hci_trans_set_acl_free_cb(os_mempool_put_fn *cb, void *arg)
{
    ble_hci_edtt_acl_pool.mpe_put_cb = cb;
    ble_hci_edtt_acl_pool.mpe_put_arg = arg;
    return 0;
}

void
ble_hci_trans_cfg_ll(ble_hci_trans_rx_cmd_fn *cmd_cb,
                     void *cmd_arg,
                     ble_hci_trans_rx_acl_fn *acl_cb,
                     void *acl_arg)
{
    ble_hci_edtt_set_rx_cbs(cmd_cb, cmd_arg, acl_cb, acl_arg);
}

/**
 * @brief Clean out excess bytes from the input buffer
 */
static void
read_excess_bytes(uint16_t size)
{
    if (size > 0) {
        uint8_t buffer[size];

        edtt_read((uint8_t *) buffer, size, EDTTT_BLOCK);
        bs_trace_raw_time(3, "command size wrong! (%u extra bytes removed)", size);
    }
}

/**
 * @brief Provide an error response when an HCI command send failed
 */
static void
error_response(int error)
{
    uint16_t response = waiting_response;
    int le_error = error;
    uint16_t size = sizeof(le_error);

    edtt_write((uint8_t *) &response, sizeof(response), EDTTT_BLOCK);
    edtt_write((uint8_t *) &size, sizeof(size), EDTTT_BLOCK);
    edtt_write((uint8_t *) &le_error, sizeof(le_error), EDTTT_BLOCK);
    waiting_response = CMD_NOTHING;
    waiting_opcode = 0;
}

/**
 * @brief Allocate buffer for HCI command, fill in parameters and send the
 * command
 */
static int
send_hci_cmd_to_ctrl(uint16_t opcode, uint8_t param_len, uint16_t response) {
    struct ble_hci_cmd *buf;
    int err = 0;
    waiting_response = response;
    waiting_opcode = opcode;

    buf = (void *) ble_hci_trans_buf_alloc(BLE_HCI_TRANS_BUF_CMD);

    if (buf != NULL) {
        buf->opcode = opcode;
        buf->length = param_len;

        if (param_len) {
            edtt_read((uint8_t *) buf->data, param_len, EDTTT_BLOCK);
        }

#if EDTT_HCI_LOGS
        log_hci_cmd(opcode, buf->data, param_len);
#endif

        err = ble_hci_edtt_rx_cmd_cb((uint8_t *) buf, NULL);
        if (err) {
            ble_hci_trans_buf_free((uint8_t *) buf);
            bs_trace_raw_time(3, "Failed to send HCI command %d (err %d)", opcode, err);
            error_response(err);
        }
    } else {
        bs_trace_raw_time(3, "Failed to create buffer for HCI command 0x%04x", opcode);
        error_response(-1);
    }
    return err;
}

/**
 * @brief Echo function - echo input received
 */
static void
echo(uint16_t size)
{
    uint16_t response = CMD_ECHO_RSP;

    edtt_write((uint8_t *) &response, sizeof(response), EDTTT_BLOCK);
    edtt_write((uint8_t *) &size, sizeof(size), EDTTT_BLOCK);

    if (size > 0) {
        uint8_t buff[size];

        edtt_read(buff, size, EDTTT_BLOCK);
        edtt_write(buff, size, EDTTT_BLOCK);
    }
}

/**
 * @brief Handle Command Complete HCI event
 */
static void
command_complete(struct ble_hci_ev *evt)
{
    struct ble_hci_ev_command_complete *evt_cc = (void *) evt->data;
    uint16_t response = waiting_response;
    uint16_t size = evt->length - sizeof(evt_cc->num_packets) - sizeof(evt_cc->opcode);

    if (evt_cc->opcode == waiting_opcode) {
        bs_trace_raw_time(9, "Command complete for 0x%04x", waiting_opcode);

        edtt_write((uint8_t *) &response, sizeof(response), EDTTT_BLOCK);
        edtt_write((uint8_t *) &size, sizeof(size), EDTTT_BLOCK);
        edtt_write((uint8_t *) &evt_cc->status, sizeof(evt_cc->status), EDTTT_BLOCK);
        edtt_write((uint8_t *) &evt_cc->return_params, size - sizeof(evt_cc->status), EDTTT_BLOCK);
        waiting_opcode = 0;
    } else {
        bs_trace_raw_time(5, "Not waiting for 0x(%04x) command status,"
                             " expected 0x(%04x)", evt_cc->opcode, waiting_opcode);
    }
}

/**
 * @brief Handle Command Status HCI event
 */
static void
command_status(struct ble_hci_ev *evt)
{
    struct ble_hci_ev_command_status *evt_cs = (void *) evt->data;
    uint16_t opcode = evt_cs->opcode;
    uint16_t response = waiting_response;
    uint16_t size;

    size = evt->length - sizeof(evt_cs->num_packets) - sizeof(evt_cs->opcode);

    if (opcode == waiting_opcode) {
        bs_trace_raw_time(9, "Command status for 0x%04x", waiting_opcode);

        edtt_write((uint8_t *) &response, sizeof(response), EDTTT_BLOCK);
        edtt_write((uint8_t *) &size, sizeof(size), EDTTT_BLOCK);
        edtt_write((uint8_t *) &evt_cs->status, sizeof(evt_cs->status), EDTTT_BLOCK);
        edtt_write((uint8_t *) &evt_cs->num_packets, size - sizeof(evt_cs->status), EDTTT_BLOCK);
        waiting_opcode = 0;
    } else {
        bs_trace_raw_time(5, "Not waiting for 0x(%04x) command status,"
                             " expected 0x(%04x)", opcode, waiting_opcode);
    }
}

static void
free_data(struct ble_hci_edtt_pkt *pkt)
{
    assert(pkt);
    os_mbuf_free_chain(pkt->data);
    free(pkt);
}

static void
free_event(struct ble_hci_edtt_pkt *pkt)
{
    assert(pkt);
    ble_hci_trans_buf_free((void *)pkt->data);
    free(pkt);
}

/**
 * @brief Allocate and store an event in the event queue
 */
static struct ble_hci_edtt_pkt *
queue_event(struct ble_hci_ev *evt)
{
    struct ble_hci_edtt_pkt *pkt;

    pkt = calloc(1, sizeof(*pkt));
    assert(pkt);
    pkt->timestamp = tm_get_hw_time();
    pkt->type = BLE_HCI_EDTT_EVT;
    pkt->data = evt;

    os_eventq_put(&edtt_q_event, &pkt->ev);
    edtt_q_event_count++;

    return pkt;
}

static struct ble_hci_edtt_pkt *
queue_data(struct os_mbuf *om)
{
    struct ble_hci_edtt_pkt *pkt;

    pkt = calloc(1, sizeof(*pkt));
    assert(pkt);
    pkt->timestamp = tm_get_hw_time();
    pkt->type = BLE_HCI_EDTT_ACL;
    pkt->data = om;

    os_eventq_put(&edtt_q_data, &pkt->ev);

    return pkt;
}

/**
 * @brief Thread to service events and ACL data packets from the HCI input queue
 */
static void
service_events(void *arg)
{
    struct ble_hci_edtt_pkt *pkt;
    struct ble_hci_ev *evt;

    while (1) {
        pkt = (void *)os_eventq_get(&edtt_q_svc);

        if (pkt->type == BLE_HCI_EDTT_EVT) {
            evt = (void *)pkt->data;

#if EDTT_HCI_LOGS
            log_hci_evt(hdr);
#endif

            /* Prepare and send EDTT events */
            switch (evt->opcode) {
            case BLE_HCI_EVCODE_COMMAND_COMPLETE:
                queue_event(evt);
                command_complete(evt);
                break;
            case BLE_HCI_EVCODE_COMMAND_STATUS:
                queue_event(evt);
                command_status(evt);
                break;
            case BLE_HCI_EVCODE_NUM_COMP_PKTS:
                /* EDTT does not handle this event and treats like fail */
            case BLE_HCI_OPCODE_NOP:
                /* Ignore noop bytes from Link layer */
                ble_hci_trans_buf_free((void *)evt);
                break;
            default:
                /* Queue HCI events. We will send them to EDTT
                 * on CMD_GET_EVENT_REQ. */
                queue_event(evt);
            }
        } else if (pkt->type == BLE_HCI_EDTT_ACL) {
            queue_data(pkt->data);
        }

        free(pkt);
    }
}

/**
 * @brief Flush all HCI events from the input-copy queue
 */
static void
flush_events(uint16_t size)
{
    uint16_t response = CMD_FLUSH_EVENTS_RSP;
    struct ble_hci_edtt_pkt *pkt;

    while ((pkt = (void *)os_eventq_get_no_wait(&edtt_q_event))) {
        free_event(pkt);
        edtt_q_event_count--;
    }
    read_excess_bytes(size);
    size = 0;

    edtt_write((uint8_t *)&response, sizeof(response), EDTTT_BLOCK);
    edtt_write((uint8_t *)&size, sizeof(size), EDTTT_BLOCK);
}

/**
 * @brief Get next available HCI event from the input-copy queue
 */
static void
get_event(uint16_t size)
{
    uint16_t response = CMD_GET_EVENT_RSP;
    struct ble_hci_edtt_pkt *pkt;
    struct ble_hci_ev *evt;

    read_excess_bytes(size);
    size = 0;

    edtt_write((uint8_t *)&response, sizeof(response), EDTTT_BLOCK);
    pkt = (void*)os_eventq_get(&edtt_q_event);
    if (pkt) {
        evt = pkt->data;
        size = sizeof(pkt->timestamp) + sizeof(*evt) + evt->length;

        edtt_write((uint8_t *)&size, sizeof(size), EDTTT_BLOCK);
        edtt_write((uint8_t *)&pkt->timestamp, sizeof(pkt->timestamp), EDTTT_BLOCK);
        edtt_write((uint8_t *)evt, sizeof(*evt) + evt->length, EDTTT_BLOCK);

        free_event(pkt);

        edtt_q_event_count--;
    } else {
        edtt_write((uint8_t *)&size, sizeof(size), EDTTT_BLOCK);
    }
}

/**
 * @brief Get next available HCI events from the input-copy queue
 */
static void
get_events(uint16_t size)
{
    uint16_t response = CMD_GET_EVENT_RSP;
    struct ble_hci_edtt_pkt *pkt;
    struct ble_hci_ev *evt;
    uint8_t count = edtt_q_event_count;

    read_excess_bytes(size);
    size = 0;

    edtt_write((uint8_t *)&response, sizeof(response), EDTTT_BLOCK);
    edtt_write((uint8_t *)&count, sizeof(count), EDTTT_BLOCK);

    while (count--) {
        pkt = (void *)os_eventq_get_no_wait(&edtt_q_event);
        assert(pkt);
        evt = pkt->data;
        size = sizeof(pkt->timestamp) + sizeof(*evt) + evt->length;

        edtt_write((uint8_t *)&size, sizeof(size), EDTTT_BLOCK);
        edtt_write((uint8_t *)&pkt->timestamp, sizeof(pkt->timestamp), EDTTT_BLOCK);
        edtt_write((uint8_t *)evt, sizeof(*evt) + evt->length, EDTTT_BLOCK);

        free_event(pkt);

        edtt_q_event_count--;
    }
}

/**
 * @brief Check whether an HCI event is available in the input-copy queue
 */
static void
has_event(uint16_t size)
{
    struct has_event_resp {
        uint16_t response;
        uint16_t size;
        uint8_t count;
    } __attribute__((packed));
    struct has_event_resp le_response = {
        .response = CMD_HAS_EVENT_RSP,
        .size = 1,
        .count = edtt_q_event_count
    };

    if (size > 0) {
        read_excess_bytes(size);
    }
    edtt_write((uint8_t *) &le_response, sizeof(le_response), EDTTT_BLOCK);
}

/**
 * @brief Flush all ACL Data Packages from the input-copy queue
 */
static void
le_flush_data(uint16_t size)
{
    uint16_t response = CMD_LE_FLUSH_DATA_RSP;
    struct ble_hci_edtt_pkt *pkt;

    while ((pkt = (void *)os_eventq_get_no_wait(&edtt_q_data))) {
        free_data(pkt);
    }

    read_excess_bytes(size);
    size = 0;

    edtt_write((uint8_t *)&response, sizeof(response), EDTTT_BLOCK);
    edtt_write((uint8_t *)&size, sizeof(size), EDTTT_BLOCK);
}

/**
 * @brief Check whether an ACL Data Package is available in the input-copy queue
 */
static void
le_data_ready(uint16_t size)
{
    struct has_data_resp {
        uint16_t response;
        uint16_t size;
        uint8_t empty;
    } __attribute__((packed));
    struct has_data_resp le_response = {
        .response = CMD_LE_DATA_READY_RSP,
        .size = 1,
        .empty = 0
    };

    if (size > 0) {
        read_excess_bytes(size);
    }

    /* There's no API to check if eventq is empty but a little hack will do... */
    if (edtt_q_data.evq_list.stqh_first == NULL) {
        le_response.empty = 1;
    }

    edtt_write((uint8_t *) &le_response, sizeof(le_response), EDTTT_BLOCK);
}

/**
 * @brief Get next available HCI Data Package from the input-copy queue
 */
static void
le_data_read(uint16_t size)
{
    uint16_t response = CMD_LE_DATA_READ_RSP;
    struct ble_hci_edtt_pkt *pkt;
    struct os_mbuf *om;

    read_excess_bytes(size);
    size = 0;

    edtt_write((uint8_t *)&response, sizeof(response), EDTTT_BLOCK);
    pkt = (void *)os_eventq_get(&edtt_q_data);
    if (pkt) {
        om = pkt->data;
        size = OS_MBUF_PKTLEN(om);
        edtt_write((uint8_t *)&size, sizeof(size), EDTTT_BLOCK);

        while (om != NULL) {
            edtt_write((uint8_t *)om->om_data, om->om_len, EDTTT_BLOCK);
            om = SLIST_NEXT(om, om_next);
        }

        free_data(pkt);
    } else {
        edtt_write((uint8_t *)&size, sizeof(size), EDTTT_BLOCK);
    }
}

/**
 * @brief Write ACL Data Package to the Controller
 */
static void
le_data_write(uint16_t size)
{
    struct data_write_resp {
        uint16_t code;
        uint16_t size;
        uint8_t status;
    } __attribute__((packed));
    struct data_write_resp response = {
        .code = CMD_LE_DATA_WRITE_RSP,
        .size = 1,
        .status = 0
    };
    struct os_mbuf *om;
    struct hci_data_hdr hdr;
    int err;

    if (size >= sizeof(hdr)) {
        edtt_read((uint8_t *) &hdr, sizeof(hdr), EDTTT_BLOCK);
        size -= sizeof(hdr);
        om = ble_hci_trans_acl_buf_alloc();

        if (om) {
            memcpy(OS_MBUF_USRHDR(om), &hdr, sizeof(hdr));
            uint16_t hdr_length = hdr.hdh_len;

            if (size >= hdr_length) {
                edtt_read(om->om_data, hdr_length, EDTTT_BLOCK);
                size -= hdr_length;
            }

            err = ble_hci_edtt_rx_acl_cb(om, NULL);
            if (err) {
                bs_trace_raw_time(3, "Failed to send ACL Data (err %d)", err);
            }
        } else {
            err = -2; /* Failed to allocate data buffer */
            bs_trace_raw_time(3, "Failed to create buffer for ACL Data.");
        }
    } else {
        /* Size too small for header (handle and data length) */
        err = -3;
    }
    read_excess_bytes(size);

    response.status = err;
    edtt_write((uint8_t *) &response, sizeof(response), EDTTT_BLOCK);
}

static void
fake_set_public_address()
{
    struct ble_hci_ev_command_complete *ev;
    struct ble_hci_ev *hci_ev;
    waiting_opcode = BT_HCI_OP_VS_WRITE_BD_ADDR;
    waiting_response = CMD_WRITE_BD_ADDR_RSP;

    hci_ev = (void *) ble_hci_trans_buf_alloc(BLE_HCI_TRANS_BUF_EVT_HI);
    if (hci_ev) {
        hci_ev->opcode = BLE_HCI_EVCODE_COMMAND_COMPLETE;
        hci_ev->length = sizeof(*ev);
        ev = (void *) hci_ev->data;

        ev->num_packets = 1;
        ev->opcode = waiting_opcode;
        ev->status = 0;
        ble_hci_edtt_cmdevt_tx((uint8_t *) hci_ev, BLE_HCI_EDTT_EVT);
    }
}

/* Reads and executes EDTT commands. */
static void
edtt_poller(void *arg) {
    uint16_t command;
    uint16_t size;
    uint16_t opcode;

    /* Initialize HCI command opcode and response variables */
    waiting_opcode = 0;
    waiting_response = CMD_NOTHING;
    edtt_q_event_count = 0;

    /* Initialize and start EDTT system */
    enable_edtt_mode();
    set_edtt_autoshutdown(true);
    edtt_start();

#if EDTT_HCI_LOGS
    log_hci_init();
#endif

    while (1) {
        edtt_read((uint8_t *) &command, sizeof(command), EDTTT_BLOCK);
        edtt_read((uint8_t *) &size, sizeof(size), EDTTT_BLOCK);

        bs_trace_raw_time(4, "command 0x%04X received (size %u) "
                             "events=%u\n",
                          command, size, edtt_q_event_count);

        switch (command) {
            case CMD_ECHO_REQ:
                echo(size);
                break;
            case CMD_FLUSH_EVENTS_REQ:
                flush_events(size);
                break;
            case CMD_HAS_EVENT_REQ:
                has_event(size);
                break;
            case CMD_GET_EVENT_REQ: {
                uint8_t multiple;

                edtt_read((uint8_t *) &multiple, sizeof(multiple), EDTTT_BLOCK);
                if (multiple)
                    get_events(--size);
                else
                    get_event(--size);
            }
                break;
            case CMD_LE_FLUSH_DATA_REQ:
                le_flush_data(size);
                break;
            case CMD_LE_DATA_READY_REQ:
                le_data_ready(size);
                break;
            case CMD_LE_DATA_WRITE_REQ:
                le_data_write(size);
                break;
            case CMD_LE_DATA_READ_REQ:
                le_data_read(size);
                break;
            case CMD_WRITE_BD_ADDR_REQ:
                edtt_read((uint8_t *) &opcode, sizeof(opcode), EDTTT_BLOCK);

                if (opcode == BT_HCI_OP_VS_WRITE_BD_ADDR) {
                    fake_set_public_address();
                    read_excess_bytes(size - 2);
                }
                break;
            default:
                if (size >= 2) {
                    edtt_read((uint8_t *) &opcode, sizeof(opcode), EDTTT_BLOCK);
                    send_hci_cmd_to_ctrl(opcode, size - 2, command + 1);
                }
        }
    }
}

int
edtt_init(void)
{
    os_stack_t dummy_stack;
    int rc;

    rc = os_task_init(&edtt_poller_task, "edttpoll", edtt_poller, NULL,
                      MYNEWT_VAL(EDTT_POLLER_PRIO), OS_WAIT_FOREVER,
                      &dummy_stack, 1);
    assert(rc == 0);

    rc = os_task_init(&edtt_service_task, "edttsvc", service_events, NULL,
                      MYNEWT_VAL(EDTT_POLLER_PRIO) + 1, OS_WAIT_FOREVER,
                      &dummy_stack, 1);
    assert(rc == 0);

    return 0;
}

/**
 * Initializes the EDTT HCI transport module.
 *
 * @return                      0 on success;
 *                              A BLE_ERR_[...] error code on failure.
 */
void
ble_hci_edtt_init(void)
{
    int rc;

    /* Ensure this function only gets called by sysinit. */
    SYSINIT_ASSERT_ACTIVE();

    rc = os_mempool_ext_init(&ble_hci_edtt_acl_pool,
                             MYNEWT_VAL(BLE_ACL_BUF_COUNT),
                             ACL_BLOCK_SIZE,
                             ble_hci_edtt_acl_buf,
                             "ble_hci_edtt_acl_pool");
    SYSINIT_PANIC_ASSERT(rc == 0);

    rc = os_mbuf_pool_init(&ble_hci_edtt_acl_mbuf_pool,
                           &ble_hci_edtt_acl_pool.mpe_mp,
                           ACL_BLOCK_SIZE,
                           MYNEWT_VAL(BLE_ACL_BUF_COUNT));
    SYSINIT_PANIC_ASSERT(rc == 0);

    /*
     * Create memory pool of HCI command buffers. NOTE: we currently dont
     * allow this to be configured. The controller will only allow one
     * outstanding command. We decided to keep this a pool in case we allow
     * allow the controller to handle more than one outstanding command.
     */
    rc = os_mempool_init(&ble_hci_edtt_cmd_pool,
                         1,
                         BLE_HCI_TRANS_CMD_SZ,
                         ble_hci_edtt_cmd_buf,
                         "ble_hci_edtt_cmd_pool");
    SYSINIT_PANIC_ASSERT(rc == 0);

    rc = os_mempool_init(&ble_hci_edtt_evt_hi_pool,
                         MYNEWT_VAL(BLE_HCI_EVT_HI_BUF_COUNT),
                         MYNEWT_VAL(BLE_HCI_EVT_BUF_SIZE),
                         ble_hci_edtt_evt_hi_buf,
                         "ble_hci_edtt_evt_hi_pool");
    SYSINIT_PANIC_ASSERT(rc == 0);

    rc = os_mempool_init(&ble_hci_edtt_evt_lo_pool,
                         MYNEWT_VAL(BLE_HCI_EVT_LO_BUF_COUNT),
                         MYNEWT_VAL(BLE_HCI_EVT_BUF_SIZE),
                         ble_hci_edtt_evt_lo_buf,
                         "ble_hci_edtt_evt_lo_pool");
    SYSINIT_PANIC_ASSERT(rc == 0);

    SYSINIT_PANIC_ASSERT_MSG(rc == 0, "Failure configuring edtt HCI");

    os_eventq_init(&edtt_q_svc);
    os_eventq_init(&edtt_q_event);
    os_eventq_init(&edtt_q_data);
}
