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

#include <inttypes.h>
#include <string.h>
#include "ble_hs_uuid.h"

static uint8_t ble_hs_uuid_base[16] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
    0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB
};

/**
 * Attempts to convert the supplied 128-bit UUID into its shortened 16-bit
 * form.
 *
 * @return                          Positive 16-bit unsigned integer on
 *                                      success;
 *                                  -1 if the UUID could not be converted.
 */
int
ble_hs_uuid_16bit(uint8_t *uuid128)
{
    uint16_t uuid16;
    int rc;

    /* The UUID can only be converted if its final 96 bits are equal to the
     * base UUID.
     */
    rc = memcmp(uuid128 + 4, ble_hs_uuid_base + 4,
                sizeof ble_hs_uuid_base - 4);
    if (rc != 0) {
        return -1;
    }

    if (uuid128[0] != 0 || uuid128[1] != 0) {
        /* This UUID has a 32-bit form, but not a 16-bit form. */
        return -1;
    }

    uuid16 = (uuid128[2] << 8) + uuid128[3];
    if (uuid16 == 0) {
        return -1;
    }

    return uuid16;
}
