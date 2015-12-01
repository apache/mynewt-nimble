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
#include "nimble/ble.h"
#include "nimble/hci_common.h"
#include "nimble/hci_transport.h"
#include "controller/ble_ll_adv.h"
#include "controller/ble_ll_scan.h"
#include "controller/ble_ll.h"
#include "controller/ble_ll_hci.h"
#include "controller/ble_ll_whitelist.h"
#include "controller/ble_ll_conn.h"

/* LE event mask */
uint8_t g_ble_ll_hci_le_event_mask[BLE_HCI_SET_LE_EVENT_MASK_LEN];
uint8_t g_ble_ll_hci_event_mask[BLE_HCI_SET_EVENT_MASK_LEN];

/**
 * ll hci get num cmd pkts 
 *  
 * Returns the number of command packets that the host is allowed to send 
 * to the controller. 
 *  
 * @return uint8_t 
 */
static uint8_t
ble_ll_hci_get_num_cmd_pkts(void)
{
    return BLE_LL_CFG_NUM_HCI_CMD_PKTS;
}

/**
 * Send an event to the host. 
 * 
 * @param evbuf Pointer to event buffer to send
 * 
 * @return int 0: success; -1 otherwise.
 */
int
ble_ll_hci_event_send(uint8_t *evbuf)
{
    int rc;

    /* Count number of events sent */
    ++g_ble_ll_stats.hci_events_sent;

    /* Send the event to the host */
    rc = ble_hci_transport_ctlr_event_send(evbuf);

    return rc;
}

/**
 * ll hci set le event mask
 *  
 * Called when the LL controller receives a set LE event mask command.
 *  
 * Context: Link Layer task (HCI command parser) 
 * 
 * @param cmdbuf Pointer to command buf.
 * 
 * @return int BLE_ERR_SUCCESS. Does not return any errors.
 */
static int
ble_ll_hci_set_le_event_mask(uint8_t *cmdbuf)
{
    /* Copy the data into the event mask */
    memcpy(g_ble_ll_hci_le_event_mask, cmdbuf, BLE_HCI_SET_LE_EVENT_MASK_LEN);
    return BLE_ERR_SUCCESS;
}

/**
 * HCI read buffer size command. Returns the ACL data packet length and 
 * num data packets. 
 * 
 * @param rspbuf Pointer to response buffer
 * @param rsplen Length of response buffer
 * 
 * @return int BLE error code
 */
static int
ble_ll_hci_le_read_bufsize(uint8_t *rspbuf, uint8_t *rsplen)
{    
    /* Place the data packet length and number of packets in the buffer */
    htole16(rspbuf, BLE_LL_CFG_ACL_DATA_PKT_LEN);
    rspbuf[2] = BLE_LL_CFG_NUM_ACL_DATA_PKTS;
    *rsplen = BLE_HCI_RD_BUF_SIZE_RSPLEN;
    return BLE_ERR_SUCCESS;
}

/**
 * HCI read local supported features command. Returns the features 
 * supported by the controller.
 * 
 * @param rspbuf Pointer to response buffer
 * @param rsplen Length of response buffer
 * 
 * @return int BLE error code
 */
static int
ble_ll_hci_le_read_local_features(uint8_t *rspbuf, uint8_t *rsplen)
{    
    /* Add list of supported features. */
    memset(rspbuf, 0, BLE_HCI_RD_LOC_SUPP_FEAT_RSPLEN);
    *rsplen = BLE_HCI_RD_LOC_SUPP_FEAT_RSPLEN;
    return BLE_ERR_SUCCESS;
}

/**
 * Checks to see if a LE event has been disabled by the host. 
 * 
 * @param bitpos This is the bit position of the LE event. Note that this can 
 * be a value from 0 to 63, inclusive. 
 * 
 * @return uint8_t 0: event is not enabled; otherwise event is enabled.
 */
uint8_t
ble_ll_hci_is_le_event_enabled(int bitpos)
{
    uint8_t enabled;
    uint8_t bytenum;
    uint8_t bitmask;

    bytenum = bitpos / 8;
    bitmask = 1 << (bitpos & 0x7);
    enabled = g_ble_ll_hci_le_event_mask[bytenum] & bitmask;

    return enabled;
}

/**
 * Process a LE command sent from the host to the controller. The HCI command 
 * has a 3 byte command header followed by data. The header is: 
 *  -> opcode (2 bytes)
 *  -> Length of parameters (1 byte; does include command header bytes).
 * 
 * @param cmdbuf Pointer to command buffer. Points to start of command header.
 * @param ocf    Opcode command field.
 * @param *rsplen Pointer to length of response
 *  
 * @return int  This function returns a BLE error code. If a command status 
 *              event should be returned as opposed to command complete,
 *              256 gets added to the return value.
 */
