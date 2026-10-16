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
#include <os/mynewt.h>
#include <nimble/hci_common.h>
#include <nimble/transport.h>
#include <nimble/transport/hci_h4.h>

#include "am_mcu_apollo.h"

#define HCI_CMD_HDR_LEN (3) /*!< \brief Command packet header length */

static os_event_fn apollo3_ble_hci_ev_cb;

static struct os_event apollo3_ble_hci_ev = {
    .ev_cb = apollo3_ble_hci_ev_cb,
};

static struct os_eventq apollo3_ble_hci_evq;
static struct os_task apollo3_ble_hci_task;
static os_stack_t apollo3_ble_hci_stack[MYNEWT_VAL(BLE_HCI_APOLLO3_STACK_SIZE)];

struct apollo3_ble_hci_pkt {
    uint8_t type;
    void *data;
    STAILQ_ENTRY(apollo3_ble_hci_pkt) next;
};

static STAILQ_HEAD(, apollo3_ble_hci_pkt) apollo3_ble_hci_tx_q;

static struct os_mempool apollo3_ble_hci_pkt_pool;
static os_membuf_t apollo3_ble_hci_pkt_buf[OS_MEMPOOL_SIZE(
    1 + MYNEWT_VAL(BLE_TRANSPORT_ACL_FROM_HS_COUNT), sizeof(struct apollo3_ble_hci_pkt))];

/* Tx power level in dBm. */
typedef enum {
  TX_POWER_LEVEL_MINUS_10P0_dBm = 0x4,
  TX_POWER_LEVEL_MINUS_5P0_dBm = 0x5,
  TX_POWER_LEVEL_0P0_dBm = 0x8,
  TX_POWER_LEVEL_PLUS_3P0_dBm = 0xF,
  TX_POWER_LEVEL_INVALID = 0x10,
} txPowerLevel_t;

/* Structure for holding outgoing HCI packets. */
typedef struct {
    uint32_t len;
    uint32_t data[MYNEWT_VAL(BLE_TRANSPORT_APOLLO3_MAX_TX_PACKET) / sizeof(uint32_t)];
} hci_drv_write_t;

uint32_t g_read_buf[MYNEWT_VAL(BLE_TRANSPORT_APOLLO3_MAX_RX_PACKET) / sizeof(uint32_t)];

/* BLE module handle for Ambiq HAL functions */
void *ble_handle;

uint8_t g_ble_mac_address[6] = {0};

static struct hci_h4_sm hci_apollo3_h4sm;

static void
apollo3_ble_hci_trans_rx_process(void)
{
    uint32_t status;
    uint32_t len = 0;
    int rlen;
    uint8_t *buf = (uint8_t *)g_read_buf;

    status = am_hal_ble_blocking_hci_read(ble_handle, (uint32_t *)buf, &len);
    if (status != AM_HAL_STATUS_SUCCESS || len == 0) {
        return;
    }

    /* NOTE: Ambiq Apollo3 controller does not have local supported lmp features implemented
     * The command will always return 0 so we overwrite the buffer here
     */
    if(buf[4] == 0x03 && buf[5] == 0x10 && len == 15) {
        memset(&buf[11], 0x60, sizeof(uint8_t));
    }
    rlen = hci_h4_sm_rx(&hci_apollo3_h4sm, buf, len);
    assert(rlen >= 0);
}

/* Interrupt handler that looks for BLECIRQ. This gets set by BLE core when there is something to read */
static void
apollo3_hci_int(void)
{
    uint32_t int_status;

    /* Read and clear the interrupt status. */
    int_status = am_hal_ble_int_status(ble_handle, true);
    am_hal_ble_int_clear(ble_handle, int_status);

    /* Handle any DMA or Command Complete interrupts. */
    am_hal_ble_int_service(ble_handle, int_status);

    /* If this was a BLEIRQ interrupt, attempt to start a read operation. */
    if (int_status & AM_HAL_BLE_INT_BLECIRQ)
    {
        /* Lower WAKE */
        am_hal_ble_wakeup_set(ble_handle, 0);

        os_eventq_put(&apollo3_ble_hci_evq, &apollo3_ble_hci_ev);
    }
    else {
        assert(0);
    }
}

