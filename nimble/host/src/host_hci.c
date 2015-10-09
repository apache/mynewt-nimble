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
#include "nimble/hci_common.h"
#include "controller/ll.h" /* XXX: not good that this needs to be included */
#include "controller/ll_adv.h"

#define HCI_CMD_BUFS        (4)
#define HCI_CMD_BUF_SIZE    (260)       /* XXX: temporary, Fix later */
struct os_mempool g_hci_cmd_pool;
os_membuf_t g_hci_cmd_buf[OS_MEMPOOL_SIZE(HCI_CMD_BUFS, HCI_CMD_BUF_SIZE)];

/* XXX: this might be transport layer*/
#define HCI_NUM_OS_EVENTS       (32)
#define HCI_OS_EVENT_BUF_SIZE   (sizeof(struct os_event))

struct os_mempool g_hci_os_event_pool;
os_membuf_t g_hci_os_event_buf[OS_MEMPOOL_SIZE(HCI_NUM_OS_EVENTS, 
                                               HCI_OS_EVENT_BUF_SIZE)];
static uint8_t *
host_hci_le_cmdbuf_get(uint16_t ocf, uint8_t len)
{
    uint8_t *cmd;
    uint16_t opcode;

    cmd = os_memblock_get(&g_hci_cmd_pool);
    if (cmd) {
        opcode = (BLE_HCI_OGF_LE << 10) | ocf;
        htole16(cmd, opcode);
        cmd[2] = len;
    }

    return cmd;
}

/* XXX:
 * this needs the LL event queue from the LL global data. Not sure I
 * would want this here. In fact, I know I dont. This should be a transport
 * layer call and that should do it.
 */
int
host_hci_cmd_send(void *cmdbuf)
{
    os_error_t err;
    struct os_event *ev;

    /* Get an event structure off the queue */
    ev = (struct os_event *)os_memblock_get(&g_hci_os_event_pool);
    if (!ev) {
        err = os_memblock_put(&g_hci_cmd_pool, cmdbuf);
        assert(err == OS_OK);
        return -1;
    }

    /* XXX: Do I need to initialize the tailq entry at all? */
    ev->ev_queued = 0;
    ev->ev_type = BLE_LL_EVENT_HCI_CMD;
    ev->ev_arg = cmdbuf;
    os_eventq_put(&g_ll_data.ll_evq, ev);

    return 0;
}

int
host_hci_cmd_le_set_adv_params(struct hci_adv_params *adv)
{
    int rc;
    uint8_t *cmd;
    uint8_t *dptr;
    uint16_t itvl;

    assert(adv != NULL);

    /* Make sure parameters are valid */
    rc = -1;
    if ((adv->adv_itvl_min > adv->adv_itvl_max) ||
        (adv->adv_itvl_min == adv->adv_itvl_max) ||
        (adv->own_addr_type > BLE_ADV_OWN_ADDR_MAX) ||
        (adv->peer_addr_type > BLE_ADV_PEER_ADDR_MAX) ||
        (adv->adv_filter_policy > BLE_ADV_FILT_MAX) ||
        (adv->adv_type > BLE_ADV_TYPE_MAX) ||
        (adv->adv_channel_map == 0) ||
        ((adv->adv_channel_map & 0xF8) != 0)) {
        /* These parameters are not valid */
        return rc;
    }

    /* Make sure interval is valid for advertising type. */
    if ((adv->adv_type == BLE_ADV_TYPE_ADV_NONCONN_IND) || 
        (adv->adv_type == BLE_ADV_TYPE_ADV_SCAN_IND)) {
        itvl = BLE_LL_ADV_ITVL_NONCONN_MIN;
    } else {
        itvl = BLE_LL_ADV_ITVL_MIN;
    }

    if ((adv->adv_itvl_min < itvl) || 
        (adv->adv_itvl_min > BLE_LL_ADV_ITVL_MAX)) {
        return rc;
    }

    cmd = host_hci_le_cmdbuf_get(BLE_HCI_OCF_LE_SET_ADV_PARAMS, 
                                 BLE_HCI_CMD_SET_ADV_PARAM_LEN);
    if (cmd) {
        dptr = cmd + BLE_HCI_CMD_HDR_LEN;
        htole16(dptr, adv->adv_itvl_min);
        htole16(dptr + 2, adv->adv_itvl_max);
        dptr[4] = adv->adv_type;
        dptr[5] = adv->own_addr_type;
        dptr[6] = adv->peer_addr_type;
        memcpy(dptr + 7, adv->peer_addr, BLE_DEV_ADDR_LEN);
        dptr[13] = adv->adv_channel_map;
        dptr[14] = adv->adv_filter_policy;
        rc = host_hci_cmd_send(cmd);
    }

    return rc;
}

