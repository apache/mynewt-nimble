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
#include "console/console.h"
#include "nimble/hci_common.h"
#include "nimble/hci_transport.h"

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

/* Host HCI Task Events */
struct os_eventq g_ble_host_hci_evq;
#define BLE_HOST_HCI_EVENT_CTLR_EVENT   (OS_EVENT_T_PERUSER)

/* Statistics */
struct host_hci_stats
{
    uint32_t events_rxd;
};

struct host_hci_stats g_host_hci_stats;


/* Callout for timer */
extern void bletest_execute(void);
struct os_callout_func g_ble_host_hci_timer;

static int
host_hci_cmd_send(uint8_t *cmdbuf)
{
    return ble_hci_transport_host_cmd_send(cmdbuf);
}

static int
host_hci_le_cmd_send(uint16_t ocf, uint8_t len, void *cmddata)
{
    int rc;
    uint8_t *cmd;
    uint16_t opcode;

    rc = -1;
    cmd = os_memblock_get(&g_hci_cmd_pool);
    if (cmd) {
        opcode = (BLE_HCI_OGF_LE << 10) | ocf;
        htole16(cmd, opcode);
        cmd[2] = len;
        if (len) {
            memcpy(cmd + BLE_HCI_CMD_HDR_LEN, cmddata, len);
        }
        rc = host_hci_cmd_send(cmd);
    }

    return rc;
}

int
host_hci_cmd_le_set_adv_params(struct hci_adv_params *adv)
{
    int rc;
    uint16_t itvl;
    uint8_t cmd[BLE_HCI_SET_ADV_PARAM_LEN];

    assert(adv != NULL);

    /* Make sure parameters are valid */
    rc = -1;
    if ((adv->adv_itvl_min > adv->adv_itvl_max) ||
        (adv->adv_itvl_min == adv->adv_itvl_max) ||
        (adv->own_addr_type > BLE_HCI_ADV_OWN_ADDR_MAX) ||
        (adv->peer_addr_type > BLE_HCI_ADV_PEER_ADDR_MAX) ||
        (adv->adv_filter_policy > BLE_HCI_ADV_FILT_MAX) ||
        (adv->adv_type > BLE_HCI_ADV_TYPE_MAX) ||
        (adv->adv_channel_map == 0) ||
        ((adv->adv_channel_map & 0xF8) != 0)) {
        /* These parameters are not valid */
        return rc;
    }

    /* Make sure interval is valid for advertising type. */
    if ((adv->adv_type == BLE_HCI_ADV_TYPE_ADV_NONCONN_IND) || 
        (adv->adv_type == BLE_HCI_ADV_TYPE_ADV_SCAN_IND)) {
        itvl = BLE_HCI_ADV_ITVL_NONCONN_MIN;
    } else {
        itvl = BLE_HCI_ADV_ITVL_MIN;
    }

    /* Do not check if high duty-cycle directed */
    if (adv->adv_type != BLE_HCI_ADV_TYPE_ADV_DIRECT_IND_HD) {
        if ((adv->adv_itvl_min < itvl) || 
            (adv->adv_itvl_min > BLE_HCI_ADV_ITVL_MAX)) {
            return rc;
        }
    }

    htole16(cmd, adv->adv_itvl_min);
    htole16(cmd + 2, adv->adv_itvl_max);
    cmd[4] = adv->adv_type;
    cmd[5] = adv->own_addr_type;
    cmd[6] = adv->peer_addr_type;
    memcpy(cmd + 7, adv->peer_addr, BLE_DEV_ADDR_LEN);
    cmd[13] = adv->adv_channel_map;
    cmd[14] = adv->adv_filter_policy;

    rc = host_hci_le_cmd_send(BLE_HCI_OCF_LE_SET_ADV_PARAMS, 
                              BLE_HCI_SET_ADV_PARAM_LEN, cmd);

    return rc;
}

