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
    uint32_t len;
    int rlen;
    uint8_t *buf = (uint8_t *)g_read_buf;

    am_hal_ble_blocking_hci_read(ble_handle, (uint32_t *)buf, &len);

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

        /* Call read function to pull in data from controller */
        apollo3_ble_hci_trans_rx_process();
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

/*
 * Function used by the BLE stack to send HCI messages to the BLE controller.
 * The payload is placed into a queue and the controller is turned on. When it is ready
 * an interrupt will fire to handle sending a message
 */
static uint8_t
apollo3_hci_write(uint8_t type, uint16_t len, uint8_t *data)
{
    uint8_t *write_ptr;
    hci_drv_write_t write_buf;

    /* comparison compensates for the type byte at index 0. */
    if (len > (MYNEWT_VAL(BLE_TRANSPORT_APOLLO3_MAX_TX_PACKET)-1)) {
        return 0;
    }

    /* Set all of the fields in the hci write structure. */
    write_buf.len = len + 1;

    write_ptr = (uint8_t *) write_buf.data;

    *write_ptr++ = type;

    for (uint32_t i = 0; i < len; i++) {
        write_ptr[i] = data[i];
    }

    /* Wake up the BLE controller. */
    apollo3_update_wake();

    /* Wait on SPI status before writing */
    while (BLEIFn(0)->BSTATUS_b.SPISTATUS) {
        os_time_delay(1);
    }

    am_hal_ble_blocking_hci_write(ble_handle, AM_HAL_BLE_RAW, write_buf.data, write_buf.len);

    return 0;
}

static int
apollo3_ble_hci_acl_tx(struct os_mbuf *om)
{
    struct os_mbuf *x;
    int rc = 0;

    x = om;
    while (x) {
        rc = apollo3_hci_write(HCI_H4_ACL, x->om_len, x->om_data);
        if (rc < 0) {
            break;
        }
        x = SLIST_NEXT(x, om_next);
    }

    os_mbuf_free_chain(om);

    return (rc < 0) ? BLE_ERR_MEM_CAPACITY : 0;
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
    int rc;
    uint8_t *cmd = buf;
    int len = HCI_CMD_HDR_LEN + cmd[2];

    rc = apollo3_hci_write(HCI_H4_CMD, len, cmd);

    ble_transport_free(cmd);

    return (rc < 0) ? BLE_ERR_MEM_CAPACITY :  0;
}

int
ble_transport_to_ll_acl_impl(struct os_mbuf *om)
{
    return apollo3_ble_hci_acl_tx(om);
}

void
ble_transport_ll_init(void)
{
    hci_h4_sm_init(&hci_apollo3_h4sm, &hci_h4_allocs_from_ll,
                   apollo3_ble_hci_frame_cb);
    apollo3_ble_hci_init();
}