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
#include "mcu/nrf52_bitfields.h"

#define BLE_HW_WHITE_LIST_SIZE      (8)

/* We use this to keep track of which entries are set to valid addresses */
static uint8_t g_ble_hw_whitelist_mask;

/**
 * Clear the whitelist
 * 
 * @return int 
 */
void
ble_hw_whitelist_clear(void)
{
    NRF_RADIO->DACNF = 0;
    g_ble_hw_whitelist_mask = 0;
}

/**
 * Add a device to the hw whitelist 
 * 
 * @param addr 
 * @param addr_type 
 * 
 * @return int 0: success, BLE error code otherwise
 */
int
ble_hw_whitelist_add(uint8_t *addr, uint8_t addr_type)
{
    int i;
    uint32_t mask;

    /* Find first ununsed device address match element */
    mask = 0x01;
    for (i = 0; i < BLE_HW_WHITE_LIST_SIZE; ++i) {
        if ((mask & g_ble_hw_whitelist_mask) == 0) {
            NRF_RADIO->DAB[i] = le32toh(addr);
            NRF_RADIO->DAP[i] = le16toh(addr + 4);
            if (addr_type == BLE_ADDR_TYPE_RANDOM) {
                NRF_RADIO->DACNF |= (mask << 8);
            }
            g_ble_hw_whitelist_mask |= mask;
            return BLE_ERR_SUCCESS;
        }
        mask <<= 1;
    }

    return BLE_ERR_MEM_CAPACITY;
}

/**
 * Remove a device from the hw whitelist 
 * 
 * @param addr 
 * @param addr_type 
 * 
 */
void
ble_hw_whitelist_rmv(uint8_t *addr, uint8_t addr_type)
{
    int i;
    uint8_t cfg_addr;
    uint16_t dap;
    uint16_t txadd;
    uint32_t dab;
    uint32_t mask;

    /* Find first ununsed device address match element */
    dab = le32toh(addr);
    dap = le16toh(addr + 4);
    txadd = NRF_RADIO->DACNF >> 8;
    mask = 0x01;
    for (i = 0; i < BLE_HW_WHITE_LIST_SIZE; ++i) {
        if (mask & g_ble_hw_whitelist_mask) {
            if ((dab == NRF_RADIO->DAB[i]) && (dap == NRF_RADIO->DAP[i])) {
                cfg_addr = txadd & mask;
                if (addr_type == BLE_ADDR_TYPE_RANDOM) {
                    if (cfg_addr != 0) {
                        break;
                    }
                } else {
                    if (cfg_addr == 0) {
                        break;
                    }
                }
            }
        }
        mask <<= 1;
    }

    if (i < BLE_HW_WHITE_LIST_SIZE) {
        g_ble_hw_whitelist_mask &= ~mask;
        NRF_RADIO->DACNF &= ~mask;
    }
}

/**
 * Returns the size of the whitelist in HW 
 * 
 * @return int Number of devices allowed in whitelist
 */
uint8_t
ble_hw_whitelist_size(void)
{
    return BLE_HW_WHITE_LIST_SIZE;
}

/**
 * Enable the whitelisted devices 
 */
void
ble_hw_whitelist_enable(void)
{
    /* Enable the configured device addresses */
    NRF_RADIO->DACNF |= g_ble_hw_whitelist_mask;
}

/**
 * Disables the whitelisted devices 
 */
void
ble_hw_whitelist_disable(void)
{
    /* Disable all whitelist devices */
    NRF_RADIO->DACNF &= 0x0000ff00;
}

/**
 * Boolean function which returns true ('1') if there is a match on the 
 * whitelist. 
 * 
 * @return int 
 */
int
ble_hw_whitelist_match(void)
{
    return (int)NRF_RADIO->EVENTS_DEVMATCH;
}
