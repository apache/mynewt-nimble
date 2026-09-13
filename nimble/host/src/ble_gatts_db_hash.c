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

#include "syscfg/syscfg.h"

#if MYNEWT_VAL(BLE_GATT_CACHING)

#include <string.h>
#include "os/endian.h"
#include "host/ble_gatt.h"
#include "ble_hs_priv.h"
#include <mbedtls/cipher.h>
#include <mbedtls/cmac.h>

/* The longest attribute value contributing to the hash is a characteristic
 * declaration: properties (1) + value handle (2) + 128-bit UUID (16).
 */
#define BLE_GATTS_DB_HASH_MAX_VAL_SZ    19

static int
ble_gatts_db_hash_update_val(mbedtls_cipher_context_t *ctx, uint16_t handle)
{
    struct os_mbuf *om;
    uint8_t buf[BLE_GATTS_DB_HASH_MAX_VAL_SZ];
    uint16_t len;
    int rc;

    rc = ble_att_svr_read_local(handle, &om);
    if (rc != 0) {
        return rc;
    }

    len = OS_MBUF_PKTLEN(om);
    if (len > sizeof buf) {
        rc = BLE_HS_EINVAL;
        goto done;
    }

    rc = os_mbuf_copydata(om, 0, len, buf);
    BLE_HS_DBG_ASSERT(rc == 0);

    if (mbedtls_cipher_cmac_update(ctx, buf, len) != 0) {
        rc = BLE_HS_EUNKNOWN;
        goto done;
    }

    rc = 0;

done:
    os_mbuf_free_chain(om);
    return rc;
}

/**
 * Calculates the hash of the local attribute database, as defined in
 * Core spec v5.4, Vol 3, Part G, 7.3.1.
 */
int
ble_gatts_calculate_hash(uint8_t *out_hash_key)
{
    mbedtls_cipher_context_t ctx;
    struct ble_att_svr_entry *entry;
    uint8_t key[16];
    uint8_t buf[4];
    uint16_t uuid16;
    uint16_t handle;
    int with_val;
    int rc;

    mbedtls_cipher_init(&ctx);

    memset(key, 0, sizeof key);
    if (mbedtls_cipher_setup(&ctx, mbedtls_cipher_info_from_type(
                                       MBEDTLS_CIPHER_AES_128_ECB)) != 0 ||
        mbedtls_cipher_cmac_starts(&ctx, key, 128) != 0) {
        rc = BLE_HS_EUNKNOWN;
        goto done;
    }

    for (handle = 1; handle != 0 && handle <= ble_att_svr_prev_handle();
         handle++) {
        entry = ble_att_svr_find_by_handle(handle);
        if (entry == NULL) {
            /* Hidden attribute; not part of the client-visible database. */
            continue;
        }

        if (entry->ha_uuid->type != BLE_UUID_TYPE_16) {
            continue;
        }
        uuid16 = BLE_UUID16(entry->ha_uuid)->value;

        switch (uuid16) {
        case BLE_ATT_UUID_PRIMARY_SERVICE:
        case BLE_ATT_UUID_SECONDARY_SERVICE:
        case BLE_ATT_UUID_INCLUDE:
        case BLE_ATT_UUID_CHARACTERISTIC:
        case BLE_GATT_DSC_EXT_PROP_UUID16:
            with_val = 1;
            break;

        case BLE_GATT_DSC_USER_DESC_UUID16:
        case BLE_GATT_DSC_CLT_CFG_UUID16:
        case BLE_GATT_DSC_SRV_CFG_UUID16:
        case BLE_GATT_DSC_CHR_FMT_UUID16:
        case BLE_GATT_DSC_AGG_FMT_UUID16:
            with_val = 0;
            break;

        default:
            continue;
        }

        put_le16(buf, handle);
        put_le16(buf + 2, uuid16);
        if (mbedtls_cipher_cmac_update(&ctx, buf, sizeof buf) != 0) {
            rc = BLE_HS_EUNKNOWN;
            goto done;
        }

        if (with_val) {
            rc = ble_gatts_db_hash_update_val(&ctx, handle);
            if (rc != 0) {
                goto done;
            }
        }
    }

    if (mbedtls_cipher_cmac_finish(&ctx, out_hash_key) != 0) {
        rc = BLE_HS_EUNKNOWN;
        goto done;
    }

    /* CMAC output is big-endian; the hash is transmitted little-endian. */
    swap_in_place(out_hash_key, 16);

    rc = 0;

done:
    mbedtls_cipher_free(&ctx);
    return rc;
}

#endif /* MYNEWT_VAL(BLE_GATT_CACHING) */
