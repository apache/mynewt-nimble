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

#include <assert.h>
#include <string.h>
#include <errno.h>
#include "nimble/ble.h"
#include "ble_hs_adv.h"
#include "ble_hs_priv.h"

static int
ble_hs_adv_set_hdr(uint8_t type, uint8_t data_len, uint8_t max_len,
                   uint8_t *dst, uint8_t *dst_len)
{
    if (*dst_len + 2 + data_len > max_len) {
        return BLE_HS_EMSGSIZE;
    }

    dst[*dst_len] = data_len + 1;
    dst[*dst_len + 1] = type;

    *dst_len += 2;

    return 0;
}

int
ble_hs_adv_set_flat(uint8_t type, int data_len, void *data,
                    uint8_t *dst, uint8_t *dst_len, uint8_t max_len)
{
    int rc;

    rc = ble_hs_adv_set_hdr(type, data_len, max_len, dst, dst_len);
    if (rc != 0) {
        return rc;
    }

    memcpy(dst + *dst_len, data, data_len);
    *dst_len += data_len;

    return 0;
}

static int
ble_hs_adv_set_array16(uint8_t type, uint8_t num_elems, uint16_t *elems,
                       uint8_t *dst, uint8_t *dst_len, uint8_t max_len)
{
    int rc;
    int i;

    rc = ble_hs_adv_set_hdr(type, num_elems * sizeof *elems, max_len, dst,
                            dst_len);
    if (rc != 0) {
        return rc;
    }

    for (i = 0; i < num_elems; i++) {
        htole16(dst + *dst_len, elems[i]);
        *dst_len += sizeof elems[i];
    }

    return 0;
}

static int
ble_hs_adv_set_array32(uint8_t type, uint8_t num_elems, uint32_t *elems,
                       uint8_t *dst, uint8_t *dst_len, uint8_t max_len)
{
    int rc;
    int i;

    rc = ble_hs_adv_set_hdr(type, num_elems * sizeof *elems, max_len, dst,
                            dst_len);
    if (rc != 0) {
        return rc;
    }

    for (i = 0; i < num_elems; i++) {
        htole32(dst + *dst_len, elems[i]);
        *dst_len += sizeof elems[i];
    }

    return 0;
}

/**
 * Sets the significant part of the data in outgoing advertisements.
 *
 * @return                      0 on success;  on failure.
 */
int
ble_hs_adv_set_fields(struct ble_hs_adv_fields *adv_fields,
                      uint8_t *dst, uint8_t *dst_len, uint8_t max_len)
{
    uint8_t type;
    int rc;

    *dst_len = 0;

    /*** 0x01 - Flags (written automatically by GAP). */

    /*** 0x02,0x03 - 16-bit service class UUIDs. */
    if (adv_fields->uuids16 != NULL && adv_fields->num_uuids16) {
        if (adv_fields->uuids16_is_complete) {
            type = BLE_HS_ADV_TYPE_COMP_UUIDS16;
        } else {
            type = BLE_HS_ADV_TYPE_INCOMP_UUIDS16;
        }

        rc = ble_hs_adv_set_array16(type, adv_fields->num_uuids16,
                                    adv_fields->uuids16, dst, dst_len,
                                    max_len);
        if (rc != 0) {
            return rc;
        }
    }

    /*** 0x04,0x05 - 32-bit service class UUIDs. */
    if (adv_fields->uuids32 != NULL && adv_fields->num_uuids32) {
        if (adv_fields->uuids32_is_complete) {
            type = BLE_HS_ADV_TYPE_COMP_UUIDS32;
        } else {
            type = BLE_HS_ADV_TYPE_INCOMP_UUIDS32;
        }

        rc = ble_hs_adv_set_array32(type, adv_fields->num_uuids32,
                                    adv_fields->uuids32, dst, dst_len,
                                    max_len);
        if (rc != 0) {
            return rc;
        }
    }

    /*** 0x06,0x07 - 128-bit service class UUIDs. */
    if (adv_fields->uuids128 != NULL && adv_fields->num_uuids128 > 0) {
        if (adv_fields->uuids128_is_complete) {
            type = BLE_HS_ADV_TYPE_COMP_UUIDS128;
        } else {
            type = BLE_HS_ADV_TYPE_INCOMP_UUIDS128;
        }

        rc = ble_hs_adv_set_flat(type, adv_fields->num_uuids128 * 16,
                                 adv_fields->uuids128, dst, dst_len, max_len);
        if (rc != 0) {
            return rc;
        }
    }

    /*** 0x08,0x09 - Local name. */
    if (adv_fields->name != NULL && adv_fields->name_len > 0) {
        if (adv_fields->name_is_complete) {
            type = BLE_HS_ADV_TYPE_COMP_NAME;
        } else {
            type = BLE_HS_ADV_TYPE_INCOMP_NAME;
        }

        rc = ble_hs_adv_set_flat(type, adv_fields->name_len, adv_fields->name,
                                 dst, dst_len, max_len);
        if (rc != 0) {
            return rc;
        }
    }

    /*** 0x0a - Tx power level (written automatically by GAP). */

    /*** 0x1c - LE role. */
    if (adv_fields->le_role_is_present) {
        rc = ble_hs_adv_set_flat(BLE_HS_ADV_TYPE_LE_ROLE, 1,
                                 &adv_fields->le_role, dst, dst_len, max_len);
        if (rc != 0) {
            return rc;
        }
    }

    return 0;
}

static int
ble_hs_adv_parse_one_field(struct ble_hs_adv_fields *adv_fields,
                           uint8_t *total_len, uint8_t *src, uint8_t src_len)
{
    uint8_t data_len;
    uint8_t type;
    uint8_t *data;

    if (src_len < 1) {
        return BLE_HS_EMSGSIZE;
    }
    *total_len = src[0] + 1;

    if (src_len < *total_len) {
        return BLE_HS_EMSGSIZE;
    }

    type = src[1];
    data = src + 2;
    data_len = *total_len - 2;

    switch (type) {
    case BLE_HS_ADV_TYPE_FLAGS:
        if (data_len != BLE_HS_ADV_FLAGS_LEN) {
            return BLE_HS_EBADDATA;
        }
        adv_fields->flags = *data;
        break;

    case BLE_HS_ADV_TYPE_INCOMP_NAME:
        adv_fields->name = data;
        adv_fields->name_len = data_len;
        adv_fields->name_is_complete = 0;
        break;

    case BLE_HS_ADV_TYPE_COMP_NAME:
        adv_fields->name = data;
        adv_fields->name_len = data_len;
        adv_fields->name_is_complete = 1;
        break;

    default:
        break;
    }

    return 0;
}

int
ble_hs_adv_parse_fields(struct ble_hs_adv_fields *adv_fields, uint8_t *src,
                        uint8_t src_len)
{
    uint8_t field_len;
    int rc;

    memset(adv_fields, 0, sizeof *adv_fields);

    while (src_len > 0) {
        rc = ble_hs_adv_parse_one_field(adv_fields, &field_len, src, src_len);
        if (rc != 0) {
            return rc;
        }

        src += field_len;
        src_len -= field_len;
    }

    return 0;
}