int
host_hci_cmd_le_set_adv_data(uint8_t *data, uint8_t len)
{
    int rc;
    uint8_t *cmd;

    /* Check for valid parameters */
    rc = -1;
    if (((data == NULL) && (len == 0)) || (len > BLE_HCI_MAX_ADV_DATA_LEN)) {
        return rc;
    }

    cmd = host_hci_le_cmdbuf_get(BLE_HCI_OCF_LE_SET_ADV_DATA, len + 1);
    if (cmd) {
        cmd[BLE_HCI_CMD_HDR_LEN] = len;
        memcpy(cmd + BLE_HCI_CMD_HDR_LEN + 1, data, len);
        rc = host_hci_cmd_send(cmd);
    }

    return rc;
}

/**
 * ble host hci cmd le set rand addr
 *  
 * Sets the random address to be used in advertisements. 
 * 
 * @param addr Pointer to the random address to send to device
 * 
 * @return int 
 */
int
host_hci_cmd_le_set_rand_addr(uint8_t *addr)
{
    int rc;
    uint8_t *cmd;

    /* Check for valid parameters */
    rc = -1;
    if (addr ) {
        cmd = host_hci_le_cmdbuf_get(BLE_HCI_OCF_LE_SET_RAND_ADDR,
                                         BLE_DEV_ADDR_LEN);
        if (cmd) {
            memcpy(cmd + BLE_HCI_CMD_HDR_LEN, addr, BLE_DEV_ADDR_LEN);
            rc = host_hci_cmd_send(cmd);
        }
    }

    return rc;
}

int
host_hci_cmd_le_set_event_mask(uint64_t event_mask)
{
    int rc;
    uint8_t *cmd;

    /* XXX: Parameter checking event_mask? */
    rc = -1;
    cmd = host_hci_le_cmdbuf_get(BLE_HCI_OCF_LE_SET_EVENT_MASK, 
                                     sizeof(uint64_t));
    if (cmd) {
        htole64(cmd + BLE_HCI_CMD_HDR_LEN, event_mask);
        rc = host_hci_cmd_send(cmd);
    }

    return rc;
}

int
host_hci_cmd_le_set_adv_enable(uint8_t enable)
{
    int rc;
    uint8_t *cmd;

    /* XXX: parameter error check */
    rc = -1;
    cmd = host_hci_le_cmdbuf_get(BLE_HCI_OCF_LE_SET_ADV_ENABLE, 
                                     BLE_HCI_CMD_SET_ADV_ENABLE_LEN);
    if (cmd) {
        /* Set the data in the command */
        cmd[BLE_HCI_CMD_HDR_LEN] = enable;
        rc = host_hci_cmd_send(cmd);
    }

    return rc;
}

int
host_hci_init(void)
{
    int rc;

    /* Create memory pool of command buffers */
    rc = os_mempool_init(&g_hci_cmd_pool, HCI_CMD_BUFS, HCI_CMD_BUF_SIZE, 
                         &g_hci_cmd_buf, "HCICmdPool");
    assert(rc == 0);

    /* XXX: really only needed by the transport */
    /* Create memory pool of OS events */
    rc = os_mempool_init(&g_hci_os_event_pool, HCI_NUM_OS_EVENTS, 
                         HCI_OS_EVENT_BUF_SIZE, &g_hci_os_event_buf, 
                         "HCIOsEventPool");
    assert(rc == 0);

    return rc;
}