/* Boot the radio. */
uint32_t
apollo3_hci_radio_boot(bool is_cold_boot)
{
    uint32_t xtal_retry_cnt = 0;
    uint32_t am_boot_status;
    am_hal_mcuctrl_device_t device_info;
    am_hal_ble_config_t ble_config =
    {
        /* Configure the HCI interface clock for 6 MHz */
        .ui32SpiClkCfg = AM_HAL_BLE_HCI_CLK_DIV8,

        /* Set HCI read and write thresholds to 32 bytes each. */
        .ui32ReadThreshold = 32,
        .ui32WriteThreshold = 32,

        /* The MCU will supply the clock to the BLE core. */
        .ui32BleClockConfig = AM_HAL_BLE_CORE_MCU_CLK,

        /* Note: These settings only apply to Apollo3 A1/A2 silicon, not B0 silicon.
         * Default settings for expected BLE clock drift (measured in PPM).
         */
        .ui32ClockDrift = 0,
        .ui32SleepClockDrift = 50,

        /* Default setting - AGC Enabled */
        .bAgcEnabled = true,

        /* Default setting - Sleep Algo enabled */
        .bSleepEnabled = true,

        /* Apply the default patches when am_hal_ble_boot() is called. */
        .bUseDefaultPatches = true,
    };

    /* Configure and enable the BLE interface. */
    am_boot_status = AM_HAL_STATUS_FAIL;
    while (am_boot_status != AM_HAL_STATUS_SUCCESS) {
        am_hal_pwrctrl_low_power_init();
        am_hal_ble_initialize(0, &ble_handle);
        am_hal_ble_power_control(ble_handle, AM_HAL_BLE_POWER_ACTIVE);

        am_hal_ble_config(ble_handle, &ble_config);

        /*
         * Delay 1s for 32768Hz clock stability. This isn't required unless this is
         * our first run immediately after a power-up.
         */
        if (is_cold_boot) {
            os_time_delay(OS_TICKS_PER_SEC);
        }

        /* Attempt to boot the radio. */
        am_boot_status = am_hal_ble_boot(ble_handle);

        /* Check our status, exit if radio is running */
        if (am_boot_status == AM_HAL_STATUS_SUCCESS) {
            break;
        }
        else if (am_boot_status == AM_HAL_BLE_32K_CLOCK_UNSTABLE) {
            /* If the radio is running, but the clock looks bad, we can try to restart. */
            am_hal_ble_power_control(ble_handle, AM_HAL_BLE_POWER_OFF);
            am_hal_ble_deinitialize(ble_handle);

            /* We won't restart forever. After we hit the maximum number of retries, we'll just return with failure. */
            if (xtal_retry_cnt++ < MYNEWT_VAL(BLE_TRANSPORT_APOLLO3_MAX_XTAL_RETRIES)) {
                os_time_delay(OS_TICKS_PER_SEC);
            }
            else {
                return SYS_EUNKNOWN;
            }
        }
        else {
            am_hal_ble_power_control(ble_handle, AM_HAL_BLE_POWER_OFF);
            am_hal_ble_deinitialize(ble_handle);
            /*
             * If the radio failed for some reason other than 32K Clock
             * instability, we should just report the failure and return.
             */
            return SYS_EUNKNOWN;
        }
    }

    /* Set the BLE TX Output power to 0dBm. */
    am_hal_ble_tx_power_set(ble_handle, TX_POWER_LEVEL_0P0_dBm);

    /* Enable interrupts for the BLE module. */
    am_hal_ble_int_clear(ble_handle, (AM_HAL_BLE_INT_CMDCMP |
                               AM_HAL_BLE_INT_DCMP |
                               AM_HAL_BLE_INT_BLECIRQ));

    am_hal_ble_int_enable(ble_handle, (AM_HAL_BLE_INT_CMDCMP |
                                AM_HAL_BLE_INT_DCMP |
                                AM_HAL_BLE_INT_BLECIRQ));

    /* When it's is_cold_boot, it will use Apollo's Device ID to form Bluetooth address. */
    if (is_cold_boot) {
        am_hal_mcuctrl_info_get(AM_HAL_MCUCTRL_INFO_DEVICEID, &device_info);

        /* Bluetooth address formed by ChipID1 (32 bits) and ChipID0 (8-23 bits). */
        memcpy(g_ble_mac_address, &device_info.ui32ChipID1, sizeof(device_info.ui32ChipID1));

        /* ui32ChipID0 bit 8-31 is test time during chip manufacturing */
        g_ble_mac_address[4] = (device_info.ui32ChipID0 >> 8) & 0xFF;
        g_ble_mac_address[5] = (device_info.ui32ChipID0 >> 16) & 0xFF;
    }

    NVIC_EnableIRQ(BLE_IRQn);

    return 0;
}

