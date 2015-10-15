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
#include "controller/ll_adv.h"

/* XXX: not sure where to put these */
extern struct os_mempool g_hci_cmd_pool;
extern struct os_mempool g_hci_os_event_pool;
/* XXX */

/* LE event mask */
uint8_t g_ll_hci_le_event_mask[BLE_HCI_SET_LE_EVENT_MASK_LEN];

/**
 * ll hci get num cmd pkts 
 *  
 * Returns the number of command packets that the host is allowed to send 
 * to the controller. 
 *  
 * XXX: For now, we will return 1. 
 * 
 * @return uint8_t 
 */
static uint8_t
ll_hci_get_num_cmd_pkts(void)
{
    return 1;
}

/* XXX: Not sure where to do it, but we need to check the LE event mask
   before sending a particular type of event. */

/* XXX: we need to implement the sending of the event. For now, I just free
   the memory buffers */
static int
ll_hci_event_send(struct os_event *ev)
{
    os_error_t err;

    /* XXX: For now, just free it all */
    /* Free the command buffer */
    err = os_memblock_put(&g_hci_cmd_pool, ev->ev_arg);
    assert(err == OS_OK);

    /* Free the event */
    err = os_memblock_put(&g_hci_os_event_pool, ev);
    assert(err == OS_OK);
    /* XXX */

    return 0;
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
ll_hci_set_le_event_mask(uint8_t *cmdbuf)
{
    /* Copy the data into the event mask */
    memcpy(g_ll_hci_le_event_mask, cmdbuf, BLE_HCI_SET_LE_EVENT_MASK_LEN);
    return BLE_ERR_SUCCESS;
}

/**
 * ll hci le cmd proc
 * 
 * 
 * @param cmdbuf 
 * @param len 
 * @param ocf 
 * 
 * @return int 
 */
int
ll_hci_le_cmd_proc(uint8_t *cmdbuf, uint8_t len, uint16_t ocf)
{
    int rc;

    /* Assume error; if all pass rc gets set to 0 */
    rc = BLE_ERR_INV_HCI_CMD_PARMS;

    switch (ocf) {
    case BLE_HCI_OCF_LE_SET_EVENT_MASK:
        if (len == BLE_HCI_SET_LE_EVENT_MASK_LEN) {
            rc = ll_hci_set_le_event_mask(cmdbuf);
        }
        break;
    case BLE_HCI_OCF_LE_SET_RAND_ADDR:
        if (len == BLE_DEV_ADDR_LEN) {
            rc = ll_adv_set_rand_addr(cmdbuf);
        }
        break;
    case BLE_HCI_OCF_LE_SET_ADV_PARAMS:
        /* Length should be one byte */
        if (len == BLE_HCI_SET_ADV_PARAM_LEN) {
            rc = ll_adv_set_adv_params(cmdbuf);
        }
        break;
    case BLE_HCI_OCF_LE_RD_ADV_CHAN_TXPWR:
        /* XXX: implement this */
        break;
    case BLE_HCI_OCF_LE_SET_ADV_DATA:
        if (len > 0) {
            --len;
            rc = ll_adv_set_adv_data(cmdbuf, len);
        }
        break;
    case BLE_HCI_OCF_LE_SET_SCAN_RSP_DATA:
        if (len > 0) {
            --len;
            rc = ll_adv_set_scan_rsp_data(cmdbuf, len);
        }
        break;
    case BLE_HCI_OCF_LE_SET_ADV_ENABLE:
        /* Length should be one byte */
        if (len == BLE_HCI_SET_ADV_ENABLE_LEN) {
            rc = ll_adv_set_enable(cmdbuf);
        }
        break;
    default:
        /* XXX: deal with unsupported command */
        break;
    }

    return rc;
}

void
ll_hci_cmd_proc(struct os_event *ev)
{
    int rc;
    uint8_t len;
    uint8_t ogf;
    uint8_t *cmdbuf;
    uint16_t opcode;
    uint16_t ocf;
    os_error_t err;

    /* The command buffer is the event argument */
    cmdbuf = (uint8_t *)ev->ev_arg;
    assert(cmdbuf != NULL);

    /* Get the opcode from the command buffer */
    opcode = le16toh(cmdbuf);
    ocf = BLE_HCI_OCF(opcode);
    ogf = BLE_HCI_OGF(opcode);

    /* Get the length of all the parameters */
    len = cmdbuf[sizeof(uint16_t)];

    switch (ogf) {
    case BLE_HCI_OGF_LE:
        rc = ll_hci_le_cmd_proc(cmdbuf + BLE_HCI_CMD_HDR_LEN, len, ocf);
        break;
    default:
        /* XXX: these are all un-handled. What to do? */
        rc = -1;
        break;
    }

    /* XXX: This assumes controller and host are in same MCU */
    /* If no response is generated, we free the buffers */
    if (rc < 0) {
        /* Free the command buffer */
        err = os_memblock_put(&g_hci_cmd_pool, cmdbuf);
        assert(err == OS_OK);

        /* Free the event */
        err = os_memblock_put(&g_hci_os_event_pool, ev);
        assert(err == OS_OK);
    } else if (rc <= 255) {
        /* Create a command complete event with status from command */
        cmdbuf[0] = BLE_HCI_EVCODE_COMMAND_COMPLETE;
        cmdbuf[1] = 4;    /* Length of the data */
        cmdbuf[2] = ll_hci_get_num_cmd_pkts();
        htole16(cmdbuf + 3, opcode);
        cmdbuf[5] = (uint8_t)rc;

        /* Send the event */
        ll_hci_event_send(ev);
    } else {
        /* XXX: placeholder for sending command status or other events */
    }
}