int
host_hci_cmd_le_set_adv_data(uint8_t *data, uint8_t len)
{
    int rc;
    uint8_t cmd[BLE_HCI_MAX_ADV_DATA_LEN + 1];

    /* Check for valid parameters */
    if (((data == NULL) && (len != 0)) || (len > BLE_HCI_MAX_ADV_DATA_LEN)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    cmd[0] = len;
    memcpy(cmd + 1, data, len);
    rc = host_hci_le_cmd_send(BLE_HCI_OCF_LE_SET_ADV_DATA, len + 1, cmd);

    return rc;
}

int
host_hci_cmd_le_set_scan_rsp_data(uint8_t *data, uint8_t len)
{
    int rc;
    uint8_t cmd[BLE_HCI_MAX_SCAN_RSP_DATA_LEN + 1];

    /* Check for valid parameters */
    if (((data == NULL) && (len != 0)) || (len > BLE_HCI_MAX_SCAN_RSP_DATA_LEN)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    cmd[0] = len;
    memcpy(cmd + 1, data, len);
    rc = host_hci_le_cmd_send(BLE_HCI_OCF_LE_SET_SCAN_RSP_DATA, len + 1, cmd);

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
    uint8_t cmd[BLE_DEV_ADDR_LEN];

    /* Check for valid parameters */
    rc = -1;
    if (addr) {
        rc = host_hci_le_cmd_send(BLE_HCI_OCF_LE_SET_RAND_ADDR,
                                  BLE_DEV_ADDR_LEN, cmd);
    }

    return rc;
}

int
host_hci_cmd_le_set_event_mask(uint64_t event_mask)
{
    int rc;
    uint8_t cmd[sizeof(uint64_t)];

    htole64(cmd, event_mask);
    rc = host_hci_le_cmd_send(BLE_HCI_OCF_LE_SET_EVENT_MASK, sizeof(uint64_t), 
                              cmd);

    return rc;
}

int
host_hci_cmd_le_set_adv_enable(uint8_t enable)
{
    int rc;
    uint8_t cmd[BLE_HCI_SET_ADV_ENABLE_LEN];

    cmd[0] = enable;
    rc = host_hci_le_cmd_send(BLE_HCI_OCF_LE_SET_ADV_ENABLE, 
                              BLE_HCI_SET_ADV_ENABLE_LEN, cmd);

    return rc;
}

int
host_hci_cmd_le_set_scan_params(uint8_t scan_type, uint16_t scan_itvl, 
                                uint16_t scan_window, uint8_t own_addr_type, 
                                uint8_t filter_policy) {
    int rc;
    uint8_t cmd[BLE_HCI_SET_SCAN_PARAM_LEN];

    /* Make sure parameters are valid */
    if ((scan_type != BLE_HCI_SCAN_TYPE_PASSIVE) && 
        (scan_type != BLE_HCI_SCAN_TYPE_ACTIVE)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Check interval and window */
    if ((scan_itvl < BLE_HCI_SCAN_ITVL_MIN) || 
        (scan_itvl > BLE_HCI_SCAN_ITVL_MAX) ||
        (scan_window < BLE_HCI_SCAN_WINDOW_MIN) ||
        (scan_window > BLE_HCI_SCAN_WINDOW_MAX) ||
        (scan_itvl < scan_window)) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Check own addr type */
    if (own_addr_type > BLE_HCI_ADV_OWN_ADDR_MAX) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    /* Check scanner filter policy */
    if (filter_policy > BLE_HCI_SCAN_FILT_MAX) {
        return BLE_ERR_INV_HCI_CMD_PARMS;
    }

    cmd[0] = scan_type;
    htole16(cmd + 1, scan_itvl);
    htole16(cmd + 3, scan_window);
    cmd[5] = own_addr_type;
    cmd[6] = filter_policy;

    rc = host_hci_le_cmd_send(BLE_HCI_OCF_LE_SET_SCAN_PARAMS, 
                              BLE_HCI_SET_SCAN_PARAM_LEN, cmd);

    return rc;
}

int
host_hci_cmd_le_set_scan_enable(uint8_t enable, uint8_t filter_dups)
{
    int rc;
    uint8_t cmd[BLE_HCI_SET_SCAN_ENABLE_LEN];

    cmd[0] = enable;
    cmd[1] = filter_dups;
    rc = host_hci_le_cmd_send(BLE_HCI_OCF_LE_SET_SCAN_ENABLE, 
                              BLE_HCI_SET_SCAN_ENABLE_LEN, cmd);
    return rc;
}

void
host_hci_event_proc(struct os_event *ev)
{
    uint8_t *evbuf;
    os_error_t err;

    /* Count events received */
    ++g_host_hci_stats.events_rxd;

    /* Display to console */
    evbuf = (uint8_t *)ev->ev_arg;
    console_printf("Host received event %u", evbuf[0]);

    /* XXX: Process the event */

    /* Free the command buffer */
    err = os_memblock_put(&g_hci_cmd_pool, ev->ev_arg);
    assert(err == OS_OK);

    /* Free the event */
    err = os_memblock_put(&g_hci_os_event_pool, ev);
    assert(err == OS_OK);
}

/* XXX: For now, put this here */
int
ble_hci_transport_ctlr_event_send(uint8_t *hci_ev)
{
    os_error_t err;
    struct os_event *ev;

    /* Get an event structure off the queue */
    ev = (struct os_event *)os_memblock_get(&g_hci_os_event_pool);
    if (!ev) {
        err = os_memblock_put(&g_hci_cmd_pool, hci_ev);
        assert(err == OS_OK);
        return -1;
    }

    /* Fill out the event and post to Link Layer */
    ev->ev_queued = 0;
    ev->ev_type = BLE_HOST_HCI_EVENT_CTLR_EVENT;
    ev->ev_arg = hci_ev;
    os_eventq_put(&g_ble_host_hci_evq, ev);

    return 0;
}

void
host_hci_timer_cb(void *arg)
{
    /* Call the bletest code */
    bletest_execute();

    /* Re-start the timer */
    os_callout_reset(&g_ble_host_hci_timer.cf_c, OS_TICKS_PER_SEC);
}

void
host_hci_task(void *arg)
{
    struct os_event *ev;
    struct os_callout_func *cf;

    host_hci_timer_cb(NULL);

    while (1) {
        ev = os_eventq_get(&g_ble_host_hci_evq);
        switch (ev->ev_type) {
        case OS_EVENT_T_TIMER:
            cf = (struct os_callout_func *)ev;
            assert(cf->cf_func);
            cf->cf_func(cf->cf_arg);
            break;
        case BLE_HOST_HCI_EVENT_CTLR_EVENT:
            /* Process HCI event from controller */
            host_hci_event_proc(ev);
            break;
        default:
            assert(0);
            break;
        }
    }
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

    /* Initialize the host timer */
    os_callout_func_init(&g_ble_host_hci_timer, &g_ble_host_hci_evq,
                         host_hci_timer_cb, NULL);

    /* Initialize eventq */
    os_eventq_init(&g_ble_host_hci_evq);

    return rc;
}