/* Wake update helper function */
static void
apollo3_update_wake(void)
{
    AM_CRITICAL_BEGIN;

    /* Set WAKE if there's something in the write queue, but not if SPISTATUS or IRQ is high. */
    if ((BLEIFn(0)->BSTATUS_b.SPISTATUS == 0) && (BLEIF->BSTATUS_b.BLEIRQ == false)) {
        am_hal_ble_wakeup_set(ble_handle, 1);

        /* If we've set wakeup, but IRQ came up at the same time, we should just lower WAKE again. */
        if (BLEIF->BSTATUS_b.BLEIRQ == true) {
            am_hal_ble_wakeup_set(ble_handle, 0);
        }
    }

    AM_CRITICAL_END;
}

static int
apollo3_ble_hci_enqueue_acl(struct os_mbuf *om)
{
    struct apollo3_ble_hci_pkt *pkt;
    os_sr_t sr;

    pkt = os_memblock_get(&apollo3_ble_hci_pkt_pool);
    if (pkt == NULL) {
        os_mbuf_free_chain(om);
        return BLE_ERR_MEM_CAPACITY;
    }

    pkt->type = AM_HAL_BLE_ACL;
    pkt->data = om;

    OS_ENTER_CRITICAL(sr);
    STAILQ_INSERT_TAIL(&apollo3_ble_hci_tx_q, pkt, next);
    OS_EXIT_CRITICAL(sr);

    os_eventq_put(&apollo3_ble_hci_evq, &apollo3_ble_hci_ev);

    return 0;
}

static int
apollo3_ble_hci_enqueue_cmd(void *buf)
{
    struct apollo3_ble_hci_pkt *pkt;
    uint8_t *cmd = buf;
    os_sr_t sr;

    pkt = os_memblock_get(&apollo3_ble_hci_pkt_pool);
    if (pkt == NULL) {
        ble_transport_free(cmd);
        return BLE_ERR_MEM_CAPACITY;
    }

    pkt->type = AM_HAL_BLE_CMD;
    pkt->data = cmd;

    OS_ENTER_CRITICAL(sr);
    STAILQ_INSERT_TAIL(&apollo3_ble_hci_tx_q, pkt, next);
    OS_EXIT_CRITICAL(sr);

    os_eventq_put(&apollo3_ble_hci_evq, &apollo3_ble_hci_ev);

    return 0;
}

static int
apollo3_ble_hci_tx_acl(void *data)
{
    struct os_mbuf *om = data;
    hci_drv_write_t write_buf;
    uint8_t *ptr = (uint8_t *)write_buf.data;
    uint32_t pktlen = OS_MBUF_PKTLEN(om);

    if (pktlen > MYNEWT_VAL(BLE_TRANSPORT_APOLLO3_MAX_TX_PACKET)) {
        return -1;
    }
    os_mbuf_copydata(om, 0, pktlen, ptr);
    write_buf.len = pktlen;
    apollo3_update_wake();

    if (am_hal_ble_blocking_hci_write(ble_handle, AM_HAL_BLE_ACL, write_buf.data,
                                      write_buf.len) != AM_HAL_STATUS_SUCCESS) {
        return -1;
    }
    return 0;
}

static int
apollo3_ble_hci_tx_cmd(void *data)
{
    am_hal_status_e status;
    uint8_t *cmd = data;
    uint32_t len = HCI_CMD_HDR_LEN + cmd[2];

    apollo3_update_wake();
    status = am_hal_ble_blocking_hci_write(ble_handle, AM_HAL_BLE_CMD,
                                           (uint32_t *)cmd, len);

    if (status != AM_HAL_STATUS_SUCCESS) {
        return -1;
    }
    return 0;
}

static int
apollo3_ble_hci_frame_cb(uint8_t pkt_type, void *data)
{
    int rc;

    switch (pkt_type) {
    case HCI_H4_ACL:
        rc = ble_transport_to_hs_acl(data);
        break;
    case HCI_H4_EVT:
        rc = ble_transport_to_hs_evt(data);
        break;
    default:
        assert(0);
        break;
    }

    return rc;
}

