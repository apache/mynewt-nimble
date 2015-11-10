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

#define BLE_HW_WHITE_LIST_SIZE      (0)

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
    return;
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
    return;
}

/**
 * Disables the whitelisted devices 
 */
void
ble_hw_whitelist_disable(void)
{
    return;
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
    return 0;
}
