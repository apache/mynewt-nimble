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
#include "os/os.h"
#include "nimble/ble.h"
#include "nimble/hci_common.h"
#include "controller/ll_adv.h"

/* XXX: not sure where to put these */
extern struct os_mempool g_hci_cmd_pool;
extern struct os_mempool g_hci_os_event_pool;
/* XXX */

/* XXX: We have to send a command complete event or a command status */
static int
ll_hci_event_send(uint8_t event_id, uint8_t err)
{
    return 0;
}

/* XXX: we need to respond with complete events */
int
ll_hci_le_cmd_proc(uint8_t *dptr, uint8_t len, uint16_t ocf)
{
    int rc;

    /* Assume error; if all pass rc gets set to 0 */
    rc = BLE_ERR_INV_HCI_CMD_PARMS;

    switch (ocf) {
    case BLE_HCI_OCF_LE_SET_SCAN_RSP_DATA:
        if (len > 0) {
            --len;
            rc = ll_adv_set_scan_rsp_data(dptr, len);
        }
        break;
    case BLE_HCI_OCF_LE_SET_ADV_ENABLE:
        /* Length should be one byte */
        if (len == BLE_HCI_CMD_SET_ADV_ENABLE_LEN) {
            rc = ll_adv_set_enable(dptr);
        }
        break;
    case BLE_HCI_OCF_LE_SET_ADV_DATA:
        if (len > 0) {
            --len;
            rc = ll_adv_set_adv_data(dptr, len);
        }
        break;
    case BLE_HCI_OCF_LE_SET_ADV_PARAMS:
        /* Length should be one byte */
        if (len == BLE_HCI_CMD_SET_ADV_PARAM_LEN) {
            rc = ll_adv_set_adv_params(dptr);
        }
        break;
    case BLE_HCI_OCF_LE_SET_RAND_ADDR:
        if (len == BLE_DEV_ADDR_LEN) {
            rc = ll_adv_set_rand_addr(dptr);
        }
        break;
    case BLE_HCI_OCF_LE_RD_ADV_CHAN_TXPWR:
        break;
        default:
        /* XXX: deal with unsupported command */
        break;
    }

    /* XXX: while debugging, just assert if there are errors */
    assert(rc == 0);

    /* XXX: We have to send a command complete event or a command status */
    ll_hci_event_send(0, rc);

    return rc;
}

void
ll_hci_cmd_proc(struct os_event *ev)
{
    uint8_t len;
    uint8_t ogf;
    uint8_t *dptr;
    uint16_t opcode;
    uint16_t ocf;
    os_error_t err;

    /* The command buffer is the event argument */
    dptr = (uint8_t *)ev->ev_arg;
    assert(dptr != NULL);

    /* Get the opcode from the command buffer */
    opcode = le16toh(dptr);
    ocf = BLE_HCI_OCF(opcode);
    ogf = BLE_HCI_OGF(opcode);

    /* Get the length of all the parameters */
    len = dptr[sizeof(uint16_t)];

    switch (ogf) {
    case BLE_HCI_OGF_LE:
        ll_hci_le_cmd_proc(dptr + BLE_HCI_CMD_HDR_LEN, len, ocf);
        break;
    /* XXX: these are all un-handled */
    default:
        break;
    }

    /* XXX: this is a bit weird. If the controller were separate, these pools
     * would be allocated by the controller. In the shared case, they are
     * allocated by the host. I think we need to create a "transport layer"
     * and have the buffers freed there, or keep a pointer to the pool to free
     * these things.
     */

    /* Free the command buffer */
    err = os_memblock_put(&g_hci_cmd_pool, dptr);
    assert(err == OS_OK);

    /* Free the event */
    err = os_memblock_put(&g_hci_os_event_pool, ev);
    assert(err == OS_OK);
}