static void
apollo3_ble_hci_init(void)
{
    SYSINIT_ASSERT_ACTIVE();

    /* Enable interrupt to handle read based on BLECIRQ */
    NVIC_SetVector(BLE_IRQn, (uint32_t)apollo3_hci_int);

    /* Initial coldboot configuration */
    apollo3_hci_radio_boot(1);
}

int
ble_transport_to_ll_cmd_impl(void *buf)
{
    return apollo3_ble_hci_enqueue_cmd(buf);
}

int
ble_transport_to_ll_acl_impl(struct os_mbuf *om)
{
    return apollo3_ble_hci_enqueue_acl(om);
}

static void
apollo3_ble_hci_pkt_free(struct apollo3_ble_hci_pkt *pkt)
{
    switch (pkt->type) {
    case AM_HAL_BLE_CMD:
        ble_transport_free(pkt->data);
        break;
    case AM_HAL_BLE_ACL:
        os_mbuf_free_chain(pkt->data);
        break;
    default:
        break;
    }
    os_memblock_put(&apollo3_ble_hci_pkt_pool, pkt);
}

static int
apollo3_ble_hci_tx(struct apollo3_ble_hci_pkt *pkt)
{
    int rc;
    switch (pkt->type) {
    case AM_HAL_BLE_CMD:
        rc = apollo3_ble_hci_tx_cmd(pkt->data);
        break;
    case AM_HAL_BLE_ACL:
        rc = apollo3_ble_hci_tx_acl(pkt->data);
        break;
    default:
        rc = -1;
        break;
    }
    return rc;
}

static void
apollo3_ble_hci_ev_cb(struct os_event *ev)
{
    struct apollo3_ble_hci_pkt *pkt;
    int rc = 0;
    os_sr_t sr;

    while (BLEIF->BSTATUS_b.BLEIRQ || !STAILQ_EMPTY(&apollo3_ble_hci_tx_q)) {
        if (BLEIF->BSTATUS_b.BLEIRQ) {
            apollo3_ble_hci_trans_rx_process();
            continue;
        }

        OS_ENTER_CRITICAL(sr);
        pkt = STAILQ_FIRST(&apollo3_ble_hci_tx_q);
        OS_EXIT_CRITICAL(sr);
        if (pkt == NULL) {
            break;
        }

        rc = apollo3_ble_hci_tx(pkt);

        if (rc != 0) {
            break;
        }

        OS_ENTER_CRITICAL(sr);
        STAILQ_REMOVE_HEAD(&apollo3_ble_hci_tx_q, next);
        OS_EXIT_CRITICAL(sr);

        apollo3_ble_hci_pkt_free(pkt);
    }
}

static void
apollo3_ble_hci_task_loop(void *unused)
{
    while (1) {
        os_eventq_run(&apollo3_ble_hci_evq);
    }
}

void
ble_transport_ll_init(void)
{
    int rc;

    hci_h4_sm_init(&hci_apollo3_h4sm, &hci_h4_allocs_from_ll,
                   apollo3_ble_hci_frame_cb);

    rc = os_mempool_init(&apollo3_ble_hci_pkt_pool,
                         1 + MYNEWT_VAL(BLE_TRANSPORT_ACL_FROM_HS_COUNT),
                         sizeof(struct apollo3_ble_hci_pkt),
                         apollo3_ble_hci_pkt_buf, "apollo3_ble_hci_pkt_pool");
    SYSINIT_PANIC_ASSERT(rc == 0);

    STAILQ_INIT(&apollo3_ble_hci_tx_q);

    os_eventq_init(&apollo3_ble_hci_evq);

    rc = os_task_init(
        &apollo3_ble_hci_task, "apollo3_ble_hci", apollo3_ble_hci_task_loop,
        NULL, MYNEWT_VAL(BLE_HCI_APOLLO3_TASK_PRIO), OS_WAIT_FOREVER,
        apollo3_ble_hci_stack, MYNEWT_VAL(BLE_HCI_APOLLO3_STACK_SIZE));
    SYSINIT_PANIC_ASSERT(rc == 0);

    apollo3_ble_hci_init();
}
