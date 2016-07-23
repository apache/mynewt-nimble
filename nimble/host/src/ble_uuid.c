/**
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

#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include "os/os_mbuf.h"
#include "nimble/ble.h"
#include "ble_hs_priv.h"
#include "host/ble_uuid.h"

static uint8_t ble_uuid_base[16] = {
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
    0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/**
 * Attempts to convert the supplied 128-bit UUID into its shortened 16-bit
 * form.
 *
 * @param uuid128                   The 128-bit UUID to attempt to convert.
 *                                      This must point to 16 contiguous bytes.
 *
 * @return                          A positive 16-bit unsigned integer on
 *                                      success;
 *                                  0 if the UUID cannot be represented in 16
 *                                      bits.
 */
uint16_t
ble_uuid_128_to_16(const void *uuid128)
{
    const uint8_t *u8ptr;
    uint16_t uuid16;
    int rc;

    u8ptr = uuid128;

    /* The UUID can only be converted if the final 96 bits of its big endian
     * representation are equal to the base UUID.
     */
    rc = memcmp(u8ptr, ble_uuid_base, sizeof ble_uuid_base - 4);
    if (rc != 0) {
        return 0;
    }

    if (u8ptr[14] != 0 || u8ptr[15] != 0) {
        /* This UUID has a 32-bit form, but not a 16-bit form. */
        return 0;
    }

    uuid16 = le16toh(u8ptr + 12);
    if (uuid16 == 0) {
        return 0;
    }

    return uuid16;
}

/**
 * Expands a 16-bit UUID into its 128-bit form.
 *
 * @param uuid16                The 16-bit UUID to convert.
 * @param out_uuid128           On success, the resulting 128-bit UUID gets
 *                                  written here.
 *
 * @return                      0 on success;
 *                              BLE_HS_EINVAL if uuid16 is not a valid 16-bit
 *                                  UUID.
 */
int
ble_uuid_16_to_128(uint16_t uuid16, void *out_uuid128)
{
    uint8_t *u8ptr;

    if (uuid16 == 0) {
        return BLE_HS_EINVAL;
    }

    u8ptr = out_uuid128;

    memcpy(u8ptr, ble_uuid_base, 16);
    htole16(u8ptr + 12, uuid16);

    return 0;
}

int
ble_uuid_append(struct os_mbuf *om, const void *uuid128)
{
    uint16_t uuid16;
    void *buf;
    int rc;

    uuid16 = ble_uuid_128_to_16(uuid128);
    if (uuid16 != 0) {
        buf = os_mbuf_extend(om, 2);
        if (buf == NULL) {
            return BLE_HS_ENOMEM;
        }

        htole16(buf, uuid16);
    } else {
        rc = os_mbuf_append(om, uuid128, 16);
        if (rc != 0) {
            return BLE_HS_ENOMEM;
        }
    }

    return 0;
}

int
ble_uuid_extract(struct os_mbuf *om, int off, void *uuid128)
{
    uint16_t uuid16;
    int remlen;
    int rc;

    remlen = OS_MBUF_PKTHDR(om)->omp_len - off;
    switch (remlen) {
    case 2:
        rc = os_mbuf_copydata(om, off, 2, &uuid16);
        BLE_HS_DBG_ASSERT_EVAL(rc == 0);

        uuid16 = le16toh(&uuid16);
        rc = ble_uuid_16_to_128(uuid16, uuid128);
        if (rc != 0) {
            return rc;
        }
        return 0;

    case 16:
        rc = os_mbuf_copydata(om, off, 16, uuid128);
        BLE_HS_DBG_ASSERT_EVAL(rc == 0);
        return 0;

    default:
        return BLE_HS_EMSGSIZE;
    }
}
