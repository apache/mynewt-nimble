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

#include <string.h>
#include "host/ble_hs_id.h"
#include "ble_hs_priv.h"

static uint8_t ble_hs_id_pub[6];
static uint8_t ble_hs_id_rnd[6];

void
ble_hs_id_set_pub(const uint8_t *pub_addr)
{
    ble_hs_lock();
    memcpy(ble_hs_id_pub, pub_addr, 6);
    ble_hs_unlock();
}

/**
 * Generates a new random address.  This function does not configure the device
 * with the new address; the caller can use the address in subsequent
 * operations.
 *
 * @param nrpa                  The type of random address to generate:
 *                                  0: static
 *                                  1: non-resolvable private
 * @param out_addr              On success, the generated address gets written
 *                                  here.
 *
 * @return                      0 on success; nonzero on failure.
 */
int
ble_hs_id_gen_rnd(int nrpa, uint8_t *out_addr)
{
    int rc;

    rc = ble_hs_hci_util_rand(out_addr, 6);
    if (rc != 0) {
        return rc;
    }

    if (nrpa) {
        out_addr[5] &= ~0xc0;
    } else {
        out_addr[5] |= 0xc0;
    }

    return 0;
}

/**
 * Sets the device's random address.  The address type (static vs.
 * non-resolvable private) is inferred from the most-significant byte of the
 * address.  The address is specified in host byte order (little-endian!).
 *
 * @param rnd_addr              The random address to set.
 *
 * @return                      0 on success;
 *                              BLE_HS_EINVAL if the specified address is not a
 *                                  valid static random or non-resolvable
 *                                  private address.
 *                              Other nonzero on error.
 */
int
ble_hs_id_set_rnd(const uint8_t *rnd_addr)
{
    uint8_t addr_type_byte;
    int rc;

    ble_hs_lock();

    addr_type_byte = rnd_addr[5] & 0xc0;
    if (addr_type_byte != 0x00 && addr_type_byte != 0xc0) {
        rc = BLE_HS_EINVAL;
        goto done;
    }

    rc = ble_hs_hci_util_set_random_addr(rnd_addr);
    if (rc != 0) {
        goto done;
    }

    memcpy(ble_hs_id_rnd, rnd_addr, 6);

    rc = 0;

done:
    ble_hs_unlock();
    return rc;
}

/**
 * Retrieves one of the device's identity addresses.  The device can have two
 * identity addresses: one public and one random.  The id_addr_type argument
 * specifies which of these two addresses to retrieve.
 *
 * @param id_addr_type          The type of identity address to retrieve.
 *                                  Valid values are:
 *                                      o BLE_ADDR_TYPE_PUBLIC
 *                                      o BLE_ADDR_TYPE_RANDOM
 * @param out_id_addr           On success, this is reseated to point to the
 *                                  retrieved 6-byte identity address.
 * @param out_is_nrpa           On success, the pointed-to value indicates
 *                                  whether the retrieved address is a
 *                                  non-resolvable private address.
 *
 * @return                      0 on success;
 *                              BLE_HS_EINVAL if an invalid address type was
 *                                  specified;
 *                              BLE_HS_ENOADDR if the device does not have an
 *                                  identity address of the requested type;
 *                              Other BLE host core code on error.
 */
int
ble_hs_id_addr(uint8_t id_addr_type, const uint8_t **out_id_addr,
               int *out_is_nrpa)
{
    const uint8_t *id_addr;
    int nrpa;

    BLE_HS_DBG_ASSERT(ble_hs_locked_by_cur_task());

    switch (id_addr_type) {
    case BLE_ADDR_TYPE_PUBLIC:
        id_addr = ble_hs_id_pub;
        nrpa = 0;
        break;

    case BLE_ADDR_TYPE_RANDOM:
        id_addr = ble_hs_id_rnd;
        nrpa = (ble_hs_id_rnd[5] & 0xc0) == 0;
        break;

    default:
        return BLE_HS_EINVAL;
    }

    if (memcmp(id_addr, ble_hs_misc_null_addr, 6) == 0) {
        return BLE_HS_ENOADDR;
    }

    if (out_id_addr != NULL) {
        *out_id_addr = id_addr;
    }
    if (out_is_nrpa != NULL) {
        *out_is_nrpa = nrpa;
    }

    return 0;
}

/**
 * Retrieves one of the device's identity addresses.  The device can have two
 * identity addresses: one public and one random.  The id_addr_type argument
 * specifies which of these two addresses to retrieve.
 *
 * @param id_addr_type          The type of identity address to retrieve.
 *                                  Valid values are:
 *                                      o BLE_ADDR_TYPE_PUBLIC
 *                                      o BLE_ADDR_TYPE_RANDOM
 * @param out_id_addr           On success, the requested identity address is
 *                                  copied into this buffer.  The buffer must
 *                                  be at least six bytes in size.
 * @param out_is_nrpa           On success, the pointed-to value indicates
 *                                  whether the retrieved address is a
 *                                  non-resolvable private address.
 *
 * @return                      0 on success;
 *                              BLE_HS_EINVAL if an invalid address type was
 *                                  specified;
 *                              BLE_HS_ENOADDR if the device does not have an
 *                                  identity address of the requested type;
 *                              Other BLE host core code on error.
 */
int
ble_hs_id_copy_addr(uint8_t id_addr_type, uint8_t *out_id_addr,
                    int *out_is_nrpa)
{
    const uint8_t *addr;
    int rc;

    ble_hs_lock();

    rc = ble_hs_id_addr(id_addr_type, &addr, out_is_nrpa);
    if (rc == 0) {
        memcpy(out_id_addr, addr, 6);
    }

    ble_hs_unlock();

    return rc;
}

int
ble_hs_id_use_addr(uint8_t addr_type)
{
    uint8_t id_addr_type;
    int nrpa;
    int rc;

    switch (addr_type) {
    case BLE_ADDR_TYPE_PUBLIC:
    case BLE_ADDR_TYPE_RANDOM:
        rc = ble_hs_id_addr(addr_type, NULL, NULL);
        if (rc != 0) {
            return rc;
        }
        break;

    case BLE_ADDR_TYPE_RPA_PUB_DEFAULT:
    case BLE_ADDR_TYPE_RPA_RND_DEFAULT:
        id_addr_type = ble_hs_misc_addr_type_to_id(addr_type);
        rc = ble_hs_id_addr(id_addr_type, NULL, &nrpa);
        if (rc != 0) {
            return rc;
        }
        if (nrpa) {
            return BLE_HS_ENOADDR;
        }

        rc = ble_hs_pvcy_ensure_started();
        if (rc != 0) {
            return rc;
        }
        break;

    default:
        return BLE_HS_EINVAL;
    }

    return 0;
}