static int
ble_ll_hci_le_cmd_proc(uint8_t *cmdbuf, uint16_t ocf, uint8_t *rsplen)
{
    int rc;
    uint8_t len;
    uint8_t *rspbuf;

    /* Assume error; if all pass rc gets set to 0 */
    rc = BLE_ERR_INV_HCI_CMD_PARMS;

    /* Get length from command */
    len = cmdbuf[sizeof(uint16_t)];

    /* 
     * The command response pointer points into the same buffer as the
     * command data itself. That is fine, as each command reads all the data
     * before crafting a response.
     */ 
    rspbuf = cmdbuf + BLE_HCI_EVENT_CMD_COMPLETE_MIN_LEN;

    /* Move past HCI command header */
    cmdbuf += BLE_HCI_CMD_HDR_LEN;

    switch (ocf) {
    case BLE_HCI_OCF_LE_SET_EVENT_MASK:
        if (len == BLE_HCI_SET_LE_EVENT_MASK_LEN) {
            rc = ble_ll_hci_set_le_event_mask(cmdbuf);
        }
        break;
    case BLE_HCI_OCF_LE_RD_BUF_SIZE:
        if (len == BLE_HCI_RD_BUF_SIZE_LEN) {
            rc = ble_ll_hci_le_read_bufsize(rspbuf, rsplen);
        }
        break;
    case BLE_HCI_OCF_LE_RD_LOC_SUPP_FEAT:
        if (len == 0) {
            rc = ble_ll_hci_le_read_local_features(rspbuf, rsplen);
        }
        break;
    case BLE_HCI_OCF_LE_SET_RAND_ADDR:
        if (len == BLE_DEV_ADDR_LEN) {
            rc = ble_ll_set_random_addr(cmdbuf);
        }
        break;
    case BLE_HCI_OCF_LE_SET_ADV_PARAMS:
        /* Length should be one byte */
        if (len == BLE_HCI_SET_ADV_PARAM_LEN) {
            rc = ble_ll_adv_set_adv_params(cmdbuf);
        }
        break;
    case BLE_HCI_OCF_LE_RD_ADV_CHAN_TXPWR:
        if (len == 0) {
            rc = ble_ll_adv_read_txpwr(rspbuf, rsplen);
        }
        break;
    case BLE_HCI_OCF_LE_SET_ADV_DATA:
        if (len > 0) {
            --len;
            rc = ble_ll_adv_set_adv_data(cmdbuf, len);
        }
        break;
    case BLE_HCI_OCF_LE_SET_SCAN_RSP_DATA:
        if (len > 0) {
            --len;
            rc = ble_ll_adv_set_scan_rsp_data(cmdbuf, len);
        }
        break;
    case BLE_HCI_OCF_LE_SET_ADV_ENABLE:
        /* Length should be one byte */
        if (len == BLE_HCI_SET_ADV_ENABLE_LEN) {
            rc = ble_ll_adv_set_enable(cmdbuf);
        }
        break;
    case BLE_HCI_OCF_LE_SET_SCAN_ENABLE:
        if (len == BLE_HCI_SET_SCAN_ENABLE_LEN) {
            rc = ble_ll_scan_set_enable(cmdbuf);
        }
        break;
    case BLE_HCI_OCF_LE_SET_SCAN_PARAMS:
        /* Length should be one byte */
        if (len == BLE_HCI_SET_SCAN_PARAM_LEN) {
            rc = ble_ll_scan_set_scan_params(cmdbuf);
        }
        break;
    case BLE_HCI_OCF_LE_CREATE_CONN:
        if (len == BLE_HCI_CREATE_CONN_LEN) {
            rc = ble_ll_conn_create(cmdbuf);
        }
        /* This is a hack; command status gets sent instead of cmd complete */
        rc += (BLE_ERR_MAX + 1);
        break;
    case BLE_HCI_OCF_LE_CREATE_CONN_CANCEL:
        if (len == 0) {
            rc = ble_ll_conn_create_cancel();
        }
        break;
    case BLE_HCI_OCF_LE_CLEAR_WHITE_LIST:
        if (len == 0) {
            rc = ble_ll_whitelist_clear();
        }
        break;
    case BLE_HCI_OCF_LE_RD_WHITE_LIST_SIZE:
        if (len == 0) {
            rc = ble_ll_whitelist_read_size(rspbuf, rsplen);
        }
        break;
    case BLE_HCI_OCF_LE_ADD_WHITE_LIST:
        if (len == BLE_HCI_CHG_WHITE_LIST_LEN) {
            rc = ble_ll_whitelist_add(cmdbuf + 1, cmdbuf[0]);
        }
        break;
    case BLE_HCI_OCF_LE_RMV_WHITE_LIST:
        if (len == BLE_HCI_CHG_WHITE_LIST_LEN) {
            rc = ble_ll_whitelist_rmv(cmdbuf + 1, cmdbuf[0]);
        }
        break;
    default:
        rc = BLE_ERR_UNKNOWN_HCI_CMD;
        break;
    }

    return rc;
}

void
ble_ll_hci_cmd_proc(struct os_event *ev)
{
    int rc;
    uint8_t ogf;
    uint8_t rsplen;
    uint8_t *cmdbuf;
    uint16_t opcode;
    uint16_t ocf;
    os_error_t err;

    /* The command buffer is the event argument */
    cmdbuf = (uint8_t *)ev->ev_arg;
    assert(cmdbuf != NULL);

    /* Free the event */
    err = os_memblock_put(&g_hci_os_event_pool, ev);
    assert(err == OS_OK);

    /* Get the opcode from the command buffer */
    opcode = le16toh(cmdbuf);
    ocf = BLE_HCI_OCF(opcode);
    ogf = BLE_HCI_OGF(opcode);

    /* Assume response length is zero */
    rsplen = 0;

    switch (ogf) {
    case BLE_HCI_OGF_LE:
        rc = ble_ll_hci_le_cmd_proc(cmdbuf, ocf, &rsplen);
        break;
    case BLE_HCI_OGF_CTLR_BASEBAND:
        /* XXX: Implement  */
        rc = BLE_ERR_UNKNOWN_HCI_CMD;
        break;
    default:
        /* XXX: Need to support other OGF. For now, return unsupported */
        rc = BLE_ERR_UNKNOWN_HCI_CMD;
        break;
    }

    /* Make sure valid error code */
    assert(rc >= 0);
    if (rc) {
        ++g_ble_ll_stats.hci_cmd_errs;
    } else {
        ++g_ble_ll_stats.hci_cmds;
    }

    /* If no response is generated, we free the buffers */
    if (rc <= BLE_ERR_MAX) {
        /* Create a command complete event with status from command */
        cmdbuf[0] = BLE_HCI_EVCODE_COMMAND_COMPLETE;
        cmdbuf[1] = 4 + rsplen;
        cmdbuf[2] = ble_ll_hci_get_num_cmd_pkts();
        htole16(cmdbuf + 3, opcode);
        cmdbuf[5] = (uint8_t)rc;
    } else {
        /* Create a command complete event with status from command */
        cmdbuf[0] = BLE_HCI_EVCODE_COMMAND_STATUS;
        cmdbuf[1] = 4;
        cmdbuf[2] = (uint8_t)(rc - (BLE_ERR_MAX + 1));
        cmdbuf[3] = ble_ll_hci_get_num_cmd_pkts();
        htole16(cmdbuf + 4, opcode);
    }

    /* Send the event (events cannot be masked) */
    ble_ll_hci_event_send(cmdbuf);
}

/* XXX: For now, put this here */
int
ble_hci_transport_host_cmd_send(uint8_t *cmd)
{
    os_error_t err;
    struct os_event *ev;

    /* Get an event structure off the queue */
    ev = (struct os_event *)os_memblock_get(&g_hci_os_event_pool);
    if (!ev) {
        err = os_memblock_put(&g_hci_cmd_pool, cmd);
        assert(err == OS_OK);
        return -1;
    }

    /* Fill out the event and post to Link Layer */
    ev->ev_queued = 0;
    ev->ev_type = BLE_LL_EVENT_HCI_CMD;
    ev->ev_arg = cmd;
    os_eventq_put(&g_ble_ll_data.ll_evq, ev);

    return 0;
}

/* Send ACL data from host to contoller */
int
ble_hci_transport_host_acl_data_send(struct os_mbuf *om)
{
    ble_ll_acl_data_in(om);
    return 0;
}

/**
 * Initalize the LL HCI.
 */
void
ble_ll_hci_init(void)
{
    /* Set defaults for LE events: Vol 2 Part E 7.8.1 */
    g_ble_ll_hci_le_event_mask[0] = 0x1f;

    /* Set defaults for controller/baseband events: Vol 2 Part E 7.3.1 */
    g_ble_ll_hci_event_mask[0] = 0xff;
    g_ble_ll_hci_event_mask[1] = 0xff;
    g_ble_ll_hci_event_mask[2] = 0xff;
    g_ble_ll_hci_event_mask[3] = 0xff;
    g_ble_ll_hci_event_mask[4] = 0xff;
    g_ble_ll_hci_event_mask[5] = 0x1f;
}
